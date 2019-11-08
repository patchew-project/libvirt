===============
Libvirt Daemons
===============

.. contents:: Topics

A libvirt deployment for accessing one of the stateful drivers will require
one or more daemons to be deployed on the virtualization host. There are a
number of ways the daemons can be configured which will be outlined in this
page

Monolithic driver daemon
========================

Traditionally libvirt has provided a single monolithic daemon called `libvirtd`
which exposed support for all the stateful drivers, both primary hypervisor
drivers and secondary supporting drivers. It also enables secure remote access
from clients running off host.

Operating modes
---------------

The daemon can operate in two modes

* *System mode* - `libvirtd` is running as the root user account, enabling
  access to its full range of functionality. A read-write connection to
  `libvirtd` in system mode **implies privileges equivalent to having a root
  shell**. Suitable `authentication mechanisms <auth.html>`_ **must be enabled**
  to secure it against untrustworthy clients/users.

* *Session mode* - `libvirtd` is running as any non-root user account, enabling
  access to a more restricted range of functionality. Only client apps/users
  running under **the same UID are permitted to connect**, thus a connection
  does not imply any elevation of privileges.


Sockets
-------

When running in system mode, `libvirtd` exposes three UNIX domain sockets, and
optionally, one or two TCP sockets

* `/var/run/libvirt/libvirt-sock` - the primary socket for accessing libvirt
  APIs, with full read-write privileges. A connection to this socket gives the
  client privileges that are equivalent to having a root shell. This is the
  socket that most management applications connect to by default.

* `/var/run/libvirt/libvirt-sock-ro` - the secondary socket for accessing
  libvirt APIs, with limited read-only privileges. A connection to this socket
  gives the ability to query the existance of objects and monitor some aspects
  of their operation. This is the socket that most management applications
  connect to when requesting read only mode. Typically this is what a
  monitoring app would use.

* `/var/run/libvirt/libvirt-sock-admin` - the administrative socket for
  controlling operation of the daemon itself (as opposed to drivers it is
  running). This can be used to dynamically reconfigure some aspects of the
  daemon and monitor/control connected clients.

* `TCP 16509` - the non-TLS socket for remotely accessing the libvirt APIs,
  with full read-write privileges. A connection to this socket gives the
  client privileges that are equivalent to having a root shell. Since it does
  not use TLS, an `authentication mechanism <auth.html>`_ that provides
  encryption must be used. Only the GSSAPI/Kerberos mechanism is capable of
  satisfying this requirement. In general applications should not use this
  socket except for debugging in a development/test environment.

* `TCP 16514` - the TLS socket for remotely accessing the libvirt APIs,
  with full read-write privileges. A connection to this socket gives the
  client privileges that are equivalent to having a root shell. Access control
  can be enforced either through validation of `x509 certificates
  <tlscerts.html>`_, and/or by enabling an `authentication mechanism
  <auth.html>`_.

When running in session mode, `libvirtd` exposes two UNIX domain sockets

* `$XDG_RUNTIME_DIR/libvirt/libvirt-sock` - the primary socket for accessing
  libvirt APIs, with full read-write privileges. A connection to this socket
  does not alter the privileges that the client already has. This is the
  socket that most management applications connect to by default.

* `$XDG_RUNTIME_DIR/libvirt/libvirt-sock-admin` - the administrative socket for
  controlling operation of the daemon itself (as opposed to drivers it is
  running). This can be used to dynamically reconfigure some aspects of the
  daemon and monitor/control connected clients.

Notice that the session mode does not have a separate read-only socket. Since
the clients must be running as the same user as the daemon itself, there is
not any security benefit from attempting to enforce a read-only mode.

`$XDG_RUNTIME_DIR` commonly points to a per-user private location on tmpfs,
such as `/run/user/$UID`.

Systemd Integration
-------------------

When the `libvirtd` daemon is managed by `systemd` a number of desirable
features are available, most notably socket activation.

Libvirt ships a number of unit files for controlling libvirtd

* `libvirtd.service` - the main unit file for launching the libvirtd daemon
  in system mode. The command line arguments passed can be configured by
  editting `/etc/sysconfig/libvirtd`. This is typically only needed to control
  the use of the auto shutdown timeout value. It is recommended that this
  the service unit be configured to start on boot. This is because various
  libvirt drivers support autostart of their objects. If it is known that
  autostart is not required, this unit can be left to start on demand.

