
examples_admin_dir = examples_dir / 'c' / 'admin'
examples_admin_deps = [libvirt_dep, libvirt_admin_dep]

client_close_src = ['client_close.c']
client_close = executable(
  'client_close',
  client_close_src,
  dependencies: examples_admin_deps
)
install_data(client_close_src, install_dir: examples_admin_dir)

client_info_src = ['client_info.c']
client_info = executable(
  'client_info',
  client_info_src,
  dependencies: examples_admin_deps
)
install_data(client_info_src, install_dir: examples_admin_dir)

client_limits_src = ['client_limits.c']
client_limits = executable(
  'client_limits',
  client_limits_src,
  dependencies: examples_admin_deps
)
install_data(client_limits_src, install_dir: examples_admin_dir)

list_clients_src = ['list_clients.c']
list_clients = executable(
  'list_clients',
  list_clients_src,
  dependencies: examples_admin_deps
)
install_data(list_clients_src, install_dir: examples_admin_dir)

list_server_src = ['list_servers.c']
list_servers = executable(
  'list_servers',
  list_server_src,
  dependencies: examples_admin_deps
)
install_data(list_server_src, install_dir: examples_admin_dir)

logging_src = ['logging.c']
logging = executable(
  'logging',
  logging_src,
  dependencies: examples_admin_deps
)
install_data(logging_src, install_dir: examples_admin_dir)

threadpool_params_src = ['threadpool_params.c']
threadpool_params = executable(
  'threadpool_params',
  threadpool_params_src,
  dependencies: examples_admin_deps
)
install_data(threadpool_params_src, install_dir: examples_admin_dir)
