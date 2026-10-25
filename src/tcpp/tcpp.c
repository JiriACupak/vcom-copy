#include <socket_server.h>
#include <csp/arch/csp_malloc.h>
#include "tcpp.h"
#include <string.h>
#include <unistd.h>
#include <csp/csp_endian.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <csp/csp_rtable.h>
#include <csp/csp_debug.h>
#include <csp/csp_buffer.h>
#include <csp/csp.h>

#define TCPP_LOG(format, ...) csp_log_protocol("tcpp: " format, ##__VA_ARGS__)
#define TCPP_LOG_ERR(format, ...) csp_log_error("tcpp: " format, ##__VA_ARGS__)

/** The active server handle */
Tcpd_t g_tcpp_server = NULL;

/** client data received callback */
static tcpd_err_t client_read(Tcpd_t serv, TcpdClient_t client, int sockfd);

/** client open callback */
static tcpd_err_t client_open(Tcpd_t serv, TcpdClient_t client);

/** client closed callback */
static tcpd_err_t client_close(Tcpd_t serv, TcpdClient_t client);

#define MIN(a, b) ((a) < (b) ? (a) : (b))

/**
 * Start the TCP CSP server
 * @param ppcfg - config
 * @return
 */
int tcpp_start(const struct tcpp_config *ppcfg)
{
    struct tcpp_server_context *sctx = csp_malloc(sizeof(struct tcpp_server_context));
    if (!sctx) {
        return ESP_ERR_NO_MEM;
    }

    memcpy(sctx->password, ppcfg->password, TCPP_PASSWORD_LEN);

    tcpd_config_t cfg = TCPD_INIT_DEFAULT();

    cfg.max_clients = 32;
    cfg.port = TCPP_PORT;
    cfg.close_lru = false;
    cfg.sctx = sctx;
    cfg.sctx_free_fn = csp_free;

    cfg.read_fn = client_read;
    cfg.open_fn = client_open;
    cfg.close_fn = client_close;

    if (CSP_ERR_NONE != csp_iflist_add(&csp_if_tcpp)) {
        TCPP_LOG_ERR("err add tcpp iface");
    }

    return tcpd_init(&cfg, &g_tcpp_server);
}

/**
 * Socket read handler
 */
static tcpd_err_t client_read(Tcpd_t serv, TcpdClient_t client, int sockfd)
{
    TCPP_LOG("rx!\n");

    struct tcpp_client_ctx *ctx = tcpd_get_client_ctx(client);

#define BUFLEN (TCPP_MTU+2)
    uint8_t buf[BUFLEN];

    int nbytes = read(sockfd, buf, BUFLEN);
    if (nbytes <= 0) { return ESP_FAIL; }

    TCPP_LOG("read %d bytes", nbytes);
    //csp_hex_dump("rx", buf, nbytes);

    uint8_t *pbuf = buf;

    while (nbytes > 0) {
        if (ctx->frame_len == 0) {
            ctx->rxbuf[ctx->write_pos++] = *pbuf++;
            nbytes--;

            if (ctx->write_pos == 2) {
                ctx->frame_len =
                    (((uint16_t) ctx->rxbuf[0]) << 8)
                    | ((uint16_t) ctx->rxbuf[1]);

                TCPP_LOG("start rx of %d-byte frame", ctx->frame_len);
            } else {
                continue;
            }
        }

        uint16_t chunk = MIN(nbytes, (ctx->frame_len + 2) - ctx->write_pos);
        TCPP_LOG("add chunk %d", chunk);
        memcpy(&ctx->rxbuf[ctx->write_pos], pbuf, (size_t) chunk);
        ctx->write_pos += chunk;
        pbuf += chunk;
        nbytes -= chunk;

        if (ctx->write_pos == ctx->frame_len + 2) {
            TCPP_LOG("received frame of len %d", ctx->frame_len);

            if (ctx->addr == 0) {
                /* Client did not introduce itself yet - this is the "hello" frame */
                struct tcpp_hello_frame *hello = (void *) &ctx->rxbuf[2];

                if (csp_ntoh32(hello->magic) != TCPP_HELLO_MAGIC) {
                    TCPP_LOG_ERR("Hello magic mismatch");
                    return -1; // close the client
                }

                struct tcpp_server_context *sctx = tcpd_get_server_ctx(serv);
                assert(serv);

                if (0 != memcmp(sctx->password, hello->password, TCPP_PASSWORD_LEN)) {
                    TCPP_LOG_ERR("password mismatch");
                    return -1; // close the client
                }

                if (hello->addr == csp_get_address()) {
                    TCPP_LOG_ERR("client connects with our address!");
                    return -1; // close the client
                }

                ctx->addr = hello->addr;

                tcpd_kick_by_tag(serv, ctx->addr);
                tcpd_set_client_tag(client, ctx->addr);

                csp_route_set(ctx->addr, &csp_if_tcpp, CSP_NODE_MAC);

                TCPP_LOG("Client %d connected", ctx->addr);
            } else {
                uint16_t needed = sizeof(csp_packet_t) - sizeof(csp_id_t) + (ctx->frame_len);

                TCPP_LOG("rx from client id=%d", ctx->addr);

                TCPP_LOG("Need buf len %d", needed);

                csp_packet_t *packet = csp_buffer_get(needed);
                if (packet == NULL) {
                    TCPP_LOG_ERR("Error to get CSP buffer len=%d in tcpbridge_received!", needed);
                    return -1;
                }

                memcpy((void *) (&packet->id), &ctx->rxbuf[2], ctx->frame_len);
                packet->id.ext = csp_ntoh32(packet->id.ext);
                packet->length = ctx->frame_len - sizeof(csp_id_t);

                //csp_hex_dump("packet", (void *) packet, needed);

                TCPP_LOG("que packet (%d bytes) to csp", packet->length);
                csp_qfifo_write(packet, &csp_if_tcpp, NULL);
            }

            ctx->write_pos = 0;
            ctx->frame_len = 0;
        }
    }

    return ESP_OK;
}

