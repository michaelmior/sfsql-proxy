#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([sfsql-proxy],[0.1],[mmior@cs.toronto.edu])
AC_USE_SYSTEM_EXTENSIONS
AC_CONFIG_SRCDIR([src/proxy.c])
AC_CONFIG_HEADERS([config.h])
LT_CONFIG_LTDL_DIR([libltdl])
AC_CONFIG_AUX_DIR([libltdl/config])
AC_CONFIG_MACRO_DIR([m4 libltdl/m4])

AM_INIT_AUTOMAKE(1.10 no-define)
AC_LANG([C])

LT_INIT([dlopen shared])
LTDL_INIT

# Disable default CFLAGS
CFLAGS=""

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_CC_C99
AC_PROG_GCC_TRADITIONAL
AX_CFLAGS_GCC_OPTION(-Wall)
AX_CFLAGS_GCC_OPTION(-Werror)
AX_CFLAGS_GCC_OPTION(-Wextra)
AX_CFLAGS_GCC_OPTION(-Wstrict-aliasing=2)
AC_PROG_RANLIB

# Checks for libraries.
ACX_PTHREAD
AC_CHECK_LIB(mysqlclient_r, mysql_real_connect)

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_TIME
AC_CHECK_HEADERS([\
    arpa/inet.h \
    netdb.h \
    netinet/in.h \
    sys/socket.h \
    sys/ioctl.h ])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_INTPTR_T
AC_TYPE_SIGNAL
AC_TYPE_PID_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T
AC_C_INLINE
AC_C_RESTRICT

# Checks for library functions.
AC_CHECK_FUNCS([ \
    bzero memmove memset \
    strdup strndup strchr strrchr strncasecmp strcasecmp strtol strerror \
    select socket gethostname gethostbyname inet_ntoa ])
AC_FUNC_FORK
AC_FUNC_ALLOCA
AC_FUNC_SELECT_ARGTYPES
AC_FUNC_VPRINTF

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

# Configure check
AC_ARG_WITH(check,
    AS_HELP_STRING([--without-check], [Disable support for unit tests using check]),
    [], [with_check=yes])

AM_CONDITIONAL([COND_CHECK], [test "$with_check" = yes])
if test "$with_check" = "yes"; then
    PKG_CHECK_MODULES([CHECK], [check >= 0.9.4])
fi

# Configure SnowFlock
SF_LIBS=""

AC_ARG_WITH(snowflock,
    AS_HELP_STRING([--without-snowflock], [Disable support for the SnowFlock C API]),
    [], [with_snowflock=yes])

if test "$with_snowflock" = "yes"; then
    AC_CHECK_LIB([sf], [clone_master], [],
        [AC_MSG_ERROR([SnowFlock support requested, but library not found])])
    SF_LIBS="-lsf"
fi

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
  MYSQL_LIBS="`$MYSQL_CONFIG --libs_r | sed s/\'//g`"
fi
AC_MSG_RESULT([$MYSQL_LIBS])

AC_SUBST(MYSQL_CFLAGS)
AC_SUBST(MYSQL_LIBS)

AC_ARG_ENABLE(debug,
  [AS_HELP_STRING([--enable-debug], [enable debugging])])
if test "$enable_debug" = "yes"; then
  CFLAGS="$CFLAGS -g -O0 -DDEBUG"
else
  CFLAGS="$CFLAGS -O3"
fi

AC_ARG_ENABLE(threading-debug,
  [AS_HELP_STRING([--enable-threading-debug], [enable pthreads debugging])])
if test "$enable_threading_debug" = "yes"; then
  CFLAGS="$CFLAGS -DTHREADING_DEBUG"
fi

AC_CONFIG_FILES([Makefile libltdl/Makefile src/Makefile map/Makefile tests/Makefile])
AC_OUTPUT
