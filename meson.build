project('gst-emeautodecrypt', 'c',
        version : '1.0.0',
        license : 'LGPL-2.1+',
        meson_version : '>= 0.56.0')

gst_version = meson.project_version()
apiversion = '1.0' # GStreamer API version

# GStreamer Dependencies
gst_dep = dependency('gstreamer-1.0', version : '>= 1.20', required : true)
gstbase_dep = dependency('gstreamer-base-1.0', version : '>= 1.20', required : true)
# GStreamer EME Dependency (usually part of gst-plugins-bad)
gstbadbase_dep = dependency('gstreamer-plugins-bad-1.0', version : '>= 1.24', required : true, fallback : ['gst-plugins-bad', 'gstbadbase_dep'])
# Libsoup Dependency
libsoup_dep = dependency('libsoup-3.0', version : '>= 3.0', required: true)

# GLib Dependency (usually pulled in by gstreamer)
glib_dep = dependency('glib-2.0', version : '>= 2.60', required : true)
gobject_dep = dependency('gobject-2.0', version : '>= 2.60', required : true)

plugin_sources = [
  'gstemeautodecryptbin.c',
  # Add other .c files if you split implementation
  # 'gstplugin.c', # If you have a separate plugin_init file
]

# Ensure you have a gstplugin.c file with plugin_init and GST_PLUGIN_DEFINE
# or put them directly in gstemeautodecryptbin.c

gstemeautodecrypt = library('gstemeautodecrypt',
                           plugin_sources,
                           c_args : ['-DHAVE_CONFIG_H'], # If using config.h
                           include_directories : [configinc], # If using config.h
                           dependencies : [gst_dep, gstbase_dep, gstbadbase_dep, libsoup_dep, glib_dep, gobject_dep],
                           install : true,
                           install_dir : join_paths(get_option('libdir'), 'gstreamer-1.0'))

pkgconfig = import('pkgconfig')
pkgconfig.generate(gstemeautodecrypt, # Use library object here
    libraries : [gst_dep, gstbase_dep, gstbadbase_dep, libsoup_dep, glib_dep, gobject_dep], # List dependencies for pkg-config
    variables : ['plugindir=${libdir}/gstreamer-1.0'],
    version : gst_version,
    name : 'gstreamer-emeautodecrypt-1.0',
    description : 'GStreamer EME Auto Decrypt Bin',
    # url : '', # Add project URL
    # requires_private: '', # Add internal deps if needed
    install_dir : join_paths(get_option('datadir'), 'pkgconfig')
)
