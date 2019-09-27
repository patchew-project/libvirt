project(
  'libvirt',
  'c',
  version: '5.8.0',
  license: 'LGPLv2+',
  default_options: [
    'buildtype=debugoptimized',
    'c_std=gnu99',
  ],
  meson_version: '>= 0.49.0'
)

prefix = get_option('prefix')
bin_dir = get_option('bindir')
lib_dir = get_option('libdir')
sys_conf_dir = get_option('sysconfdir')
data_dir = get_option('datadir')
local_state_dir = get_option('localstatedir')
pkg_data_dir = data_dir / meson.project_name()
pkg_doc_dir = data_dir / 'doc' / meson.project_name() + '-' + meson.project_version()

meson.add_dist_script('build-aux' / 'dist.py', meson.source_root(), meson.build_root())
