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

#include <stdint.h>
#include <asm/types.h>

struct hashtable *trans_table = NULL;

/**
 * @param True if the keys are equal, false otherwise.
 **/
int trans_eq(void *key1, void *key2) {
    return *((ulong*) key1) == *((ulong*) key2);
}

__attribute__((always_inline)) uint32_t SuperFastHash (const char * data, int len);

/**
 * Hash function which wraps SuperFastHash.
 * @param key Pointer to an unsigned long to use as key.
 * @return Hash value of the long.
 **/
unsigned int trans_hash(void *key) {
    return SuperFastHash((char*) key, sizeof(ulong));
}

void proxy_trans_init() {
    /* Initialize the transaction hashtable */
    if (options.two_pc && (options.coordinator || options.coordinator)
            && options.add_ids)
        trans_table = create_hashtable(16, trans_hash, trans_eq);
    else
        proxy_log(LOG_INFO, "Skipping transaction hashtable initialization");
}

void proxy_trans_end() {
    /* Delete the hash table and all values */
    if (trans_table)
        hashtable_destroy(trans_table, 1);
}

#undef get16bits
#if (defined(__GNUC__) && defined(__i386__)) || defined(__WATCOMC__) \
  || defined(_MSC_VER) || defined (__BORLANDC__) || defined (__TURBOC__)
#define get16bits(d) (*((const uint16_t *) (d)))
#endif

#if !defined (get16bits)
#define get16bits(d) ((((uint32_t)(((const uint8_t *)(d))[1])) << 8)\
                       +(uint32_t)(((const uint8_t *)(d))[0]) )
#endif

inline uint32_t SuperFastHash (const char * data, int len) {
    uint32_t hash = len, tmp;
    int rem;

    if (len <= 0 || data == NULL) return 0;

    rem = len & 3;
    len >>= 2;

    /* Main loop */
    for (;len > 0; len--) {
        hash  += get16bits (data);
        tmp    = (get16bits (data+2) << 11) ^ hash;
        hash   = (hash << 16) ^ tmp;
        data  += 2*sizeof (uint16_t);
        hash  += hash >> 11;
    }

    /* Handle end cases */
    switch (rem) {
        case 3: hash += get16bits (data);
                hash ^= hash << 16;
                hash ^= data[sizeof (uint16_t)] << 18;
                hash += hash >> 11;
                break;
        case 2: hash += get16bits (data);
                hash ^= hash << 11;
                hash += hash >> 17;
                break;
        case 1: hash += *data;
                hash ^= hash << 10;
                hash += hash >> 1;
    }

    /* Force "avalanching" of final 127 bits */
    hash ^= hash << 3;
    hash += hash >> 5;
    hash ^= hash << 4;
    hash += hash >> 17;
    hash ^= hash << 25;
    hash += hash >> 6;

    return hash;
}
