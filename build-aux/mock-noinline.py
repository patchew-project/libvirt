#!/usr/bin/env python
#
# Copyright (C) 2017-2019 Red Hat, Inc.
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

noninlined = {}
mocked = {}

# Functions in public header don't get the noinline annotation
# so whitelist them here
noninlined["virEventAddTimeout"] = True
# This one confuses the script as its defined in the mock file
# but is actually just a local helper
noninlined["virMockStatRedirect"] = True

def scan_annotations(filename):
    funcprog1 = re.compile(r'''^\s*(\w+)\(.*''')
    funcprog2 = re.compile(r'''^(?:\w+\*?\s+)+(?:\*\s*)?(\w+)\(.*''')
    with open(filename, "r") as fh:
        func = None
        for line in fh:
            line = line.strip()
            m = funcprog1.match(line)
            if m is None:
                m = funcprog2.match(line)
            if m is not None:
                name = m.group(1)
                if name.find("ATTRIBUTE") == -1:
                    func = name
            elif line == "":
                func = None

            if line.find("ATTRIBUTE_NOINLINE") != -1:
                if func is not None:
                    noninlined[func] = True

def scan_overrides(filename):
    funcprog1 = re.compile(r'''^(\w+)\(.*''')
    funcprog2 = re.compile(r'''^\w+\s*(?:\*\s*)?(\w+)\(.*''')
    with open(filename, "r") as fh:
        func = None
        lineno = 0
        for line in fh:
            lineno = lineno + 1
            #line = line.strip()

            m = funcprog1.match(line)
            if m is None:
                m = funcprog2.match(line)
            if m is not None:
                name = m.group(1)
                if name.startswith("vir"):
                    mocked[name] = "%s:%d" % (filename, lineno)


for filename in sys.argv[1:]:
    if filename.endswith(".h"):
        scan_annotations(filename)
    elif filename.endswith("mock.c"):
        scan_overrides(filename)

warned = False
for func in mocked.keys():
    if func not in noninlined:
        warned = True
        print("%s is mocked at %s but missing noinline annotation" % (func, mocked[func]), file=sys.stderr)

if warned:
    sys.exit(1)
sys.exit(0)
