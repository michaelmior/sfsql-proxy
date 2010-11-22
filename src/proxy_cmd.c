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

#ifdef HAVE_SF_H
#include <sf.h>
#endif
#include <netdb.h>

extern CHARSET_INFO *system_charset_info;

static uchar *net_store_data(uchar *to, const uchar *from, size_t length);

/* Definitions for PROXY STATUS functions */
static void send_status_field(MYSQL *mysql, char *name, char *org_name, status_t *status);
static void add_row(MYSQL *mysql, uchar *buff, char *name, long value, status_t *status);
static my_bool net_status(MYSQL *mysql, char *query, ulong query_len, status_t *status);

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
        total_status.bytes_recv = global_status.bytes_recv;
        total_status.bytes_sent = global_status.bytes_sent;
        total_status.queries = global_status.queries;

        /* Accumulate data from client threads */
        for (i=0; i<options.client_threads; i++) {
            total_status.bytes_recv += net_threads[i].status->bytes_recv;
            total_status.bytes_sent += net_threads[i].status->bytes_sent;
            total_status.queries += net_threads[i].status->queries;
        }

        send_status = &total_status;
    }

    /* Send status info to client */
    pos = buff;
    add_row(mysql, buff, "Connections",    global_connections, status);
    add_row(mysql, buff, "Bytes_Received", send_status->bytes_recv, status);
    add_row(mysql, buff, "Bytes_sent",     send_status->bytes_sent, status);
    add_row(mysql, buff, "Queries",        send_status->queries, status);
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
    char *buff = alloca(BUFSIZ), *tok, *t=NULL, host[HOST_NAME_MAX+1];
    int nclones = 1, ret, errno;

    /* Check if we think we can clone */
    if (options.cloneable) {
        /* Get the number of clones to create */
        tok = strtok_r(query, " ", &t);
        if (tok) {
            nclones = strtol(tok, NULL, 10);
            if (errno || nclones <= 0)
                return proxy_net_send_error(mysql, ER_SYNTAX_ERROR, "Invalid number of clones");
        }

        /* Perform the clone and return the result */
        ret = proxy_do_clone(nclones, &buff, BUFSIZ);

        if (ret < 0) {
            /* Cloning failed */
            return proxy_net_send_error(mysql, ER_ERROR_WHEN_EXECUTING_COMMAND, buff);
        } else if (ret == 0) {
            /* This is the master, and cloning succeeded */
            return proxy_net_send_ok(mysql, 0, 0, 0);
        } else {
            /* This a clone, notify the coordinator */
            if (gethostname(host, HOST_NAME_MAX+1))
                return TRUE;

            snprintf(buff, BUFSIZ, "PROXY ADD %s:%d;", host, options.pport);
            mysql_query((MYSQL*) coordinator, buff);

            if (mysql_errno((MYSQL*) coordinator))
                proxy_log(LOG_ERROR, "Error notifying coordinator about clone host %d", ret);

            return TRUE;
        }
    } else if (options.coordinator) {
        /* Contact the master to perform cloning */
        mysql_query((MYSQL*) master, "PROXY CLONE;");

        /* Wait for the clones to be ready */
        if (proxy_clone_wait(1))
            return proxy_net_send_error(mysql, ER_LOCK_WAIT_TIMEOUT, "Error waiting for new clones");

        if ((errno = mysql_errno((MYSQL*) master)))
            return proxy_net_send_error(mysql, errno, mysql_error((MYSQL*) master));
        else
            return proxy_net_send_ok(mysql, 0, 0, 0);
    } else {
        return proxy_net_send_error(mysql, ER_NOT_ALLOWED_COMMAND, "Proxy server can't be cloned");
    }
}