/**
 * Socket open handler
 */
static tcpd_err_t client_open(Tcpd_t serv, TcpdClient_t client)
{
    (void)serv;
    TCPP_LOG("Client open");
    struct tcpp_client_ctx *ctx = csp_calloc(sizeof(struct tcpp_client_ctx), 1);
    if (!ctx) {
        return ESP_ERR_NO_MEM;
    }
    tcpd_set_client_ctx(client, ctx);
    return ESP_OK;
}

/**
 * Socket close handler
 */
static tcpd_err_t client_close(Tcpd_t serv, TcpdClient_t client)
{
    (void)serv;
    TCPP_LOG("Client closed.");
    struct tcpp_client_ctx *ctx = tcpd_get_client_ctx(client);
    if (ctx) {
        csp_free(ctx);
    }
    return ESP_OK;
}

/** CSP nexhop function */
static int tcpp_tx(const csp_route_t *ifroute, csp_packet_t *packet)
{
    if (packet == NULL || packet->length + 2 + sizeof(csp_id_t) >= TCPP_MTU) {
        TCPP_LOG_ERR("Bad tx packet");
        return CSP_ERR_DRIVER;
    }

    csp_debug(CSP_PROTOCOL, "Sending packet via TCPP");

    uint8_t via = (ifroute->via == CSP_NO_VIA_ADDRESS) ?
                  (uint8_t) packet->id.dst :
                  ifroute->via;

    TcpdClient_t client = tcpd_client_by_tag(g_tcpp_server, via);
    if (!client) {
        TCPP_LOG_ERR("No TCPP client with addr %d", via);
        return CSP_ERR_DRIVER;
    }

    static uint8_t sendbuf[TCPP_MTU];

    const int packetlen = packet->length + sizeof(csp_id_t);
    const int sendlen = packetlen + 2;

    sendbuf[0] = 0xFF & ((packetlen) >> 8);
    sendbuf[1] = 0xFF & packetlen;

    packet->id.ext = csp_hton32(packet->id.ext);
    memcpy(&(sendbuf[2]), &packet->id, packetlen);

    //csp_hex_dump("sending", sendbuf, sendlen);

    int fd = tcpd_get_client_fd(client);
    int rv = write(fd, sendbuf, sendlen);

    if (rv <= 0) {
        TCPP_LOG_ERR("er write");
        tcpd_kick(client);
    }

    TCPP_LOG("pp sent.\n");

    csp_buffer_free(packet);

    return CSP_ERR_NONE;
}

/** Shutdown the TCP CSP server */
void tcpp_shutdown(void)
{
    tcpd_shutdown(g_tcpp_server);
}

/* Interface definition */
csp_iface_t csp_if_tcpp = {
    .name = "TCPP",
    .nexthop = tcpp_tx,
    .mtu = TCPP_MTU,
};
