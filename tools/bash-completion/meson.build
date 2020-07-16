install_data('vsh', install_dir: bash_completion_dir)

meson.add_install_script(
  install_symlink_prog.path(),
  bash_completion_dir,
  'vsh', 'virsh',
)

meson.add_install_script(
  install_symlink_prog.path(),
  bash_completion_dir,
  'vsh', 'virt-admin',
)
