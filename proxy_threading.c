/******************************************************************************
 * proxy_threading.c
 *
 * Initialize any necessary data structures for threading
 *
 * Copyright (c) 2010, Michael Mior <mmior@cs.toronto.edu>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */

#include "proxy.h"

void proxy_threading_init() {
#ifdef DEBUG
    pthread_mutexattr_init(&__proxy_mutexattr);
    pthread_mutexattr_settype(&__proxy_mutexattr, PTHREAD_MUTEX_ERRORCHECK);
#endif
}

void proxy_threading_end() {
#ifdef DEBUG
    pthread_mutexattr_destroy(&__proxy_mutexattr);
#endif
}
