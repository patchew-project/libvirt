#!/bin/sh

printf 'EXPORTS\n'
sed -e '/^$/d; /#/d; /:/d; /}/d; /\*/d; /LIBVIRT_/d' \
    -e 's/[  ]*\(.*\)\;/    \1/g' $1
