dnl The OVS timeout check
dnl
dnl Copyright (C) 2017 IBM Corporation
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

AC_DEFUN([LIBVIRT_ARG_OVS_TIMEOUT], [
  LIBVIRT_ARG_WITH([OVS_TIMEOUT],
                   [set the default OVS timeout],
                   120)
])

AC_DEFUN([LIBVIRT_CHECK_OVS_TIMEOUT], [
  AC_DEFINE_UNQUOTED([OVS_TIMEOUT], ["$with_ovs_timeout"],
                     [default OVS timeout])
])

AC_DEFUN([LIBVIRT_RESULT_OVS_TIMEOUT], [
dnl  AC_MSG_NOTICE([OVS timeout: $with_ovs_timeout])
  LIBVIRT_RESULT([       OVS timeout], [$with_ovs_timeout])
])
