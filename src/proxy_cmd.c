/******************************************************************************
 * proxy_cmd.c
 *
 * Respond to PROXY commands.
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
#include <netdb.h>

extern CHARSET_INFO *system_charset_info;

static uchar *net_store_data(uchar *to, const uchar *from, size_t length);

/* Definitions for PROXY STATUS functions */
static void send_status_field(MYSQL *mysql, char *name, char *org_name, status_t *status);
static void add_row(MYSQL *mysql, uchar *buff, char *name, long value, status_t *status);
static my_bool net_status(MYSQL *mysql, char *query, ulong query_len, status_t *status);

/** Mutex for locking transaction results so we can safely make insertions into the hashtable */
static pthread_mutex_t result_mutex;

/* taken from sql/protocol.cc */
static uchar *net_store_data(uchar *to, const uchar *from, size_t length) {
  to = net_store_length(to,length);
  memcpy(to,from,length);
  return to+length;
}

/**
 * Send information on a SHOW STATUS field to the client.
 *
 * This function is only called twice and exists for convenience.
 *
 * @param mysql          MYSQL object where the field packet should be sent.
 * @param name           Column identifer after AS clause.
 * @param org_name       Column identifer before AS clause.
 * @param[in,out] status Status information for the connection.
 **/
static void send_status_field(MYSQL *mysql, char *name, char *org_name, status_t *status) {
    uchar buff[BUFSIZ], *pos;

    /* These values are the same for SHOW STATUS */
    static int len = 0x0400;
    static int type = FIELD_TYPE_VAR_STRING;
    static int flags = NOT_NULL_FLAG;

    /* For protocol details, see
     * http://forge.mysql.com/wiki/MySQL_Internals_ClientServer_Protocol#Field_Packet */
    pos = buff;
    pos = net_store_data(buff, (uchar*) "def", 3);                  /* catalog */
    pos = net_store_data(pos, (uchar*) "", 0);                      /* database */

    pos = net_store_data(pos, (uchar*) "STATUS", 6);                /* table */
    pos = net_store_data(pos, (uchar*) "", 0);                      /* org_table */
    pos = net_store_data(pos, (uchar*) name, strlen(name));         /* name */
    pos = net_store_data(pos, (uchar*) org_name, strlen(org_name)); /* org_name */
    pos[0] = (uchar) 0xc; pos++;
    int2store(pos, system_charset_info->number);
    pos += 2;
    int4store(pos, len);
    pos += 4;
    pos[0] = type; pos++;

    int2store(pos, flags);
    pos += 2;

    pos[0] = 0; pos++;
    int2store(pos, 0x00);
    pos += 2;

    my_net_write(&mysql->net, buff, (size_t) (pos - buff));
    status->bytes_sent += pos-buff;
    proxy_net_flush(mysql);
}

/** Check if cmp is a prefix of str */
#define strprefix(str, cmp, len) (((sizeof(cmp) - 1) > len) ? 0 : (strncasecmp(str, cmp, sizeof(cmp)-1) == 0))

/**
 * Send one row of output from a PROXY STATUS command.
 *
 * @param mysql          MYSQL object where the row packet should be sent.
 * @param buff           A buffer which can be used to store data.
 * @param name           Name of the variable to send.
 * @param value          Value of the variable to send.
 * @param[in,out] status Status information for the connection.
 **/
static void add_row(MYSQL *mysql, uchar *buff, char *name, long value, status_t *status) {
    uchar *pos;
    char val[LONG_LEN];
    int len;

    pos = buff;
    pos = net_store_data(pos, (uchar*) name, strlen(name));
    len = snprintf(val, LONG_LEN, "%lu", value);
    pos = net_store_data(pos, (uchar*) val, len);
    my_net_write(&mysql->net, buff, (size_t) (pos - buff));
    status->bytes_sent += pos-buff;
}

