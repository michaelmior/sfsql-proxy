MYSQL_CONFIG = /apps/mysql-5.1.49/scripts/mysql_config
MYSQL_CFLAGS = $(shell $(MYSQL_CONFIG) --cflags) -I/apps/mysql-5.1.49/include -I/apps/mysql-5.1.49/libmysql_r
MYSQL_LIBS = -rdynamic -L/apps/mysql-5.1.49/libmysql_r/.libs -lmysqlclient_r -lz -lpthread -lcrypt -lnsl -lm#$(shell $(MYSQL_CONFIG) --libs_r)
CFLAGS = $(MYSQL_CFLAGS) -g -O0 -Werror -Wall
LIBS = $(MYSQL_LIBS)

OBJS = proxy.o sql_string.o proxy_net.o proxy_backend.o
HEADERS = proxy.h proxy_net.h proxy_backend.h

all: sfsql-proxy

clean:
	rm -f *.o

$(OBJS): %.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

sfsql-proxy: $(OBJS) $(HEADERS)
	$(CC) $(CFLAGS) $(LIBS) $(OBJS) -o sfsql-proxy
