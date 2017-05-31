#!/bin/bash -e

function do_Install_Dependencies(){
    echo
    echo '-- Installing Dependencies --'

    if [[ $DISTRO = "ubuntu:17.04" ]]; then
        apt-get update -qq
        apt-get -y -qq install \
            build-essential git clang autoconf libtool libcmpicppimpl0 gettext \
            xsltproc autopoint libxml2-dev libncurses5-dev libreadline-dev \
            zlib1g-dev libgnutls28-dev libgcrypt11-dev libavahi-client-dev libsasl2-dev \
            libxen-dev lvm2 libgcrypt11-dev libparted0-dev libdevmapper-dev uuid-dev \
            libudev-dev libpciaccess-dev libcap-ng-dev libnl-3-dev libnl-route-3-dev \
            libyajl-dev libpcap0.8-dev libnuma-dev libnetcf-dev libaudit-dev \
            libxml2-utils libapparmor-dev dnsmasq-base librbd-dev w3c-markup-validator kmod > /dev/null
    else
        echo "Please, change 'tests/travis-ci.sh' to add the needed dependencies for $DISTRO."
        exit 1
    fi
}


function do_Show_Info(){
    echo
    echo '-- Environment --'
    echo "Running on Docker: $DISTRO"
    id
    uname -a

    if [[ -n $CC ]]; then
        echo
        echo '-- Compiler in use --'
        "$CC" --version
    fi

    echo -en 'travis_fold:start:printenv\r'
    echo '-- Environment Variables --'
    printenv
    echo -en 'travis_fold:end:printenv\r'
}


# ----------- Build and Test libvirt -----------

if [[ -n $IMAGE ]]; then
    # Run docker using the selected image; then build and test
    docker run --privileged --cap-add=ALL -v /lib/modules:/lib/modules \
      -v "$(pwd)":/cwd -e CC=$CC -e DISTRO=$IMAGE "$IMAGE" sh -e -c " \
        cd /cwd; \
        tests/travis-ci.sh"
    exit $?
fi

if [[ -n $DISTRO ]]; then
    do_Install_Dependencies
    do_Show_Info
fi

# Build and test
if [[ "$TRAVIS_OS_NAME" = "osx" ]]; then
    echo -en 'travis_fold:start:autogen\r'
    echo '-- Running ./autogen.sh --'
    # The custom PATH is just to pick up OS-X homebrew
    PATH="/usr/local/opt/gettext/bin:/usr/local/opt/rpcgen/bin:$PATH" ./autogen.sh
    echo -en 'travis_fold:end:autogen\r'

    # many unit tests fail & so does syntax-check, so skip for now
    # one day we must fix it though....
    make -j3
else
    echo -en 'travis_fold:start:autogen\r'
    echo '-- Running ./autogen.sh --'
    ./autogen.sh
    echo -en 'travis_fold:end:autogen\r'

    VIR_TEST_DEBUG=1 make -j3 && make -j3 syntax-check && make -j3 check
fi

exit $?
