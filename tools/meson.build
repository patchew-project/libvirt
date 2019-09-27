
subdir('nss')

with_tools_host_validate = get_option('with-tools-host-validate')
if not (with_driver_qemu or with_driver_lxc or with_driver_bhyve)
  with_tools_host_validate = false
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
