/******************************************************************************
 * proxy_net.c
 *
 * Authenticate with clients and read requests to be forwarded to backends
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

#include <netdb.h>

/** Minimum size of a handshake from a client (from sql/sql_connect.cc) */
#define MIN_HANDSHAKE_SIZE 6

/** Exposes the default charset in the client library */
extern CHARSET_INFO *default_charset_info;
CHARSET_INFO *system_charset_info = &my_charset_utf8_general_ci;

ulong transaction_id = 1;

/* Definitions of functions to deal with client connections */
void client_do_work(proxy_work_t *work, int thread_id, commitdata_t *commit, status_t *status);
void client_destroy(proxy_thread_t *thread);
void net_thread_destroy(void *ptr);
static inline MYSQL* client_init(int clientfd);
static my_bool check_user(char *user, uint user_len, char *passwd, uint passwd_len, char *db, uint db_len);

/**
 * Perform client authentication.
 *
 * This code is derived from sql/sql_connect.cc:check_connection
 * XXX: not currently using thread ID
 *
 * @param mysql MySQL object corresponding to the client connection.
 * @param clientaddr Address of the newly connected client.
 * @param thread_id Identifier of the thread handling the connection.
 *
 * @return TRUE on error, FALSE otherwise
 **/
my_bool proxy_net_handshake(MYSQL *mysql, struct sockaddr_in *clientaddr, __attribute__((unused)) int thread_id) {
    NET *net;
    char ip[30], buff[SERVER_VERSION_LENGTH + 1 + SCRAMBLE_LENGTH + 1 + 64], scramble[SCRAMBLE_LENGTH + 1], *end;
    ulong server_caps, client_caps, pkt_len=0;
    struct rand_struct rand; /* XXX: reuse this */
    uint16 port = PROXY_PORT;
    CHARSET_INFO *charset;

    net = &mysql->net;

    vio_peer_addr(net->vio, ip, &port);
    vio_in_addr(net->vio, &(clientaddr->sin_addr));

    /* Save version number */
    end = strnmov(buff, mysql->server_version, SERVER_VERSION_LENGTH) + 1;
    int4store((uchar*) end, 0);
    end += 4;

    /* Generate scramble string */
    randominit(&rand, time(NULL), thread_id); /* XXX: init elsewhere */
    create_random_string(scramble, SCRAMBLE_LENGTH, &rand);
    end = strmake(end, scramble, SCRAMBLE_LENGTH_323) + 1;

    /* Add capabilities */
    /* XXX: transactions flag? get from backend?
     *      don't allow client to pick a DB or use multiple statements for now */
    server_caps = CLIENT_BASIC_FLAGS & ~(CLIENT_CONNECT_WITH_DB & CLIENT_MULTI_STATEMENTS);
    int2store(end, server_caps);

    end[2] = (char) default_charset_info->number;
    int2store(end+3, SERVER_STATUS_AUTOCOMMIT);
    bzero(end+5, 13);
    end += 18;

    /* Write rest of scramble */
    end = strmake(end, scramble + SCRAMBLE_LENGTH_323,
            SCRAMBLE_LENGTH - SCRAMBLE_LENGTH_323) + 1;

    if (net_write_command(net, (uchar) mysql->protocol_version, (uchar*) "", 0,
                (uchar*) buff, (size_t) (end-buff)) ||
            (pkt_len = my_net_read(net)) == packet_error ||
            pkt_len < MIN_HANDSHAKE_SIZE) {
        proxy_log(LOG_ERROR, "Error sending handshake to client");
        return TRUE;
    }

    /* XXX: pre-4.1 protocol not supported (or even checked) */
    client_caps = uint2korr(net->read_pos);
    client_caps |= ((ulong) uint2korr(net->read_pos + 2)) << 16;
    //pkt_len = unint4korr(net->read_pos + 4); /* max_client_packet_length */
    charset = get_charset((uint) net->read_pos[8], MYF(0));
    end = (char*) net->read_pos + 32;

    client_caps &= server_caps;

    if (end >= (char*) net->read_pos + pkt_len + 2) {
        proxy_log(LOG_ERROR, "Error handshaking with client,"
                "expecting max size %lu"
                ", got size %lu",
                pkt_len + 2, (ulong) (end - (char*) (net->read_pos + pkt_len + 2)));
        return TRUE;
    }

    {
        char *user, *passwd, *db;
        char db_buff[NAME_LEN+1];
        char user_buff[USERNAME_LENGTH+1];
        uint user_len, passwd_len, db_len, dummy_errors;

        /* Grab auth data from packet */
        user = end;
        passwd = strend(user) + 1;
        user_len = passwd - user - 1;
        db = passwd;

        passwd_len = (uchar) (*passwd++); /* XXX: ignoring old-school auth */
        db = client_caps & CLIENT_CONNECT_WITH_DB ? db + passwd_len + 1 : 0;
        db_len = db ? strlen(db) : 0;

        if (passwd + passwd_len + db_len > (char*) net->read_pos + pkt_len) {
            proxy_log(LOG_ERROR, "Client sent oversized auth packet");
            return TRUE;
        }

        /* If a DB was specified, read it */
        if (db) {
            db_buff[copy_and_convert(user_buff, sizeof(user_buff)-1,
                    system_charset_info, db, db_len, charset, &dummy_errors)] = '\0';
            db = db_buff;
        }

        /* Read username */
        user_buff[user_len = copy_and_convert(user_buff, sizeof(user_buff)-1,
                system_charset_info, user, user_len, charset, &dummy_errors)] = '\0';
        user = user_buff;

        /* Strip quotes */
        if (user_len > 1 && user[0] == '\'' && user[user_len - 1] == '\'') {
            user[user_len - 1] = 0;
            user++;
            user_len -= 2;
        }

        /* Tell pre-4.1 clients to get lost */
#if 0
        if (passwd_len == SCRAMBLE_LENGTH_323)
            proxy_net_send_error(mysql, ER_NOT_SUPPORTED_AUTH_MODE,
                    "Clients using the pre-4.1 protocol are not supported");
#endif

        /* Authenticate the user */
        if (check_user(user, user_len, passwd, passwd_len, db, db_len)) {
            if (db && db[0]) {
                /* XXX: need to set DB here eventually */
            }
        } else {
            /* XXX: send error, not authenticated */
            proxy_net_send_error(mysql, ER_HANDSHAKE_ERROR, "Error authenticating user");
            return TRUE;
        }

        /* Ok, client. You're good to go */
        proxy_net_send_ok(mysql, 0, 0, 0);
    }

    return FALSE;
}

