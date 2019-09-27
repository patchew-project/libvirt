
libvirt = cc.find_library('libvirt', dirs: [meson.build_root() / 'src' / '.libs'])
libvirt_admin = cc.find_library('libvirt-admin', dirs: [meson.build_root() / 'src' / '.libs'])
libvirt_qemu = cc.find_library('libvirt-qemu', dirs: [meson.build_root() / 'src' / '.libs'])
libvirt_lxc = cc.find_library('libvirt-lxc', dirs: [meson.build_root() / 'src' / '.libs'])

libvirt_dep = declare_dependency(
  dependencies: libvirt,
  include_directories: [public_inc_dir]
)

libvirt_admin_dep = declare_dependency(
  dependencies: libvirt_admin,
  include_directories: [public_inc_dir]
)

libvirt_qemu_dep = declare_dependency(
  dependencies: libvirt_qemu,
  include_directories: [public_inc_dir]
)

libvirt_lxc_dep = declare_dependency(
  dependencies: libvirt_lxc,
  include_directories: [public_inc_dir]
)

# General
subdir('remote')

# Primary virt drivers
subdir('bhyve')
subdir('esx')
subdir('hyperv')
subdir('libxl')
subdir('lxc')
subdir('openvz')
subdir('phyp')
subdir('qemu')
subdir('test')
subdir('vbox')
subdir('vz')
subdir('vmware')

# Secondary drivers
subdir('interface')
subdir('network')
subdir('node_device')
subdir('nwfilter')
subdir('secret')
subdir('storage')
