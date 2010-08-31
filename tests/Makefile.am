## Process this file automake to produce Makefile.in

TESTS = check_pool check_net
check_PROGRAMS = check_pool check_net

check_pool_SOURCES = check_pool.c $(top_builddir)/proxy_pool.c $(top_builddir)/proxy_pool.h
check_pool_CFLAGS = $(MYSQL_CFLAGS) @CHECK_CFLAGS@
check_pool_LDADD = $(MYSQL_LIBS) @CHECK_LIBS@

check_net_SOURCES = check_net.c net_stubs.c check_net.h $(top_builddir)/proxy_net.c $(top_builddir)/sql_string.c
check_net_CFLAGS = $(MYSQL_CFLAGS) @CHECK_CFLAGS@
check_net_LDADD = $(MYSQL_LIBS) @CHECK_LIBS@
check_net_LDFLAGS = \
	-Wl,--wrap,my_net_init \
	-Wl,--wrap,proxy_backend_query \
	-Wl,--wrap,proxy_pool_return
