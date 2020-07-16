#!/bin/sh

AUGPARSE=$1
srcdir=$2
builddir=$3
augeastest=$4

set -vx

for f in $augeastest; do
    ${AUGPARSE} -I "$srcdir" -I "$builddir" $f
done
