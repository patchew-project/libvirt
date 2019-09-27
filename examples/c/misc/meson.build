
examples_misc_dir = examples_dir / 'c' / 'misc'
examples_misc_deps = [libvirt_dep]

hellolibvirt_src = ['hellolibvirt.c']
hellolibvirt = executable(
  'hellolibvirt',
  hellolibvirt_src,
  dependencies: examples_misc_deps
)
install_data(hellolibvirt_src, install_dir: examples_misc_dir)

eventtest_src = ['event-test.c']
eventtest = executable(
  'event-test',
  eventtest_src,
  dependencies: examples_misc_deps
)
install_data(eventtest_src, install_dir: examples_misc_dir)

openauth_src = ['openauth.c']
openauth = executable(
  'openauth',
  openauth_src,
  dependencies: examples_misc_deps
)
install_data(openauth_src, install_dir: examples_misc_dir)
