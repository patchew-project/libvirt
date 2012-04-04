# -*- rpm-spec -*-

# If neither fedora nor rhel was defined, try to guess them from %{dist}
%if !0%{?rhel} && !0%{?fedora}
%{expand:%(echo "%{?dist}" | \
  sed -ne 's/^\.el\([0-9]\+\).*/%%define rhel \1/p')}
%{expand:%(echo "%{?dist}" | \
  sed -ne 's/^\.fc\?\([0-9]\+\).*/%%define fedora \1/p')}
%endif

# Default to skipping autoreconf.  Distros can change just this one line
# (or provide a command-line override) if they backport any patches that
# touch configure.ac or Makefile.am.
%{!?enable_autotools:%define enable_autotools 0}

# A client only build will create a libvirt.so only containing
# the generic RPC driver, and test driver and no libvirtd
# Default to a full server + client build
%define client_only        0

# Now turn off server build in certain cases

# RHEL-5 builds are client-only for s390, ppc
%if 0%{?rhel} == 5
%ifnarch %{ix86} x86_64 ia64
%define client_only        1
%endif
%endif

# Disable all server side drivers if client only build requested
%if %{client_only}
%define server_drivers     0
%else
%define server_drivers     1
%endif


# Now set the defaults for all the important features, independent
# of any particular OS

# First the daemon itself
%define with_libvirtd      0%{!?_without_libvirtd:%{server_drivers}}
%define with_avahi         0%{!?_without_avahi:%{server_drivers}}

# Then the hypervisor drivers that run on local host
%define with_xen           0%{!?_without_xen:%{server_drivers}}
%define with_qemu          0%{!?_without_qemu:%{server_drivers}}
%define with_openvz        0%{!?_without_openvz:%{server_drivers}}
%define with_lxc           0%{!?_without_lxc:%{server_drivers}}
%define with_vbox          0%{!?_without_vbox:%{server_drivers}}
%define with_uml           0%{!?_without_uml:%{server_drivers}}
%define with_libxl         0%{!?_without_libxl:%{server_drivers}}
%define with_vmware        0%{!?_without_vmware:%{server_drivers}}

# Then the hypervisor drivers that talk via a native remote protocol
%define with_phyp          0%{!?_without_phyp:1}
%define with_esx           0%{!?_without_esx:1}
%define with_hyperv        0%{!?_without_hyperv:1}
%define with_xenapi        0%{!?_without_xenapi:1}

# Then the secondary host drivers
%define with_network       0%{!?_without_network:%{server_drivers}}
%define with_storage_fs    0%{!?_without_storage_fs:%{server_drivers}}
%define with_storage_lvm   0%{!?_without_storage_lvm:%{server_drivers}}
%define with_storage_iscsi 0%{!?_without_storage_iscsi:%{server_drivers}}
%define with_storage_disk  0%{!?_without_storage_disk:%{server_drivers}}
%define with_storage_mpath 0%{!?_without_storage_mpath:%{server_drivers}}
%define with_numactl       0%{!?_without_numactl:%{server_drivers}}
%define with_selinux       0%{!?_without_selinux:%{server_drivers}}

# A few optional bits off by default, we enable later
%define with_polkit        0%{!?_without_polkit:0}
%define with_capng         0%{!?_without_capng:0}
%define with_netcf         0%{!?_without_netcf:0}
%define with_udev          0%{!?_without_udev:0}
%define with_hal           0%{!?_without_hal:0}
%define with_yajl          0%{!?_without_yajl:0}
%define with_nwfilter      0%{!?_without_nwfilter:0}
%define with_libpcap       0%{!?_without_libpcap:0}
%define with_macvtap       0%{!?_without_macvtap:0}
%define with_libnl         0%{!?_without_libnl:0}
%define with_audit         0%{!?_without_audit:0}
%define with_dtrace        0%{!?_without_dtrace:0}
%define with_cgconfig      0%{!?_without_cgconfig:0}
%define with_sanlock       0%{!?_without_sanlock:0}
%define with_systemd       0%{!?_without_systemd:0}

# Non-server/HV driver defaults which are always enabled
%define with_python        0%{!?_without_python:1}
%define with_sasl          0%{!?_without_sasl:1}


# Finally set the OS / architecture specific special cases

# Xen is available only on i386 x86_64 ia64
%ifnarch %{ix86} x86_64 ia64
%define with_xen 0
%define with_libxl 0
%endif

# Numactl is not available on s390[x] and ARM
%ifarch s390 s390x %{arm}
%define with_numactl 0
%endif

# RHEL doesn't ship OpenVZ, VBox, UML, PowerHypervisor,
# VMWare, libxenserver (xenapi), libxenlight (Xen 4.1 and newer),
# or HyperV.
%if 0%{?rhel}
%define with_openvz 0
%define with_vbox 0
%define with_uml 0
%define with_phyp 0
%define with_vmware 0
%define with_xenapi 0
%define with_libxl 0
%define with_hyperv 0
%endif

# Although earlier Fedora has systemd, libvirt still used sysvinit
%if 0%{?fedora} >= 17
%define with_systemd 1
%endif

# RHEL-5 has restricted QEMU to x86_64 only and is too old for LXC
%if 0%{?rhel} == 5
%ifnarch x86_64
%define with_qemu 0
%endif
%define with_lxc 0
%endif

# RHEL-6 has restricted QEMU to x86_64 only, stopped including Xen
# on all archs. Other archs all have LXC available though
%if 0%{?rhel} >= 6
%ifnarch x86_64
%define with_qemu 0
%endif
%define with_xen 0
%endif

# Fedora doesn't have any QEMU on ppc64 - only ppc
%if 0%{?fedora}
%ifarch ppc64
%define with_qemu 0
%endif
%endif

# Fedora doesn't have new enough Xen for libxl until F16
%if 0%{?fedora} && 0%{?fedora} < 16
%define with_libxl 0
%endif

# PolicyKit was introduced in Fedora 8 / RHEL-6 or newer
%if 0%{?fedora} >= 8 || 0%{?rhel} >= 6
%define with_polkit    0%{!?_without_polkit:1}
%endif

# libcapng is used to manage capabilities in Fedora 12 / RHEL-6 or newer
%if 0%{?fedora} >= 12 || 0%{?rhel} >= 6
%define with_capng     0%{!?_without_capng:1}
%endif

# netcf is used to manage network interfaces in Fedora 12 / RHEL-6 or newer
%if 0%{?fedora} >= 12 || 0%{?rhel} >= 6
%define with_netcf     0%{!?_without_netcf:%{server_drivers}}
%endif

# udev is used to manage host devices in Fedora 12 / RHEL-6 or newer
%if 0%{?fedora} >= 12 || 0%{?rhel} >= 6
%define with_udev     0%{!?_without_udev:%{server_drivers}}
%else
%define with_hal       0%{!?_without_hal:%{server_drivers}}
%endif

# Enable yajl library for JSON mode with QEMU
%if 0%{?fedora} >= 13 || 0%{?rhel} >= 6
%define with_yajl     0%{!?_without_yajl:%{server_drivers}}
%endif

# Enable sanlock library for lock management with QEMU
# Sanlock is available only on i686 x86_64 for RHEL
%if 0%{?fedora} >= 16
%define with_sanlock 0%{!?_without_sanlock:%{server_drivers}}
%endif
%if 0%{?rhel} >= 6
%ifarch %{ix86} x86_64
%define with_sanlock 0%{!?_without_sanlock:%{server_drivers}}
%endif
%endif

# Disable some drivers when building without libvirt daemon.
# The logic is the same as in configure.ac
%if ! %{with_libvirtd}
%define with_network 0
%define with_qemu 0
%define with_lxc 0
%define with_uml 0
%define with_hal 0
%define with_udev 0
%define with_storage_fs 0
%define with_storage_lvm 0
%define with_storage_iscsi 0
%define with_storage_mpath 0
%define with_storage_disk 0
%endif

# Enable libpcap library
%if %{with_qemu}
%define with_nwfilter 0%{!?_without_nwfilter:%{server_drivers}}
%define with_libpcap  0%{!?_without_libpcap:%{server_drivers}}
%define with_macvtap  0%{!?_without_macvtap:%{server_drivers}}
%endif

%if %{with_macvtap}
%define with_libnl 1
%endif

%if 0%{?fedora} >= 11 || 0%{?rhel} >= 5
%define with_audit    0%{!?_without_audit:1}
%endif

%if 0%{?fedora} >= 13 || 0%{?rhel} >= 6
%define with_dtrace 1
%endif

# Pull in cgroups config system
%if 0%{?fedora} >= 12 || 0%{?rhel} >= 6
%if %{with_qemu} || %{with_lxc}
%define with_cgconfig 0%{!?_without_cgconfig:1}
%endif
%endif

# Force QEMU to run as non-root
%if 0%{?fedora} >= 12 || 0%{?rhel} >= 6
%define qemu_user  qemu
%define qemu_group  qemu
%else
%define qemu_user  root
%define qemu_group  root
%endif


# The RHEL-5 Xen package has some feature backports. This
# flag is set to enable use of those special bits on RHEL-5
%if 0%{?rhel} == 5
%define with_rhel5  1
%else
%define with_rhel5  0
%endif

