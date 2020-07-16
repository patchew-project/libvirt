syntax_check_conf = configuration_data()
syntax_check_conf.set('top_srcdir', meson.source_root())
syntax_check_conf.set('top_builddir', meson.build_root())

flake8_path = ''
if flake8_prog.found()
  flake8_path = flake8_prog.path()
endif
syntax_check_conf.set('flake8_path', flake8_path)
syntax_check_conf.set('runutf8', ' '.join(runutf8))

configure_file(
  input: 'Makefile.in',
  output: '@BASENAME@',
  configuration: syntax_check_conf,
)

make_prog = find_program('make')

# There is no way how to pass value to -j using run_target so let's use
# it without any value to run all tests in parallel.
run_target(
  'syntax-check',
  command: [
    make_prog, '-C', meson.current_build_dir(), '-j', 'syntax-check',
  ],
  depends: [
    potfiles_dep,
  ],
)
