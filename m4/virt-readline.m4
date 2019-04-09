dnl The readline library
dnl
dnl Copyright (C) 2005-2013 Red Hat, Inc.
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

AC_DEFUN([LIBVIRT_ARG_READLINE],[
  LIBVIRT_ARG_WITH_FEATURE([READLINE], [readline], [check])
])

AC_DEFUN([LIBVIRT_CHECK_READLINE],[

  # We have to check for readline.pc's presence beforehand because for
  # the longest time the library didn't ship a .pc file at all
  PKG_CHECK_EXISTS([readline], [use_pkgconfig=1], [use_pkgconfig=0])

  if test $use_pkgconfig = 1; then
    # readline 7.0 is the first version which includes pkg-config support
    LIBVIRT_CHECK_PKG([READLINE], [readline], [7.0])
  else
    # The normal library check...
    LIBVIRT_CHECK_LIB([READLINE], [readline], [readline], [readline/readline.h])
  fi

  # We need this to avoid compilation issues with modern compilers.
  # See 9ea3424a178 for a more detailed explanation
  if test "$with_readline" = "yes" ; then
    case "$READLINE_CFLAGS" in
      *-D_FUNCTION_DEF*) ;;
      *) READLINE_CFLAGS="-D_FUNCTION_DEF $READLINE_CFLAGS" ;;
    esac
  fi

  # Gross kludge for readline include path obtained through pkg-config.
  #
  # As of 8.0, upstream readline.pc has -I${includedir}/readline among
  # its Cflags, which is clearly wrong. This does not affect Linux
  # because ${includedir} is already part of the default include path,
  # but on other platforms that's not the case and the result is that
  # <readline/readline.h> can't be located, causing the build to fail.
  # A patch solving this issue has already been posted upstream, so once
  # the fix has landed in FreeBSD ports and macOS homebrew we can safely
  # drop the kludge and rely on pkg-config alone on those platforms.
  #
  # [1] http://lists.gnu.org/archive/html/bug-readline/2019-04/msg00007.html
  case "$READLINE_CFLAGS" in
    *include/readline*) READLINE_CFLAGS=$(echo $READLINE_CFLAGS | sed s,include/readline,include,g) ;;
    *) ;;
  esac
])

AC_DEFUN([LIBVIRT_RESULT_READLINE],[
  LIBVIRT_RESULT_LIB([READLINE])
])
