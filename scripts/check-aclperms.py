#!/usr/bin/env python3
#
# Copyright (C) 2013-2019 Red Hat, Inc.
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# This script just validates that the stringified version of
# a virAccessPerm enum matches the enum constant name. We do
# a lot of auto-generation of code, so when these don't match
# problems occur, preventing auth from succeeding at all.

import re
import sys

if len(sys.argv) != 3:
    print("syntax: %s HEADER IMPL" % (sys.argv[0]), file=sys.stderr)
    sys.exit(1)

hdr = sys.argv[1]
impl = sys.argv[2]

perms = {}

with open(hdr) as fh:
    for line in fh:
        symmatch = re.search(r"^\s+VIR_ACCESS_PERM_([_A-Z]+)(,?|\s|$)", line)
        if symmatch is not None:
            perm = symmatch.group(1)

            if not perm.endswith("_LAST"):
                perms[perm] = 1

warned = False

with open(impl) as fh:
    group = None

    for line in fh:
        symlastmatch = re.search(r"VIR_ACCESS_PERM_([_A-Z]+)_LAST", line)
        if symlastmatch is not None:
            group = symlastmatch.group(1)
        elif re.search(r'''"[_a-z]+"''', line) is not None:
            bits = line.split(",")
            for bit in bits:
                m = re.search(r'''"([_a-z]+)"''', bit)
                if m is not None:
                    perm = (group + "_" + m.group(1)).upper()
                    if perm not in perms:
                        print("Unknown perm string %s for group %s" %
                              (m.group(1), group), file=sys.stderr)
                        warned = True

                    del perms[perm]

for perm in perms.keys():
    print("Perm %s had not string form" % perm, file=sys.stderr)
    warned = True

if warned:
    sys.exit(1)
sys.exit(0)
