dnl The libssh.so library
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

AC_DEFUN([LIBVIRT_ARG_LIBSSH],[
  LIBVIRT_ARG_WITH_FEATURE([LIBSSH], [libssh], [check], [0.7])
])

AC_DEFUN([LIBVIRT_CHECK_LIBSSH],[
  LIBVIRT_CHECK_PKG([LIBSSH], [libssh], [0.7])

  if test "$with_libssh" = "yes" ; then
    old_CFLAGS="$CFLAGS"
    old_LIBS="$LIBS"
    CFLAGS="$CFLAGS $LIBSSH_CFLAGS"
    LIBS="$LIBS $LIBSSH_LIBS"
    AC_CHECK_FUNC([ssh_get_server_publickey],
      [],
      [AC_DEFINE_UNQUOTED([ssh_get_server_publickey], [ssh_get_publickey],
            [ssh_get_publickey is deprecated and replaced by ssh_get_server_publickey.])])
    AC_CHECK_FUNC([ssh_session_is_known_server],
      [],
      [AC_DEFINE_UNQUOTED([ssh_session_is_known_server], [ssh_is_server_known],
            [ssh_is_server_known is deprecated and replaced by ssh_session_is_known_server.])])
    AC_CHECK_TYPES([enum ssh_known_hosts_e],
        [AC_DEFINE([HAVE_SSH_KNOWN_HOSTS_E], [1],
          [Defined if enum ssh_known_hosts_e exists in libssh.h])],
        [], [[#include <libssh/libssh.h>]])
    CFLAGS="$old_CFLAGS"
    LIBS="$old_LIBS"
  fi
])

AC_DEFUN([LIBVIRT_RESULT_LIBSSH],[
  LIBVIRT_RESULT_LIB([LIBSSH])
])
