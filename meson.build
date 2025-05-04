project('gstemeautodecryptbin', 'c',
  version : '1.0.0',
  meson_version : '>= 0.50.0',
  default_options : [ 'warning_level=1',
                       'buildtype=debugoptimized' ])

# Configuration data for config.h
conf_data = configuration_data()
conf_data.set_quoted('PACKAGE', 'gstemeautodecryptbin')
conf_data.set_quoted('VERSION', meson.project_version())
conf_data.set('GST_LICENSE', '"LGPL"')
conf_data.set('PACKAGE_BUGREPORT', '"https://github.com/yourusername/gstemeautodecryptbin/issues"')
conf_data.set('PACKAGE_NAME', '"GStreamer EME Auto Decrypt Bin"')
conf_data.set('PACKAGE_STRING', '"GStreamer EME Auto Decrypt Bin @0@"'.format(meson.project_version()))
conf_data.set('PACKAGE_TARNAME', '"gstemeautodecryptbin"')
conf_data.set('PACKAGE_URL', '"https://github.com/yourusername/gstemeautodecryptbin"')
conf_data.set('PACKAGE_VERSION', '"@0@"'.format(meson.project_version()))

# Generate config.h
configure_file(output : 'config.h',
               configuration : conf_data)

# Include directories
configinc = include_directories('.')

gst_version = '1.0'
cc = meson.get_compiler('c')

# Dependencies
gst_dep = dependency('gstreamer-1.0')
gstbase_dep = dependency('gstreamer-base-1.0')
soup_dep = dependency('libsoup-3.0')
eme_dep = dependency('gstreamer-eme-1.0')
xml_dep = dependency('libxml-2.0')  # Add libxml2 dependency

# Define GST_USE_UNSTABLE_API to suppress warnings
add_project_arguments('-DGST_USE_UNSTABLE_API', language: 'c')

plugin_sources = [
  'gstemeautodecryptbin.c',
]

plugin = shared_library('gstemeautodecryptbin',
  plugin_sources,
  dependencies : [gst_dep, gstbase_dep, soup_dep, eme_dep, xml_dep],  # Add xml_dep here
  install : true,
  install_dir : get_option('libdir') / 'gstreamer-1.0',
  c_args : ['-DHAVE_CONFIG_H'],
  include_directories : [configinc],
)
