## Process this file automake to produce Makefile.in

TESTS = check_pool
check_PROGRAMS = check_pool
check_pool_SOURCES = check_pool.c $(top_builddir)/proxy_pool.c $(top_builddir)/proxy_pool.h
check_pool_CFLAGS = $(MYSQL_CFLAGS) @CHECK_CFLAGS@
check_pool_LDADD = $(MYSQL_LIBS) @CHECK_LIBS@
