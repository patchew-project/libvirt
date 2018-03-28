dnl The JSON libraries
dnl
dnl Copyright (C) 2012-2013 Red Hat, Inc.
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

AC_DEFUN([LIBVIRT_ARG_JSON],[
  LIBVIRT_ARG_WITH_FEATURE([JANSSON], [jansson], [check])
  LIBVIRT_ARG_WITH_FEATURE([YAJL], [yajl], [check])
])

AC_DEFUN([LIBVIRT_CHECK_JSON],[
  if test "$with_yajl:$with_jansson" = "yes:yes"; then
    AC_MSG_ERROR("Compiling with both jansson and yajl is unsupported")
  fi

  if test "$with_yajl" = "yes"; then
    with_jansson=no
  elif test "$with_jansson" = "yes"; then
    with_yajl=no
  fi

  need_json=no
  if test "$with_qemu:$with_yajl" = yes:check or
     test "$with_qemu:$with_jansson" = yes:check; then
    dnl Nearly all supported QEMU versions require JSON.
    dnl Assume we need it by default, but still let the user
    dnl shoot it the foot.
    need_json=yes
  fi

  dnl Jansson http://www.digip.org/jansson/
  LIBVIRT_CHECK_PKG([JANSSON], [jansson], [2.7])

  if test "$with_jansson" = "no"; then
    dnl YAJL JSON library http://lloyd.github.com/yajl/
    LIBVIRT_CHECK_LIB_ALT([YAJL], [yajl],
                          [yajl_parse_complete], [yajl/yajl_common.h],
                          [YAJL2], [yajl],
                          [yajl_tree_parse], [yajl/yajl_common.h])
  else
    AM_CONDITIONAL([WITH_YAJL], 0)
    AM_CONDITIONAL([WITH_YAJL2], 0)
  fi

  AM_CONDITIONAL([WITH_JSON],
                 [test "$with_yajl" = "yes" || test "$with_jansson" = "yes"])
  if test "$with_yajl" = "yes" || test "$with_jansson" = "yes"; then
    AC_DEFINE([WITH_JSON], [1], [whether a JSON library is available])
  elif "$need_json" = "yes"; then
    AC_MSG_ERROR([QEMU needs JSON but no library is available])
  fi

  if test "$with_jansson" = "yes"; then
    AC_SUBST([JSON_CFLAGS], [$JANSSON_CFLAGS])
    AC_SUBST([JSON_LIBS], [$JANSSON_LIBS])
  else
    AC_SUBST([JSON_CFLAGS], [$YAJL_CFLAGS])
    AC_SUBST([JSON_LIBS], [$YAJL_LIBS])
  fi

])

AC_DEFUN([LIBVIRT_RESULT_JSON],[
  if test "$with_jansson" = "yes"; then
    msg = "Jansson"
  elif test "$with_yajl" = "yes"; then
    msg = "yajl"
  else
    msg = "none"
  fi
  LIBVIRT_RESULT([JSON library:], [$msg])
])
