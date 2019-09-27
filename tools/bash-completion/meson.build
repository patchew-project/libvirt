
with_tools_bash_completion = get_option('with-tools-bash-completion')
if host_machine.system() == 'windows'
  with_tools_bash_completion = false
endif

if with_tools_bash_completion
  install_data(
    'vsh',
    rename: 'virsh',
    install_dir: bash_completion_dir,
  )

  install_data(
    'vsh',
    rename: 'virt-admin',
    install_dir: bash_completion_dir,
  )
endif
