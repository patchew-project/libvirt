
if host_machine.system() == 'freebsd'
  with_nss = true
  libnss_prefix = ''
  libnss_so_version = '1'
  libnss_syms = 'libvirt_nss_bsd.syms'
  libnss_guest_syms = 'libvirt_nss_bsd.syms'
elif host_machine.system() == 'linux'
  with_nss = true
  libnss_prefix = 'lib'
  libnss_so_version = '2'
  libnss_syms = 'libvirt_nss.syms'
  libnss_guest_syms = 'libvirt_guest_nss.syms'
else
  with_nss = false
endif

if with_nss
  libnss_common_src = [
    'libvirt_nss.c',
    'libvirt_nss.h',
    'libvirt_nss_leases.c',
    'libvirt_nss_leases.h'
  ]

  libnss_src = libnss_common_src
  libnss_guest_src = libnss_common_src + [
    'libvirt_nss_macs.c',
    'libvirt_nss_macs.h',
  ]

  libnss_deps = [yajl_dep]
  libnss_link_args = [
    '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), libnss_syms)
  ]
  libnss_guest_link_args = [
    '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), libnss_guest_syms)
  ]
  libnss_inc_dirs = [top_inc_dir]

  libnss_libvirt_so = shared_library('nss_libvirt',
				     libnss_src,
				     name_prefix: libnss_prefix,
				     version: libnss_so_version,
				     c_args: ['-DLIBVIRT_NSS'],
				     link_args: libnss_link_args,
				     dependencies: libnss_deps,
				     include_directories: libnss_inc_dirs,
				     link_depends: libnss_syms)

  libnss_libvirt_guest_so = shared_library('nss_libvirt_guest',
					   libnss_guest_src,
					   name_prefix: libnss_prefix,
					   version: libnss_so_version,
					   c_args: ['-DLIBVIRT_NSS',
						    '-DLIBVIRT_NSS_GUEST'],
					   link_args: libnss_guest_link_args,
					   dependencies: libnss_deps,
					   include_directories: libnss_inc_dirs,
					   link_depends: libnss_guest_syms)


endif
