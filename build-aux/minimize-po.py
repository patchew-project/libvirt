#!/usr/bin/env python
#
# Copyright (C) 2018-2019 Red Hat, Inc.
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

from __future__ import print_function

import re
import sys

block = []
msgstr = False
empty = False
unused = False
fuzzy = False

strprog = re.compile(r'''.*".+".*''')

for line in sys.stdin:
    line = line.rstrip("\n")

    if line == "":
        if not empty and not unused and not fuzzy:
            for b in block:
                print(b)

        block = []
        msgstr = False
        fuzzy = False
        block.append(line)
    else:
        if line.startswith("msgstr"):
            msgstr = True
            empty = True

        if line[0] == '#' and line.find("fuzzy") != -1:
            fuzzy = True
        if line.startswith("#~ msgstr"):
            unused = True
        if msgstr and strprog.match(line):
            empty = False

        block.append(line)

if not empty and not unused and not fuzzy:
    for b in block:
        print(b)