static inline void net_result_header(NET *net, uchar *buff, int nfields, status_t *status) {
    uchar *pos;

    pos = buff;
    pos = net_store_length(pos, nfields);
    pos = net_store_length(pos, 0);
    my_net_write(net, buff, (size_t) (pos - buff));
    status->bytes_sent += pos-buff;
}

/**
 * Respond to a PROXY STATUS command.
 *
 * @param mysql          MYSQL object where status should be sent.
 * @param query          Query string from client.
 * @param query_len      Length of query string.
 * @param[in,out] status Status information for the connection.
 *
 * @return TRUE on error, FALSE otherwise.
 **/
static my_bool net_status(MYSQL *mysql, char *query, ulong query_len, status_t *status) {
    uchar buff[BUFSIZ], *pos;
    NET *net = &mysql->net;
    char *last_tok = strrchr(query, ' ')+1;
    char *pch, *t = NULL;
    my_bool global = FALSE, session = FALSE;
    status_t total_status, *send_status;
    int i;

    /* Get status request type */
    pch = strtok_r(query, " ", &t);
    global = (pch == last_tok) || strprefix(pch, "GLOBAL", query_len);
    if (!global)
        session = strprefix(pch, "SESSION", query_len);

    /* Invalid status request */
    if (!(global || session) && pch != last_tok)
        return proxy_net_send_error(mysql, ER_SYNTAX_ERROR, "Status type must be GLOBAL or SESSION");

    /* Send result header packet specifying two fields */
    net_result_header(net, buff, 2, status);

    /* Send list of fields */
    send_status_field(mysql, "Variable_name", "VARIABLE_NAME", status);
    send_status_field(mysql, "Value", "VARIABLE_VALUE", status);
    proxy_net_send_eof(mysql, status);

    /* Gather status data */
    if (session) {
        send_status = status;
    } else {
        /* Start with global data */
        proxy_status_reset(&total_status);
        proxy_status_add(&global_status, &total_status);

        /* Accumulate data from client threads */
        for (i=0; i<options.client_threads; i++)
            proxy_status_add(net_threads[i].status, &total_status);

        send_status = &total_status;
    }

    /* Send status info to client */
    pos = buff;
    add_row(mysql, buff, "Connections",       global_connections, status);
    add_row(mysql, buff, "Bytes_received",    send_status->bytes_recv, status);
    add_row(mysql, buff, "Bytes_sent",        send_status->bytes_sent, status);
    add_row(mysql, buff, "Queries",           send_status->queries, status);
    add_row(mysql, buff, "Queries_any",       send_status->queries_any, status);
    add_row(mysql, buff, "Queries_all",       send_status->queries_all, status);
    add_row(mysql, buff, "Threads_connected", thread_pool->locked, status);
    add_row(mysql, buff, "Threads_running",   global_running, status);
    add_row(mysql, buff, "Uptime",         (long) (time(NULL) - proxy_start_time), status);

    proxy_net_send_eof(mysql, status);
    proxy_net_flush(mysql);

    return FALSE;
}

/**
 * Respond to a PROXY CLONE[S] command.
 *
 * @param mysql          MYSQL object where status should be sent.
 * @param query          Query string from client.
 * @param[in,out] status Status information for the connection.
 *
 * @return TRUE on error, FALSE otherwise.
 **/
