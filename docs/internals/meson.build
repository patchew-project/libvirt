internals_in_files = [
  'command',
  'eventloop',
  'locking',
  'rpc',
]

foreach name : internals_in_files
  html_in_file = '@0@.html.in'.format(name)
  html_file = '@0@.html'.format(name)

  out_file = custom_target(
    html_file,
    input: html_in_file,
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
    install_dir: docs_html_dir / 'internals',
  )

  install_web_deps += out_file
  install_web_files += '@0@:@1@'.format(out_file.full_path(), docs_html_dir / 'internals')
endforeach
