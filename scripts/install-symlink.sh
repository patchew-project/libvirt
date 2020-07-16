#!/bin/sh

mkdir -p $DESTDIR/$1

cd $DESTDIR/$1

ln -f -s $2 $3
