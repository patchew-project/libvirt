#!/usr/bin/env python3

import os
import sys
import tempfile

destdir = os.environ.get('DESTDIR', os.sep)
dirname = sys.argv[1]
target = sys.argv[2]
link = sys.argv[3]

workdir = os.path.join(destdir, dirname.strip(os.sep))

os.makedirs(workdir, exist_ok=True)
os.chdir(workdir)

templink = tempfile.mktemp(dir=workdir)
os.symlink(target, templink)
os.replace(templink, link)
