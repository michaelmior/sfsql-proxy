#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.65)
AC_INIT([sfsql-proxy],[0.1],[mmior@cs.toronto.edu])
AC_USE_SYSTEM_EXTENSIONS
AC_CONFIG_SRCDIR([proxy.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile map/Makefile tests/Makefile])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE(1.11 no-define)
AC_LANG([C])

LT_INIT([shared, dlopen])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC_C99
AX_CFLAGS_GCC_OPTION(-Wall)
AX_CFLAGS_GCC_OPTION(-Werror)
AX_CFLAGS_GCC_OPTION(-Wextra)
AX_CFLAGS_GCC_OPTION(-pedantic)

# Checks for libraries.
ACX_PTHREAD
AC_CHECK_LIB(mysqlclient_r, mysql_real_connect)
PKG_CHECK_MODULES([CHECK], [check >= 0.9.4])

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([\
    arpa/inet.h \
    netdb.h \
    netinet/in.h \
    sys/socket.h ])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_C_INLINE

# Checks for library functions.
AC_CHECK_FUNCS([bzero memmove memset strdup select socket strerror gethostbyname])
AC_FUNC_MALLOC
AC_FUNC_FORK

# Doxygen support
DX_HTML_FEATURE(ON)
DX_CHM_FEATURE(OFF)
DX_CHI_FEATURE(OFF)
DX_MAN_FEATURE(OFF)
DX_RTF_FEATURE(OFF)
DX_XML_FEATURE(OFF)
DX_PDF_FEATURE(OFF)
DX_PS_FEATURE(OFF)
DX_INIT_DOXYGEN([$PACKAGE_NAME], [doxygen.cfg], [doc])

# Configure MySQL
MYSQL_CFLAGS=""
MYSQL_LIBS=""

AC_MSG_CHECKING(for MySQL support)
AC_ARG_WITH(mysql,
    AS_HELP_STRING([--with-mysql@<:@=PATH@:>@],[Include MySQL support. PATH is the path to 'mysql_config']),
    [WITH_MYSQL=$withval],[WITH_MYSQL=yes])

if test "$WITH_MYSQL" = "no"; then
  AC_MSG_ERROR([mysql support is required, don't use --with-mysql=no or --without-mysql])
fi

AC_MSG_RESULT(yes)  
if test "$WITH_MYSQL" = "yes"; then
  AC_PATH_PROG(MYSQL_CONFIG, mysql_config)
else
  MYSQL_CONFIG=$WITH_MYSQL
fi

if test "$MYSQL_CONFIG" = ""; then
  AC_MSG_ERROR([mysql_config is not found, use $ ./configure --with-mysql=/path/to/mysql_config])
fi

if test -d $MYSQL_CONFIG; then
  MYSQL_CONFIG="$MYSQL_CONFIG/bin/mysql_config"
fi

if test \! -x $MYSQL_CONFIG; then
  AC_MSG_ERROR([mysql_config not exists or not executable, use $ ./configure --with-mysql=/path/to/mysql_config])
fi

if $MYSQL_CONFIG | grep -- '--include' > /dev/null ; then
  MYSQL_CFLAGS="`$MYSQL_CONFIG --include | sed s/\'//g`"
else
  MYSQL_CFLAGS="`$MYSQL_CONFIG --cflags | sed s/\'//g`"
fi

AC_MSG_CHECKING(for MySQL includes at)
AC_MSG_RESULT($MYSQL_CFLAGS)

AC_MSG_CHECKING(for MySQL libs)
if test x"$MYSQL_LIBS" = x; then
  MYSQL_LIBS="`$MYSQL_CONFIG --libs | sed s/\'//g`"
fi
AC_MSG_RESULT([$MYSQL_LIBS])

AC_ARG_WITH([mysql-source],
  [AS_HELP_STRING([--with-mysql-source], [location of MySQL source tree])],
  [MYSQL_CFLAGS="-I$withval/include -I$withval/libmysql_r $MYSQL_CFLAGS"],
  [with_mysql_source=no])

AS_IF([test "x$with_mysql_source" = xno],
  [AC_MSG_FAILURE([must specify path to MySQL source tree via --with-mysql-source])])

AC_SUBST(MYSQL_CFLAGS)
AC_SUBST(MYSQL_LIBS)

AC_ARG_WITH(debug,
  [AS_HELP_STRING([--with-debug], [enable debugging])])
if test "$with_debug" = "yes"; then
  CFLAGS="$CFLAGS -g -O0 -DDEBUG"
fi

AC_DEFINE_UNQUOTED([TESTS_DIR], ["$srcdir/tests/"], [Directory where test files are stored])

AC_OUTPUT