static my_bool net_clone(MYSQL *mysql, char *query,
        __attribute__((unused)) status_t *status) {
    char *buff=alloca(BUFSIZ), *tok, *t=NULL;
    int nclones = 1, ret, sql_errno, i;
    my_bool error = FALSE;

    /* Get the number of clones to create */
    tok = strtok_r(query, " ", &t);
    if (tok) {
        errno = 0;
        nclones = strtol(tok, NULL, 10);
        if (errno || nclones <= 0)
            return proxy_net_send_error(mysql, ER_SYNTAX_ERROR, "Invalid number of clones");
    }

    /* Check if we think we can clone */
    if (options.cloneable) {
        /* Perform the clone and return the result */
        ret = proxy_do_clone(nclones, &buff, BUFSIZ);

        if (ret < 0) {
            /* Cloning failed */
            proxy_clone_complete();
            return proxy_net_send_error(mysql, ER_ERROR_WHEN_EXECUTING_COMMAND, buff);
        } else if (ret == 0) {
            /* This is the master, and cloning succeeded */
            proxy_clone_complete();
            return proxy_net_send_ok(mysql, 0, 0, 0);
        } else {
            /* Reconnect to the coordinator for notification */
            MYSQL *new_coordinator = mysql_init(NULL), *old_coordinator;
            my_bool reconnect = 1;
            mysql_options(new_coordinator, MYSQL_OPT_RECONNECT, &reconnect);

            /* Give 5 tries at reconnecting to the coordinator */
            for (i=0; i<5; i++) {
                error = FALSE;

                proxy_log(LOG_INFO, "Attempt %d at reconnecting to coordinator", i);
                if (!mysql_real_connect(new_coordinator, coordinator->host,
                        options.user, options.pass,
                        NULL, coordinator->port, NULL, 0)) {
                    mysql_close(new_coordinator);

                    error = TRUE;
                }

                if (!error)
                    break;
            }

            proxy_clone_complete();

            /* Switch coordinators and construct the query */
            if (!error) {
                pthread_spin_lock(&coordinator_lock);

                old_coordinator = (MYSQL*) coordinator;
                coordinator = new_coordinator;
                mysql_close(old_coordinator);

                snprintf(buff, BUFSIZ, "PROXY ADD %d %s:%d;", server_id, options.phost, options.pport);
                proxy_log(LOG_INFO, "Sending add query %s to coordinator", buff);
                mysql_query((MYSQL*) coordinator, buff);

                pthread_spin_unlock(&coordinator_lock);
            } else {
                proxy_log(LOG_ERROR, "Error reconnecting to coordinator: %s",
                    mysql_error(new_coordinator));

                /* The old coordinator may not be any good any more, but we keep it around */

                return TRUE;
            }

            if (mysql_errno((MYSQL*) coordinator))
                proxy_log(LOG_ERROR, "Error notifying coordinator about clone host %d: %s",
                    ret, mysql_error((MYSQL*) coordinator));

            return TRUE;
        }
    } else if (options.coordinator) {
        proxy_log(LOG_INFO, "Received clone command, waiting for queries in commit phase");

        /* Wait until we can clone */
        cloning = 1;
        while (committing) { usleep(SYNC_SLEEP); }

        /* Get ready and make sure no one else is cloning */
        if (!proxy_clone_prepare(nclones))
            return proxy_net_send_error(mysql, ER_CANT_LOCK, "Previous cloning operation not complete");

        /* Contact the master to perform cloning */
        proxy_log(LOG_INFO, "Requesting %d clone(s) from master", nclones);
        snprintf(buff, BUFSIZ, "PROXY CLONE %d;", nclones);
        mysql_query((MYSQL*) master, buff);

        if ((sql_errno = mysql_errno((MYSQL*) master))) {
            return proxy_net_send_error(mysql, sql_errno, mysql_error((MYSQL*) master));
        } else {
            (void) __sync_fetch_and_add(&clone_generation, 1);
            proxy_debug("Cloning successful, clone generation is %d", clone_generation);

            /* Wait for clones to finish */
            ret = proxy_clone_wait();
            cloning = 0;

            /* Send result of cloning to clients */
            if (ret)
                return proxy_net_send_error(mysql, ER_LOCK_WAIT_TIMEOUT, "Error waiting for new clones");
            else
                return proxy_net_send_ok(mysql, 0, 0, 0);
        }
    } else {
        return proxy_net_send_error(mysql, ER_NOT_ALLOWED_COMMAND, "Proxy server can't be cloned");
    }
}

