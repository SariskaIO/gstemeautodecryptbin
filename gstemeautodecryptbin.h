#ifndef __GST_EME_AUTO_DECRYPT_BIN_H__
#define __GST_EME_AUTO_DECRYPT_BIN_H__

#include <gst/gst.h>
#include <gst/gstbin.h>
#include <libsoup/soup.h>

G_BEGIN_DECLS

#define GST_TYPE_EME_AUTO_DECRYPT_BIN (gst_eme_auto_decrypt_bin_get_type())
G_DECLARE_FINAL_TYPE (GstEmeAutoDecryptBin, gst_eme_auto_decrypt_bin, GST, EME_AUTO_DECRYPT_BIN, GstBin)

struct _GstEmeAutoDecryptBin {
    GstBin parent_instance;

    // Internal decryptor
    GstElement *decryptor;

    // Properties
    gchar *laurls;
    gchar *key_systems; // Note: Only uses the first one if multiple provided

    // HTTP Session
    SoupSession *soup_session;

    // Bus Watch
    guint bus_watch_id;

    // Pad pointers for ghosting
    GstPad *sinkpad;
    GstPad *srcpad;
};

G_END_DECLS

#endif /* __GST_EME_AUTO_DECRYPT_BIN_H__ */
