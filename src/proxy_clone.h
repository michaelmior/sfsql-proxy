/*
 * proxy_clone.h
 *
 * Functionality related to SnowFlock cloning.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2010 by Michael Mior <mmior@cs.toronto.edu>
 *
 */

/** Identifier of the server (clone ID). */
extern volatile sig_atomic_t server_id;

/** Signify that we are currently cloning. */
extern volatile sig_atomic_t cloning;

/** Current generation of clones. */
extern volatile sig_atomic_t clone_generation;

int proxy_do_clone(int nclones, char **err, int errlen);
my_bool proxy_clone_wait(int nclones);
void proxy_clone_notify();
