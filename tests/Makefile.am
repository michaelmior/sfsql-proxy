## Process this file automake to produce Makefile.in

TESTS = check_options check_pool check_net check_backend check_map
check_PROGRAMS = check_options check_pool check_net check_backend check_map

AM_CFLAGS = $(MYSQL_CFLAGS) @CHECK_CFLAGS@ $(LTDLINCL) -DTESTS_DIR="\"$(top_srcdir)/tests/\"" -DPKG_LIB_DIR="\"$(pkglibdir)\"" -I$(top_srcdir)/src

SRC_DIR = $(abs_top_srcdir)/src

check_pool_SOURCES = check_pool.c
check_pool_LDADD = $(MYSQL_LIBS) @CHECK_LIBS@
check_pool_DEPENDENCIES = $(SRC_DIR)/proxy_pool.c $(SRC_DIR)/proxy_pool.h

check_net_SOURCES = check_net.c net_stubs.c check_net.h $(SRC_DIR)/sql_string.c
check_net_LDADD = $(MYSQL_LIBS) @CHECK_LIBS@
check_net_LDFLAGS = \
	-Wl,--wrap,my_net_init \
	-Wl,--wrap,proxy_backend_query \
	-Wl,--wrap,proxy_pool_return \
    -Wl,--wrap,randominit \
	-Wl,--wrap,proxy_threading_mask
check_net_DEPENDENCIES = $(SRC_DIR)/proxy_net.c $(SRC_DIR)/proxy_net.h

check_backend_SOURCES = check_backend.c $(SRC_DIR)/proxy_pool.c
check_backend_LDADD = $(MYSQL_LIBS) @CHECK_LIBS@ $(LIBLTDL)
check_backend_DEPENDENCIES = $(LTDLDEPS) $(SRC_DIR)/proxy_backend.c $(SRC_DIR)/proxy_backend.h
check_backend_LDFLAGS = \
	-Wl,--wrap,proxy_threading_cancel \
	-Wl,--wrap,proxy_threading_cleanup \
	-Wl,--wrap,proxy_threading_mask

check_map_SOURCES = check_map.c
check_map_LDADD = $(MYSQL_LIBS) @CHECK_LIBS@ $(LIBLTDL)
check_map_DEPENDENCIES = $(top_srcdir)/map/proxy_map.h $(top_srcdir)/map/proxy_map_rowa.c

check_options_SOURCES = check_options.c
check_options_LDADD = $(MYSQL_LIBS) @CHECK_LIBS@
check_options_DEPENDENCIES = $(SRC_DIR)/proxy_options.c $(SRC_DIR)/proxy_options.h

EXTRA_DIST = net backend
