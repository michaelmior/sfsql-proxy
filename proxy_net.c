#include "proxy.h"

#define MIN_HANDSHAKE_SIZE 6

extern CHARSET_INFO *default_charset_info;
CHARSET_INFO *system_charset_info = &my_charset_utf8_general_ci;

/* derived from sql/sql_connect.cc:check_connection */
void proxy_handshake(MYSQL *mysql, struct sockaddr_in *clientaddr, int thread_id) {
    NET *net;
    char ip[30], buff[SERVER_VERSION_LENGTH + 1 + SCRAMBLE_LENGTH + 1 + 64], scramble[SCRAMBLE_LENGTH + 1], *end;
    ulong server_caps, client_caps, pkt_len;
    struct rand_struct rand; /* XXX: reuse this */
    uint16 port = PROXY_PORT;
    CHARSET_INFO *charset;

    net = &mysql->net;

    vio_peer_addr(net->vio, ip, &port);
    vio_in_addr(net->vio, (struct in_addr*) &(clientaddr->sin_addr.s_addr));

    /* Save version number */
    end = strnmov(buff, mysql->server_version, SERVER_VERSION_LENGTH) + 1;
    int4store((uchar*) end, 0);
    end += 4;

    /* Generate scramble string */
    randominit(&rand, 0, 0); /* XXX: init elsewhere */
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
        proxy_error("Error sending handshake to client");
    }

    /* XXX: pre-4.1 protocol not supported (or even checked) */
    client_caps = uint2korr(net->read_pos);
    client_caps |= ((ulong) uint2korr(net->read_pos + 2)) << 16;
    //pkt_len = unint4korr(net->read_pos + 4); /* max_client_packet_length */
    charset = get_charset((uint) net->read_pos[8], MYF(0));
    end = (char*) net->read_pos + 32;

    client_caps &= server_caps;

    if (end >= (char*) net->read_pos + pkt_len + 2) {
        proxy_error("Error handshaking with client,"
                "expecting max size %d"
                ", got size %d",
                pkt_len + 2, end - (char*) (net->read_pos + pkt_len + 2));
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
            proxy_error("Client sent oversized auth packet");
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

        /* Authenticate the user */
        if (proxy_check_user(user, user_len, passwd, passwd_len, db, db_len)) {
            if (db && db[0]) {
                /* XXX: need to set DB here eventually */
            }
        } else {
            /* XXX: send error, not authenticated */
        }

        /* Ok, client. You're good to go */
        proxy_send_ok(mysql, 0, 0, 0, 0);
    }
}

my_bool proxy_check_user(char *user, uint user_len, char *passwd, uint passwd_len, char *db, uint db_len) {
    /* XXX: Not doing auth. see sql/sql_connect.cc:check_user */
    return TRUE;
}

/* derived from sql/sql_parse.cc:do_command */
/* returns positive to disconnect without error,
 * negative for errors, 0 to keep going */
int proxy_read_query(MYSQL *mysql) {
    NET *net = &(mysql->net);
    ulong pkt_len;
    char *packet = 0;
    enum enum_server_command command;

    /* Start a new transaction and read the incoming packet */
    net_new_transaction(net);
    if ((pkt_len = my_net_read(net)) == packet_error) {
        proxy_error("Error reading query from client");
        return -1;
    }

    printf("Read %lu byte packet from client\n", pkt_len);

    packet = (char*) net->read_pos;
    if (pkt_len == 0) {
        packet[0] = (uchar) COM_SLEEP;
        pkt_len = 1;
    }

    /* Pull the command out of the packet */
    command = (enum enum_server_command) (uchar) packet[0];
    packet[pkt_len] = '\0';
    packet++; pkt_len--;

    /* Reset server status flags */
    mysql->server_status &= ~SERVER_STATUS_CLEAR_SET;

    printf("Got command %d\n", command);

    switch (command) {
        case COM_INIT_DB:
            /* XXX: using a single DB for now */
            return -1;
            break;
        case COM_QUERY:
            /* pass the query to the backend */
            return proxy_backend_query(mysql, packet, pkt_len) ? -1 : 0;
            break;
        case COM_QUIT:
            return 1;
            break;
        case COM_PING:
            /* Yep, still here */
            return proxy_send_ok(mysql, 0, 0, 0, 0) ? -1 : 0;
            break;

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
            /* XXX: send error */
            return -1;
            break;
    }
}

/* derived from sql/protocol.cc:net_send_ok */
my_bool proxy_send_ok(MYSQL *mysql, uint status, uint warnings, ha_rows affected_rows, ulonglong last_insert_id) {
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
        proxy_error("Error writing OK to client");
        return TRUE;
    } else {
        return net_flush(net);
    }
}
