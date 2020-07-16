docs_kbase_files = [
  'backing_chains',
  'domainstatecapture',
  'launch_security_sev',
  'locking-lockd',
  'locking',
  'locking-sanlock',
  'migrationinternals',
  'qemu-passthrough-security',
  'rpm-deployment',
  'secureusage',
  'virtiofs',
]

foreach name : docs_kbase_files
  rst_file = '@0@.rst'.format(name)
  html_file = '@0@.html'.format(name)

  html_in = docs_rst2html_gen.process(rst_file)

  out_file = custom_target(
    html_file,
    input: html_in,
    output: html_file,
    command: [
      meson_python_prog,
      meson_html_gen_prog,
      xsltproc_prog.path(),
      xmllint_prog.path(),
      meson.build_root(),
      docs_timestamp,
      site_xsl,
      '@INPUT@',
      '@OUTPUT@',
    ],
    depends: [ aclperms_gen ],
    depend_files: [ page_xsl ],
    install: true,
    install_dir: docs_html_dir / 'kbase',
  )

  install_web_deps += out_file
  install_web_files += '@0@:@1@'.format(out_file.full_path(), docs_html_dir / 'kbase')
endforeach
