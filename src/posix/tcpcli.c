#include "posix/tcpcli.h"
#include "posix/stcpsc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "csp/csp.h"
#include "csp/csp_interface.h"
#include "csp/csp_endian.h"
#include "tcpp/tcpp.h"
#include "vcom_debug.h"

int tcpcli_fd = -1;
simple_msg_t tcpcli_out_msg = {0, NULL};

int tcpcli_init()
{
    csp_iflist_add(&csp_if_tcpcli);
    return CSP_ERR_NONE;
}

int tcpcli_received(int fd, const simple_msg_t *in_msg)
{
    if (fd != tcpcli_fd) {
        vd_error("TCPCL: fd missmatch %d != %d!", fd, tcpcli_fd);
        return -1;
    }

    csp_packet_t *packet = csp_buffer_get(in_msg->len);
    if (packet == NULL) {
        vd_error("TCPCL: Error to get CSP buffer len=%d!", in_msg->len);
        return -1;
    }

    //csp_hex_dump("cli rx", in_msg->data, (int) in_msg->len);

    memcpy((void *) (&packet->id), in_msg->data, in_msg->len);
    packet->id.ext = csp_ntoh32(packet->id.ext);
    packet->length = in_msg->len - 4;

    csp_qfifo_write(packet, &csp_if_tcpcli, NULL);
    return 0;
}

int tcpcli_start(const struct tcpcli_config * conf)
{
    assert(conf->address_port);

    tcpcli_fd = simple_tcp_client_start(conf->address_port, CSP_TCP_CLI_MTU, conf->def_port, tcpcli_received);
    if (tcpcli_fd < 0)
        return -1;
    tcpcli_out_msg.len = 0;
    tcpcli_out_msg.data = (unsigned char *) malloc(CSP_TCP_CLI_MTU);
    if (!tcpcli_out_msg.data) {
        return CSP_ERR_NOMEM;
    }

    if (conf->pp_hello) {
        struct tcpp_hello_frame hello = {
            .addr = csp_get_address(),
            .magic = csp_hton32(TCPP_HELLO_MAGIC),
            .password = { /* nil */ }
        };
        memcpy(hello.password, conf->pp_password, TCPP_PASSWORD_LEN);

        memcpy(tcpcli_out_msg.data, &hello, sizeof(struct tcpp_hello_frame));
        tcpcli_out_msg.len = sizeof(struct tcpp_hello_frame);
        csp_debug(CSP_PROTOCOL, "Sending TCPP hello via TCP cli\r\n");

        simple_tcp_send(tcpcli_fd, &tcpcli_out_msg);
    }

    return 0;
}

int tcpcli_done()
{
    if (tcpcli_fd < 0)
        return -1;

    if (simple_tcp_client_done() != 0)
        return -1;
    tcpcli_fd = -1;
    tcpcli_out_msg.len = 0;
    if (tcpcli_out_msg.data)
        free(tcpcli_out_msg.data);
    tcpcli_out_msg.data = NULL;
    return 0;
}

static int tcpcli_tx(const csp_route_t * ifroute, csp_packet_t *packet)
{
    (void) ifroute; //un-used

    if (tcpcli_fd < 0 || packet == NULL || packet->length + 4 >= CSP_TCP_CLI_MTU)
        return CSP_ERR_DRIVER;

    /* Convert header to network byte order */
    packet->id.ext = csp_hton32(packet->id.ext);

    memcpy(tcpcli_out_msg.data, &packet->id, packet->length + 4);
    tcpcli_out_msg.len = packet->length + 4;

    csp_debug(CSP_PROTOCOL, "Sending packet via TCP cli\r\n");

    //csp_hex_dump("cli tx", tcpcli_out_msg.data, (int) tcpcli_out_msg.len);

    simple_tcp_send(tcpcli_fd, &tcpcli_out_msg);

    csp_buffer_free(packet);
    return CSP_ERR_NONE;
}

/* Interface definition */
csp_iface_t csp_if_tcpcli = {
    .name = "TCPCL",
    .nexthop = tcpcli_tx,
    .mtu = CSP_TCP_CLI_MTU - 2,
};
