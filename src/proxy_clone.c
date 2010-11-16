/******************************************************************************
 * proxy_clone.h
 *
 * Main proxy executable and server setup
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

#include <sf.h>

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
int proxy_do_clone(int nclones, char **err, int errlen) {
    sf_result *result;
    char ticket[SF_TICKET_SIZE+1];
    int vmid = -1;

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
        return -1;
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
            if (vmid == 0)
                proxy_log(LOG_INFO, "%d clones successfully created", result->rc.number_clones);
            else
                proxy_log(LOG_INFO, "I am clone %d", vmid);
        }
    }

out:
    if (result)
        FREE_SF_RES(result);
    return vmid;
}
