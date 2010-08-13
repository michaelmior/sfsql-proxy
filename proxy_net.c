#include "proxy.h"

#define MIN_HANDSHAKE_SIZE 6

extern CHARSET_INFO *default_charset_info;
CHARSET_INFO *system_charset_info = &my_charset_utf8_general_ci;

/* derived from sql/sql_connect.cc:check_connection */
void proxy_handshake(struct sockaddr_in *clientaddr, int thread_id) {
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
    server_caps = CLIENT_BASIC_FLAGS; /* XXX: transactions flag? get from backend? */
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

#if 0
        /* Authenticate the user */
        if proxy_check_user(user, user_len, passwd, passwd_len, db, db_len) {
            if (db && db[0]) {
                /* Set DB */
            }
        }
#endif

        /* Ok, client. You're good to go */
        proxy_send_ok(0, 0, 0, 0);
    }
}

my_bool proxy_check_user(char *user, uint user_len, char *passwd, uint passwd_len, char *db, uint db_len) {
    /* XXX: Not doing auth. see sql/sql_connect.cc:check_user */
    return TRUE;
}

/* derived from sql/protocol.cc:net_send_ok */
my_bool proxy_send_ok(uint status, uint warnings, ha_rows affected_rows, ulonglong last_insert_id) {
    NET *net = &(mysql->net);
    uchar buff[MYSQL_ERRMSG_SIZE + 10], *pos;
    my_bool error = FALSE;

    buff[0] = 0;
    pos = net_store_length(buff + 1, 0);
    pos = net_store_length(pos, 0);

    /* XXX: not tracking status */
    int2store(pos, 0);
    pos += 1;

    /* XXX: 0 warnings */
    int2store(pos, 0);
    pos += 2;

    /* Send an ok back to the client */
    if (my_net_write(net, buff, (size_t) (pos - buff))) {
        proxy_error("Error writing OK to client");
    } else {
        error = net_flush(net);
    }

    return error;
}
