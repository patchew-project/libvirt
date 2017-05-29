#!/bin/bash -e

function do_Install_Dependencies(){
    echo
    echo '-- Installing Dependencies --'

    apt-get update -qq
    apt-get -y -qq install \
            build-essential git clang autoconf libtool libcmpicppimpl0 gettext \
            xsltproc autopoint libxml2-dev libncurses5-dev libreadline-dev \
            zlib1g-dev libgnutls28-dev libgcrypt11-dev libavahi-client-dev libsasl2-dev \
            libxen-dev lvm2 libgcrypt11-dev libparted0-dev libdevmapper-dev uuid-dev \
            libudev-dev libpciaccess-dev libcap-ng-dev libnl-3-dev libnl-route-3-dev \
            libyajl-dev libpcap0.8-dev libnuma-dev libnetcf-dev libaudit-dev \
            libxml2-utils libapparmor-dev dnsmasq-base librbd-dev w3c-markup-validator
}


function do_Show_Info(){
    echo
    echo '-- Environment --'
    echo "Running on Docker: $DISTRO"
}


function do_Show_Compiler(){

    if [[ -n $CC ]]; then
        echo
        echo '-- Compiler in use --'
        "$CC" --version
    fi
}


# ----------- Build and Test libvirt -----------

if [[ -n $IMAGE ]]; then
    # Run docker using the selected image; then build and test
    docker run -v "$(pwd)":/cwd -e CC=$CCO -e DISTRO=$IMAGE "$IMAGE" sh -e -c " \
        cd /cwd; \
        tests/travis-ci.sh"
    exit $?
fi

if [[ -n $DISTRO ]]; then
    do_Show_Info
    do_Install_Dependencies
    do_Show_Compiler
fi

# The custom PATH is just to pick up OS-X homebrew & its harmless on Linux
PATH="/usr/local/opt/gettext/bin:/usr/local/opt/rpcgen/bin:$PATH" ./autogen.sh

# Build and test
if [[ "$TRAVIS_OS_NAME" = "osx" ]]; then
    # many unit tests fail & so does syntax-check, so skip for now
    # one day we must fix it though....
    make -j3
else
    VIR_TEST_DEBUG=1 make -j3 && make -j3 syntax-check && make -j3 check
fi

exit $?
