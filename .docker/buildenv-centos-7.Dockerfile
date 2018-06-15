FROM centos:centos7
ENV PACKAGES audit-libs-devel \
             augeas \
             autoconf \
             automake \
             avahi-devel \
             bash \
             bash-completion \
             chrony \
             cyrus-sasl-devel \
             dbus-devel \
             device-mapper-devel \
             dnsmasq \
             ebtables \
             fuse-devel \
             gcc \
             gettext \
             gettext-devel \
             git \
             glibc-common \
             glibc-devel \
             glusterfs-api-devel \
             gnutls-devel \
             iproute \
             iscsi-initiator-utils \
             libacl-devel \
             libattr-devel \
             libblkid-devel \
             libcap-ng-devel \
             libcurl-devel \
             libnl3-devel \
             libpcap-devel \
             libpciaccess-devel \
             librbd1-devel \
             libselinux-devel \
             libssh-devel \
             libssh2-devel \
             libtirpc-devel \
             libtool \
             libudev-devel \
             libwsman-devel \
             libxml2 \
             libxml2-devel \
             libxslt \
             lvm2 \
             make \
             netcf-devel \
             nfs-utils \
             numactl-devel \
             numad \
             parted \
             parted-devel \
             patch \
             perl \
             pkgconfig \
             polkit \
             qemu-img \
             radvd \
             readline-devel \
             rpm-build \
             sanlock-devel \
             screen \
             scrub \
             sudo \
             systemtap-sdt-devel \
             vim \
             yajl-devel
RUN yum install -y ${PACKAGES} && \
    yum autoremove -y && \
    yum clean all -y
