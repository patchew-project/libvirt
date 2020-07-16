#!/bin/sh

if test -n "$SOURCE_DATE_EPOCH";
then
    date -u --date="$SOURCE_DATE_EPOCH"
else
    date -u
fi
