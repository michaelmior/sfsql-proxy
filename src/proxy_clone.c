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
#define CLONE_TIMEOUT 60

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

/** Hashtable for storing ID-to-IP mappings for clones */
struct hashtable *clone_table = NULL;
/** Hashtable for storing number of clones per generation */
static struct hashtable *clone_num_table = NULL;
DEFINE_HASHTABLE_INSERT(clone_num_insert, int);
DEFINE_HASHTABLE_SEARCH(clone_num_search, int);
DEFINE_HASHTABLE_REMOVE(clone_num_remove, int);
void clone_set_num(int clone_generation, int num);

/**
 * Initialize data structures required for cloning.
 **/
void proxy_clone_init() {
    /* Create the clone hashtables */
    if (options.coordinator)
        clone_table = create_hashtable(16);
    if (options.cloneable || options.coordinator)
        clone_num_table = create_hashtable(16);
}

/**
 * Destroy data structures required for cloning.
 **/
void proxy_clone_end() {
    /* Destroy the hashtables */
    if (clone_table)
        hashtable_destroy(clone_table, 1);
    if (clone_num_table)
        hashtable_destroy(clone_num_table, 1);
}

/**
 * Retrieve the number of clones which were created in a particular generation.
 *
 * @param clone_generation Generation number to retrieve clone count for.
 *
 * @return Number of clones in the given generation.
 **/
int proxy_clone_get_num(int clone_generation) {
    int *num = clone_num_search(clone_num_table, clone_generation);

    if (!num)
        return -1;
    else
        return *num;
}

/**
 * Save the number of created clones in a generation so
 * they can be retrieved later when committing.
 *
 * @param clone_generation Generation of clones.
 * @param num              Number of clones created.
 **/
void clone_set_num(int clone_generation, int num) {
    int *nump = (int*) malloc(sizeof(num));
    *nump = num;
    clone_num_insert(clone_num_table, clone_generation, nump);

    proxy_debug("Set %d clones for generation %d", num, clone_generation);
}

/**
 * Wait for new clones to become live on the coordinator.
 *
 * @return TRUE on error, FALSE otherwise.
 **/
my_bool proxy_clone_wait() {
    struct timespec wait_time;
    struct timeval start, end;
    double time;
    int wait_errno = 0;
    my_bool error = FALSE;

    /* Store the number of clones for later use */
    clone_set_num(clone_generation, new_clones);

    /* Initialize locking */
    proxy_cond_init(&new_cv);
    proxy_mutex_init(&new_mutex);

    /* Prepare time for waiting */
    gettimeofday(&start, NULL);
    wait_time.tv_sec  = start.tv_sec + CLONE_TIMEOUT;
    wait_time.tv_nsec = start.tv_usec * 1000;

    /* Wait for the clones */
    proxy_log(LOG_INFO, "Waiting %ds for %d new clones", CLONE_TIMEOUT, req_clones);
    proxy_mutex_lock(&new_mutex);
    while (new_clones < req_clones && !wait_errno)
        wait_errno = pthread_cond_timedwait(&new_cv, &new_mutex, &wait_time);

    gettimeofday(&end, NULL);
    time = end.tv_sec - start.tv_sec + (end.tv_usec - start.tv_usec) / 1000000.0f;
    proxy_debug("After waiting %.3fs for clones, got %d/%d", time, new_clones, req_clones);

    /* Check if we timed out waiting */
    if (wait_errno == ETIMEDOUT) {
        /* If no clones came up, then pretend cloning didn't happen */
        if (new_clones == 0)
            (void) __sync_fetch_and_sub(&clone_generation, 1);

        proxy_log(LOG_ERROR, "Timed out waiting for new clones");
        error = TRUE;
    }

    if (new_clones > req_clones)
        proxy_log(LOG_ERROR, "More clones arrived than expected");

    req_clones = 0;
    proxy_mutex_unlock(&new_mutex);
    proxy_mutex_destroy(&new_mutex);
    proxy_cond_destroy(&new_cv);

    return error;
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

    proxy_mutex_lock(&new_mutex);

    /* Increment the number of clones and check if we're done */
    (void) __sync_fetch_and_add(&new_clones, 1);
    if (new_clones >= req_clones)
        proxy_cond_signal(&new_cv);

    proxy_debug("Received notification for %d of %d clones", new_clones, req_clones);
    proxy_mutex_unlock(&new_mutex);
}

/**
 * Prepare to execute a cloning operation.
 *
 * @param nclones Number of clones which will be created.
 *
 * @return TRUE if ready to clone, FALSE otherwise.
 **/
my_bool proxy_clone_prepare(int nclones) {
    if (req_clones) {
        proxy_log(LOG_ERROR, "Previous cloning operation not yet complete");
        return FALSE;
    }

    req_clones = nclones;
    new_clones = 0;
    return TRUE;
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
    char ticket[SF_TICKET_SIZE+1], oldip[INET6_ADDRSTRLEN+1];
    int vmid = -1, i;
    struct timeval start, end;
    double time;

    if (!proxy_clone_prepare(nclones))
        return -1;

    /* Wait until any outstanding queries have committed */
    cloning = 1;
    __sync_synchronize();
    while (committing) { usleep(SYNC_SLEEP); }

    /* If we need to wait for queries to complete, do so */
    if (options.query_wait)
        while (querying) { usleep(SYNC_SLEEP); }

    gettimeofday(&start, NULL);

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

    /* Save the old IP address */
    strncpy(oldip, options.phost, INET6_ADDRSTRLEN);
    
    /* Clone from the ticket and check that the clone succeeded */
    result = CLONE_MASTER(ticket);
    vmid = result->vmid;

    if (!result) {
        snprintf(*err, errlen, "Error cloning from ticket");

        DISPOSE_TICKET(ticket);
        vmid = -1;
    } else {
        if (result->rc.number_clones == 0) {
            MERGE_MASTER(ticket);
            snprintf(*err, errlen, "Cloning produced zero clones");
            vmid = -1;
        } else {
            gettimeofday(&end, NULL);
            new_clones = result->rc.number_clones;

            if (vmid == 0) {
                (void) __sync_fetch_and_add(&clone_generation, 1);

                /* Store the number of clones for later use */
                clone_set_num(clone_generation, result->rc.number_clones);

                time = end.tv_sec - start.tv_sec + (end.tv_usec - start.tv_usec) / 1000000.0f;
                proxy_log(LOG_INFO, "%d clones successfully created in %.3fs",
                    result->rc.number_clones,
                    time);
            } else {
                server_id = vmid;
                proxy_log(LOG_INFO, "I am clone %d", vmid);

                /* Force a return from poll so old connections
                 * with the load balancer will drop */
                for (i=0; i<options.client_threads; i++)
                    pthread_kill(net_threads[i].thread, SIGPOLL);

                /* Wait for IP reconfig */
                do {
                    proxy_options_update_host();
                } while (strncmp(options.phost, oldip, INET6_ADDRSTRLEN) == 0);

                proxy_log(LOG_INFO, "New clone IP is %s", options.phost);
            }
        }
    }

out:
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

/**
 * Called after :proxy_do_clone to signify that any post-cloning
 * actions are complete and querying can now continue .
 **/
void proxy_clone_complete() {
    req_clones = 0;
    new_clones = 0;
    cloning = 0;
}