Summary: Library providing a simple virtualization API
Name: libvirt
Version: 0.9.10
Release: 4%{?dist}%{?extra_release}
License: LGPLv2+
Group: Development/Libraries
Source: http://libvirt.org/sources/libvirt-%{version}.tar.gz
Patch1: %{name}-%{version}-qemu-replace-deprecated-fedora-13-machine.patch

BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
URL: http://libvirt.org/

# All runtime requirements for the libvirt package (runtime requrements
# for subpackages are listed later in those subpackages)

# The client side, i.e. shared libs and virsh are in a subpackage
Requires: %{name}-client = %{version}-%{release}

# Used by many of the drivers, so turn it on whenever the
# daemon is present
%if %{with_libvirtd}
# for modprobe of pci devices
Requires: module-init-tools
# for /sbin/ip & /sbin/tc
Requires: iproute
%if %{with_avahi}
Requires: avahi-libs
%endif
%endif
%if %{with_network}
Requires: dnsmasq >= 2.41
Requires: radvd
%endif
%if %{with_network} || %{with_nwfilter}
Requires: iptables
Requires: iptables-ipv6
%endif
%if %{with_nwfilter}
Requires: ebtables
%endif
# needed for device enumeration
%if %{with_hal}
Requires: hal
%endif
%if %{with_udev}
Requires: udev >= 145
%endif
%if %{with_polkit}
%if 0%{?fedora} >= 12 || 0%{?rhel} >=6
Requires: polkit >= 0.93
%else
Requires: PolicyKit >= 0.6
%endif
%endif
%if %{with_storage_fs}
Requires: nfs-utils
# For mkfs
Requires: util-linux-ng
# For pool-build probing for existing pools
BuildRequires: libblkid-devel >= 2.17
# For glusterfs
%if 0%{?fedora} >= 11
Requires: glusterfs-client >= 2.0.1
%endif
%endif
%if %{with_qemu}
# From QEMU RPMs
Requires: /usr/bin/qemu-img
# For image compression
Requires: gzip
Requires: bzip2
Requires: lzop
Requires: xz
%else
%if %{with_xen}
# From Xen RPMs
Requires: /usr/sbin/qcow-create
%endif
%endif
%if %{with_storage_lvm}
# For LVM drivers
Requires: lvm2
%endif
%if %{with_storage_iscsi}
# For ISCSI driver
Requires: iscsi-initiator-utils
%endif
%if %{with_storage_disk}
# For disk driver
Requires: parted
Requires: device-mapper
%endif
%if %{with_storage_mpath}
# For multipath support
Requires: device-mapper
%endif
%if %{with_cgconfig}
Requires: libcgroup
%endif
%ifarch %{ix86} x86_64 ia64
# For virConnectGetSysinfo
Requires: dmidecode
%endif
# For service management
%if %{with_systemd}
Requires(post): systemd-units
Requires(post): systemd-sysv
Requires(preun): systemd-units
Requires(postun): systemd-units
%endif

# All build-time requirements
%if 0%{?enable_autotools}
BuildRequires: autoconf
BuildRequires: automake
BuildRequires: gettext-devel
BuildRequires: libtool
%endif
BuildRequires: python-devel
%if %{with_systemd}
BuildRequires: systemd-units
%endif
%if %{with_xen}
BuildRequires: xen-devel
%endif
BuildRequires: libxml2-devel
BuildRequires: xhtml1-dtds
BuildRequires: libxslt
BuildRequires: readline-devel
BuildRequires: ncurses-devel
BuildRequires: gettext
BuildRequires: libtasn1-devel
BuildRequires: gnutls-devel
%if 0%{?fedora} >= 12 || 0%{?rhel} >= 6
# for augparse, optionally used in testing
BuildRequires: augeas
%endif
%if %{with_hal}
BuildRequires: hal-devel
%endif
%if %{with_udev}
BuildRequires: libudev-devel >= 145
BuildRequires: libpciaccess-devel >= 0.10.9
%endif
%if %{with_yajl}
BuildRequires: yajl-devel
%endif
%if %{with_sanlock}
BuildRequires: sanlock-devel >= 1.8
%endif
%if %{with_libpcap}
BuildRequires: libpcap-devel
%endif
%if %{with_libnl}
BuildRequires: libnl-devel
%endif
%if %{with_avahi}
BuildRequires: avahi-devel
%endif
%if %{with_selinux}
BuildRequires: libselinux-devel
%endif
%if %{with_network}
BuildRequires: dnsmasq >= 2.41
BuildRequires: iptables
BuildRequires: iptables-ipv6
BuildRequires: radvd
%endif
%if %{with_nwfilter}
BuildRequires: ebtables
%endif
BuildRequires: module-init-tools
%if %{with_sasl}
BuildRequires: cyrus-sasl-devel
%endif
%if %{with_polkit}
%if 0%{?fedora} >= 12 || 0%{?rhel} >= 6
# Only need the binary, not -devel
BuildRequires: polkit >= 0.93
%else
BuildRequires: PolicyKit-devel >= 0.6
%endif
%endif
%if %{with_storage_fs}
# For mount/umount in FS driver
BuildRequires: util-linux
%endif
%if %{with_qemu}
# From QEMU RPMs
BuildRequires: /usr/bin/qemu-img
%else
%if %{with_xen}
# From Xen RPMs
BuildRequires: /usr/sbin/qcow-create
%endif
%endif
%if %{with_storage_lvm}
# For LVM drivers
BuildRequires: lvm2
%endif
%if %{with_storage_iscsi}
# For ISCSI driver
BuildRequires: iscsi-initiator-utils
%endif
%if %{with_storage_disk}
# For disk driver
BuildRequires: parted-devel
%if 0%{?rhel} == 5
# Broken RHEL-5 parted RPM is missing a dep
BuildRequires: e2fsprogs-devel
%endif
%endif
%if %{with_storage_mpath}
# For Multipath support
%if 0%{?rhel} == 5
# Broken RHEL-5 packaging has header files in main RPM :-(
BuildRequires: device-mapper
%else
BuildRequires: device-mapper-devel
%endif
%endif
%if %{with_numactl}
# For QEMU/LXC numa info
BuildRequires: numactl-devel
%endif
%if %{with_capng}
BuildRequires: libcap-ng-devel >= 0.5.0
%endif
%if %{with_phyp}
BuildRequires: libssh2-devel
%endif
%if %{with_netcf}
%if 0%{?fedora} >= 16 || 0%{?rhel} >= 6
BuildRequires: netcf-devel >= 0.1.8
%else
BuildRequires: netcf-devel >= 0.1.4
%endif
%endif
%if %{with_esx}
%if 0%{?fedora} >= 9 || 0%{?rhel} >= 6
BuildRequires: libcurl-devel
%else
BuildRequires: curl-devel
%endif
%endif
%if %{with_hyperv}
BuildRequires: libwsman-devel >= 2.2.3
%endif
%if %{with_audit}
BuildRequires: audit-libs-devel
%endif
%if %{with_dtrace}
# we need /usr/sbin/dtrace
BuildRequires: systemtap-sdt-devel
%endif

%if %{with_storage_fs}
# For mount/umount in FS driver
BuildRequires: util-linux
# For showmount in FS driver (netfs discovery)
BuildRequires: nfs-utils
%endif

# Fedora build root suckage
BuildRequires: gawk

%description
Libvirt is a C toolkit to interact with the virtualization capabilities
of recent versions of Linux (and other OSes). The main package includes
the libvirtd server exporting the virtualization support.

%package client
Summary: Client side library and utilities of the libvirt library
Group: Development/Libraries
Requires: readline
Requires: ncurses
# So remote clients can access libvirt over SSH tunnel
# (client invokes 'nc' against the UNIX socket on the server)
Requires: nc
# Needed by libvirt-guests init script.
Requires: gettext
# Needed by virt-pki-validate script.
Requires: gnutls-utils
# Needed for probing the power management features of the host.
Requires: pm-utils
%if %{with_sasl}
Requires: cyrus-sasl
# Not technically required, but makes 'out-of-box' config
# work correctly & doesn't have onerous dependencies
Requires: cyrus-sasl-md5
%endif

%description client
Shared libraries and client binaries needed to access to the
virtualization capabilities of recent versions of Linux (and other OSes).

%package devel
Summary: Libraries, includes, etc. to compile with the libvirt library
Group: Development/Libraries
Requires: %{name}-client = %{version}-%{release}
Requires: pkgconfig
%if %{with_xen}
Requires: xen-devel
%endif

%description devel
Includes and documentations for the C library providing an API to use
the virtualization capabilities of recent versions of Linux (and other OSes).

%if %{with_sanlock}
%package lock-sanlock
Summary: Sanlock lock manager plugin for QEMU driver
Group: Development/Libraries
Requires: sanlock >= 1.8
#for virt-sanlock-cleanup require augeas
Requires: augeas
Requires: %{name} = %{version}-%{release}

%description lock-sanlock
Includes the Sanlock lock manager plugin for the QEMU
driver
%endif

%if %{with_python}
%package python
Summary: Python bindings for the libvirt library
Group: Development/Libraries
Requires: %{name}-client = %{version}-%{release}

%description python
The libvirt-python package contains a module that permits applications
written in the Python programming language to use the interface
supplied by the libvirt library to use the virtualization capabilities
of recent versions of Linux (and other OSes).
%endif

