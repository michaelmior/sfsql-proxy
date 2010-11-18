/*
 * proxy_cmd.h
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2010 by Michael Mior <mmior@cs.toronto.edu>
 *
 */

#ifndef _proxy_cmd_h
#define _proxy_cmd_h

my_bool proxy_cmd(MYSQL *mysql, char *query, ulong query_len, status_t *status);

#endif /* _proxy_cmd_h */
