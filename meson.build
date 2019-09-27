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

public_inc_dir = include_directories('include')
gnulib_inc_dir = include_directories('gnulib/lib')
top_inc_dir = include_directories('.')

cc = meson.get_compiler('c')
pod2man = find_program('pod2man')

gnulib = cc.find_library('libgnu', dirs: [meson.build_root() / 'gnulib' / 'lib' / '.libs'])
gnulib_dep = declare_dependency(
  dependencies: [gnulib],
  include_directories: [gnulib_inc_dir]
)

meson.add_dist_script('build-aux' / 'dist.py', meson.source_root(), meson.build_root())

yajl_min_version = '>= 2.0.3'
libxml_min_version = '>= 2.9.1'

yajl_dep = dependency('yajl', version: yajl_min_version)
libxml_dep = dependency('libxml-2.0', version: libxml_min_version)

subdir('src')
subdir('examples')
subdir('tools')