%prep
%setup -q
%patch1 -p1

%build
%if ! %{with_xen}
%define _without_xen --without-xen
%endif

%if ! %{with_qemu}
%define _without_qemu --without-qemu
%endif

%if ! %{with_openvz}
%define _without_openvz --without-openvz
%endif

%if ! %{with_lxc}
%define _without_lxc --without-lxc
%endif

%if ! %{with_vbox}
%define _without_vbox --without-vbox
%endif

%if ! %{with_xenapi}
%define _without_xenapi --without-xenapi
%endif

%if ! %{with_libxl}
%define _without_libxl --without-libxl
%endif

%if ! %{with_sasl}
%define _without_sasl --without-sasl
%endif

%if ! %{with_avahi}
%define _without_avahi --without-avahi
%endif

%if ! %{with_phyp}
%define _without_phyp --without-phyp
%endif

%if ! %{with_esx}
%define _without_esx --without-esx
%endif

%if ! %{with_hyperv}
%define _without_hyperv --without-hyperv
%endif

%if ! %{with_vmware}
%define _without_vmware --without-vmware
%endif

%if ! %{with_polkit}
%define _without_polkit --without-polkit
%endif

%if ! %{with_python}
%define _without_python --without-python
%endif

%if ! %{with_libvirtd}
%define _without_libvirtd --without-libvirtd
%endif

%if ! %{with_uml}
%define _without_uml --without-uml
%endif

%if %{with_rhel5}
%define _with_rhel5_api --with-rhel5-api
%endif

%if ! %{with_network}
%define _without_network --without-network
%endif

%if ! %{with_storage_fs}
%define _without_storage_fs --without-storage-fs
%endif

%if ! %{with_storage_lvm}
%define _without_storage_lvm --without-storage-lvm
%endif

%if ! %{with_storage_iscsi}
%define _without_storage_iscsi --without-storage-iscsi
%endif

%if ! %{with_storage_disk}
%define _without_storage_disk --without-storage-disk
%endif

%if ! %{with_storage_mpath}
%define _without_storage_mpath --without-storage-mpath
%endif

%if ! %{with_numactl}
%define _without_numactl --without-numactl
%endif

%if ! %{with_capng}
%define _without_capng --without-capng
%endif

%if ! %{with_netcf}
%define _without_netcf --without-netcf
%endif

%if ! %{with_selinux}
%define _without_selinux --without-selinux
%endif

%if ! %{with_hal}
%define _without_hal --without-hal
%endif

%if ! %{with_udev}
%define _without_udev --without-udev
%endif

%if ! %{with_yajl}
%define _without_yajl --without-yajl
%endif

%if ! %{with_sanlock}
%define _without_sanlock --without-sanlock
%endif

%if ! %{with_libpcap}
%define _without_libpcap --without-libpcap
%endif

%if ! %{with_macvtap}
%define _without_macvtap --without-macvtap
%endif

%if ! %{with_audit}
%define _without_audit --without-audit
%endif

%if ! %{with_dtrace}
%define _without_dtrace --without-dtrace
%endif

%define when  %(date +"%%F-%%T")
%define where %(hostname)
%define who   %{?packager}%{!?packager:Unknown}
%define with_packager --with-packager="%{who}, %{when}, %{where}"
%define with_packager_version --with-packager-version="%{release}"

%if %{with_systemd}
# We use 'systemd+redhat', so if someone installs upstart or
# legacy init scripts, they can still start libvirtd, etc
%define init_scripts --with-init_script=systemd+redhat
%else
%define init_scripts --with-init_script=redhat
%endif

%if 0%{?enable_autotools}
autoreconf -if
%endif
%configure %{?_without_xen} \
           %{?_without_qemu} \
           %{?_without_openvz} \
           %{?_without_lxc} \
           %{?_without_vbox} \
           %{?_without_xenapi} \
           %{?_without_sasl} \
           %{?_without_avahi} \
           %{?_without_polkit} \
           %{?_without_python} \
           %{?_without_libvirtd} \
           %{?_without_uml} \
           %{?_without_phyp} \
           %{?_without_esx} \
           %{?_without_hyperv} \
           %{?_without_vmware} \
           %{?_without_network} \
           %{?_with_rhel5_api} \
           %{?_without_storage_fs} \
           %{?_without_storage_lvm} \
           %{?_without_storage_iscsi} \
           %{?_without_storage_disk} \
           %{?_without_storage_mpath} \
           %{?_without_numactl} \
           %{?_without_capng} \
           %{?_without_netcf} \
           %{?_without_selinux} \
           %{?_without_hal} \
           %{?_without_udev} \
           %{?_without_yajl} \
           %{?_without_sanlock} \
           %{?_without_libpcap} \
           %{?_without_macvtap} \
           %{?_without_audit} \
           %{?_without_dtrace} \
           %{with_packager} \
           %{with_packager_version} \
           --with-qemu-user=%{qemu_user} \
           --with-qemu-group=%{qemu_group} \
           %{init_scripts} \
           --with-remote-pid-file=%{_localstatedir}/run/libvirtd.pid
make %{?_smp_mflags}
gzip -9 ChangeLog

%install
rm -fr %{buildroot}

%makeinstall SYSTEMD_UNIT_DIR=%{buildroot}%{_unitdir}
for i in domain-events/events-c dominfo domsuspend hellolibvirt openauth python xml/nwfilter systemtap
do
  (cd examples/$i ; make clean ; rm -rf .deps .libs Makefile Makefile.in)
