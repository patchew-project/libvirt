#!/bin/sh

perms=$1
file=$2

chmod $perms $MESON_BUILD_ROOT/$file
