#!/bin/sh

for dir in "$@"; do
    mkdir -p $DESTDIR/$dir
done
