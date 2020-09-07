#!/usr/bin/env python3

import os
import sys

destdir = os.environ.get('DESTDIR', os.sep)

for dirname in sys.argv[1:]:
    realdir = os.path.realpath(os.path.join(destdir, dirname.strip(os.sep)))
    if not realdir.startswith(destdir):
        realdir = os.path.join(destdir, realdir.strip(os.sep))
    os.makedirs(realdir, exist_ok=True)
