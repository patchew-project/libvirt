# This script is used to build libvirt inside the container.
#
# You can customize it to your liking, or alternatively use a
# completely different script by passing
#
#  CI_BUILD_SCRIPT=/path/to/your/build/script
#
# to make.

mkdir -p "$CI_CONT_SRCDIR" || exit 1
cd "$CI_CONT_SRCDIR"

export VIR_TEST_DEBUG=1

meson build --werror || (cat build/meson-logs/meson-log.txt && exit 1)
ninja -C build "$CI_NINJA_ARGS"
ninja -C build

find -name test-suite.log -delete

if test $? != 0; then \
    LOGS=$(find -name test-suite.log)
    if test "$LOGS"; then
        echo "=== LOG FILE(S) START ==="
        cat $LOGS
        echo "=== LOG FILE(S) END ==="
    fi
    exit 1
fi
