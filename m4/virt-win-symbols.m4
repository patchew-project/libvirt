dnl The MinGW symbols checks
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

AC_DEFUN([LIBVIRT_WIN_CHECK_SYMBOLS], [
  LIBVIRT_ADMIN_SYMBOL_FILE=admin/libvirt_admin.syms
  case "$host" in
    *-*-mingw* )
      # Also set the symbol file to .def, so src/Makefile generates libvirt.def
      # from libvirt.syms and passes libvirt.def instead of libvirt.syms to the
      # linker
      LIBVIRT_ADMIN_SYMBOL_FILE=admin/libvirt_admin.def
      ;;
  esac
  AC_SUBST([LIBVIRT_ADMIN_SYMBOL_FILE])
])
