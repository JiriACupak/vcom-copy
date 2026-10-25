#include <string.h>
#include <csp/csp.h>
#include <csp/csp_endian.h>
#include <console/cmddef.h>
#include <socket_server.h>
#include "tcpp.h"
#include <arpa/inet.h>

int cmd_tcp_list(console_ctx_t *ctx, cmd_signature_t *reg)
{
    EMPTY_CMD_SETUP("List clients connected to the TCP CSP server");

    if (!g_tcpp_server) {
        console_color_printf(COLOR_RED, "TCP server not enabled!\n");
        return CONSOLE_ERR_NOT_POSSIBLE;
    }

    struct tcpd_client_iter iter;
    tcpd_iter_init(&iter, g_tcpp_server);

    console_color_printf(COLOR_BOLD, "CSP  IP:PORT\n");

    TcpdClient_t client = NULL;
    while (NULL != (client = tcpd_client_iter_next(&iter))) {
        struct tcpp_client_ctx *cctx = tcpd_get_client_ctx(client);
        const struct sockaddr_in *sockaddr = tcpd_get_client_addr(client);
        console_printf("%3d  %s:%d\n",
                 cctx->addr,
                 inet_ntoa(sockaddr->sin_addr),
                 ntohs(sockaddr->sin_port));
    }

    return CONSOLE_OK;
}

int cmd_tcp_kick(console_ctx_t *ctx, cmd_signature_t *reg)
{
    (void)ctx;

    static struct {
        struct arg_int *addr;
        struct arg_end *end;
    } args;

    if (reg) {
        args.addr = arg_int1(NULL, NULL, "<addr>", EXPENDABLE_STRING("Client CSP address"));
        args.end = arg_end(2);

        reg->argtable = &args;
        reg->help = EXPENDABLE_STRING("Kick a TCP client");
        return CONSOLE_OK;
    }

    if (!g_tcpp_server) {
        console_color_printf(COLOR_RED, "TCP server not enabled!\n");
        return CONSOLE_ERR_NOT_POSSIBLE;
    }

    int num = tcpd_kick_by_tag(g_tcpp_server, args.addr->ival[0]);
    if (num >= 0) {
        console_printf("Kicked %d clients.\n", num);
    }

    return CONSOLE_OK;
}

void register_cmd_tcp(void)
{
    console_group_add("tcp", "TCP CSP server");
    console_cmd_register(cmd_tcp_list, "tcp list");
    console_cmd_register(cmd_tcp_kick, "tcp kick");
}
