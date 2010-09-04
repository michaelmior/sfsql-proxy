## Process this file automake to produce Makefile.in

TESTS = check_options check_pool check_net check_backend check_map
check_PROGRAMS = check_options check_pool check_net check_backend check_map

check_pool_SOURCES = check_pool.c
check_pool_CFLAGS = $(MYSQL_CFLAGS) @CHECK_CFLAGS@
check_pool_LDADD = $(MYSQL_LIBS) @CHECK_LIBS@
check_pool_DEPENDENCIES = $(top_srcdir)/proxy_pool.c $(top_srcdir)/proxy_pool.h

check_net_SOURCES = check_net.c net_stubs.c check_net.h $(top_srcdir)/sql_string.c
check_net_CFLAGS = $(MYSQL_CFLAGS) @CHECK_CFLAGS@
check_net_LDADD = $(MYSQL_LIBS) @CHECK_LIBS@
check_net_LDFLAGS = \
	-Wl,--wrap,my_net_init \
	-Wl,--wrap,proxy_backend_query \
	-Wl,--wrap,proxy_pool_return
check_net_DEPENDENCIES = $(top_srcdir)/proxy_net.c $(top_srcdir)/proxy_net.h

check_backend_SOURCES = check_backend.c $(top_srcdir)/proxy_pool.c
check_backend_CFLAGS = $(MYSQL_CFLAGS) @CHECK_CFLAGS@
check_backend_LDADD = $(MYSQL_LIBS) @CHECK_LIBS@
check_backend_DEPENDENCIES = $(top_srcdir)/proxy_backend.c $(top_srcdir)/proxy_backend.h 

check_map_SOURCES = check_map.c
check_map_CFLAGS = $(MYSQL_CFLAGS) @CHECK_CFLAGS@
check_map_LDADD = $(MYSQL_LIBS) @CHECK_LIBS@ -lltdl
check_map_DEPENDENCIES = $(top_srcdir)/map/proxy_map.h

check_options_SOURCES = check_options.c
check_options_CFLAGS = $(MYSQL_CFLAGS) @CHECK_CFLAGS@
check_options_LDADD = $(MYSQL_LIBS) @CHECK_LIBS@
check_options_DEPENDENCIES = $(top_srcdir)/proxy_options.c $(top_srcdir)/proxy_options.h
