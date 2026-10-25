#include "posix/tcpbridge.h"
#include "posix/stcpsc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "csp/csp.h"
#include "csp/csp_interface.h"
#include "csp/csp_endian.h"
#include "vcom_debug.h"

#define CSP_TCP_BRIDGE_PORT    9210
#define CSP_TCP_BRIDGE_MTU     500

int cli_number;
int tcpbridge_port;
simple_msg_t tcpbridge_out_msg = {0, NULL};
char *peer_adr = NULL;

int tcpbridge_received(int client, const simple_msg_t *in_msg)
{
    (void) client; //un-used

    csp_packet_t *packet = csp_buffer_get(in_msg->len);
    if (packet == NULL) {
        vd_error("Error to get CSP buffer len=%d in tcpbridge_received!", in_msg->len);
        return -1;
    }

    memcpy((void *) (&packet->id), in_msg->data, in_msg->len);
    packet->id.ext = csp_ntoh32(packet->id.ext);
    packet->length = in_msg->len - 4;

//    csp_new_packet(packet, &csp_if_tcpbridge, NULL);
    csp_qfifo_write(packet, &csp_if_tcpbridge, NULL);
    return 0;
}

int tcpbridge_cli_connected(int client, const char *address)
{
    if (cli_number >= 0) {
        vd_error("TCP client[%d] kicked (%s). Only one connection allowed!", client, address);
        return -1;
    }

    peer_adr = strdup(address);
    vd_info("TCP client[%d] connected (from %s)", client, peer_adr);
    cli_number = client;
    tcpbridge_out_msg.len = 0;
    tcpbridge_out_msg.data = (unsigned char *) malloc(CSP_TCP_BRIDGE_MTU);
    return 0;
}

void tcpbridge_cli_disconnected(int client, int reason)
{
    vd_info("TCP client disconnected");
    (void) client; //un-used
    cli_number = -1;
    if (tcpbridge_out_msg.data)
        free(tcpbridge_out_msg.data);
    tcpbridge_out_msg.data = NULL;
    tcpbridge_out_msg.len = 0;
    if (peer_adr) {
        free(peer_adr);
        peer_adr = NULL;
    }
    switch (reason) {
        case STCP_DISCONNECT_REASON_ERROR:
            vd_info("TCP client[%d] disconnected becouse of read error.", client);
            break;
        case STCP_DISCONNECT_REASON_INTERNAL:
            vd_info("TCP client[%d] disconnected bocouse of internal error.", client);
            break;
        case STCP_DISCONNECT_REASON_MTU:
            vd_info("TCP client[%d] disconnected becouse MTU exceeded.", client);
            break;
        case STCP_DISCONNECT_REASON_KICK:
            vd_info("TCP client[%d] kicked.", client);
            break;
        case STCP_DISCONNECT_REASON_SERVER_CLOSE:
            vd_info("TCP client[%d] closed.", client);
            break;
        default: //STCP_DISCONNECT_REASON_CLOSED:
            vd_info("TCP client[%d] disconnected.", client);
    }
}

int tcpbridge_init(int port)
{
    if (port > 0 && port < 0xFFFF)
        tcpbridge_port = port;
    else
        tcpbridge_port = CSP_TCP_BRIDGE_PORT;
    csp_iflist_add(&csp_if_tcpbridge);
    return CSP_ERR_NONE;
}

int tcpbridge_start()
{
    cli_number = -1;
    return simple_tcp_server_start(tcpbridge_port, CSP_TCP_BRIDGE_MTU,
                                   tcpbridge_received, tcpbridge_cli_connected, tcpbridge_cli_disconnected);
}

int tcpbridge_done()
{
    cli_number = -1;
    return simple_tcp_server_stop();
}

int tcpbridge_tx(const csp_route_t * ifroute, csp_packet_t *packet)
{
    (void) ifroute; //un-used

    if (cli_number < 0 || packet == NULL || packet->length + 4 >= CSP_TCP_BRIDGE_MTU)
        return CSP_ERR_DRIVER;

    /* Convert header to network byte order */
    packet->id.ext = csp_hton32(packet->id.ext);

    memcpy(tcpbridge_out_msg.data, &packet->id, packet->length + 4);
    tcpbridge_out_msg.len = packet->length + 4;

    csp_debug(CSP_PROTOCOL, "Sending packet via TCP bridge\r\n");

    simple_tcp_send(cli_number, &tcpbridge_out_msg);

    csp_buffer_free(packet);

    return CSP_ERR_NONE;
}

/* Interface definition */
csp_iface_t csp_if_tcpbridge = {
    .name = "TCPBR",
    .nexthop = tcpbridge_tx,
    .mtu = CSP_TCP_BRIDGE_MTU - 4,
};
