# This script is used to build libvirt inside the container.
#
# You can customize it to your liking, or alternatively use a
# completely different script by passing
#
#  CI_BUILD_SCRIPT=/path/to/your/build/script
#
# to make.

cd "$CI_CONT_SRCDIR"

export VIR_TEST_DEBUG=1

meson build --werror $MESON_OPTS || (cat build/meson-logs/meson-log.txt && exit 1)
ninja -C build $CI_NINJA_ARGS
