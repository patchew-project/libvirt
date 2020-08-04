#!/usr/bin/env python3
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
#
# Regroup array values into smaller groups separated by numbered comments.
#
# If --check is the first parameter, the script will return
# a non-zero value if a file is not grouped correctly.
# Otherwise the files are regrouped in place.

import argparse
import re
import subprocess
import sys


def regroup_caps(check, filename, start_regex, end_regex,
                 trailing_newline, counter_prefix):
    step = 5

    original = []
    with open(filename, "r") as fh:
        for line in fh:
            original.append(line)

    fixed = []
    game_on = False
    counter = 0
    for line in original:
        line = line.rstrip("\n")
        if game_on:
            if re.search(r'''.*/\* [0-9]+ \*/.*''', line):
                continue
            if re.search(r'''^\s*$''', line):
                continue
            if counter % step == 0:
                if counter != 0:
                    fixed.append("\n")
                fixed.append("%s/* %d */\n" % (counter_prefix, counter))

            if not (line.find("/*") != -1 and line.find("*/") == -1):
                # count two-line comments as one line
                counter = counter + 1

        if re.search(start_regex, line):
            game_on = True
        elif game_on and re.search(end_regex, line):
            if (counter - 1) % step == 0:
                fixed = fixed[:-1]  # /* $counter */
                if counter != 1:
                    fixed = fixed[:-1]  # \n

            if trailing_newline:
                fixed.append("\n")

            game_on = False

        fixed.append(line + "\n")

    if check:
        orig = "".join(original)
        new = "".join(fixed)
        if new != orig:
            diff = subprocess.Popen(["diff", "-u", filename, "-"],
                                    stdin=subprocess.PIPE)
            diff.communicate(input=new.encode('utf-8'))

            print("Incorrect line wrapping in $file",
                  file=sys.stderr)
            print("Use group-qemu-caps.py to generate data files",
                  file=sys.stderr)
            return False
    else:
        with open(filename, "w") as fh:
            for line in fixed:
                print(line, file=fh, end='')

    return True


parser = argparse.ArgumentParser(description='QEMU capabilities group formatter')
parser.add_argument('--check', action="store_true",
                    help='check existing files only')
parser.add_argument('--prefix', default='',
                    help='source code tree prefix')
args = parser.parse_args()

errs = False

if not regroup_caps(args.check,
                    args.prefix + 'src/qemu/qemu_capabilities.c',
                    r'virQEMUCaps grouping marker',
                    r'\);',
                    0,
                    "              "):
    errs = True

if not regroup_caps(args.check,
                    args.prefix + 'src/qemu/qemu_capabilities.h',
                    r'virQEMUCapsFlags grouping marker',
                    r'QEMU_CAPS_LAST \/\* this must',
                    1,
                    "    "):
    errs = True

if errs:
    sys.exit(1)
sys.exit(0)
