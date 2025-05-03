#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/eme/gstemeutils.h>
#include <gst/eme/gstmediakeysession.h> // For GstMediaKeySession type
#include <libsoup/soup.h>

#include "gstemeautodecryptbin.h"

GST_DEBUG_CATEGORY_STATIC (gst_eme_auto_decrypt_bin_debug);
#define GST_CAT_DEFAULT gst_eme_auto_decrypt_bin_debug

enum {
    PROP_0,
    PROP_LAURLS,
    PROP_KEY_SYSTEMS,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

// Forward declarations
static void gst_eme_auto_decrypt_bin_set_property (GObject * object, guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_eme_auto_decrypt_bin_get_property (GObject * object, guint property_id, GValue * value, GParamSpec * pspec);
static void gst_eme_auto_decrypt_bin_dispose (GObject * object);
static void gst_eme_auto_decrypt_bin_finalize (GObject * object);
static GstStateChangeReturn gst_eme_auto_decrypt_bin_change_state (GstElement * element, GstStateChange transition);
static gboolean bus_message_cb (GstBus * bus, GstMessage * message, gpointer user_data);
static void license_request_finished_cb (SoupSession * session, SoupMessage * msg, gpointer user_data);

typedef struct {
    GstEmeAutoDecryptBin *bin;
    GstMediaKeySession *media_key_session; // Need to track the target session
    // Add promise tracking here if needed
} LicenseRequestContext;


G_DEFINE_TYPE (GstEmeAutoDecryptBin, gst_eme_auto_decrypt_bin, GST_TYPE_BIN);

static void
gst_eme_auto_decrypt_bin_class_init (GstEmeAutoDecryptBinClass * klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

    GST_DEBUG_CATEGORY_INIT (gst_eme_auto_decrypt_bin_debug, "emeautodecryptbin", 0,
        "EME Auto Decrypt Bin");

    gobject_class->set_property = gst_eme_auto_decrypt_bin_set_property;
    gobject_class->get_property = gst_eme_auto_decrypt_bin_get_property;
    gobject_class->dispose = gst_eme_auto_decrypt_bin_dispose;
    gobject_class->finalize = gst_eme_auto_decrypt_bin_finalize;

    obj_properties[PROP_LAURLS] =
        g_param_spec_string ("laurls", "License URL(s)",
        "URL of the license acquisition server", NULL,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_KEY_SYSTEMS] =
        g_param_spec_string ("key-systems", "Key System",
        "DRM Key System identifier string (e.g., com.widevine.alpha)", NULL,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (gobject_class, N_PROPERTIES, obj_properties);

    gst_element_class_set_static_metadata (element_class,
        "EME Auto Decrypt Bin", "Bin/EME/Decryptor",
        "Automatically handles EME license acquisition based on properties",
        "Your Name <your.email@example.com>");

    element_class->change_state = GST_DEBUG_FUNCPTR (gst_eme_auto_decrypt_bin_change_state);

    // No static pads needed for a bin, pads are added dynamically/ghosted
}

static void
gst_eme_auto_decrypt_bin_init (GstEmeAutoDecryptBin * self)
{
    self->laurls = NULL;
    self->key_systems = NULL;
    self->bus_watch_id = 0;
    self->decryptor = NULL;
    self->sinkpad = NULL;
    self->srcpad = NULL;

    // Create the SoupSession
    // Consider making this configurable (sync vs async)
    self->soup_session = soup_session_new ();
    if (!self->soup_session) {
        GST_ERROR_OBJECT(self, "Failed to create SoupSession");
        // Consider failing init?
    }

    // Create internal decryptor - choose one known to exist
    // Prefer emeopencdmdecryptor if that's what you have
    self->decryptor = gst_element_factory_make ("emeopencdmdecryptor", "internal_decryptor");
    // Fallback or alternative:
    // if (!self->decryptor) {
    //     self->decryptor = gst_element_factory_make ("sparkledecryptor", "internal_decryptor");
    // }

    if (!self->decryptor) {
        GST_ERROR_OBJECT (self, "Failed to create internal decryptor element.");
        // Consider failing init?
        return;
    }

    // Add internal element to the bin
    if (!gst_bin_add (GST_BIN (self), self->decryptor)) {
        GST_ERROR_OBJECT (self, "Failed to add internal decryptor to bin.");
        gst_object_unref(self->decryptor);
        self->decryptor = NULL;
        return;
    }

    // Get pads from internal element BEFORE ghosting
    self->sinkpad = gst_element_get_static_pad (self->decryptor, "sink");
    self->srcpad = gst_element_get_static_pad (self->decryptor, "src");

    if (!self->sinkpad || !self->srcpad) {
         GST_ERROR_OBJECT (self, "Internal decryptor missing required pads.");
         // Clean up needed
         return;
    }

    // Add ghost pads
    if (!gst_element_add_pad (GST_ELEMENT (self), gst_ghost_pad_new ("sink", self->sinkpad))) {
        GST_WARNING_OBJECT(self, "Failed to add ghost sink pad");
    }
     if (!gst_element_add_pad (GST_ELEMENT (self), gst_ghost_pad_new ("src", self->srcpad))) {
        GST_WARNING_OBJECT(self, "Failed to add ghost src pad");
    }

    // Unref pads obtained with get_static_pad
    gst_object_unref (self->sinkpad);
    gst_object_unref (self->srcpad);
    self->sinkpad = NULL; // Should use the ghost pads now
    self->srcpad = NULL;
}

static void
gst_eme_auto_decrypt_bin_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
    GstEmeAutoDecryptBin *self = GST_EME_AUTO_DECRYPT_BIN (object);

    switch (property_id) {
        case PROP_LAURLS:
            g_free (self->laurls);
            self->laurls = g_value_dup_string (value);
            GST_INFO_OBJECT(self, "License URL set to: %s", self->laurls ? self->laurls : "(null)");
            break;
        case PROP_KEY_SYSTEMS:
            g_free (self->key_systems);
            self->key_systems = g_value_dup_string (value);
             GST_INFO_OBJECT(self, "Key system set to: %s", self->key_systems ? self->key_systems : "(null)");
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
gst_eme_auto_decrypt_bin_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
    GstEmeAutoDecryptBin *self = GST_EME_AUTO_DECRYPT_BIN (object);

    switch (property_id) {
        case PROP_LAURLS:
            g_value_set_string (value, self->laurls);
            break;
         case PROP_KEY_SYSTEMS:
            g_value_set_string (value, self->key_systems);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
remove_bus_watch (GstEmeAutoDecryptBin * self) {
    if (self->bus_watch_id != 0) {
        GST_DEBUG_OBJECT(self, "Removing bus watch id %u", self->bus_watch_id);
        g_source_remove (self->bus_watch_id);
 аспекты>self->bus_watch_id = 0;
    }
}

static void
setup_bus_watch (GstEmeAutoDecryptBin * self) {
    GstBus *bus = NULL;

    if (self->bus_watch_id != 0) {
        GST_DEBUG_OBJECT(self, "Bus watch already exists");
        return;
    }

    bus = gst_element_get_bus (GST_ELEMENT (self));
    if (!bus) {
        GST_WARNING_OBJECT(self, "Element has no bus, cannot watch for messages");
        return;
    }

    self->bus_watch_id = gst_bus_add_watch_full (bus, G_PRIORITY_DEFAULT,
                                               bus_message_cb, gst_object_ref(self),
                                               (GDestroyNotify)gst_object_unref);
    gst_object_unref (bus);
    GST_DEBUG_OBJECT(self, "Added bus watch id %u", self->bus_watch_id);
}

static void
gst_eme_auto_decrypt_bin_dispose (GObject * object)
{
    GstEmeAutoDecryptBin *self = GST_EME_AUTO_DECRYPT_BIN (object);

    GST_DEBUG_OBJECT (self, "Disposing");

    remove_bus_watch(self);

    // Unref internal element if it exists
    if (self->decryptor) {
        // Ensure it's removed from the bin before unreffing fully outside bin lock
        gst_bin_remove(GST_BIN(self), self->decryptor);
        gst_clear_object (&self->decryptor);
    }
     // Unref soup session
    g_clear_object(&self->soup_session);


    G_OBJECT_CLASS (gst_eme_auto_decrypt_bin_parent_class)->dispose (object);
}

static void
gst_eme_auto_decrypt_bin_finalize (GObject * object)
{
    GstEmeAutoDecryptBin *self = GST_EME_AUTO_DECRYPT_BIN (object);

    GST_DEBUG_OBJECT (self, "Finalizing");

    g_free (self->laurls);
    g_free (self->key_systems);
    self->laurls = NULL;
    self->key_systems = NULL;

    G_OBJECT_CLASS (gst_eme_auto_decrypt_bin_parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_eme_auto_decrypt_bin_change_state (GstElement * element, GstStateChange transition)
{
    GstEmeAutoDecryptBin *self = GST_EME_AUTO_DECRYPT_BIN (element);
    GstStateChangeReturn ret;

    GST_DEBUG_OBJECT (self, "Changing state %s -> %s",
        gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
        gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

    // Handle bus watch based on state transitions
    switch (transition) {
        case GST_STATE_CHANGE_NULL_TO_READY:
        case GST_STATE_CHANGE_READY_TO_PAUSED:
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING: // Set up earlier is fine too
             setup_bus_watch(self);
            break;
        default:
            break;
    }

    ret = GST_ELEMENT_CLASS (gst_eme_auto_decrypt_bin_parent_class)->change_state (element, transition);
    if (ret == GST_STATE_CHANGE_FAILURE) {
         remove_bus_watch(self); // Clean up watch on failure
        return ret;
    }

     switch (transition) {
        case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
        case GST_STATE_CHANGE_PAUSED_TO_READY:
        case GST_STATE_CHANGE_READY_TO_NULL:
            remove_bus_watch(self);
            break;
        default:
            break;
    }

    return ret;
}

// Callback for the HTTP request completion
static void
license_request_finished_cb (SoupSession * session, SoupMessage * msg, gpointer user_data)
{
    LicenseRequestContext *ctx = (LicenseRequestContext *) user_data;
    GstEmeAutoDecryptBin *self = ctx->bin;
    GstMediaKeySession *media_key_session = ctx->media_key_session; // Get the session pointer
    GstBuffer *response_buffer = NULL;
    GstPromise *update_promise = NULL;

    // Check if the session object is still valid
    if (!media_key_session || !GST_IS_MEDIA_KEY_SESSION(media_key_session)) {
        GST_WARNING_OBJECT(self, "Media key session is no longer valid in callback");
        goto cleanup;
    }

    GST_INFO_OBJECT (self, "License request finished, status: %u %s",
        msg->status_code, msg->reason_phrase);

    if (SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
        if (msg->response_body && msg->response_body->data) {
            // Create GstBuffer from response body (takes ownership)
            response_buffer = gst_buffer_new_wrapped_bytes(
                g_bytes_new_take(msg->response_body->data, msg->response_body->length));
             // Need to ensure response_body data is not freed by libsoup now
             msg->response_body->data = NULL;
             msg->response_body->length = 0;

            GST_INFO_OBJECT(self, "Received license response, size: %" G_GSIZE_FORMAT,
                            gst_buffer_get_size(response_buffer));

            // Call gst_media_key_session_update
            // IMPORTANT: Need the correct GstMediaKeySession object here!
            // Using the one passed in user_data (needs careful management).
            update_promise = gst_promise_new();
            gst_media_key_session_update(media_key_session, response_buffer, update_promise);

            // Optionally wait for or handle the update promise result
            // For simplicity, we are not waiting here. A real app might.
            GST_DEBUG_OBJECT(self, "Called gst_media_key_session_update");
            // gst_promise_unref(update_promise); // Unref if not waiting

        } else {
            GST_ERROR_OBJECT (self, "License server returned success but no response body");
            gst_element_post_message(GST_ELEMENT(self),
                gst_message_new_error(GST_OBJECT(self), g_error_new(GST_CORE_ERROR, GST_CORE_ERROR_FAILED, "License server success but no body"), "License Error"));
        }
    } else {
        GST_ERROR_OBJECT (self, "License request failed: %u %s", msg->status_code, msg->reason_phrase);
        if (msg->response_body && msg->response_body->data) {
             GST_ERROR_OBJECT (self, "Server response body: %.*s", (int)msg->response_body->length, msg->response_body->data);
        }
         gst_element_post_message(GST_ELEMENT(self),
                gst_message_new_error(GST_OBJECT(self), g_error_new(GST_NETWORK_ERROR, GST_NETWORK_ERROR_FAILED, "License request failed: %u", msg->status_code), "License Error"));
        // Here you might try rejecting the original promise if you tracked it
    }

cleanup:
    gst_clear_buffer(&response_buffer);
    g_clear_pointer(&update_promise, gst_promise_unref); // Unref promise if we created it
    // Unref the objects we passed in user_data
    gst_object_unref(media_key_session);
    gst_object_unref(self);
    g_slice_free(LicenseRequestContext, ctx);
}

// Callback for bus messages
static gboolean
bus_message_cb (GstBus * bus, GstMessage * message, gpointer user_data)
{
    GstEmeAutoDecryptBin *self = GST_EME_AUTO_DECRYPT_BIN (user_data);
    GstBuffer *challenge_buffer = NULL;
    gboolean keep_watch = TRUE;

    // Filter messages: Only interested in messages from our internal decryptor
    if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (self->decryptor)) {
        return TRUE; // Continue watching
    }

    GST_DEBUG_OBJECT(self, "Received bus message from decryptor: %s", GST_MESSAGE_TYPE_NAME(message));

    // Check if it's the license request message using the utility function
    if (gst_message_parse_eme_license_request (message, &challenge_buffer)) {
        GST_INFO_OBJECT (self, "Got EME license request message with challenge");

        if (!self->laurls) {
            GST_ERROR_OBJECT (self, "License request received, but 'laurls' property not set!");
            gst_element_post_message(GST_ELEMENT(self),
                gst_message_new_error(GST_OBJECT(self), g_error_new(GST_CORE_ERROR, GST_CORE_ERROR_MISSING_PLUGIN, "License URL not set"), "Configuration Error")); // Use appropriate error
            goto cleanup;
        }
        if (!self->soup_session) {
             GST_ERROR_OBJECT (self, "SoupSession is not initialized!");
             goto cleanup;
        }

        // --- Find the Media Key Session ---
        // THIS IS THE SIMPLIFIED/ASSUMED PART:
        // We assume the message source *is* the session object.
        // In reality, you might need a more robust way (e.g., querying context)
        GstMediaKeySession *session = GST_MEDIA_KEY_SESSION(gst_object_ref(GST_MESSAGE_SRC(message))); // REF the source
        if (!GST_IS_MEDIA_KEY_SESSION(session)) {
             GST_ERROR_OBJECT(self, "Message source is not a GstMediaKeySession!");
             gst_clear_object(&session);
             goto cleanup;
        }
         GST_INFO_OBJECT(self, "Identified target session: %p", session);
        // ---

        // Prepare the request
        SoupMessage *soup_msg = soup_message_new (SOUP_METHOD_POST, self->laurls);
        if (!soup_msg) {
            GST_ERROR_OBJECT(self, "Failed to create SoupMessage");
            gst_object_unref(session);
            goto cleanup;
        }

        // Set request body from challenge buffer
        GstMapInfo map;
        if (gst_buffer_map (challenge_buffer, &map, GST_MAP_READ)) {
             soup_message_set_request_body_from_bytes (soup_msg, "application/octet-stream",
                                                  g_bytes_new_static (map.data, map.size));
             gst_buffer_unmap (challenge_buffer, &map);
        } else {
             GST_ERROR_OBJECT(self, "Failed to map challenge buffer");
             g_object_unref(soup_msg);
             gst_object_unref(session);
             goto cleanup;
        }

        // Add any required custom headers here based on DRM provider needs
        // soup_message_headers_append(soup_msg->request_headers, "X-Custom-Auth", "YourToken");

        // Prepare user data for the callback
        LicenseRequestContext *ctx = g_slice_new(LicenseRequestContext);
        ctx->bin = gst_object_ref(self); // Ref self for callback
        ctx->media_key_session = session; // Transfer ref to context (will be unreffed in callback)

        GST_INFO_OBJECT(self, "Queueing license request to %s", self->laurls);

        // Queue the async request
        soup_session_queue_message (self->soup_session, soup_msg,
                                    license_request_finished_cb, ctx); // Pass context

        g_object_unref (soup_msg); // soup session takes ownership

        // We've started handling it
        keep_watch = TRUE; // Keep watch active
    } else {
        // Handle other messages from the decryptor if necessary
        // e.g., GST_MESSAGE_EME_HAVE_KEY
    }

cleanup:
    gst_clear_buffer (&challenge_buffer);
    return keep_watch; // Keep the watch active unless we explicitly remove it
}

// --- Plugin Entry Point ---

// Defined in lib.rs or a separate file if using standard plugin template
// static gboolean plugin_init (GstPlugin * plugin) { ... register element ... }
// GST_PLUGIN_DEFINE (...)
