dnl The storage Gluster check
dnl
dnl Copyright (C) 2016 Red Hat, Inc.
dnl
dnl This library is free software; you can redistribute it and/or
dnl modify it under the terms of the GNU Lesser General Public
dnl License as published by the Free Software Foundation; either
dnl version 2.1 of the License, or (at your option) any later version.
dnl
dnl This library is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
dnl Lesser General Public License for more details.
dnl
dnl You should have received a copy of the GNU Lesser General Public
dnl License along with this library.  If not, see
dnl <http://www.gnu.org/licenses/>.
dnl

AC_DEFUN([LIBVIRT_STORAGE_ARG_GLUSTER], [
  LIBVIRT_ARG_WITH([STORAGE_GLUSTER], [Gluster backend for the storage driver],
                   [check])
])

AC_DEFUN([LIBVIRT_STORAGE_CHECK_GLUSTER], [
  AC_REQUIRE([LIBVIRT_CHECK_GLUSTER])

  if test "$with_storage_gluster" = "check"; then
    with_storage_gluster=$with_glusterfs
  fi
  if test "$with_storage_gluster" = "yes"; then
    if test "$with_glusterfs" = no; then
      AC_MSG_ERROR([Need glusterfs (libgfapi) for gluster storage driver])
    fi
    AC_DEFINE_UNQUOTED([WITH_STORAGE_GLUSTER], [1],
      [whether Gluster backend for storage driver is enabled])
  fi
  AM_CONDITIONAL([WITH_STORAGE_GLUSTER], [test "$with_storage_gluster" = "yes"])
])

AC_DEFUN([LIBVIRT_STORAGE_RESULT_GLUSTER], [
  LIBVIRT_RESULT([Gluster], [$with_storage_gluster])
])
