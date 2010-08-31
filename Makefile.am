SUBDIRS = . tests

EXTRA_DIST = \
	doxygen.cfg

ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = sfsql-proxy

sfsql_proxy_SOURCES = proxy.c proxy_net.c proxy_backend.c proxy_pool.c proxy_threading.c sql_string.c
sfsql_proxy_CFLAGS = $(MYSQL_CFLAGS) $(PTHREAD_CFLAGS) 
sfsql_proxy_LIBS = $(MYSQL_LIBS) $(PTHREAD_LIBS)

include_HEADERS = \
	proxy.h \
	proxy_net.h \
	proxy_backend.h \
	proxy_pool.h \
	proxy_threading.h

include $(top_srcdir)/doxygen.am
MOSTLYCLEANFILES = $(DX_CLEANFILES)
doc: doxygen-doc
