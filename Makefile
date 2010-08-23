# 
# Makefile
#
# This file is subject to the terms and conditions of the GNU General
# Public License.  See the file "COPYING" in the main directory of
# this archive for more details.
#
# Copyright (C) 2010 by Michael Mior <mmior@cs.toronto.edu>
#

MYSQL_SRC_PATH = /apps/mysql-5.1.49
MYSQL_CONFIG = $(MYSQL_SRC_PATH)/scripts/mysql_config
MYSQL_CFLAGS = $(shell $(MYSQL_CONFIG) --cflags) -I$(MYSQL_SRC_PATH)/include -I$(MYSQL_SRC_PATH)/libmysql_r
MYSQL_LIBS = $(shell $(MYSQL_CONFIG) --libs_r)
CFLAGS = $(MYSQL_CFLAGS) -g -O0 -Werror -Wall -Wextra -D_GNU_SOURCE -DDEBUG
LIBS = $(MYSQL_LIBS)

OBJS = proxy.o sql_string.o proxy_net.o proxy_backend.o proxy_pool.o proxy_threading.o
HEADERS = proxy.h proxy_net.h proxy_backend.h proxy_threading.h

all: sfsql-proxy

clean:
	rm -f *.o
	rm -f sfsql-proxy

$(OBJS): %.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

sfsql-proxy: $(OBJS) $(HEADERS)
	$(CC) $(CFLAGS) $(LIBS) $(OBJS) -o sfsql-proxy