done
rm -f $RPM_BUILD_ROOT%{_libdir}/*.la
rm -f $RPM_BUILD_ROOT%{_libdir}/*.a
rm -f $RPM_BUILD_ROOT%{_libdir}/python*/site-packages/*.la
rm -f $RPM_BUILD_ROOT%{_libdir}/python*/site-packages/*.a
rm -f $RPM_BUILD_ROOT%{_libdir}/libvirt/lock-driver/*.la
rm -f $RPM_BUILD_ROOT%{_libdir}/libvirt/lock-driver/*.a

%if %{with_network}
install -d -m 0755 $RPM_BUILD_ROOT%{_datadir}/lib/libvirt/dnsmasq/
# We don't want to install /etc/libvirt/qemu/networks in the main %files list
# because if the admin wants to delete the default network completely, we don't
# want to end up re-incarnating it on every RPM upgrade.
install -d -m 0755 $RPM_BUILD_ROOT%{_datadir}/libvirt/networks/
cp $RPM_BUILD_ROOT%{_sysconfdir}/libvirt/qemu/networks/default.xml \
   $RPM_BUILD_ROOT%{_datadir}/libvirt/networks/default.xml
rm -f $RPM_BUILD_ROOT%{_sysconfdir}/libvirt/qemu/networks/default.xml
rm -f $RPM_BUILD_ROOT%{_sysconfdir}/libvirt/qemu/networks/autostart/default.xml
# Strip auto-generated UUID - we need it generated per-install
sed -i -e "/<uuid>/d" $RPM_BUILD_ROOT%{_datadir}/libvirt/networks/default.xml
%else
rm -f $RPM_BUILD_ROOT%{_sysconfdir}/libvirt/qemu/networks/default.xml
rm -f $RPM_BUILD_ROOT%{_sysconfdir}/libvirt/qemu/networks/autostart/default.xml
%endif
%if ! %{with_qemu}
rm -f $RPM_BUILD_ROOT%{_datadir}/augeas/lenses/libvirtd_qemu.aug
rm -f $RPM_BUILD_ROOT%{_datadir}/augeas/lenses/tests/test_libvirtd_qemu.aug
%endif
%find_lang %{name}

%if ! %{with_lxc}
rm -f $RPM_BUILD_ROOT%{_datadir}/augeas/lenses/libvirtd_lxc.aug
rm -f $RPM_BUILD_ROOT%{_datadir}/augeas/lenses/tests/test_libvirtd_lxc.aug
%endif

%if ! %{with_python}
rm -rf $RPM_BUILD_ROOT%{_datadir}/doc/libvirt-python-%{version}
%endif

%if %{client_only}
rm -rf $RPM_BUILD_ROOT%{_datadir}/doc/libvirt-%{version}
%endif

%if ! %{with_libvirtd}
rm -rf $RPM_BUILD_ROOT%{_sysconfdir}/libvirt/nwfilter
mv $RPM_BUILD_ROOT%{_datadir}/doc/libvirt-%{version}/html \
   $RPM_BUILD_ROOT%{_datadir}/doc/libvirt-devel-%{version}/
%endif

%if ! %{with_qemu}
rm -rf $RPM_BUILD_ROOT%{_sysconfdir}/libvirt/qemu.conf
rm -rf $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d/libvirtd.qemu
%endif
%if ! %{with_lxc}
rm -rf $RPM_BUILD_ROOT%{_sysconfdir}/libvirt/lxc.conf
rm -rf $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d/libvirtd.lxc
%endif
%if ! %{with_uml}
rm -rf $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d/libvirtd.uml
%endif

%clean
rm -fr %{buildroot}

%check
cd tests
# These 3 tests don't current work in a mock build root
for i in nodeinfotest daemon-conf seclabeltest
do
  rm -f $i
  printf "#!/bin/sh\nexit 0\n" > $i
  chmod +x $i
done
make check

%pre
%if 0%{?fedora} >= 12 || 0%{?rhel} >= 6
# Normally 'setup' adds this in /etc/passwd, but this is
# here for case of upgrades from earlier Fedora/RHEL. This
# UID/GID pair is reserved for qemu:qemu
getent group kvm >/dev/null || groupadd -g 36 -r kvm
getent group qemu >/dev/null || groupadd -g 107 -r qemu
getent passwd qemu >/dev/null || \
  useradd -r -u 107 -g qemu -G kvm -d / -s /sbin/nologin \
    -c "qemu user" qemu
%endif

%post

%if %{with_libvirtd}
%if %{with_network}
# We want to install the default network for initial RPM installs
# or on the first upgrade from a non-network aware libvirt only.
# We check this by looking to see if the daemon is already installed
if ! /sbin/chkconfig libvirtd && test ! -f %{_sysconfdir}/libvirt/qemu/networks/default.xml
then
    UUID=`/usr/bin/uuidgen`
    sed -e "s,</name>,</name>\n  <uuid>$UUID</uuid>," \
         < %{_datadir}/libvirt/networks/default.xml \
         > %{_sysconfdir}/libvirt/qemu/networks/default.xml
    ln -s ../default.xml %{_sysconfdir}/libvirt/qemu/networks/autostart/default.xml
fi

# All newly defined networks will have a mac address for the bridge
# auto-generated, but networks already existing at the time of upgrade
# will not. We need to go through all the network configs, look for
# those that don't have a mac address, and add one.

network_files=$( (cd %{_localstatedir}/lib/libvirt/network && \
                  grep -L "mac address" *.xml; \
                  cd %{_sysconfdir}/libvirt/qemu/networks && \
                  grep -L "mac address" *.xml) 2>/dev/null \
                | sort -u)

for file in $network_files
do
   # each file exists in either the config or state directory (or both) and
   # does not have a mac address specified in either. We add the same mac
   # address to both files (or just one, if the other isn't there)

   mac4=`printf '%X' $(($RANDOM % 256))`
   mac5=`printf '%X' $(($RANDOM % 256))`
   mac6=`printf '%X' $(($RANDOM % 256))`
   for dir in %{_localstatedir}/lib/libvirt/network \
              %{_sysconfdir}/libvirt/qemu/networks
   do
      if test -f $dir/$file
      then
         sed -i.orig -e \
           "s|\(<bridge.*$\)|\0\n  <mac address='52:54:00:$mac4:$mac5:$mac6'/>|" \
           $dir/$file
         if test $? != 0
         then
             echo "failed to add <mac address='52:54:00:$mac4:$mac5:$mac6'/>" \
                  "to $dir/$file"
             mv -f $dir/$file.orig $dir/$file
         else
             rm -f $dir/$file.orig
         fi
      fi
   done
done
%endif

%if %{with_systemd}
if [ $1 -eq 1 ] ; then
    # Initial installation
    /bin/systemctl enable libvirtd.service >/dev/null 2>&1 || :
    /bin/systemctl enable cgconfig.service >/dev/null 2>&1 || :
fi
%else
%if %{with_cgconfig}
# Starting with Fedora 16, systemd automounts all cgroups, and cgconfig is
# no longer a necessary service.
%if 0%{?rhel} || (0%{?fedora} && 0%{?fedora} < 16)
if [ "$1" -eq "1" ]; then
/sbin/chkconfig cgconfig on
fi
%endif
%endif

/sbin/chkconfig --add libvirtd
if [ "$1" -ge "1" ]; then
	/sbin/service libvirtd condrestart > /dev/null 2>&1
fi
%endif
%endif

%preun
%if %{with_libvirtd}
%if %{with_systemd}
if [ $1 -eq 0 ] ; then
    # Package removal, not upgrade
    /bin/systemctl --no-reload disable libvirtd.service > /dev/null 2>&1 || :
    /bin/systemctl stop libvirtd.service > /dev/null 2>&1 || :
fi
%else
if [ $1 = 0 ]; then
    /sbin/service libvirtd stop 1>/dev/null 2>&1
    /sbin/chkconfig --del libvirtd
fi
%endif
%endif

%postun
%if %{with_libvirtd}
%if %{with_systemd}
/bin/systemctl daemon-reload >/dev/null 2>&1 || :
if [ $1 -ge 1 ] ; then
    # Package upgrade, not uninstall
    /bin/systemctl try-restart libvirtd.service >/dev/null 2>&1 || :
fi
%endif
%endif

%if %{with_libvirtd}
%if %{with_systemd}
%triggerun -- libvirt < 0.9.4
%{_bindir}/systemd-sysv-convert --save libvirtd >/dev/null 2>&1 ||:

# If the package is allowed to autostart:
/bin/systemctl --no-reload enable libvirtd.service >/dev/null 2>&1 ||:

# Run these because the SysV package being removed won't do them
/sbin/chkconfig --del libvirtd >/dev/null 2>&1 || :
/bin/systemctl try-restart libvirtd.service >/dev/null 2>&1 || :
%endif
%endif

%preun client

%if %{with_systemd}
%else
if [ $1 = 0 ]; then
    /sbin/chkconfig --del libvirt-guests
    rm -f /var/lib/libvirt/libvirt-guests
fi
%endif

%post client

/sbin/ldconfig
%if %{with_systemd}
%else
/sbin/chkconfig --add libvirt-guests
if [ $1 -ge 1 ]; then
    level=$(/sbin/runlevel | /bin/cut -d ' ' -f 2)
    if /sbin/chkconfig --levels $level libvirt-guests; then
        # this doesn't do anything but allowing for libvirt-guests to be
        # stopped on the first shutdown
        /sbin/service libvirt-guests start > /dev/null 2>&1 || true
    fi
fi
%endif

%postun client -p /sbin/ldconfig

%if %{with_systemd}
%triggerun client -- libvirt < 0.9.4
%{_bindir}/systemd-sysv-convert --save libvirt-guests >/dev/null 2>&1 ||:

# If the package is allowed to autostart:
/bin/systemctl --no-reload enable libvirt-guests.service >/dev/null 2>&1 ||:

# Run these because the SysV package being removed won't do them
/sbin/chkconfig --del libvirt-guests >/dev/null 2>&1 || :
/bin/systemctl try-restart libvirt-guests.service >/dev/null 2>&1 || :
%endif

%if %{with_libvirtd}
%files
%defattr(-, root, root)

%doc AUTHORS ChangeLog.gz NEWS README COPYING.LIB TODO
%dir %attr(0700, root, root) %{_sysconfdir}/libvirt/

%if %{with_network}
%dir %attr(0700, root, root) %{_sysconfdir}/libvirt/qemu/
%dir %attr(0700, root, root) %{_sysconfdir}/libvirt/qemu/networks/
%dir %attr(0700, root, root) %{_sysconfdir}/libvirt/qemu/networks/autostart
%endif

%dir %attr(0700, root, root) %{_sysconfdir}/libvirt/nwfilter/
%{_sysconfdir}/libvirt/nwfilter/*.xml

%{_sysconfdir}/rc.d/init.d/libvirtd
%if %{with_systemd}
%{_unitdir}/libvirtd.service
%endif
%doc daemon/libvirtd.upstart
%config(noreplace) %{_sysconfdir}/sysconfig/libvirtd
%config(noreplace) %{_sysconfdir}/libvirt/libvirtd.conf
%if 0%{?fedora} >= 14 || 0%{?rhel} >= 6
%config(noreplace) %{_sysconfdir}/sysctl.d/libvirtd
%else
rm -f $RPM_BUILD_ROOT%{_sysconfdir}/sysctl.d/libvirtd
%endif
%if %{with_dtrace}
%{_datadir}/systemtap/tapset/libvirt_probes.stp
%{_datadir}/systemtap/tapset/libvirt_functions.stp
%endif
%dir %attr(0700, root, root) %{_localstatedir}/log/libvirt/qemu/
%dir %attr(0700, root, root) %{_localstatedir}/log/libvirt/lxc/
%dir %attr(0700, root, root) %{_localstatedir}/log/libvirt/uml/
%if %{with_libxl}
%dir %attr(0700, root, root) %{_localstatedir}/log/libvirt/libxl/
%endif

%config(noreplace) %{_sysconfdir}/logrotate.d/libvirtd
%if %{with_qemu}
%config(noreplace) %{_sysconfdir}/libvirt/qemu.conf
%config(noreplace) %{_sysconfdir}/logrotate.d/libvirtd.qemu
%endif
%if %{with_lxc}
%config(noreplace) %{_sysconfdir}/libvirt/lxc.conf
%config(noreplace) %{_sysconfdir}/logrotate.d/libvirtd.lxc
%endif
%if %{with_uml}
%config(noreplace) %{_sysconfdir}/logrotate.d/libvirtd.uml
%endif

%dir %{_datadir}/libvirt/

%if %{with_network}
%dir %{_datadir}/libvirt/networks/
%{_datadir}/libvirt/networks/default.xml
%endif

%ghost %dir %{_localstatedir}/run/libvirt/

%dir %attr(0711, root, root) %{_localstatedir}/lib/libvirt/images/
%dir %attr(0711, root, root) %{_localstatedir}/lib/libvirt/filesystems/
%dir %attr(0711, root, root) %{_localstatedir}/lib/libvirt/boot/
%dir %attr(0711, root, root) %{_localstatedir}/cache/libvirt/

%if %{with_qemu}
%ghost %dir %attr(0700, root, root) %{_localstatedir}/run/libvirt/qemu/
%dir %attr(0750, %{qemu_user}, %{qemu_group}) %{_localstatedir}/lib/libvirt/qemu/
%dir %attr(0750, %{qemu_user}, %{qemu_group}) %{_localstatedir}/cache/libvirt/qemu/
%endif
%if %{with_lxc}
%ghost %dir %{_localstatedir}/run/libvirt/lxc/
%dir %attr(0700, root, root) %{_localstatedir}/lib/libvirt/lxc/
%endif
%if %{with_uml}
%ghost %dir %{_localstatedir}/run/libvirt/uml/
%dir %attr(0700, root, root) %{_localstatedir}/lib/libvirt/uml/
%endif
%if %{with_libxl}
%ghost %dir %{_localstatedir}/run/libvirt/libxl/
%dir %attr(0700, root, root) %{_localstatedir}/lib/libvirt/libxl/
%endif
%if %{with_network}
%ghost %dir %{_localstatedir}/run/libvirt/network/
%dir %attr(0700, root, root) %{_localstatedir}/lib/libvirt/network/
%dir %attr(0755, root, root) %{_localstatedir}/lib/libvirt/dnsmasq/
%endif

%if %{with_qemu}
%{_datadir}/augeas/lenses/libvirtd_qemu.aug
%{_datadir}/augeas/lenses/tests/test_libvirtd_qemu.aug
%endif

%if %{with_lxc}
%{_datadir}/augeas/lenses/libvirtd_lxc.aug
%{_datadir}/augeas/lenses/tests/test_libvirtd_lxc.aug
%endif

%{_datadir}/augeas/lenses/libvirtd.aug
%{_datadir}/augeas/lenses/tests/test_libvirtd.aug

%if %{with_polkit}
%if 0%{?fedora} >= 12 || 0%{?rhel} >= 6
%{_datadir}/polkit-1/actions/org.libvirt.unix.policy
%else
%{_datadir}/PolicyKit/policy/org.libvirt.unix.policy
%endif
%endif

%dir %attr(0700, root, root) %{_localstatedir}/log/libvirt/

%if %{with_lxc}
%attr(0755, root, root) %{_libexecdir}/libvirt_lxc
%endif

%if %{with_storage_disk}
%attr(0755, root, root) %{_libexecdir}/libvirt_parthelper
%endif

%attr(0755, root, root) %{_libexecdir}/libvirt_iohelper
%attr(0755, root, root) %{_sbindir}/libvirtd

%{_mandir}/man8/libvirtd.8*

%doc docs/*.xml
%endif

%if %{with_sanlock}
%files lock-sanlock
%defattr(-, root, root)
%if %{with_qemu}
%config(noreplace) %{_sysconfdir}/libvirt/qemu-sanlock.conf
%endif
%attr(0755, root, root) %{_libdir}/libvirt/lock-driver/sanlock.so
%{_datadir}/augeas/lenses/libvirt_sanlock.aug
%{_datadir}/augeas/lenses/tests/test_libvirt_sanlock.aug
%dir %attr(0700, root, root) %{_localstatedir}/lib/libvirt/sanlock
%{_sbindir}/virt-sanlock-cleanup
%{_mandir}/man8/virt-sanlock-cleanup.8*
%endif

%files client -f %{name}.lang
%defattr(-, root, root)
%doc AUTHORS ChangeLog.gz NEWS README COPYING.LIB TODO

%config(noreplace) %{_sysconfdir}/libvirt/libvirt.conf
%{_mandir}/man1/virsh.1*
%{_mandir}/man1/virt-xml-validate.1*
%{_mandir}/man1/virt-pki-validate.1*
%{_mandir}/man1/virt-host-validate.1*
%{_bindir}/virsh
%{_bindir}/virt-xml-validate
%{_bindir}/virt-pki-validate
%{_bindir}/virt-host-validate
%{_libdir}/lib*.so.*

%dir %{_datadir}/libvirt/
%dir %{_datadir}/libvirt/schemas/

%{_datadir}/libvirt/schemas/basictypes.rng
%{_datadir}/libvirt/schemas/capability.rng
%{_datadir}/libvirt/schemas/domain.rng
%{_datadir}/libvirt/schemas/domaincommon.rng
%{_datadir}/libvirt/schemas/domainsnapshot.rng
%{_datadir}/libvirt/schemas/interface.rng
%{_datadir}/libvirt/schemas/network.rng
%{_datadir}/libvirt/schemas/networkcommon.rng
%{_datadir}/libvirt/schemas/nodedev.rng
%{_datadir}/libvirt/schemas/nwfilter.rng
%{_datadir}/libvirt/schemas/secret.rng
%{_datadir}/libvirt/schemas/storageencryption.rng
%{_datadir}/libvirt/schemas/storagepool.rng
%{_datadir}/libvirt/schemas/storagevol.rng

%{_datadir}/libvirt/cpu_map.xml

%{_sysconfdir}/rc.d/init.d/libvirt-guests
%if %{with_systemd}
%{_unitdir}/libvirt-guests.service
%endif
%config(noreplace) %{_sysconfdir}/sysconfig/libvirt-guests
%dir %attr(0755, root, root) %{_localstatedir}/lib/libvirt/

%if %{with_sasl}
%config(noreplace) %{_sysconfdir}/sasl2/libvirt.conf
%endif

%files devel
%defattr(-, root, root)

%{_libdir}/lib*.so
%dir %{_includedir}/libvirt
%{_includedir}/libvirt/*.h
%{_libdir}/pkgconfig/libvirt.pc
%dir %{_datadir}/gtk-doc/html/libvirt/
%doc %{_datadir}/gtk-doc/html/libvirt/*.devhelp
%doc %{_datadir}/gtk-doc/html/libvirt/*.html
%doc %{_datadir}/gtk-doc/html/libvirt/*.png
%doc %{_datadir}/gtk-doc/html/libvirt/*.css

%dir %{_datadir}/libvirt/apis/
%{_datadir}/libvirt/apis/libvirt-api.xml
%{_datadir}/libvirt/apis/libvirt-qemu-api.xml

%doc docs/*.html docs/html docs/*.gif
%doc docs/libvirt-api.xml
%doc examples/hellolibvirt
%doc examples/domain-events/events-c
%doc examples/dominfo
%doc examples/domsuspend
%doc examples/openauth
%doc examples/xml
%doc examples/systemtap

%if %{with_python}
%files python
%defattr(-, root, root)

%doc AUTHORS NEWS README COPYING.LIB
%{_libdir}/python*/site-packages/libvirt.py*
%{_libdir}/python*/site-packages/libvirt_qemu.py*
%{_libdir}/python*/site-packages/libvirtmod*
%doc python/tests/*.py
%doc python/TODO
%doc examples/python
%doc examples/domain-events/events-python
%endif

%changelog
* Tue Apr  3 2012 Daniel P. Berrange <berrange@redhat.com> - 0.9.10-4
- Revert previous change

* Sat Mar 31 2012 Daniel P. Berrange <berrange@redhat.com> - 0.9.10-3
- Remove previous non-upstream patch which did not fix bug 802475
- Refactor RPM spec to allow install without default configs

* Wed Mar 28 2012 Kevin Fenzi <kevin@scrye.com> - 0.9.10-2.1
- Add patch to fix ordering to come up after network target. Bug 802475

* Fri Mar  9 2012 Laine Stump <laine@redhat.com> - 0.9.10-2
- replace "fedora-13" machine type with "pc-0.14" to prepare
  systems for removal of "fedora-13" from qemu - Bug 754772
  (this change is already in the F16 builds)

* Mon Feb 13 2012 Daniel P. Berrange <berrange@redhat.com> - 0.9.10-1
- Update to 0.9.10

* Thu Jan 12 2012 Daniel P. Berrange <berrange@redhat.com> - 0.9.9-2
- Fix LXC I/O handling

* Sat Jan  7 2012 Daniel Veillard <veillard@redhat.com> - 0.9.9-1
- Add API virDomain{S,G}etInterfaceParameters
- Add API virDomain{G, S}etNumaParameters
- Add support for ppc64 qemu
- Support Xen domctl v8
- many improvements and bug fixes

* Thu Dec  8 2011 Daniel P. Berrange <berrange@redhat.com> - 0.9.8-2
- Fix install of libvirt-guests.service & libvirtd.service

* Thu Dec  8 2011 Daniel Veillard <veillard@redhat.com> - 0.9.8-1
- Add support for QEMU 1.0
- Add preliminary PPC cpu driver
- Add new API virDomain{Set, Get}BlockIoTune
- block_resize: Define the new API
- Add a public API to invoke suspend/resume on the host
- various improvements for LXC containers
- Define keepalive protocol and add virConnectIsAlive API
- Add support for STP and VLAN filtering
- many improvements and bug fixes

* Mon Nov 14 2011 Justin M. Forbes <jforbes@redhat.com> - 0.9.7-3
- Remove versioned buildreq for yajl as 2.0.x features are not required.

* Thu Nov 10 2011 Daniel P. Berrange <berrange@redhat.com> - 0.9.7-2
- Rebuild for yajl 2.0.1

* Tue Nov  8 2011 Daniel P. Berrange <berrange@redhat.com> - 0.9.7-1
- Update to 0.9.7 release

* Tue Oct 11 2011 Dan Horák <dan[at]danny.cz> - 0.9.6-3
- xenlight available only on Xen arches (#745020)

* Mon Oct  3 2011 Laine Stump <laine@redhat.com> - 0.9.6-2
- Make PCI multifunction support more manual - Bug 742836
- F15 build still uses cgconfig - Bug 738725

* Thu Sep 22 2011 Daniel Veillard <veillard@redhat.com> - 0.9.6-1
- Fix the qemu reboot bug and a few others bug fixes

* Tue Sep 20 2011 Daniel Veillard <veillard@redhat.com> - 0.9.5-1
- many snapshot improvements (Eric Blake)
- latency: Define new public API and structure (Osier Yang)
- USB2 and various USB improvements (Marc-André Lureau)
- storage: Add fs pool formatting (Osier Yang)
- Add public API for getting migration speed (Jim Fehlig)
- Add basic driver for Microsoft Hyper-V (Matthias Bolte)
- many improvements and bug fixes

* Wed Aug  3 2011 Daniel Veillard <veillard@redhat.com> - 0.9.4-1
- network bandwidth QoS control
- Add new API virDomainBlockPull*
- save: new API to manipulate save file images
- CPU bandwidth limits support
- allow to send NMI and key event to guests
- new API virDomainUndefineFlags
- Implement code to attach to external QEMU instances
- bios: Add support for SGA
- various missing python binding
- many improvements and bug fixes

* Sat Jul 30 2011 Dan Hor?k <dan[at]danny.cz> - 0.9.3-3
- xenlight available only on Xen arches

* Wed Jul  5 2011 Peter Robinson <pbrobinson@gmail.com> - 0.9.3-2
- Add ARM to NUMA platform excludes

* Mon Jul  4 2011 Daniel Veillard <veillard@redhat.com> - 0.9.3-1
- new API virDomainGetVcpupinInfo
- Add TXT record support for virtual DNS service
- Support reboots with the QEMU driver
- New API virDomainGetControlInfo API
- New API virNodeGetMemoryStats
- New API virNodeGetCPUTime
- New API for send-key
- New API virDomainPinVcpuFlags
- support multifunction PCI device
- lxc: various improvements
- many improvements and bug fixes

* Wed Jun 29 2011 Richard W.M. Jones <rjones@redhat.com> - 0.9.2-3
- Rebuild because of libparted soname bump (libparted.so.0 -> libparted.so.1).

* Tue Jun 21 2011 Laine Stump <laine@redhat.com> - 0.9.2-2
- add rule to require netcf-0.1.8 during build so that new transactional
  network change APIs are included.
- document that CVE-2011-2178 has been fixed (by virtue of rebase
  to 0.9.2 - see https://bugzilla.redhat.com/show_bug.cgi?id=709777)

* Mon Jun  6 2011 Daniel Veillard <veillard@redhat.com> - 0.9.2-1
- Framework for lock manager plugins
- API for network config change transactions
- flags for setting memory parameters
- virDomainGetState public API
- qemu: allow blkstat/blkinfo calls during migration
- Introduce migration v3 API
- Defining the Screenshot public API
- public API for NMI injection
- Various improvements and bug fixes

* Wed May 25 2011 Richard W.M. Jones <rjones@redhat.com> - 0.9.1-3
- Add upstream patches:
    0001-json-Avoid-passing-large-positive-64-bit-integers-to.patch
    0001-qemudDomainMemoryPeek-change-ownership-selinux-label.patch
    0002-remote-remove-bogus-virDomainFree.patch
  so that users can try out virt-dmesg.
- Change /var/cache mode to 0711.

* Thu May  5 2011 Daniel Veillard <veillard@redhat.com> - 0.9.1-1
- support various persistent domain updates
- improvements on memory APIs
- Add virDomainEventRebootNew
- various improvements to libxl driver
- Spice: support audio, images and stream compression
- Various improvements and bug fixes

* Thu Apr  7 2011 Daniel Veillard <veillard@redhat.com> - 0.9.0-1
- Support cputune cpu usage tuning
- Add public APIs for storage volume upload/download
- Add public API for setting migration speed on the fly
- Add libxenlight driver
- qemu: support migration to fd
- libvirt: add virDomain{Get,Set}BlkioParameters
- setmem: introduce a new libvirt API (virDomainSetMemoryFlags)
- Expose event loop implementation as a public API
- Dump the debug buffer to libvirtd.log on fatal signal
- Audit support
- Various improvements and bug fixes

* Mon Mar 14 2011 Daniel Veillard <veillard@redhat.com> - 0.8.8-3
- fix a lack of API check on read-only connections
- CVE-2011-1146

* Mon Feb 21 2011 Daniel P. Berrange <berrange@redhat.com> - 0.8.8-2
- Fix kernel boot with latest QEMU

* Thu Feb 17 2011 Daniel Veillard <veillard@redhat.com> - 0.8.8-1
- expose new API for sysinfo extraction
- cgroup blkio weight support
- smartcard device support
- qemu: Support per-device boot ordering
- Various improvements and bug fixes

* Tue Feb 08 2011 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 0.8.7-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_15_Mass_Rebuild

* Thu Jan  6 2011 Daniel Veillard <veillard@redhat.com> - 0.8.7-1
- Preliminary support for VirtualBox 4.0
- IPv6 support
- Add VMware Workstation and Player driver driver
- Add network disk support
- Various improvements and bug fixes
- from 0.8.6:
- Add support for iSCSI target auto-discovery
- QED: Basic support for QED images
- remote console support
- support for SPICE graphics
- sysinfo and VMBIOS support
- virsh qemu-monitor-command
- various improvements and bug fixes

* Fri Oct 29 2010 Daniel Veillard <veillard@redhat.com> - 0.8.5-1
- Enable JSON and netdev features in QEMU >= 0.13
- framework for auditing integration
- framework DTrace/SystemTap integration
- Setting the number of vcpu at boot
- Enable support for nested SVM
- Virtio plan9fs filesystem QEMU
- Memory parameter controls
- various improvements and bug fixes

* Wed Sep 29 2010 jkeating - 0.8.4-3
- Rebuilt for gcc bug 634757

* Thu Sep 16 2010 Dan Horák <dan[at]danny.cz> - 0.8.4-2
- disable the nwfilterxml2xmltest also on s390(x)

* Mon Sep 13 2010 Daniel Veillard <veillard@redhat.com> - 0.8.4-1
- Upstream release 0.8.4

* Mon Aug 23 2010 Daniel P. Berrange <berrange@redhat.com> - 0.8.3-2
- Fix potential overflow in boot menu code

* Mon Aug 23 2010 Daniel P. Berrange <berrange@redhat.com> - 0.8.3-1
- Upstream release 0.8.3

* Wed Jul 21 2010 David Malcolm <dmalcolm@redhat.com> - 0.8.2-3
- Rebuilt for https://fedoraproject.org/wiki/Features/Python_2.7/MassRebuild

* Mon Jul 12 2010 Daniel P. Berrange <berrange@redhat.com> - 0.8.2-2
- CVE-2010-2237 ignoring defined main disk format when looking up disk backing stores
- CVE-2010-2238 ignoring defined disk backing store format when recursing into disk
  image backing stores
- CVE-2010-2239 not setting user defined backing store format when creating new image
- CVE-2010-2242 libvirt: improperly mapped source privileged ports may allow for
  obtaining privileged resources on the host

* Mon Jul  5 2010 Daniel Veillard <veillard@redhat.com> - 0.8.2-1
- Upstream release 0.8.2
- phyp: adding support for IVM
- libvirt: introduce domainCreateWithFlags API
- add 802.1Qbh and 802.1Qbg switches handling
- Support for VirtualBox version 3.2
- Init script for handling guests on shutdown/boot
- qemu: live migration with non-shared storage for kvm

* Fri Apr 30 2010 Daniel Veillard <veillard@redhat.com> - 0.8.1-1
- Upstream release 0.8.1
- Starts dnsmasq from libvirtd with --dhcp-hostsfile
- Add virDomainGetBlockInfo API to query disk sizing
- a lot of bug fixes and cleanups

* Mon Apr 12 2010 Daniel Veillard <veillard@redhat.com> - 0.8.0-1
- Upstream release 0.8.0
- Snapshotting support (QEmu/VBox/ESX)
- Network filtering API
- XenAPI driver
- new APIs for domain events
- Libvirt managed save API
- timer subselection for domain clock
- synchronous hooks
- API to update guest CPU to host CPU
- virDomainUpdateDeviceFlags new API
- migrate max downtime API
- volume wiping API
- and many bug fixes

* Tue Mar 30 2010 Richard W.M. Jones <rjones@redhat.com> - 0.7.7-3.fc14
- No change, just rebuild against new libparted with bumped soname.

* Mon Mar 22 2010 Cole Robinson <crobinso@redhat.com> - 0.7.7-2.fc14
- Fix USB devices by product with security enabled (bz 574136)
- Set kernel/initrd in security driver, fixes some URL installs (bz 566425)

* Fri Mar  5 2010 Daniel Veillard <veillard@redhat.com> - 0.7.7-1
- macvtap support
- async job handling
- virtio channel
- computing baseline CPU
- virDomain{Attach,Detach}DeviceFlags
- assorted bug fixes and lots of cleanups

* Tue Feb 16 2010 Adam Jackson <ajax@redhat.com> 0.7.6-2
- libvirt-0.7.6-add-needed.patch: Fix FTBFS from --no-add-needed
- Add BuildRequires: xmlrpc-c-client for libxmlrpc_client.so

* Wed Feb  3 2010 Daniel Veillard <veillard@redhat.com> - 0.7.6-1
- upstream release of 0.7.6
- Use QEmu new device adressing when possible
- Implement CPU topology support for QEMU driver
- Implement SCSI controller hotplug/unplug for QEMU
- Implement support for multi IQN
- a lot of fixes and improvements

* Thu Jan 14 2010 Chris Weyl <cweyl@alumni.drew.edu> 0.7.5-3
- bump for libssh2 rebuild

* Tue Jan 12 2010 Daniel P. Berrange <berrange@redhat.com> - 0.7.5-2
- Rebuild for libparted soname change

* Wed Dec 23 2009 Daniel Veillard <veillard@redhat.com> - 0.7.5-1
- Add new API virDomainMemoryStats
- Public API and domain extension for CPU flags
- vbox: Add support for version 3.1
- Support QEMU's virtual FAT block device driver
- a lot of fixes

* Fri Nov 20 2009 Daniel Veillard <veillard@redhat.com> - 0.7.4-1
- upstream release of 0.7.4
- udev node device backend
- API to check object properties
- better QEmu monitor processing
- MAC address based port filtering for qemu
- support IPv6 and multiple addresses per interfaces
- a lot of fixes

* Thu Nov 19 2009 Daniel P. Berrange <berrange@redhat.com> - 0.7.2-6
- Really fix restore file labelling this time

* Wed Nov 11 2009 Daniel P. Berrange <berrange@redhat.com> - 0.7.2-5
- Disable numactl on s390[x]. Again.

* Wed Nov 11 2009 Daniel P. Berrange <berrange@redhat.com> - 0.7.2-4
- Fix QEMU save/restore permissions / labelling

* Thu Oct 29 2009 Mark McLoughlin <markmc@redhat.com> - 0.7.2-3
- Avoid compressing small log files (#531030)

* Thu Oct 29 2009 Mark McLoughlin <markmc@redhat.com> - 0.7.2-2
- Make libvirt-devel require libvirt-client, not libvirt
- Fix qemu machine types handling

* Wed Oct 14 2009 Daniel Veillard <veillard@redhat.com> - 0.7.2-1
- Upstream release of 0.7.2
- Allow to define ESX domains
- Allows suspend and resulme of LXC domains
- API for data streams
- many bug fixes

* Tue Oct 13 2009 Mark McLoughlin <markmc@redhat.com> - 0.7.1-12
- Fix restore of qemu guest using raw save format (#523158)

* Fri Oct  9 2009 Mark McLoughlin <markmc@redhat.com> - 0.7.1-11
- Fix libvirtd memory leak during error reply sending (#528162)
- Add several PCI hot-unplug typo fixes from upstream

* Tue Oct  6 2009 Mark McLoughlin <markmc@redhat.com> - 0.7.1-10
- Create /var/log/libvirt/{lxc,uml} dirs for logrotate
- Make libvirt-python dependon on libvirt-client
- Sync misc minor changes from upstream spec

* Tue Oct  6 2009 Mark McLoughlin <markmc@redhat.com> - 0.7.1-9
- Change logrotate config to weekly (#526769)

* Thu Oct  1 2009 Mark McLoughlin <markmc@redhat.com> - 0.7.1-8
- Disable sound backend, even when selinux is disabled (#524499)
- Re-label qcow2 backing files (#497131)

* Wed Sep 30 2009 Mark McLoughlin <markmc@redhat.com> - 0.7.1-7
- Fix USB device passthrough (#522683)

* Mon Sep 21 2009 Chris Weyl <cweyl@alumni.drew.edu> - 0.7.1-6
- rebuild for libssh2 1.2

* Mon Sep 21 2009 Mark McLoughlin <markmc@redhat.com> - 0.7.1-5
- Don't set a bogus error in virDrvSupportsFeature()
- Fix raw save format

* Thu Sep 17 2009 Mark McLoughlin <markmc@redhat.com> - 0.7.1-4
- A couple of hot-unplug memory handling fixes (#523953)

* Thu Sep 17 2009 Daniel Veillard <veillard@redhat.com> - 0.7.1-3
- disable numactl on s390[x]

* Thu Sep 17 2009 Daniel Veillard <veillard@redhat.com> - 0.7.1-2
- revamp of spec file for modularity and RHELs

* Tue Sep 15 2009 Daniel Veillard <veillard@redhat.com> - 0.7.1-1
- Upstream release of 0.7.1
- ESX, VBox driver updates
- mutipath support
- support for encrypted (qcow) volume
- compressed save image format for Qemu/KVM
- QEmu host PCI device hotplug support
- configuration of huge pages in guests
- a lot of fixes

* Mon Sep 14 2009 Mark McLoughlin <markmc@redhat.com> - 0.7.1-0.2.gitfac3f4c
- Update to newer snapshot of 0.7.1
- Stop libvirt using untrusted 'info vcpus' PID data (#520864)
- Support relabelling of USB and PCI devices
- Enable multipath storage support
- Restart libvirtd upon RPM upgrade

* Sun Sep  6 2009 Mark McLoughlin <markmc@redhat.com> - 0.7.1-0.1.gitg3ef2e05
- Update to pre-release git snapshot of 0.7.1
- Drop upstreamed patches

* Wed Aug 19 2009 Mark McLoughlin <markmc@redhat.com> - 0.7.0-6
- Fix migration completion with newer versions of qemu (#516187)

* Wed Aug 19 2009 Mark McLoughlin <markmc@redhat.com> - 0.7.0-5
- Add PCI host device hotplug support
- Allow PCI bus reset to reset other devices (#499678)
- Fix stupid PCI reset error message (bug #499678)
- Allow PM reset on multi-function PCI devices (bug #515689)
- Re-attach PCI host devices after guest shuts down (bug #499561)
- Fix list corruption after disk hot-unplug
- Fix minor 'virsh nodedev-list --tree' annoyance

* Thu Aug 13 2009 Daniel P. Berrange <berrange@redhat.com> - 0.7.0-4
- Rewrite policykit support (rhbz #499970)
- Log and ignore NUMA topology problems (rhbz #506590)

* Mon Aug 10 2009 Mark McLoughlin <markmc@redhat.com> - 0.7.0-3
- Don't fail to start network if ipv6 modules is not loaded (#516497)

* Thu Aug  6 2009 Mark McLoughlin <markmc@redhat.com> - 0.7.0-2
- Make sure qemu can access kernel/initrd (bug #516034)
- Set perms on /var/lib/libvirt/boot to 0711 (bug #516034)

* Wed Aug  5 2009 Daniel Veillard <veillard@redhat.com> - 0.7.0-1
- ESX, VBox3, Power Hypervisor drivers
- new net filesystem glusterfs
- Storage cloning for LVM and Disk backends
- interface implementation based on netcf
- Support cgroups in QEMU driver
- QEmu hotplug NIC support
- a lot of fixes

* Fri Jul  3 2009 Daniel Veillard <veillard@redhat.com> - 0.6.5-1
- release of 0.6.5

* Fri May 29 2009 Daniel Veillard <veillard@redhat.com> - 0.6.4-1
- release of 0.6.4
- various new APIs

* Fri Apr 24 2009 Daniel Veillard <veillard@redhat.com> - 0.6.3-1
- release of 0.6.3
- VirtualBox driver

* Fri Apr  3 2009 Daniel Veillard <veillard@redhat.com> - 0.6.2-1
- release of 0.6.2

* Fri Mar  4 2009 Daniel Veillard <veillard@redhat.com> - 0.6.1-1
- release of 0.6.1

* Sat Jan 31 2009 Daniel Veillard <veillard@redhat.com> - 0.6.0-1
- release of 0.6.0

* Tue Nov 25 2008 Daniel Veillard <veillard@redhat.com> - 0.5.0-1
- release of 0.5.0

* Tue Sep 23 2008 Daniel Veillard <veillard@redhat.com> - 0.4.6-1
- release of 0.4.6

* Mon Sep  8 2008 Daniel Veillard <veillard@redhat.com> - 0.4.5-1
- release of 0.4.5

* Wed Jun 25 2008 Daniel Veillard <veillard@redhat.com> - 0.4.4-1
- release of 0.4.4
- mostly a few bug fixes from 0.4.3

* Thu Jun 12 2008 Daniel Veillard <veillard@redhat.com> - 0.4.3-1
- release of 0.4.3
- lots of bug fixes and small improvements

* Tue Apr  8 2008 Daniel Veillard <veillard@redhat.com> - 0.4.2-1
- release of 0.4.2
- lots of bug fixes and small improvements

* Mon Mar  3 2008 Daniel Veillard <veillard@redhat.com> - 0.4.1-1
- Release of 0.4.1
- Storage APIs
- xenner support
- lots of assorted improvements, bugfixes and cleanups
- documentation and localization improvements

* Tue Dec 18 2007 Daniel Veillard <veillard@redhat.com> - 0.4.0-1
- Release of 0.4.0
- SASL based authentication
- PolicyKit authentication
- improved NUMA and statistics support
- lots of assorted improvements, bugfixes and cleanups
- documentation and localization improvements

* Sun Sep 30 2007 Daniel Veillard <veillard@redhat.com> - 0.3.3-1
- Release of 0.3.3
- Avahi support
- NUMA support
- lots of assorted improvements, bugfixes and cleanups
- documentation and localization improvements

* Tue Aug 21 2007 Daniel Veillard <veillard@redhat.com> - 0.3.2-1
- Release of 0.3.2
- API for domains migration
- APIs for collecting statistics on disks and interfaces
- lots of assorted bugfixes and cleanups
- documentation and localization improvements

* Tue Jul 24 2007 Daniel Veillard <veillard@redhat.com> - 0.3.1-1
- Release of 0.3.1
- localtime clock support
- PS/2 and USB input devices
- lots of assorted bugfixes and cleanups
- documentation and localization improvements

* Mon Jul  9 2007 Daniel Veillard <veillard@redhat.com> - 0.3.0-1
- Release of 0.3.0
- Secure remote access support
- unification of daemons
- lots of assorted bugfixes and cleanups
- documentation and localization improvements

* Fri Jun  8 2007 Daniel Veillard <veillard@redhat.com> - 0.2.3-1
- Release of 0.2.3
- lot of assorted bugfixes and cleanups
- support for Xen-3.1
- new scheduler API

* Tue Apr 17 2007 Daniel Veillard <veillard@redhat.com> - 0.2.2-1
- Release of 0.2.2
- lot of assorted bugfixes and cleanups
- preparing for Xen-3.0.5

* Thu Mar 22 2007 Jeremy Katz <katzj@redhat.com> - 0.2.1-2.fc7
- don't require xen; we don't need the daemon and can control non-xen now
- fix scriptlet error (need to own more directories)
- update description text

* Fri Mar 16 2007 Daniel Veillard <veillard@redhat.com> - 0.2.1-1
- Release of 0.2.1
- lot of bug and portability fixes
- Add support for network autostart and init scripts
- New API to detect the virtualization capabilities of a host
- Documentation updates

* Fri Feb 23 2007 Daniel P. Berrange <berrange@redhat.com> - 0.2.0-4.fc7
- Fix loading of guest & network configs

* Fri Feb 16 2007 Daniel P. Berrange <berrange@redhat.com> - 0.2.0-3.fc7
- Disable kqemu support since its not in Fedora qemu binary
- Fix for -vnc arg syntax change in 0.9.0  QEMU

* Thu Feb 15 2007 Daniel P. Berrange <berrange@redhat.com> - 0.2.0-2.fc7
- Fixed path to qemu daemon for autostart
- Fixed generation of <features> block in XML
- Pre-create config directory at startup

* Wed Feb 14 2007 Daniel Veillard <veillard@redhat.com> 0.2.0-1.fc7
- support for KVM and QEmu
- support for network configuration
- assorted fixes

* Mon Jan 22 2007 Daniel Veillard <veillard@redhat.com> 0.1.11-1.fc7
- finish inactive Xen domains support
- memory leak fix
- RelaxNG schemas for XML configs

* Wed Dec 20 2006 Daniel Veillard <veillard@redhat.com> 0.1.10-1.fc7
- support for inactive Xen domains
- improved support for Xen display and vnc
- a few bug fixes
- localization updates

* Thu Dec  7 2006 Jeremy Katz <katzj@redhat.com> - 0.1.9-2
- rebuild against python 2.5

* Wed Nov 29 2006 Daniel Veillard <veillard@redhat.com> 0.1.9-1
- better error reporting
- python bindings fixes and extensions
- add support for shareable drives
- add support for non-bridge style networking
- hot plug device support
- added support for inactive domains
- API to dump core of domains
- various bug fixes, cleanups and improvements
- updated the localization

* Tue Nov  7 2006 Daniel Veillard <veillard@redhat.com> 0.1.8-3
- it's pkgconfig not pgkconfig !

* Mon Nov  6 2006 Daniel Veillard <veillard@redhat.com> 0.1.8-2
- fixing spec file, added %dist, -devel requires pkgconfig and xen-devel
- Resolves: rhbz#202320

* Mon Oct 16 2006 Daniel Veillard <veillard@redhat.com> 0.1.8-1
- fix missing page size detection code for ia64
- fix mlock size when getting domain info list from hypervisor
- vcpu number initialization
- don't label crashed domains as shut off
- fix virsh man page
- blktapdd support for alternate drivers like blktap
- memory leak fixes (xend interface and XML parsing)
- compile fix
- mlock/munlock size fixes

* Fri Sep 22 2006 Daniel Veillard <veillard@redhat.com> 0.1.7-1
- Fix bug when running against xen-3.0.3 hypercalls
- Fix memory bug when getting vcpus info from xend

* Fri Sep 22 2006 Daniel Veillard <veillard@redhat.com> 0.1.6-1
- Support for localization
- Support for new Xen-3.0.3 cdrom and disk configuration
- Support for setting VNC port
- Fix bug when running against xen-3.0.2 hypercalls
- Fix reconnection problem when talking directly to http xend

* Tue Sep  5 2006 Jeremy Katz <katzj@redhat.com> - 0.1.5-3
- patch from danpb to support new-format cd devices for HVM guests

* Tue Sep  5 2006 Daniel Veillard <veillard@redhat.com> 0.1.5-2
- reactivating ia64 support

* Tue Sep  5 2006 Daniel Veillard <veillard@redhat.com> 0.1.5-1
- new release
- bug fixes
- support for new hypervisor calls
- early code for config files and defined domains

* Mon Sep  4 2006 Daniel Berrange <berrange@redhat.com> - 0.1.4-5
- add patch to address dom0_ops API breakage in Xen 3.0.3 tree

* Mon Aug 28 2006 Jeremy Katz <katzj@redhat.com> - 0.1.4-4
- add patch to support paravirt framebuffer in Xen

* Mon Aug 21 2006 Daniel Veillard <veillard@redhat.com> 0.1.4-3
- another patch to fix network handling in non-HVM guests

* Thu Aug 17 2006 Daniel Veillard <veillard@redhat.com> 0.1.4-2
- patch to fix virParseUUID()

* Wed Aug 16 2006 Daniel Veillard <veillard@redhat.com> 0.1.4-1
- vCPUs and affinity support
- more complete XML, console and boot options
- specific features support
- enforced read-only connections
- various improvements, bug fixes

* Wed Aug  2 2006 Jeremy Katz <katzj@redhat.com> - 0.1.3-6
- add patch from pvetere to allow getting uuid from libvirt

* Wed Aug  2 2006 Jeremy Katz <katzj@redhat.com> - 0.1.3-5
- build on ia64 now

* Thu Jul 27 2006 Jeremy Katz <katzj@redhat.com> - 0.1.3-4
- don't BR xen, we just need xen-devel

* Thu Jul 27 2006 Daniel Veillard <veillard@redhat.com> 0.1.3-3
- need rebuild since libxenstore is now versionned

* Mon Jul 24 2006 Mark McLoughlin <markmc@redhat.com> - 0.1.3-2
- Add BuildRequires: xen-devel

* Wed Jul 12 2006 Jesse Keating <jkeating@redhat.com> - 0.1.3-1.1
- rebuild

* Tue Jul 11 2006 Daniel Veillard <veillard@redhat.com> 0.1.3-1
- support for HVM Xen guests
- various bugfixes

* Mon Jul  3 2006 Daniel Veillard <veillard@redhat.com> 0.1.2-1
- added a proxy mechanism for read only access using httpu
- fixed header includes paths

* Wed Jun 21 2006 Daniel Veillard <veillard@redhat.com> 0.1.1-1
- extend and cleanup the driver infrastructure and code
- python examples
- extend uuid support
- bug fixes, buffer handling cleanups
- support for new Xen hypervisor API
- test driver for unit testing
- virsh --conect argument

* Mon Apr 10 2006 Daniel Veillard <veillard@redhat.com> 0.1.0-1
- various fixes
- new APIs: for Node information and Reboot
- virsh improvements and extensions
- documentation updates and man page
- enhancement and fixes of the XML description format

* Tue Feb 28 2006 Daniel Veillard <veillard@redhat.com> 0.0.6-1
- added error handling APIs
- small bug fixes
- improve python bindings
- augment documentation and regression tests

* Thu Feb 23 2006 Daniel Veillard <veillard@redhat.com> 0.0.5-1
- new domain creation API
- new UUID based APIs
- more tests, documentation, devhelp
- bug fixes

* Fri Feb 10 2006 Daniel Veillard <veillard@redhat.com> 0.0.4-1
- fixes some problems in 0.0.3 due to the change of names

* Wed Feb  8 2006 Daniel Veillard <veillard@redhat.com> 0.0.3-1
- changed library name to libvirt from libvir, complete and test the python
  bindings

* Sun Jan 29 2006 Daniel Veillard <veillard@redhat.com> 0.0.2-1
- upstream release of 0.0.2, use xend, save and restore added, python bindings
  fixed

* Wed Nov  2 2005 Daniel Veillard <veillard@redhat.com> 0.0.1-1
- created