/**
 * Validate the user credentials. This currently accepts any credentials as valid.
 *
 * @param user       Username to check.
 * @param user_len   Length of the username.
 * @param passwd     Password to check.
 * @param passwd_len Length of the password.
 * @param db         Database to check.
 * @param db_len     Length of the database.
 *
 * @return TRUE if the client is authorized, FALSE otherwise.
 **/
my_bool check_user(
        __attribute__((unused)) char *user,
        __attribute__((unused)) uint user_len,
        __attribute__((unused)) char *passwd,
        __attribute__((unused)) uint passwd_len,
        __attribute__((unused)) char *db,
        __attribute__((unused)) uint db_len) {
    /* XXX: Not doing auth. see sql/sql_connect.cc:check_user */
    return TRUE;
}

/**
 * Initialize client data structures.
 *
 * @param clientfd Socket descriptor of client connection.
 *
 * @return A MYSQL structure ready to communicate with the client, or NULL on error.
 **/
MYSQL* client_init(int clientfd) {
    Vio *vio_tmp;
    int optval;
    MYSQL *mysql;
    NET *net;

    /* derived from sql/mysqld.cc:handle_connections_sockets */
    vio_tmp = vio_new(clientfd, VIO_TYPE_TCPIP, 0);
    vio_fastsend(vio_tmp);

    /* Enable TCP keepalive which equivalent to
     * to vio_keepalive(vio_tmp, TRUE) plus setting
     *   tcp_keepalive_probes = 4
     *   tcp_keepalive_time   = 60
     *   tcp_keepalive_intvl  = 60 */
    optval = 1;
    setsockopt(vio_tmp->sd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
    optval = 4;
    setsockopt(vio_tmp->sd, SOL_TCP, TCP_KEEPCNT, &optval, sizeof(optval));
    optval = 60;
    setsockopt(vio_tmp->sd, SOL_TCP, TCP_KEEPIDLE, &optval, sizeof(optval));
    setsockopt(vio_tmp->sd, SOL_TCP, TCP_KEEPINTVL, &optval, sizeof(optval));

    /* Initialize a MySQL object */
    mysql = mysql_init(NULL);
    if (mysql == NULL) {
        proxy_log(LOG_ERROR, "Out of memory when allocating proxy server");
        return NULL;
    }

    /* Initialize network structures */
    mysql->protocol_version = PROTOCOL_VERSION;
    mysql->server_version   = "5.1-sfsql_proxy";

    /* Initialize the client network structure */
    net = &(mysql->net);
    net->max_packet += ID_SIZE;
    my_net_init(net, vio_tmp);
    my_net_set_write_timeout(net, NET_WRITE_TIMEOUT);
    my_net_set_read_timeout(net, NET_READ_TIMEOUT);

    return mysql;
}

/**
 * Destroy all data structures associated with a thread.
 *
 * @param thread Thread to destroy.
 **/
void client_destroy(proxy_thread_t *thread) {
    MYSQL *mysql;

    proxy_debug("Called client_destroy on thread %d", thread->id);

    /* Clean up connection if still live */
    if (thread->data.work.proxy) {
        if ((mysql = thread->data.work.proxy)) {
            /* XXX: may need to send error before closing connection */
            /* derived from sql/sql_mysqld.cc:close_connection */
            if (vio_close(mysql->net.vio) < 0)
                proxy_log(LOG_ERROR, "Error closing client connection: %s", errstr);

            /* Clean up data structures */
            vio_delete(mysql->net.vio);
            mysql->net.vio = 0;
            net_end(&(mysql->net));
            mysql_close(mysql);
        }

        thread->data.work.proxy = NULL;
    }
}

/**
 * Destroy all data structures associated with the thread
 * and additional shut down the MySQL library for the thread.
 *
 * thread->status = (status_t*) malloc(sizeof(status_t));
 *
 * @param ptr A pointer to a #proxy_thread_t struct
 *            corresponding to the thread to be destroyed.
 **/
void net_thread_destroy(void *ptr) {
    proxy_thread_t *thread = (proxy_thread_t*) ptr;
    client_destroy(thread);

    /* Free any remaining resources */
    mysql_thread_end();
    free(thread->status);
}

/**
 * Create a new thread to service client requests
 *
 * @param ptr A pointer to a #proxy_thread_t struct which
 *            contains information on available work
 *
 * @return NULL.
 **/
void* proxy_net_new_thread(void *ptr) {
    proxy_thread_t *thread = (proxy_thread_t*) ptr;
    commitdata_t commit;

    /* Initialize status information for the connection */
    thread->status = (status_t*) malloc(sizeof(status_t));
    thread->status->bytes_sent = 0;
    thread->status->bytes_recv = 0;
    thread->status->queries = 0;

    proxy_threading_mask();
    pthread_cleanup_push(net_thread_destroy, ptr);
    proxy_mutex_lock(&(thread->lock));

    while (1) {
        if(thread->exit) {
            proxy_mutex_unlock(&(thread->lock));
            break;
        }

        /* Wait for work to be available */
        while (!thread->data.work.addr)
            proxy_cond_wait(&(thread->cv), &(thread->lock));

        proxy_debug("Thread %d signaled", thread->id);

        /* If no work specified, must be ready to exit */
        if (thread->data.work.addr == NULL) {
            proxy_mutex_unlock(&(thread->lock));
            break;
        }

        /* Handle client requests */
        (void) __sync_fetch_and_add(&global_connections, 1);
        client_do_work(&thread->data.work, thread->id, &commit, thread->status);
        client_destroy(thread);
        thread->data.work.addr = NULL;

        /* Update global statistics */
        (void) __sync_fetch_and_add(&global_status.bytes_sent, thread->status->bytes_sent);
        (void) __sync_fetch_and_add(&global_status.bytes_sent, thread->status->bytes_recv);
        (void) __sync_fetch_and_add(&global_status.queries, thread->status->queries);
        thread->status->bytes_sent = 0;
        thread->status->bytes_recv = 0;
        thread->status->queries = 0;

        /* Signify that we are available for work again */
        proxy_pool_return(thread_pool, thread->id);
    }

    proxy_debug("Exiting loop on client thead %d", thread->id);

    pthread_cleanup_pop(1);
    pthread_exit(NULL);
}

/**
 * Service a client request.
 *
 * @param work              Information on the work to be done.
 * @param thread_id         Identifier of the client thread issuing
 *                          the request.
 * @param commit            Information required to commit which is
 *                          passed to the backend.
 * @param[in,out] status    Status information for the connection.
 **/
void client_do_work(proxy_work_t *work, int thread_id, commitdata_t *commit, status_t *status) {
    int error;

    if (unlikely(!work))
        return;

    work->proxy = client_init(work->clientfd);
    if (work->proxy == NULL)
        return;

    /* Perform "authentication" (credentials not checked) */
    if (proxy_net_handshake(work->proxy, work->addr, 0))
        return;

    /* from sql/sql_connect.cc:handle_one_connection */
    while (!work->proxy->net.error && work->proxy->net.vio != 0) {
        error = proxy_net_read_query(work->proxy, thread_id, commit, status);

        /* One more flush the write buffer to make
         * sure client has everything */
        proxy_net_flush(work->proxy);

        if (error != ERROR_OK) {
            switch (error) {
                case ERROR_CLOSE:
                    return;
                case ERROR_CLIENT:
                    proxy_log(LOG_ERROR, "Error from client when processing query");
                    return;
                case ERROR_BACKEND:
                    proxy_log(LOG_ERROR, "Error from backend when processing query");
                    return;
                case ERROR_OTHER:
                default:
                    proxy_log(LOG_ERROR, "Error in processing query, disconnecting");
                    return;
            }
        }
    }
}

/**
 * Read a query from a client connection and take
 * appropriate action.
 *
 * This code is derived from sql/sql_parse.cc:do_command
 *
 * @callgraph
 *
 * @param mysql          A MySQL object for a client which
 *                       a query should be read from.
 * @param thread_id      Identifier of the client thread
 *                       issuing the query.
 * @param commit         Information required to commit 
 *                       which is passed to the backend.
 * @param[in,out] status Status information for the connection.
 *
 * @return Positive to disconnect without error, negative
 *         for errors, 0 to keep going.
 **/
conn_error_t proxy_net_read_query(MYSQL *mysql, int thread_id, commitdata_t *commit, status_t *status) {
    NET *net = &(mysql->net);
    ulong pkt_len;
    char *packet = 0;
    enum enum_server_command command;
    struct pollfd polls[1];

    /* Ensure we have a valid MySQL object */
    if (unlikely(!mysql)) {
        proxy_log(LOG_ERROR, "Invalid MySQL object for reading query");
        return ERROR_OTHER;
    }

    /* Start a new transaction and read the incoming packet */
    net_new_transaction(net);

    /* Wait for new data */
    polls[0].fd = net->vio->sd;
    polls[0].events = POLLIN | POLLRDHUP;

    if (poll(polls, 1, options.timeout * 1000) != 1)
        proxy_log(LOG_ERROR, "Error in waiting on socket data");

    /* Check if the connection is gone */
    if (polls[0].revents & POLLRDHUP) {
        proxy_log(LOG_ERROR, "Lost connection to client");
        return ERROR_CLIENT;
    }

    if ((pkt_len = my_net_read(net)) == packet_error) {
        proxy_log(LOG_ERROR, "Error reading query from client: %s", mysql_error(mysql));
        return ERROR_CLIENT;
    }

    proxy_debug("Read %lu byte packet from client", pkt_len);
    status->bytes_recv += pkt_len;

    packet = (char*) net->read_pos;
    if (unlikely(pkt_len == 0)) {
        packet[0] = (uchar) COM_SLEEP;
        pkt_len = 1;
    }

    /* Pull the command out of the packet */
    command = (enum enum_server_command) (uchar) packet[0];
    packet[pkt_len] = '\0';
    packet++; pkt_len--;

    /* Reset server status flags */
    mysql->server_status &= ~SERVER_STATUS_CLEAR_SET;

    proxy_debug("Got command %d", command);

    switch (command) {
        case COM_PROXY_QUERY:
            /* XXX: for now, we do nothing different here */
            return proxy_backend_query(mysql, thread_id, packet, pkt_len, commit, status) ? ERROR_BACKEND : ERROR_OK;
        case COM_QUERY:
            status->queries++;

            if (strncasecmp(packet, PROXY_CMD, sizeof(PROXY_CMD)-1)) {
                /* pass the query to the backend */
                return proxy_backend_query(mysql, thread_id, packet, pkt_len, commit, status) ? ERROR_BACKEND : ERROR_OK;
            } else {
                /* Execute the proxy command */
                return proxy_cmd(mysql, packet + sizeof(PROXY_CMD)-1, pkt_len, status) ? ERROR_CLIENT : ERROR_OK;
            }
        case COM_QUIT:
            return ERROR_CLOSE;
        case COM_PING:
            /* Yep, still here */
            return proxy_net_send_ok(mysql, 0, 0, 0) ? ERROR_CLIENT : ERROR_OK;
        case COM_INIT_DB:
            /* XXX: using a single DB for now */
            proxy_net_send_error(mysql, ER_NOT_ALLOWED_COMMAND, "Only a single database is supported by " PACKAGE_NAME);

        /* Commands below not implemented */
        case COM_REGISTER_SLAVE:
        case COM_TABLE_DUMP:
        case COM_CHANGE_USER:
        case COM_STMT_EXECUTE:
        case COM_STMT_FETCH:
        case COM_STMT_PREPARE:
        case COM_STMT_CLOSE:
        case COM_STMT_RESET:
        case COM_FIELD_LIST:
        case COM_CREATE_DB:
        case COM_DROP_DB:
        case COM_BINLOG_DUMP:
        case COM_REFRESH:
        case COM_SHUTDOWN:
        case COM_STATISTICS:
        case COM_PROCESS_INFO:
        case COM_PROCESS_KILL:
        case COM_SET_OPTION:
        case COM_DEBUG:
        case COM_SLEEP:
        case COM_CONNECT:
        case COM_TIME:
        case COM_DELAYED_INSERT:
        case COM_END:
        default:
            proxy_net_send_error(mysql, ER_NOT_ALLOWED_COMMAND, "Command currently not supported by " PACKAGE_NAME);
            return ERROR_OK;
    }
}

/**
 * Send an OK packet to a connected client.
 *
 * This code is derived from sql/protocol.cc:net_send_ok.
 *
 * @param mysql          MySQL object the ok should be sent to.
 * @param warnings       Number of warnings produced by the previous command.
 * @param affected_rows  Number of rows affected by the previous command.
 * @param last_insert_id ID of the last row to be inserted by the previous command.
 *
 * @return TRUE on error, FALSE otherwise.
 **/
my_bool proxy_net_send_ok(MYSQL *mysql, uint warnings, ulong affected_rows, ulonglong last_insert_id) {
    NET *net = &(mysql->net);
    uchar buff[MYSQL_ERRMSG_SIZE + 10], *pos;

    buff[0] = 0;
    pos = net_store_length(buff + 1, affected_rows);
    pos = net_store_length(pos, last_insert_id);

    /* XXX: ignoring 4.0 protocol */

    int2store(pos, mysql->server_status);
    pos += 2;

    int2store(pos, min(warnings, 65535));
    pos += 2;

    /* XXX: ignore messages for now
    if (message && message[0])
        pos = net_store_data(pos, (uchar*), message, strlen(message));
    */

    /* Send an ok back to the client */
    if (my_net_write(net, buff, (size_t) (pos - buff))) {
        proxy_log(LOG_ERROR, "Error writing OK to client");
        return TRUE;
    } else {
        return proxy_net_flush(mysql);
    }
}

/**
 * Send an an error message to a connected client.
 *
 * @param mysql     MYSQL object where the error should be sent.
 * @param sql_errno MySQL error code.
 * @param err       Error message string.
 *
 * @return TRUE on error, FALSE otherwise.
 **/
my_bool proxy_net_send_error(MYSQL *mysql, int sql_errno, const char *err) {
    /* derived from libmysql/lib_sql.cc:net_send_error_packet */
    NET *net = &(mysql->net);
    uint length;
    uchar *buff = alloca(2+1+SQLSTATE_LENGTH+MYSQL_ERRMSG_SIZE), *pos;

    if (unlikely(!net->vio))
        return FALSE;

#if PROTOCOL_VERSION > 9
    int2store(buff, sql_errno);
    pos = buff+2;
    length = (uint) (strmake((char*) pos, err, MYSQL_ERRMSG_SIZE-1) - (char*) buff);
    err = (char*) buff;
#else
    length = (uint) strlen(err);
    set_if_smaller(length, MYSQL_ERRMSG_SIZE-1);
#endif

    return net_write_command(net, (uchar) 255, (uchar*) "", 0, (uchar*) err, length);
}

/**
 * Send an EOF packet to connected client.
 *
 * @param mysql          MYSQL object where the EOF packet should be sent.
 * @param[in,out] status Status information for the connection.
 **/
void proxy_net_send_eof(MYSQL *mysql, status_t *status) {
    uchar buff[BUFSIZ], *pos;

    pos = buff;
    pos[0] = 0xfe; pos++;
    int2store(pos, 0);
    pos += 2;
    int2store(pos, 0);
    pos += 2;
    my_net_write(&mysql->net, buff, (size_t) (pos - buff));
    status->bytes_sent += pos-buff;
    proxy_net_flush(mysql);
}
