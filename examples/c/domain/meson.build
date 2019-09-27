
examples_domain_dir = examples_dir / 'c' / 'domain'
examples_domain_deps = [libvirt_dep]

dommigrate_src = ['dommigrate.c']
dommigrate = executable(
  'dommigrate',
  dommigrate_src,
  dependencies: examples_domain_deps
)
install_data(dommigrate_src, install_dir: examples_domain_dir)

domtop_src = ['domtop.c']
domtop = executable(
  'domtop',
  domtop_src,
  dependencies: examples_domain_deps
)
install_data(domtop_src, install_dir: examples_domain_dir)

info1_src = ['info1.c']
info1 = executable(
  'info1',
  info1_src,
  dependencies: examples_domain_deps
)
install_data(info1_src, install_dir: examples_domain_dir)

rename_src = ['rename.c']
rename = executable(
  'rename',
  rename_src,
  dependencies: examples_domain_deps
)
install_data(rename_src, install_dir: examples_domain_dir)

suspend_src = ['suspend.c']
suspend = executable(
  'suspend',
  suspend_src,
  dependencies: examples_domain_deps
)
install_data(suspend_src, install_dir: examples_domain_dir)
