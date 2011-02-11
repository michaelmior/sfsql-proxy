/******************************************************************************
 * proxy_trans.c
 *
 * Manage a hash table for transactions so we can do lookups
 * upon receiving messages for two-phase commit.
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

struct hashtable *trans_table = NULL;

void proxy_trans_init() {
    /* Initialize the transaction hash table */
    if (options.two_pc &&
            ((options.coordinator && options.add_ids) || options.cloneable))
        trans_table = create_hashtable(16);
    else
        proxy_log(LOG_INFO, "Skipping transaction hash table initialization");
}

void proxy_trans_end() {
    /* Delete the hash table and all values */
    if (trans_table)
        hashtable_destroy(trans_table, 1);
}
