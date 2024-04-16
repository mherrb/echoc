AC_PREREQ([2.71])
AC_INIT([echoc],[0.6.99],[https://gitlab.laas.fr/matthieu/echoc],[echoc])
AM_INIT_AUTOMAKE([foreign dist-bzip2])
AC_CONFIG_MACRO_DIR([m4])
AC_PROG_CC

AC_CHECK_HEADERS([bsd/stdlib.h])
AC_SEARCH_LIBS(strtonum, [bsd])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
