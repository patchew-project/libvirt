#!/bin/sh

version="$1"
shift
public="$1"
shift
private="$@"

printf "# WARNING: generated from the following files:\n\n"
cat $public
printf "\n\n# Private symbols\n\n"
printf "$version {\n\n"
printf "global:\n\n"
cat $private
printf "\n\nlocal:\n*;\n\n};"
