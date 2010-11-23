/******************************************************************************
 * proxy_clone.h
 *
 * Functionality related to SnowFlock cloning.
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

#ifdef HAVE_LIBSF
#include <sf.h>
#endif
#include <time.h>

/** Maximum amount of time to wait for new clones */
#define CLONE_TIMEOUT 30

volatile sig_atomic_t server_id = 0;
volatile sig_atomic_t cloning  = 0;
volatile sig_atomic_t new_clones = 0;
volatile sig_atomic_t clone_generation = 0;

/** Number of clones requested by
 *  the current cloning operation. */
static volatile sig_atomic_t req_clones = 0;

/** Condition variable for notifying of new clones */
static pthread_cond_t new_cv;
/** Mutex associated with :new_cv */
static pthread_mutex_t new_mutex;

/**
 * Wait for new clones to become live on the coordinator.
 *
 * @param nclones Number of clones to wait for.
 *
 * @return TRUE on error, FALSE otherwise.
 **/
my_bool proxy_clone_wait(int nclones) {
    struct timespec wait_time = { .tv_sec=CLONE_TIMEOUT, .tv_nsec=0 };
    int wait_errno = 0;
    my_bool error = FALSE;

    req_clones = nclones;
    new_clones = 0;

    /* Initialize locking */
    proxy_cond_init(&new_cv);
    proxy_mutex_init(&new_mutex);

    /* Wait for the clones */
    proxy_log(LOG_INFO, "Waiting %ds for new clones", CLONE_TIMEOUT);
    proxy_mutex_lock(&new_mutex);
    while (new_clones != req_clones && !wait_errno)
        wait_errno = pthread_cond_timedwait(&new_cv, &new_mutex, &wait_time);

    /* Check if we timed out waiting */
    if (wait_errno == ETIMEDOUT) {
        proxy_log(LOG_ERROR, "Timed out waiting for new clones");
        error = TRUE;
    }

    req_clones = 0;
    proxy_mutex_unlock(&new_mutex);
    proxy_mutex_destroy(&new_mutex);
    proxy_cond_destroy(&new_cv);

    return FALSE;
}

/**
 * Used by the backend to notify the coordinator
 * that some new clone has arrived.
 **/
void proxy_clone_notify() {
    /* Check if we're not expecting new clones */
    if (!req_clones) {
        proxy_log(LOG_ERROR, "Attempted to notify of new clone with no outstanding requests");
        return;
    }

    /* Increment the number of clones and check if we're done */
    new_clones++;
    if (new_clones == req_clones)
        pthread_cond_signal(&new_cv);

    if (new_clones > req_clones)
        proxy_log(LOG_ERROR, "More clones arrived than expected");
}

/**
 * Execute a cloning operation.
 *
 * @param nclones  Number of clones to create.
 * @param[out] err Buffer to hold error message
 *                 produced.
 * @param errlen   Length of error message buffer.
 *
 * @return ID of the created clone (0 on the master)
 *         or negative on error.
 **/
#ifdef HAVE_LIBSF
int proxy_do_clone(int nclones, char **err, int errlen) {
    sf_result *result;
    char ticket[SF_TICKET_SIZE+1];
    int vmid = -1;

    if (req_clones) {
        proxy_log(LOG_ERROR, "Previous cloning operation not yet complete");
        return -1;
    }

    cloning = 1;
    req_clones = nclones;

    /* Get a clone ticket and check its validity */
    proxy_log(LOG_INFO, "Requesting ticket for %d clones", nclones);
    result = REQUEST_VM_TICKET(nclones);
    if (result) {
        if (result->rc.allowed_clones != nclones) {
            DISPOSE_TICKET(result->ticket);

            snprintf(*err, errlen, "Only %d clones allowed, ticket disposed", result->rc.allowed_clones);
            goto out;
        }
    } else {
        snprintf(*err, errlen, "Unable to get clone ticket");
        goto out;
    }

    /* Print ticket information */
    strncpy(ticket, result->ticket, SF_TICKET_SIZE);
    ticket[SF_TICKET_SIZE] = '\0';
    proxy_log(LOG_INFO, "Received ticket %s for %d clones", result->ticket, result->rc.allowed_clones);
    FREE_SF_RES(result);
    
    /* Clone from the ticket and check that the clone succeeded */
    result = CLONE_MASTER(ticket);
    vmid = result->vmid;

    if (!result) {
        snprintf(*err, errlen, "Error cloning from ticket");

        DISPOSE_TICKET(ticket);
        vmid = -1;
    } else {
        if (result->rc.number_clones == 0) {
            snprintf(*err, errlen, "Cloning produced zero clones");
            vmid = -1;
        } else {
            if (vmid == 0) {
                (void) __sync_fetch_and_add(&clone_generation, 1);
                proxy_log(LOG_INFO, "%d clones successfully created", result->rc.number_clones);
            } else {
                server_id = vmid;
                proxy_log(LOG_INFO, "I am clone %d", vmid);
            }
        }
    }

out:
    cloning = 0;

    if (result)
        FREE_SF_RES(result);
    return vmid;
}
#else
int proxy_do_clone(
        __attribute__((unused)) int nclones,
        __attribute__((unused)) char **err,
        __attribute__((unused)) int errlen) {
    return -1;
}
#endif /* HAVE_LIBSF */
