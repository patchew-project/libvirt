dnl The libdlm.so library
dnl
dnl Copyright (C) 2018 SUSE LINUX Products, Beijing, China.
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

AC_DEFUN([LIBVIRT_ARG_DLM],[
  LIBVIRT_ARG_WITH_FEATURE([DLM], [Distributed Lock Manager library], [check])
])

AC_DEFUN([LIBVIRT_CHECK_DLM],[
  AC_REQUIRE([LIBVIRT_CHECK_CPG])
  LIBVIRT_CHECK_PKG([DLM], [libdlm], [4.0.0])

  if test "x$with_dlm" == "xyes" && test "x$with_cpg" != "xyes"; then
    AC_MSG_ERROR([You must install libcpg to build dlm lock])
  fi
])

AC_DEFUN([LIBVIRT_RESULT_DLM],[
  AC_REQUIRE([LIBVIRT_RESULT_CPG])
  LIBVIRT_RESULT_LIB([DLM])
])
