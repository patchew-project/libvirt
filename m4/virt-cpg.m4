dnl The libcpg.so library
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

AC_DEFUN([LIBVIRT_ARG_CPG],[
  LIBVIRT_ARG_WITH_FEATURE([CPG], [cluster engine CPG library], [check])
])

AC_DEFUN([LIBVIRT_CHECK_CPG],[
  dnl in some distribution, Version is `UNKNOW` in libcpg.pc
  if test "x$with_cpg" != "xno"; then
    PKG_CHECK_MODULES([CPG], [libcpg], [
      with_cpg=yes
    ],[
      with_cpg=no
    ])
  fi
])

AC_DEFUN([LIBVIRT_RESULT_CPG],[
  LIBVIRT_RESULT_LIB([CPG])
])
