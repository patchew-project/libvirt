#!/usr/bin/env python3

from __future__ import print_function

import os
import os.path
import time
import shutil
import subprocess
import sys

if len(sys.argv) != 3:
   print("syntax: %s SOURCE-ROOT BUILD-ROOT", sys.argv[0])
   sys.exit(1)

srcroot = sys.argv[1]
buildroot = sys.argv[2]
distdir = os.environ["MESON_DIST_ROOT"]

# We don't want to bundle the entire of gnulib.
# Bundling only the stuff we need is not as simple
# as just grabbing the gnulib/ subdir though. The
# bootstrap script is intertwined into autoreconf
# with specific ordering needs. Thus we must
# bundle the result from autoreconf + bootstrap
gnulibmod = os.path.join(distdir, ".gnulib")
shutil.rmtree(gnulibmod)

autotools = {
   "/": [
      "GNUmakefile",
      "INSTALL",
      "Makefile.in",
      "aclocal.m4",
      "config.h.in",
      "configure",
      "maint.mk",
   ],
   "/build-aux": [
      ".gitignore",
      "compile",
      "config.guess",
      "config.rpath",
      "config.sub",
      "depcomp",
      "gitlog-to-changelog",
      "install-sh",
      "ltmain.sh",
      "missing",
      "mktempd",
      "test-driver",
      "useless-if-before-free",
      "vc-list-files",
   ],
   "/docs": [
      "Makefile.in",
   ],
   "/examples": [
      "Makefile.in",
   ],
   "/gnulib": None,
   "/include/libvirt": [
      "Makefile.in",
   ],
   "/m4": None,
   "/po": [
      "Makefile.in",
   ],
   "/src": [
      "Makefile.in",
   ],
   "/tests": [
      "Makefile.in",
   ],
   "/tools": [
      "Makefile.in",
   ],
}

for dirname in autotools.keys():
   srcdir = os.path.join(srcroot, *dirname[1:].split("/"))
   dstdir = os.path.join(distdir, *dirname[1:].split("/"))

   if autotools[dirname] is None:
      shutil.rmtree(dstdir)
      shutil.copytree(srcdir, dstdir)
   else:
      os.makedirs(dstdir, exist_ok=True)
      for filename in autotools[dirname]:
         srcfile = os.path.join(srcdir, filename)
         dstfile = os.path.join(dstdir, filename)

         shutil.copyfile(srcfile, dstfile)
         shutil.copymode(srcfile, dstfile)

# Files created by meson using 'git clone' get a
# timestamp from time we run 'ninja dist'. The
# autotools files we're copying, get a time from
# time we copy them. When 'make' runs later it
# will think 'configure' is out of date and try
# to recreate it. We hack around this by setting
# all files to the same timestamp
now = time.time()
for dirname, subdirs, files in os.walk(distdir):
   for filename in files:
      dstfile = os.path.join(dirname, filename)
      os.utime(dstfile, (now, now))


# Some auto-generated files we want to include
extra_dist = [
   "libvirt.spec",
   "NEWS"
]
for filename in extra_dist:
   filesrc = os.path.join(buildroot, filename)
   filedst = os.path.join(distdir, filename)

   shutil.copyfile(filesrc, filedst)

authors = subprocess.check_output(["git", "log", "--pretty=format:%aN <%aE>"])
authorlist = sorted(set(authors.decode("utf8").split("\n")))

authorssrc = os.path.join(srcroot, "AUTHORS.in")
authorsdst = os.path.join(distdir, "AUTHORS")
with open(authorssrc, "r") as src, open(authorsdst, "w") as dst:
   for line in src:
      if line.find("#contributorslist#") != -1:
         for name in authorlist:
            print(name, file=dst)
      else:
         print(line, end='', file=dst)

