FROM debian:10

RUN export DEBIAN_FRONTEND=noninteractive && \
    apt-get update && \
    apt-get dist-upgrade -y && \
    apt-get install --no-install-recommends -y \
            augeas-lenses \
            augeas-tools \
            autoconf \
            automake \
            autopoint \
            bash \
            bash-completion \
            ca-certificates \
            ccache \
            chrony \
            cpanminus \
            dnsmasq-base \
            dwarves \
            ebtables \
            flake8 \
            gcc \
            gdb \
            gettext \
            git \
            iproute2 \
            kmod \
            libc-dev-bin \
            libtool \
            libtool-bin \
            libxml2-utils \
            locales \
            lsof \
            lvm2 \
            make \
            meson \
            net-tools \
            nfs-common \
            ninja-build \
            numad \
            open-iscsi \
            parted \
            patch \
            perl \
            pkgconf \
            policykit-1 \
            python3 \
            python3-docutils \
            python3-pip \
            python3-setuptools \
            python3-wheel \
            qemu-utils \
            radvd \
            screen \
            scrub \
            strace \
            sudo \
            vim \
            xsltproc \
            xz-utils \
            zfs-fuse && \
    apt-get autoremove -y && \
    apt-get autoclean -y && \
    sed -Ei 's,^# (en_US\.UTF-8 .*)$,\1,' /etc/locale.gen && \
    dpkg-reconfigure locales && \
    mkdir -p /usr/libexec/ccache-wrappers && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/arm-linux-gnueabi-cc && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/arm-linux-gnueabi-$(basename /usr/bin/gcc)

RUN export DEBIAN_FRONTEND=noninteractive && \
    dpkg --add-architecture armel && \
    apt-get update && \
    apt-get dist-upgrade -y && \
    apt-get install --no-install-recommends -y dpkg-dev && \
    apt-get install --no-install-recommends -y \
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
            libncurses-dev:armel \
            libnl-3-dev:armel \
            libnl-route-3-dev:armel \
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
            xfslibs-dev:armel && \
    apt-get autoremove -y && \
    apt-get autoclean -y

ENV LANG "en_US.UTF-8"

ENV MAKE "/usr/bin/make"
ENV NINJA "/usr/bin/ninja"
ENV PYTHON "/usr/bin/python3"

ENV CCACHE_WRAPPERSDIR "/usr/libexec/ccache-wrappers"

ENV ABI "arm-linux-gnueabi"
ENV CONFIGURE_OPTS "--host=arm-linux-gnueabi"
