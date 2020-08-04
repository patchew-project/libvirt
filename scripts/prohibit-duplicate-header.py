#!/usr/bin/env python3
#
# Copyright (C) 2016-2019 Red Hat, Inc.
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import re
import sys


def check_file(filename):
    includes = {}
    lineno = 0
    errs = False
    with open(filename, "r") as fh:
        for line in fh:
            lineno = lineno + 1

            # skip non-matching lines early
            if line[0] != '#':
                continue

            headermatch = re.search(r'''^# *include *[<"]([^>"]*\.h)[">]''', line)
            if headermatch is not None:
                inc = headermatch.group(1)

                if inc in includes:
                    print("%s:%d: %s" % (filename, lineno, inc),
                          file=sys.stderr)
                    errs = True
                else:
                    includes[inc] = True

    return errs


ret = 0

for filename in sys.argv[1:]:
    if check_file(filename):
        ret = 1

if ret == 1:
    print("Do not include a header more than once per file", file=sys.stderr)

sys.exit(ret)
