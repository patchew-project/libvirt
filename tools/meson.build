
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
