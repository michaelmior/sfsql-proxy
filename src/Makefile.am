bin_PROGRAMS = sfsql-proxy

sfsql_proxy_SOURCES = \
	proxy.c \
	proxy_options.c \
	proxy_logging.c \
	proxy_net.c \
	proxy_backend.c \
	proxy_pool.c \
	proxy_threading.c \
	proxy_clone.c \
	proxy_monitor.c \
	proxy_cmd.c \
	proxy_trans.c \
	sql_string.c \
	hashtable/hashtable.c
sfsql_proxy_CFLAGS = $(MYSQL_CFLAGS) $(PTHREAD_CFLAGS) $(LTDLINCL) -DPKG_LIB_DIR="\"$(pkglibdir)\"" -I$(top_srcdir)
sfsql_proxy_LDADD = $(MYSQL_LIBS) $(PTHREAD_LIBS) $(LIBLTDL) $(SF_LIBS)
sfsql_proxy_DEPENDENCIES = $(LTDLDEPS)

noinst_HEADERS = \
	proxy.h \
	proxy_options.h \
	proxy_logging.h \
	proxy_net.h \
	proxy_backend.h \
	proxy_pool.h \
	proxy_threading.h \
	proxy_clone.h \
	proxy_monitor.h \
	proxy_cmd.h \
	proxy_trans.h \
	violite.h \
	hashtable/hashtable.h \
	hashtable/hashtable_private.h
