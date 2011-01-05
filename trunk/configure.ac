dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(x360, 1.0)


AC_CANONICAL_SYSTEM
AM_INIT_AUTOMAKE()

AC_CHECK_LIB(fuse, fuse_main_real)

AC_GNU_SOURCE

AC_PROG_LIBTOOL
AC_CONFIG_MACRO_DIR([m4])
AC_PROG_CC
AC_PROG_CC_C_O

AC_CONFIG_FILES(Makefile src/Makefile src/libfatx/Makefile src/xfd/Makefile)
AC_OUTPUT

