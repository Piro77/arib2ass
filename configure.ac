#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([arib2ass], [0.1.0], [piro77@gmail.com])
AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_SRCDIR([arib2ass.c])
AC_CONFIG_HEADERS([config.h])


# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
#AC_PROG_OBJC
#AM_PROG_AS
#AM_PROG_GCJ
#AM_PROG_UPC

case "${host_os}" in
    freebsd*)
     CFLAGS_save="${CFLAGS} -I/usr/local/include"
     CFLAGS="${CFLAGS_save}"
     LDFLAGS_save="${LDFLAGS} -L/usr/local/lib"
     LDFLAGS="${LDFLAGS_save}"
     ;;
esac


AM_ICONV
# checks for libraries.
# FIXME: Replace `main' with a function in `-ldvbpsi':
AC_CHECK_LIB([dvbpsi], [main],,AC_MSG_ERROR(libdvbpsi required but not found))
# FIXME: Replace `main' with a function in `-lm':
AC_CHECK_LIB([m], [main])
# FIXME: Replace `main' with a function in `-lpng':
AC_CHECK_LIB([png], [main],,AC_MSG_ERROR(libpng required but not found))


# Checks for header files.
AC_CHECK_HEADERS([fcntl.h inttypes.h limits.h stdint.h stdlib.h string.h sys/time.h unistd.h])


# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_INT8_T
AC_C_RESTRICT
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC

AC_CHECK_FUNCS([gettimeofday memset pow sqrt strchr strdup strstr strtoul])



AC_CONFIG_FILES([Makefile])
AC_OUTPUT
