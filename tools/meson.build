
subdir('nss')

with_tools_host_validate = get_option('with-tools-host-validate')
if not (with_driver_qemu or with_driver_lxc or with_driver_bhyve)
  with_tools_host_validate = false
endif

with_tools_login_shell = get_option('with-tools-login-shell')
if not with_driver_lxc
  with_tools_login_shell = false
endif

if with_tools_host_validate
  virt_host_validate_src = [
    'virt-host-validate.c',
    'virt-host-validate-common.c',
    'virt-host-validate-common.h',
  ]

  if with_driver_qemu
    virt_host_validate_src += [
      'virt-host-validate-qemu.c',
      'virt-host-validate-qemu.h',
    ]
  endif

  if with_driver_lxc
    virt_host_validate_src += [
      'virt-host-validate-lxc.c',
      'virt-host-validate-lxc.h',
    ]
  endif

  if with_driver_bhyve
    virt_host_validate_src += [
      'virt-host-validate-bhyve.c',
      'virt-host-validate-bhyve.h',
    ]
  endif

  virt_host_validate_inc = common_inc_dir
  virt_host_validate_libs = [libvirt_dep, libxml_dep, gnulib_dep]
  virt_host_validate = executable(
    'virt-host-validate',
    install: true,
    sources: virt_host_validate_src,
    include_directories: virt_host_validate_inc,
    dependencies: virt_host_validate_libs
  )

  virt_host_validate_man = custom_target(
    'virt_host_validate_man',
    output : 'virt-host-validate.1',
    input : 'virt-host-validate.pod',
    command : [
      pod2man,
      '--section=1',
      '--center=Virtualization Support',
      '--release=libvirt @0@'.format(meson.project_version()),
      '@INPUT@', '@OUTPUT@'
    ],
    install : true,
    install_dir : join_paths(get_option('mandir'), 'man1')
  )
endif

if with_tools_login_shell
  virt_login_shell_inc = [
    top_inc_dir,
  ]
  virt_login_shell_src = [
    'virt-login-shell.c',
  ]
  virt_login_shell = executable(
    'virt-login-shell',
    install: true,
    sources: virt_login_shell_src,
    include_directories: virt_login_shell_inc,
    # We'll let downstream turn on setuid bit if they
    # feel it is justified for their needs
    #install_mode: ['rwsr-x---', 'root', 'root'],
  )

  virt_login_shell_helper_src = [
    'virt-login-shell-helper.c',
  ]
  virt_login_shell_helper_inc = common_inc_dir
  virt_login_shell_helper_libs = [libvirt_dep, libvirt_lxc_dep, libxml_dep, gnulib_dep]
  virt_login_shell_helper = executable(
    'virt-login-shell-helper',
    install: true,
    install_dir: libexec_dir,
    sources: virt_login_shell_helper_src,
    include_directories: virt_login_shell_helper_inc,
    dependencies: virt_login_shell_helper_libs
  )

  install_data(
    'virt-login-shell.conf',
    install_dir: pkg_sys_conf_dir,
  )

  virt_login_shell_man = custom_target(
    'virt_login_shell_man',
    output : 'virt-login-shell.1',
    input : 'virt-login-shell.pod',
    command : [
      pod2man,
      '--section=1',
      '--center=Virtualization Support',
      '--release=libvirt @0@'.format(meson.project_version()),
      '@INPUT@', '@OUTPUT@'
    ],
    install : true,
    install_dir : join_paths(get_option('mandir'), 'man1')
  )
endif

libvirt_shell_src = [
  'vsh.c',
  'vsh.h',
  'vsh-table.c',
  'vsh-table.h',
]
libvirt_shell_inc = common_inc_dir
libvirt_shell_libs = [libvirt_dep, readline_dep, libxml_dep, gnulib_dep]
libvirt_shell = static_library(
  'virt-shell',
  libvirt_shell_src,
  include_directories: libvirt_shell_inc,
  dependencies: libvirt_shell_libs,
)
libvirt_shell_dep = declare_dependency(
  link_with: libvirt_shell,
)


virsh_src = [
  'virsh.c',
  'virsh.h',
  'virsh-checkpoint.c',
  'virsh-checkpoint.h',
  'virsh-completer.c',
  'virsh-completer.h',
  'virsh-completer-domain.c',
  'virsh-completer-domain.h',
  'virsh-completer-checkpoint.c',
  'virsh-completer-checkpoint.h',
  'virsh-completer-host.c',
  'virsh-completer-host.h',
  'virsh-completer-interface.c',
  'virsh-completer-interface.h',
  'virsh-completer-network.c',
  'virsh-completer-network.h',
  'virsh-completer-nodedev.c',
  'virsh-completer-nodedev.h',
  'virsh-completer-nwfilter.c',
  'virsh-completer-nwfilter.h',
  'virsh-completer-pool.c',
  'virsh-completer-pool.h',
  'virsh-completer-secret.c',
  'virsh-completer-secret.h',
  'virsh-completer-snapshot.c',
  'virsh-completer-snapshot.h',
  'virsh-completer-volume.c',
  'virsh-completer-volume.h',
  'virsh-console.c',
  'virsh-console.h',
  'virsh-domain.c',
  'virsh-domain.h',
  'virsh-domain-monitor.c',
  'virsh-domain-monitor.h',
  'virsh-host.c',
  'virsh-host.h',
  'virsh-interface.c',
  'virsh-interface.h',
  'virsh-network.c',
  'virsh-network.h',
  'virsh-nodedev.c',
  'virsh-nodedev.h',
  'virsh-nwfilter.c',
  'virsh-nwfilter.h',
  'virsh-pool.c',
  'virsh-pool.h',
  'virsh-secret.c',
  'virsh-secret.h',
  'virsh-snapshot.c',
  'virsh-snapshot.h',
  'virsh-util.c',
  'virsh-util.h',
  'virsh-volume.c',
  'virsh-volume.h',
]
virsh_inc = common_inc_dir
virsh_libs = [libvirt_shell_dep, libvirt_qemu_dep, libvirt_lxc_dep, libxml_dep, threads_dep, gnulib_dep]

virsh = executable(
  'virsh',
  install: true,
  sources: virsh_src,
  include_directories: virsh_inc,
  dependencies: virsh_libs,
)
virsh_man = custom_target(
  'virsh_man',
  output : 'virsh.1',
  input : 'virsh.pod',
  command : [
    pod2man,
    '--section=1',
    '--center=Virtualization Support',
    '--release=libvirt @0@'.format(meson.project_version()),
    '@INPUT@', '@OUTPUT@'
  ],
  install : true,
  install_dir : join_paths(get_option('mandir'), 'man1')
)


virt_admin_src = [
  'virt-admin.c',
  'virt-admin.h',
  'virt-admin-completer.c',
  'virt-admin-completer.h'
]
virt_admin_inc = common_inc_dir
virt_admin_libs = [libvirt_shell_dep, libvirt_admin_dep, libxml_dep, threads_dep, gnulib_dep]

virt_admin = executable(
  'virt-admin',
  install: true,
  sources: virt_admin_src,
  include_directories: virt_admin_inc,
  dependencies: virt_admin_libs,
)
virt_admin_man = custom_target(
  'virt_admin_man',
  output : 'virt-admin.1',
  input : 'virt-admin.pod',
  command : [
    pod2man,
    '--section=1',
    '--center=Virtualization Support',
    '--release=libvirt @0@'.format(meson.project_version()),
    '@INPUT@', '@OUTPUT@'
  ],
  install : true,
  install_dir : join_paths(get_option('mandir'), 'man1')
)

subdir('bash-completion')
