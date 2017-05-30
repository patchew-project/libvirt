dnl
dnl Check for support for Sanitizers
dnl Check for -fsanitize=address and -fsanitize=undefined support
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

AC_DEFUN([LIBVIRT_COMPILE_SANITIZER],[
    LIBVIRT_ARG_ENABLE([ASAN], [Build with address sanitizer support], [no])
    LIBVIRT_ARG_ENABLE([UBSAN], [Build with undefined behavior sanitizer support], [no])

    SAN_CFLAGS=
    SAN_LDFLAGS=

    AS_IF([test "x$enable_asan" = "xyes"], [
        gl_COMPILER_OPTION_IF([-fsanitize=address -fno-omit-frame-pointer], [
            SAN_CFLAGS="-fsanitize=address -fno-omit-frame-pointer"
            SAN_LDFLAGS="-fsanitize=address"
        ])

        AC_SUBST([SAN_CFLAGS])
        AC_SUBST([SAN_LDFLAGS])
    ])

    AS_IF([test "x$enable_ubsan" = "xyes"], [
        gl_COMPILER_OPTION_IF([-fsanitize=undefined -fno-omit-frame-pointer], [
            SAN_CFLAGS="$SAN_CFLAGS -fsanitize=undefined -fno-omit-frame-pointer"
            SAN_LDFLAGS="$SAN_LDFLAGS -fsanitize=undefined"
        ])

        AC_SUBST([SAN_CFLAGS])
        AC_SUBST([SAN_LDFLAGS])
    ])
])

AC_DEFUN([LIBVIRT_RESULT_SANITIZER], [
  AC_MSG_NOTICE([              ASan: $enable_asan])
  AC_MSG_NOTICE([             UBSan: $enable_ubsan])
])
