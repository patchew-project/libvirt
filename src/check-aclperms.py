#!/usr/bin/env python
#
# Copyright (C) 2013-2019 Red Hat, Inc.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library.  If not, see
# <http://www.gnu.org/licenses/>.
#
# This script just validates that the stringified version of
# a virAccessPerm enum matches the enum constant name. We do
# a lot of auto-generation of code, so when these don't match
# problems occur, preventing auth from succeeding at all.

from __future__ import print_function

import re
import sys

if len(sys.argv) != 3:
    print("syntax: %s HEADER IMPL" % (sys.argv[0]), file=sys.stderr)
    sys.exit(1)

hdr = sys.argv[1]
impl = sys.argv[2]

perms = {}

with open(hdr) as fh:
    symprog = re.compile(r"^\s+VIR_ACCESS_PERM_([_A-Z]+)(,?|\s|$).*")
    for line in fh:
        symmatch = symprog.match(line)
        if symmatch is not None:
            perm = symmatch.group(1)

            if not perm.endswith("_LAST"):
                perms[perm] = 1

warned = False

with open(impl) as fh:
    group = None
    symlastprog = re.compile(r".*VIR_ACCESS_PERM_([_A-Z]+)_LAST.*")
    alnumprog = re.compile(r'''.*"([_a-z]+)".*''')

    for line in fh:
        symlastmatch = symlastprog.match(line)
        if symlastmatch is not None:
            group = symlastmatch.group(1)
        elif alnumprog.match(line) is not None:
            bits = line.split(",")
            for bit in bits:
                m = alnumprog.match(bit)
                if m is not None:
                    perm = (group + "_" + m.group(1)).upper()
                    if perm not in perms:
                        print("Unknown perm string %s for group %s" % (m.group(1), group), file=sys.stderr)
                        warned = True

                    del perms[perm]

for perm in perms.keys():
    print("Perm %s had not string form" % perm, file=sys.stderr)
    warned = True

if warned:
    sys.exit(1)
sys.exit(0)