* `libvirtd.socket` - the unit file corresponding to the main read-write
  UNIX socket `/var/run/libvirt/libvirt-sock`. This socket is recommended to
  be started on boot by default.

* `libvirtd-ro.socket` - the unit file corresponding to the main read-write
  UNIX socket `/var/run/libvirt/libvirt-sock-ro`. This socket is recommended
  to be started on boot by default.

* `libvirtd-admin.socket` - the unit file corresponding to the administrative
  UNIX socket `/var/run/libvirt/libvirt-sock-admin`. This socket is recommended
  to be started on boot by default.

* `libvirt-tcp.socket` - the unit file corresponding to the TCP 16509 port for
  non-TLS remote access. This socket should not be configured to start on boot
  until the administrator has configured a suitable authentication mechanism.

* `libvirt-tls.socket` - the unit file corresponding to the TCP 16509 port for
  TLS remote access. This socket should not be configured to start on boot
  until the administrator has deployed x509 certificates and optionally
  configured a suitable authentication mechanism.

The socket unit files are newly introduced in 5.6.0. On newly installed hosts
the UNIX socket units should be enabled by default. When upgrading an existing
host from a previous version of libvirt, the socket unit files will be masked
if libvirtd is currently configured to use the `--listen` argument, since the
`--listen` argument is mutually exclusive with use of socket activation.

When systemd socket activation is used a number of configuration settings in
`libvirtd.conf` are no longer honoured. Instead these settings must be
controlled via the system unit files

* `listen_tcp` - TCP socket usage is enabled by starting the
  `libvirtd-tcp.socket` unit file.

* `listen_tls` - TLS socket usage is enabled by starting the
  `libvirtd-tls.socket` unit file.

* `tcp_port` - Port for the non-TLS TCP socket, controlled via the
  `ListenStream` parameter in the `libvirtd-tcp.socket` unit file.

* `tls_port` - Port for the TLS TCP socket, controlled via the
  `ListenStream` parameter in the `libvirtd-tls.socket` unit file.

* `listen_addr` - IP address to listen on, independently controlled via the
  `ListenStream` parameter in the `libvirtd-tcp.socket`  or
  `libvirtd-tls.socket` unit files.

* `unix_sock_group` - UNIX socket group owner, controlled via the `SocketGroup`
  parameter in the `libvirtd.socket` and `libvirtd-ro.socket` unit files

* `unix_sock_ro_perms` - read-only UNIX socket permissions, controlled via the
  `SocketMode` parameter in the `libvirtd-ro.socket` unit file

* `unix_sock_rw_perms` - read-write UNIX socket permissions, controlled via the
  `SocketMode` parameter in the `libvirtd.socket` unit file

* `unix_sock_admin_perms` - admin UNIX socket permissions, controlled via the
  `SocketMode` parameter in the `libvirtd-admin.socket` unit file

* `unix_sock_dir` - directory in which all UNIX sockets are created
  independently controlled via the `ListenStream` parameter in any of the
  `libvirtd.socket`, `libvirtd-ro.socket` and `libvirtd-admin.socket` unit
  files.


Systemd releases prior to version 227 lacked support for passing the activation
socket unit names into the service. When using these old versions, the
`tcp_port`, `tls_port` and `unix_sock_dir` settings in `libvirtd.conf` must
be changed in lock-step with the equivalent settings in the unit files to
ensure that `libvirtd` can identify the sockets.

Modular driver daemons
======================

Helper daemons
==============

There are some other special purpose daemons used for certain administrative
tasks in libvirt

Logging daemon
--------------

The `virtlogd` daemon provides a service for managing log files associated with
QEMU virtual machines. The QEMU process is given one or more pipes, the other
end of which are owned by the `virtlogd` daemon. It will then write data on
those pipes to log files, while enforcing a maximum file size and performing
log rollover at the size limit.

Since the daemon holds open anoymous pipe file descriptors, it must never be
stopped while any QEMU virtual machines are running. To enable software updates
to be applied, the daemon is capable of re-executing itself while keeping all
file descriptors open. This can be triggered by sending the daemon `SIGUSR1`

Systemd integration
~~~~~~~~~~~~~~~~~~~

Locking daemon
--------------

