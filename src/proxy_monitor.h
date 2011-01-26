/*
 * proxy_monitor.h
 *
 * Monitor the load on the proxy server and perform cloning as necessary.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2010 by Michael Mior <mmior@cs.toronto.edu>
 *
 */

#ifndef _proxy_monitor_h
#define _proxy_monitor_h

my_bool proxy_monitor_init();
void proxy_monitor_end();

#endif /* _proxy_monitor_h */
