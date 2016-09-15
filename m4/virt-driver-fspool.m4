dnl The File Systems Driver
dnl
dnl Copyright (C) 2016 Parallels IP Holdings GmbH
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

AC_DEFUN([LIBVIRT_DRIVER_CHECK_FSPOOL],[
    AC_ARG_WITH([fs-dir],
      [AS_HELP_STRING([--with-fs-dir],
        [with direcktory backend for FS driver  @<:@default=yes@:>@])],
      [],[with_fs_dir=yes])
    m4_divert_text([DEFAULTS], [with_fs=check])

    if test "$with_libvirtd" = "no"; then
      with_fs_dir=no
    fi

    if test "$with_fs_dir" = "yes" ; then
      AC_DEFINE_UNQUOTED([WITH_FS_DIR], 1, [whether directory backend for fs driver is enabled])
    fi
    AM_CONDITIONAL([WITH_FS_DIR], [test "$with_fs_dir" = "yes"])

    with_fs=no
    for backend in dir; do
        if eval test \$with_fs_$backend = yes; then
            with_fs=yes
            break
        fi
    done
    if test $with_fs = yes; then
        AC_DEFINE([WITH_FS], [1],
            [Define to 1 if at least one fs backend is in use])
    fi
    AM_CONDITIONAL([WITH_FS], [test "$with_fs" = "yes"])
])

AC_DEFUN([LIBVIRT_DRIVER_RESULT_FS],[
    AC_MSG_NOTICE([       FS Driver: $with_fs_dir])
])