#ifdef HAVE_SF_H
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
#endif

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
    char *tok = strtok_r(NULL, " ", &t), *host;
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

        /* Check for a valid host and connect to the coordinator */
        new_coordinator = mysql_init(NULL);
        if (new_coordinator) {
            /* Reconnect on error */
            my_bool reconnect = 1;
            mysql_options(new_coordinator, MYSQL_OPT_RECONNECT, &reconnect);

            if (!mysql_real_connect(new_coordinator, host, options.user, options.pass, NULL, port, NULL, 0)) {
                error = proxy_net_send_error(mysql, mysql_errno(new_coordinator), mysql_error(new_coordinator));
                mysql_close(new_coordinator);
                return error;
            }
        }

        if (!new_coordinator) {
            proxy_log(LOG_ERROR, "Error setting coordinator to %s", tok);

            return proxy_net_send_error(mysql, ER_BAD_HOST_ERROR, "Invalid coordinator host");
        } else {
            proxy_log(LOG_INFO, "Coordinator successfully changed to %s:%d", host, port);

            /* Swap to the new coordinator */
            old_coordinator = (MYSQL*) coordinator;
            coordinator = new_coordinator;
            if (old_coordinator)
                mysql_close(old_coordinator);
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
        size = snprintf((char*) buff2, BUFSIZ, "%s:%d", coordinator->host, coordinator->port);

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
    char *host;
    int port;

    /* Ensure that we are the coordinator */
    if (!options.coordinator)
        return proxy_net_send_error(mysql, ER_NOT_ALLOWED_COMMAND, "Proxy server not started as coordinator");

    /* Extract and validate host information */
    parse_host(t, &host, &port);

    if (port < 0)
        return proxy_net_send_error(mysql, ER_SYNTAX_ERROR, "Invalid clone port number");

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
    int clone_id;
    ulong transaction_id;

    /* Ensure that we are the coordinator */
    if (!options.coordinator)
        return proxy_net_send_error(mysql, ER_NOT_ALLOWED_COMMAND, "Proxy server not started as coordinator");

    /* Get the clone ID */
    tok = strtok_r(NULL, " ", &t);
    if (tok)
        clone_id = strtol(tok, NULL, 10);
    if (!tok || errno || clone_id <= 0)
        return proxy_net_send_error(mysql, ER_SYNTAX_ERROR, "Invalid clone ID");

    /* Get the transaction ID */
    tok = strtok_r(NULL, " ", &t);
    if (tok)
        transaction_id = strtol(tok, NULL, 10);
    if (!tok || errno || transaction_id <= 0)
        return proxy_net_send_error(mysql, ER_SYNTAX_ERROR, "Invalid transaction ID");

    /* XXX need to actually do something with these values */
    proxy_log(LOG_INFO, "Result of transaction %lu on clone %d is %d", transaction_id, clone_id, success);

    return proxy_net_send_ok(mysql, 0, 0, 0);
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

    /* Ensure that we are the coordinator */
    if (!options.cloneable)
        return proxy_net_send_error(mysql, ER_NOT_ALLOWED_COMMAND, "Proxy server not started as cloneable");

    /* Get the transaction ID */
    tok = strtok_r(NULL, " ", &t);
    if (tok)
        commit_trans_id = strtol(tok, NULL, 10);
    if (!tok || errno || commit_trans_id <= 0)
        return proxy_net_send_error(mysql, ER_SYNTAX_ERROR, "Invalid transaction ID");

    /* Grab the transaction data from the hashtable, waiting if necessary */
    while (!(trans = proxy_trans_search(&commit_trans_id))) { usleep(100); }

    /* Tell the waiting thread to proceed with commit/rollback */
    trans->num = 1;
    trans->success = success;
    proxy_cond_signal(&trans->cv);

    if (success)
        proxy_debug("Committing transaction %lu", commit_trans_id);
    else
        proxy_debug("Rolling back transaction %lu", commit_trans_id);

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

    status->bytes_recv += query_len;

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

        last_tok = strrchr(query, ' ')+1;
        if (strprefix(last_tok, "STATUS", query_len))
            return net_status(mysql, query, query_len, status);
    }

    /* No valid command was found */
    return proxy_net_send_error(mysql, ER_SYNTAX_ERROR, "Unrecognized proxy command");
}
