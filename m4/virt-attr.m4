dnl The libattr.so library

AC_DEFUN([LIBVIRT_ARG_ATTR],[
  LIBVIRT_ARG_WITH([ATTR], [attr], [check])
])

AC_DEFUN([LIBVIRT_CHECK_ATTR],[
  LIBVIRT_CHECK_LIB([ATTR], [attr], [getxattr], [attr/xattr.h])
])

AC_DEFUN([LIBVIRT_RESULT_ATTR],[
  LIBVIRT_RESULT_LIB([ATTR])
])
