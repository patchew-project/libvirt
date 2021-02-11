# THIS FILE WAS AUTO-GENERATED
#
#  $ lcitool dockerfile --cross armv6l debian-sid libvirt
#
# https://gitlab.com/libvirt/libvirt-ci/-/commit/824b894aa3ca40e9d2807b693765e213cb8aa832
FROM docker.io/library/debian:sid-slim

RUN export DEBIAN_FRONTEND=noninteractive && \
    apt-get update && \
    apt-get install -y eatmydata && \
    eatmydata apt-get dist-upgrade -y && \
    eatmydata apt-get install --no-install-recommends -y \
            augeas-lenses \
            augeas-tools \
            bash-completion \
            ca-certificates \
            ccache \
            clang \
            diffutils \
            dnsmasq-base \
            dwarves \
            ebtables \
            flake8 \
            gcc \
            gettext \
            git \
            iproute2 \
            iptables \
            kmod \
            libc-dev-bin \
            libxml2-utils \
            locales \
            lvm2 \
            make \
            meson \
            nfs-common \
            ninja-build \
            numad \
            open-iscsi \
            parted \
            perl \
            pkgconf \
            policykit-1 \
            python3 \
            python3-docutils \
            qemu-utils \
            radvd \
            scrub \
            xsltproc \
            zfs-fuse && \
    eatmydata apt-get autoremove -y && \
    eatmydata apt-get autoclean -y && \
    sed -Ei 's,^# (en_US\.UTF-8 .*)$,\1,' /etc/locale.gen && \
    dpkg-reconfigure locales && \
    dpkg-query --showformat '${Package}_${Version}_${Architecture}\n' --show > /packages.txt && \
    mkdir -p /usr/libexec/ccache-wrappers && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/arm-linux-gnueabi-cc && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/arm-linux-gnueabi-$(basename /usr/bin/gcc)

RUN export DEBIAN_FRONTEND=noninteractive && \
    dpkg --add-architecture armel && \
    eatmydata apt-get update && \
    eatmydata apt-get dist-upgrade -y && \
    eatmydata apt-get install --no-install-recommends -y dpkg-dev && \
    eatmydata apt-get install --no-install-recommends -y \
            gcc-arm-linux-gnueabi \
            libacl1-dev:armel \
            libapparmor-dev:armel \
            libattr1-dev:armel \
            libaudit-dev:armel \
            libavahi-client-dev:armel \
            libblkid-dev:armel \
            libc6-dev:armel \
            libcap-ng-dev:armel \
            libcurl4-gnutls-dev:armel \
            libdbus-1-dev:armel \
            libdevmapper-dev:armel \
            libfuse-dev:armel \
            libglib2.0-dev:armel \
            libglusterfs-dev:armel \
            libgnutls28-dev:armel \
            libiscsi-dev:armel \
            libnl-3-dev:armel \
            libnl-route-3-dev:armel \
            libnuma-dev:armel \
            libparted-dev:armel \
            libpcap0.8-dev:armel \
            libpciaccess-dev:armel \
            librbd-dev:armel \
            libreadline-dev:armel \
            libsanlock-dev:armel \
            libsasl2-dev:armel \
            libselinux1-dev:armel \
            libssh-gcrypt-dev:armel \
            libssh2-1-dev:armel \
            libtirpc-dev:armel \
            libudev-dev:armel \
            libxml2-dev:armel \
            libyajl-dev:armel \
            systemtap-sdt-dev:armel \
            xfslibs-dev:armel && \
    eatmydata apt-get autoremove -y && \
    eatmydata apt-get autoclean -y && \
    mkdir -p /usr/local/share/meson/cross && \
    echo "[binaries]\n\
c = '/usr/bin/arm-linux-gnueabi-gcc'\n\
ar = '/usr/bin/arm-linux-gnueabi-gcc-ar'\n\
strip = '/usr/bin/arm-linux-gnueabi-strip'\n\
pkgconfig = '/usr/bin/arm-linux-gnueabi-pkg-config'\n\
\n\
[host_machine]\n\
system = 'linux'\n\
cpu_family = 'arm'\n\
cpu = 'arm'\n\
endian = 'little'" > /usr/local/share/meson/cross/arm-linux-gnueabi

ENV LANG "en_US.UTF-8"
ENV MAKE "/usr/bin/make"
ENV NINJA "/usr/bin/ninja"
ENV PYTHON "/usr/bin/python3"
ENV CCACHE_WRAPPERSDIR "/usr/libexec/ccache-wrappers"

ENV ABI "arm-linux-gnueabi"
ENV MESON_OPTS "--cross-file=arm-linux-gnueabi"
