MYSQL_CONFIG = /usr/bin/mysql_config
MYSQL_CFLAGS = $(shell $(MYSQL_CONFIG) --cflags) -I/apps/mysql-5.1.49/include
MYSQL_LIBS = $(shell $(MYSQL_CONFIG) --libs)
CFLAGS = $(MYSQL_CFLAGS) -g -O0 -Werror -Wall
LIBS = $(MYSQL_LIBS)

OBJS = proxy.o sql_string.o proxy_net.o
HEADERS = proxy.h proxy_net.h

all: sfsql-proxy

clean:
	rm -f *.o

$(OBJS): %.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

sfsql-proxy: $(OBJS) $(HEADERS)
	$(CC) $(CFLAGS) $(LIBS) $(OBJS) -o sfsql-proxy
