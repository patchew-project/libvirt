#!/usr/bin/env python3
#
# Copyright (C) 2012-2019 Red Hat, Inc.
#
# augeas-gentest.py: Generate an augeas test file, from an
#                    example config file + test file template
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import re
import sys

if len(sys.argv) != 3:
    print("syntax: %s CONFIG TEMPLATE" % sys.argv[0], file=sys.stderr)
    sys.exit(1)

config = sys.argv[1]
template = sys.argv[2]


def expand_config(config):
    with open(config) as fh:
        group = False
        for line in fh:
            if re.search(r'''^#\w''', line) is not None:
                line = line[1:]
                line = line.replace('"', '\\"')
                print(line, end='')
                if re.search(r'''\[\s$''', line):
                    group = True
            elif group:
                line = line.replace('"', '\\"')

                if re.search(r'''#\s*\]''', line):
                    group = False

                if line[0] == '#':
                    line = line[1:]
                    print(line, end='')


def expand_template(template, config):
    with open(template) as fh:
        for line in fh:
            if '@CONFIG@' in line:
                print('   let conf = "', end='')
                expand_config(config)
                print('"')
            else:
                print(line, end='')


expand_template(template, config)
