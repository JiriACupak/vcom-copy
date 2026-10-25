/**
 * \file stcpsc.h
 * \brief Simple-to-use TCP server/client
 * \author Petr Svoboda, 2014-10-11
 */

#ifndef SIMPLE_TCP_SERVER_CLI_H
#define SIMPLE_TCP_SERVER_CLI_H

#define STCP_DISCONNECT_REASON_CLOSED       -1
#define STCP_DISCONNECT_REASON_ERROR        -2
#define STCP_DISCONNECT_REASON_INTERNAL     -3
#define STCP_DISCONNECT_REASON_MTU          -4
#define STCP_DISCONNECT_REASON_KICK         -5
#define STCP_DISCONNECT_REASON_SERVER_CLOSE -6

typedef struct {
  unsigned len;
  unsigned char* data;
} simple_msg_t;

typedef int  (*msg_received_fce_t)(int fd, const simple_msg_t* msg);
typedef int  (*cli_connected_fce_t)(int fd, const char* address);
typedef void (*cli_disconnected_fce_t)(int fd, int reason);

extern int simple_tcp_server_start(unsigned short port, int mtu, msg_received_fce_t rec, cli_connected_fce_t con, cli_disconnected_fce_t dis);
extern int simple_tcp_server_stop();

extern int simple_tcp_client_start(const char* address_port, int mtu, int def_port, msg_received_fce_t rec);
extern int simple_tcp_client_done();

extern int simple_tcp_send(int fd, const simple_msg_t* msg);

#endif // SIMPLE_TCP_SERVER_CLI_H
