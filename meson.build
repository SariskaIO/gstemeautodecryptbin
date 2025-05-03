project('gstemeautodecryptbin', 'c',
  version : '1.0.0',
  meson_version : '>= 0.50.0',
  default_options : [ 'warning_level=1',
                       'buildtype=debugoptimized' ])

gst_version = '1.0'

cc = meson.get_compiler('c')

gst_dep = dependency('gstreamer-1.0')
gstbase_dep = dependency('gstreamer-base-1.0')
soup_dep = dependency('libsoup-3.0')
eme_dep = dependency('gstreamer-eme-1.0')  # Add this line

# Define GST_USE_UNSTABLE_API to suppress warnings
add_project_arguments('-DGST_USE_UNSTABLE_API', language: 'c')

plugin_sources = [
  'gstemeautodecryptbin.c',
]

plugin = shared_library('gstemeautodecryptbin',
  plugin_sources,
  dependencies : [gst_dep, gstbase_dep, soup_dep, eme_dep],
  install : true,
  install_dir : get_option('libdir') / 'gstreamer-1.0',
  c_args : ['-DHAVE_CONFIG_H'],
)
