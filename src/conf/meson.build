xmlgen_input = [
]

xmlgen_output = []
foreach name : xmlgen_input
  xmlgen_output += '@0@.generated.c'.format(name.split('.')[0])
  xmlgen_output += '@0@.generated.h'.format(name.split('.')[0])
endforeach

xmlgen_headers = []
if xmlgen_output.length() > 0
  xmlgen_objects = custom_target(
    'virxmlgen',
    input: xmlgen_self + xmlgen_input,
    output: xmlgen_output,
    command: [
      meson_python_prog, python3_prog.path(), '-B', virxmlgen_prog.path(),
      '-s', meson.source_root() / 'src',
      '-b', meson.build_root() / 'src',
      'generate', '-k', 'cpf',
    ],
  )

  index = 0
  foreach header : xmlgen_objects.to_list()
    if index % 2 == 1
      xmlgen_headers += header
    endif
    index += 1
  endforeach
else
  xmlgen_objects = []
endif

xmlgen_dep = declare_dependency(sources: xmlgen_headers)

netdev_conf_sources = [
  'netdev_bandwidth_conf.c',
  'netdev_vlan_conf.c',
  'netdev_vport_profile_conf.c',
  'virnetworkportdef.c',
]

domain_conf_sources = [
  'backup_conf.c',
  'capabilities.c',
  'checkpoint_conf.c',
  'domain_addr.c',
  'domain_audit.c',
  'domain_capabilities.c',
  'domain_conf.c',
  'domain_nwfilter.c',
  'moment_conf.c',
  'numa_conf.c',
  'snapshot_conf.c',
  'virdomaincheckpointobjlist.c',
  'virdomainmomentobjlist.c',
  'virdomainobjlist.c',
  'virdomainsnapshotobjlist.c',
  'virsavecookie.c',
]

event_conf_sources = [
  'domain_event.c',
  'network_event.c',
  'node_device_event.c',
  'object_event.c',
  'secret_event.c',
  'storage_event.c',
]

network_conf_sources = [
  'network_conf.c',
  'networkcommon_conf.c',
  'virnetworkobj.c',
]

nwfilter_conf_sources = [
  'nwfilter_conf.c',
  'nwfilter_ipaddrmap.c',
  'nwfilter_params.c',
  'virnwfilterbindingdef.c',
  'virnwfilterbindingobj.c',
  'virnwfilterbindingobjlist.c',
  'virnwfilterobj.c',
]

storage_conf_sources = [
  'storage_adapter_conf.c',
  'storage_capabilities.c',
  'storage_conf.c',
  'virstorageobj.c',
]

interface_conf_sources = [
  'interface_conf.c',
  'virinterfaceobj.c',
]

secret_conf_sources = [
  'secret_conf.c',
  'virsecretobj.c',
]

node_device_conf_sources = [
  'node_device_conf.c',
  'node_device_util.c',
  'virnodedeviceobj.c',
]

cpu_conf_sources = [
  'cpu_conf.c',
]

chrdev_conf_sources = [
  'virchrdev.c',
]

device_conf_sources = [
  'device_conf.c',
]

virt_conf_lib = static_library(
  'virt_conf',
  [
    xmlgen_objects,
    chrdev_conf_sources,
    cpu_conf_sources,
    device_conf_sources,
    domain_conf_sources,
    event_conf_sources,
    interface_conf_sources,
    netdev_conf_sources,
    network_conf_sources,
    node_device_conf_sources,
    nwfilter_conf_sources,
    secret_conf_sources,
    storage_conf_sources,
  ],
  dependencies: [
    src_dep,
  ],
)

libvirt_libs += virt_conf_lib

conf_inc_dir = include_directories('.')

subdir_done()