#ifdef HAVE_LIBSF
static my_bool net_show_clones(MYSQL *mysql,
        __attribute__((unused)) char *query,
        __attribute__((unused)) ulong query_len,
        status_t *status) {
    uchar buff[BUFSIZ];
    NET *net = &mysql->net;
    sf_result *tickets, *clones;
    sf_ticket_list *ticket;
    sf_clone_info *clone;
    int nclones = 0;

    /* Check if we can clone */
    if (!options.cloneable)
        proxy_net_send_error(mysql, ER_NOT_ALLOWED_COMMAND, "Proxy server not started as cloneable");

    /* Get a list of active tickets */
    tickets = LIST_TICKETS();
    if (!tickets)
        return proxy_net_send_error(mysql, ER_ERROR_WHEN_EXECUTING_COMMAND, "Could not get list of tickets");

    /* Check if we have no tickets */
    ticket = tickets->outstanding_tickets;
    if (!ticket)
        return proxy_net_send_ok(mysql, 0, 0, 0);

    /* Send result header packet specifying two fields */
    net_result_header(net, buff, 2, status);

    /* Send list of fields */
    send_status_field(mysql, "Ticket", "TICKET", status);
    send_status_field(mysql, "VMID", "VMID", status);
    proxy_net_send_eof(mysql, status);

    /* Read the clones from each ticket and
     * send the list to the client */
    do {
        clones = READ_TICKET_INFO(ticket->ticket);
        clone = clones->clone_list;
        if (!clone)
            continue;

        do {
            add_row(mysql, buff, ticket->ticket, clone->vmid, status);
            nclones++;
        } while ((clone = clone->next));
    } while((ticket = ticket->next));

    /* If for some reason we got here, but no clones exist,
     * then return an OK so the client knows we haven nothing */
    if (nclones == 0)
        add_row(mysql, buff, "", 0, status);

    proxy_net_send_eof(mysql, status);
    proxy_net_flush(mysql);
#else
static my_bool net_show_clones(MYSQL *mysql,
        __attribute__((unused)) char *query,
        __attribute__((unused)) ulong query_len,
        __attribute__((unused)) status_t *status) {
    proxy_net_send_error(mysql, ER_NOT_ALLOWED_COMMAND, "Proxy not compiled with SnowFlock support");
#endif /* HAVE_LIB_SF */

    return FALSE;
}

/**
 * Extract a hostname and port number from a string
 * of the format host[:port].
 *
 * @param str       String to parse.
 * @param[out] host Parsed hostname.
 * @param[out] port Parsed port number, or
 *                  default if none specified.
 **/
static void parse_host(char *str, char **host, int *port) {
    char *t = NULL;

    *host = strtok_r(str, ":", &t);
    str = strtok_r(NULL, ":", &t);
    if (str) {
        errno = 0;
        *port = strtol(str, NULL, 10);
        if (errno || *port <= 0)
            *port = -1;
    } else {
        *port = options.backend.port;
    }
}

/**
 * Respond to a PROXY COORDINATOR command.
 *
 * @param mysql  MYSQL object where results should be sent.
 * @param t      Pointer to the next token in the string.
 * @param status Status information for the connection.
 **/
static my_bool net_proxy_coordinator(MYSQL *mysql, char *t, status_t *status) {
    my_bool error = FALSE;
    MYSQL *old_coordinator, *new_coordinator = NULL;
    uchar buff1[BUFSIZ], buff2[BUFSIZ], *pos;
    char *tok = strtok_r(NULL, " ", &t), *host, ip[INET6_ADDRSTRLEN];
    struct hostent *entry;
    int port;
    size_t size;

    /* Check if a coordinator is valid */
    if (!options.cloneable)
        return proxy_net_send_error(mysql, ER_NOT_ALLOWED_COMMAND, "Coordinator cannot be used if proxy is not cloneable");

    if (tok) {
        /* Extract the host and port information */
        parse_host(tok, &host, &port);
        if (port < 0)
            return proxy_net_send_error(mysql, ER_SYNTAX_ERROR, "Invalid coordinator port number");

        /* Attempt to get the coordinator IP address */
        entry = gethostbyname(host);
        if (!entry) {
            proxy_log(LOG_ERROR, "Could not resolve coordinator hostname, using IP: %s", hstrerror(h_errno));
        } else {
            if (!inet_ntop(entry->h_addrtype, entry->h_addr, ip, INET6_ADDRSTRLEN))
                proxy_log(LOG_ERROR, "Could not convert IP to string: %s", errstr);

            /* Signify that we should not use the hostname */
            host = NULL;
        }

        /* Check for a valid host and connect to the coordinator */
        new_coordinator = mysql_init(NULL);
        if (new_coordinator) {
            /* Reconnect on error */
            my_bool reconnect = 1;
            mysql_options(new_coordinator, MYSQL_OPT_RECONNECT, &reconnect);

            if (!mysql_real_connect(new_coordinator, host ?: ip, options.user, options.pass, NULL, port, NULL, 0)) {
                error = proxy_net_send_error(mysql, mysql_errno(new_coordinator), mysql_error(new_coordinator));
                mysql_close(new_coordinator);
                return error;
            }
        }

        if (!new_coordinator) {
            proxy_log(LOG_ERROR, "Error setting coordinator to %s", tok);

            return proxy_net_send_error(mysql, ER_BAD_HOST_ERROR, "Invalid coordinator host");
        } else {
            proxy_log(LOG_INFO, "Coordinator successfully changed to %s:%d", host ?: ip, port);

            pthread_spin_lock(&coordinator_lock);

            /* Swap to the new coordinator */
            old_coordinator = (MYSQL*) coordinator;
            coordinator = new_coordinator;
            if (old_coordinator)
                mysql_close(old_coordinator);

            pthread_spin_unlock(&coordinator_lock);

            return proxy_net_send_ok(mysql, 0, 0, 0);
        }
    } else {
        /* Return nothing if no coordinator is set */
        if (!coordinator)
            return proxy_net_send_ok(mysql, 0, 0, 0);

        /* Send the header */
        net_result_header(&mysql->net, buff1, 1, status);
        send_status_field(mysql, "Coordinator", "COORDINATOR", status);
        proxy_net_send_eof(mysql, status);

        /* Send the coordinator name */
        pos = buff1;

        pthread_spin_lock(&coordinator_lock);
        size = snprintf((char*) buff2, BUFSIZ, "%s:%d", coordinator->host, coordinator->port);
        pthread_spin_unlock(&coordinator_lock);

        /* Check that the name has been successfully stored and send */
        if (size <= 0)
            pos = net_store_data(pos, (uchar*) "", 0);
        else
            pos = net_store_data(pos, (uchar*) buff2, size);

        my_net_write(&mysql->net, buff1, (size_t) (pos - buff1));
        status->bytes_sent += pos-buff1;

        proxy_net_send_eof(mysql, status);
        proxy_net_flush(mysql);

        return FALSE;
    }
}

/**
 * Respond to a PROXY ADD command received from a client.
 *
 * @param mysql   MYSQL object where results should be sent.
 * @param t       Pointer to the next token in the query string.
 * @param[in,out] status Status information for the connection.
 **/
static my_bool net_add_clone(MYSQL *mysql, char *t,
        __attribute__((unused)) status_t *status) {
    char *host, *tok;
    proxy_host_t *store_host;
    int port, clone_id;

    /* Ensure that we are the coordinator */
    if (!options.coordinator)
        return proxy_net_send_error(mysql, ER_NOT_ALLOWED_COMMAND, "Proxy server not started as coordinator");

    /* Get the clone ID */
    tok = strtok_r(NULL, " ", &t);
    if (tok) {
        errno = 0;
        clone_id = strtol(tok, NULL, 10);
    }
    if (!tok || errno || clone_id <= 0)
        return proxy_net_send_error(mysql, ER_SYNTAX_ERROR, "Invalid clone ID");

    /* Extract and validate host information */
    parse_host(t, &host, &port);

    if (port < 0)
        return proxy_net_send_error(mysql, ER_SYNTAX_ERROR, "Invalid clone port number");

    /* Save the clone's IP in the hash table */
    store_host = malloc(sizeof(proxy_host_t));
    store_host->host = malloc(HOST_NAME_MAX+1);
    strncpy(store_host->host, host, HOST_NAME_MAX+1);
    store_host->port = port;
    proxy_clone_insert((ulong) clone_id, store_host);

    /* Attempt to add the new host and report success/failure */
    if (proxy_backend_add(host, port))
        return proxy_net_send_error(mysql, ER_BAD_HOST_ERROR, "Error adding new host");
    else
        return proxy_net_send_ok(mysql, 0, 0, 0);
}

/**
 * Respond to a PROXY SUCCESS or FAILURE command
 * received from a client.
 *
 * @param mysql   MYSQL object where results should be sent.
 * @param t       Pointer to the next token in the query string.
 * @param success TRUE if the transaction succeeded, FALSE if it failed.
 * @param[in,out] status Status information for the connection.
 *
 * @return TRUE on error, FALSE otherwise.
 **/
static my_bool net_trans_result(MYSQL *mysql, char *t, my_bool success,
        __attribute__((unused)) status_t *status) {
    char *tok;
    int clone_id, i;
    ulong transaction_id;
    proxy_trans_t *trans;
    my_bool error = FALSE;

    /* Ensure that we are the coordinator */
    if (!options.coordinator)
        return proxy_net_send_error(mysql, ER_NOT_ALLOWED_COMMAND, "Proxy server not started as coordinator");

    /* Get the clone ID */
    tok = strtok_r(NULL, " ", &t);
    if (tok) {
        errno = 0;
        clone_id = strtol(tok, NULL, 10);
    }
    if (!tok || errno || clone_id <= 0)
        return proxy_net_send_error(mysql, ER_SYNTAX_ERROR, "Invalid clone ID");

    /* Get the transaction ID */
    tok = strtok_r(NULL, " ", &t);
    if (tok) {
        errno = 0;
        transaction_id = strtol(tok, NULL, 10);
    }
    if (!tok || errno || transaction_id <= 0)
        return proxy_net_send_error(mysql, ER_SYNTAX_ERROR, "Invalid transaction ID");

    /* Message received, clone */
    error = proxy_net_send_ok(mysql, 0, 0, 0);

    /* We lock around this next section so only one message can mess with the hash table */
    proxy_mutex_lock(&result_mutex);
    proxy_debug("Result of transaction %lu on clone %d is %d", transaction_id, clone_id, success);

    /* Check if we have already received some message about this transaction */
    if (!(trans = proxy_trans_search(transaction_id))) {
        proxy_debug("Creating new hash table entry for transaction %lu", transaction_id);

        /* Create a new entry in the transaction hash table */
        trans = malloc(sizeof(proxy_trans_t));
        trans->total = proxy_clone_get_num(clone_generation);
        trans->num = 0;
        trans->done = 0;
        trans->success = TRUE;
        proxy_cond_init(&trans->cv);
        proxy_mutex_init(&trans->cv_mutex);

        /* Create space to store the IDs of clones */
        trans->clone_ids = malloc(sizeof(int)*trans->total);
        for (i=0; i<trans->total;i++)
            trans->clone_ids[i] = -1;

        proxy_trans_insert(transaction_id, trans);
    }

    /* Update the commit data */
    trans->clone_ids[trans->num] = clone_id;
    trans->num++;
    trans->success = trans->success && success;

    /* Check if all responses have been received */
    if (trans->num == trans->total) {
        proxy_debug("Transaction %lu completed on all clones, signalling %s",
                transaction_id, trans->success ? "commit" : "rollback");

        /* Notify clones that they should commit */
        proxy_backend_clone_complete(trans->clone_ids, trans->total, transaction_id, trans->success);
        free(trans->clone_ids);
        trans->clone_ids = NULL;

        proxy_debug("Signalling local threads for transaction %lu", transaction_id);

        /* Signal local threads to commit */
        proxy_mutex_lock(&trans->cv_mutex);
        proxy_cond_broadcast(&trans->cv);
        proxy_mutex_unlock(&trans->cv_mutex);

        /* Wait for everyone to finish and then
         * remove and free the transaction. */
        proxy_debug("Waiting for local threads to commit before removing transaction");
        proxy_mutex_lock(&trans->cv_mutex);
        while (trans->done < (proxy_backend_num()-trans->total)) { pthread_cond_wait(&trans->cv, &trans->cv_mutex); }

        if (proxy_trans_remove(transaction_id) != trans)
            proxy_log(LOG_ERROR, "Transaction %lu changed when removed from hash table",
                transaction_id);

        proxy_mutex_unlock(&trans->cv_mutex);
        proxy_cond_destroy(&trans->cv);
        proxy_mutex_destroy(&trans->cv_mutex);
        free(trans);
    }

    proxy_mutex_unlock(&result_mutex);

    return error;
}

/**
 * Respond to a PROXY COMMIT or ROLLBACK command
 * received from a client.
 *
 * @param mysql   MYSQL object where results should be sent.
 * @param t       Pointer to the next token in the query string.
 * @param success TRUE to commit, FALSE to rollback.
 * @param[in,out] status Status information for the connection.
 *
 * @return TRUE on error, FALSE otherwise.
 **/
my_bool net_commit(MYSQL *mysql, char *t, my_bool success,
        __attribute__((unused)) status_t *status) {
    char *tok;
    ulong commit_trans_id;
    proxy_trans_t *trans;

    /* Ensure that we are cloneable */
    if (!options.cloneable)
        return proxy_net_send_error(mysql, ER_NOT_ALLOWED_COMMAND, "Proxy server not started as cloneable");

    /* Get the transaction ID */
    tok = strtok_r(NULL, " ", &t);
    if (tok) {
        errno = 0;
        commit_trans_id = strtol(tok, NULL, 10);
    }
    if (!tok || errno || commit_trans_id <= 0)
        return proxy_net_send_error(mysql, ER_SYNTAX_ERROR, "Invalid transaction ID");

    proxy_debug("Received %s message for transaction %lu",
        success ? "commit" : "rollback", commit_trans_id);

    /* Grab the transaction data from the hash table, waiting if necessary */
    while (!(trans = proxy_trans_search(commit_trans_id))) { usleep(SYNC_SLEEP); }

    proxy_debug("Found transaction %lu in hash table for completion",
        commit_trans_id);

    /* Tell the waiting thread to proceed with commit/rollback */
    trans->num = 1;
    trans->success = success;
    proxy_mutex_lock(&trans->cv_mutex);
    proxy_cond_signal(&trans->cv);
    proxy_mutex_unlock(&trans->cv_mutex);

    if (success)
        proxy_debug("Signalled commit for transaction %lu", commit_trans_id);
    else
        proxy_debug("Signalled rollback for transaction %lu", commit_trans_id);

    return proxy_net_send_ok(mysql, 0, 0, 0);
}

/**
 * Respond to a PROXY command received from a client.
 *
 * @param mysql          Client MYSQL object.
 * @param query          Query string from client.
 * @param query_len      Length of query string.
 * @param[in,out] status Status information for the connection.
 *
 * @return TRUE on error, FALSE otherwise.
 **/
my_bool proxy_cmd(MYSQL *mysql, char *query, ulong query_len, status_t *status) {
    char *tok, *last_tok, *t=NULL;

    /* Strip trailing semicolons */
    while (query[query_len-1] == ';') {
        query[query_len-1] = '\0';
        query_len--;
    }

    status->bytes_recv += query_len;

    last_tok = strrchr(query, ' ')+1;
    tok = strtok_r(query, " ", &t);

    if (tok) {
        /* Parse the command and take appropriate action */
        if (strprefix(tok, "CLONES", query_len)) {
            return net_show_clones(mysql, t, query_len-sizeof("CLONES")-1, status);
        } else if (strprefix(tok, "CLONE", query_len)) {
            return net_clone(mysql, t, status);
        } else if (strprefix(tok, "COORDINATOR", query_len)) {
            return net_proxy_coordinator(mysql, t, status);
        } else if (strprefix(tok, "ADD", query_len)) {
            return net_add_clone(mysql, t, status);
        } else if (strprefix(tok, "SUCCESS", query_len)) {
            return net_trans_result(mysql, t, TRUE, status);
        } else if (strprefix(tok, "FAILURE", query_len)) {
            return net_trans_result(mysql, t, FALSE, status);
        } else if (strprefix(tok, "COMMIT", query_len)) {
            return net_commit(mysql, t, TRUE, status);
        } else if (strprefix(tok, "ROLLBACK", query_len)) {
            return net_commit(mysql, t, FALSE, status);
        }

        if (strprefix(last_tok, "STATUS", query_len))
            return net_status(mysql, query, query_len, status);
    }

    /* No valid command was found */
    return proxy_net_send_error(mysql, ER_SYNTAX_ERROR, "Unrecognized proxy command");
}

/**
 * Start a thread to handle clients which are connected
 * to the administrative port.
 *
 * @param ptr :proxy_thread_t object for this thread.
 * @return NULL.
 **/
static void* cmd_admin_new_thread(void *ptr) {
    proxy_thread_t *thread = (proxy_thread_t*) ptr;

    /* XXX: nothing is currently done with this status,
     *      i.e. it is never merged into global_status.
     *      Perhaps there should be separate admin stats */
    status_t status;

    proxy_net_client_do_work(&thread->data.work, thread->id, NULL, &status, TRUE);

    free(thread);
    mysql_thread_end();
    pthread_exit(NULL);
}

/**
 * Start a thread for handling administrative connections
 * which can only execute PROXY commands.
 *
 * @param ptr Ignored.
 *
 * @return NULL.
 **/
void* proxy_cmd_admin_start(__attribute__((unused)) void *ptr) {
    int serverfd, clientfd;
    fd_set fds;
    union sockaddr_union clientaddr;
    socklen_t clientlen = sizeof(clientaddr);
    extern int run;
    pthread_attr_t attr;
    proxy_thread_t *thread;
    int thread_id = 0;

    /* Initialize the result mutex. Since it isn't
     * used until after cloning, this should be a
     * safe place to do this. */
    proxy_mutex_init(&result_mutex);

    proxy_threading_name("Admin");
    proxy_threading_mask();

    /* Wait for the server to start */
    while (!run) { usleep(SYNC_SLEEP); }

    /* Bind the admin socket */
    proxy_log(LOG_INFO, "Opening admin socket on 0.0.0.0:%d", options.admin_port);
    if ((serverfd = proxy_net_bind_new_socket(NULL, options.admin_port)) < 0)
        goto out;

    /* Update the host address again if we are the
     * master so it will contain the master's IP */
    if (options.cloneable)
        proxy_options_update_host();

    /* Initialize thread attributes */
    /* XXX: We currently leave these threads detached. We really
     *      should save the thread IDs so we can be sure that
     *      they are killed when we shut down. */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    /* Admin connections event loop */
    while (run) {
        FD_ZERO(&fds);
        FD_SET(serverfd, &fds);

        if (select(FD_SETSIZE, &fds, NULL, NULL, NULL) != 1)
            continue;

        clientfd = accept(serverfd, &clientaddr.sa, &clientlen);
        if (clientfd < 0) {
            if (errno != EINTR)
                proxy_log(LOG_ERROR, "Error accepting admin connection: %s", errstr);
            continue;
        }

        /* Set up thread data and create the thread */
        thread = malloc(sizeof(proxy_thread_t));
        thread->exit = 0;
        thread->data.work.clientfd = clientfd;
        thread->data.work.addr = &clientaddr.sin;
        thread->data.work.proxy = NULL;
        thread->id = thread_id++;

        proxy_threading_create(&thread->thread, &attr, cmd_admin_new_thread, (void*) thread);
    }

    pthread_attr_destroy(&attr);
out:
    pthread_exit(NULL);
}
