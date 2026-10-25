#include <string.h>
#include <stdio.h>

#include "csp_server.h"
#include "csp/csp.h"
#include "csp/csp_cmp.h"
#include "vcom_config.h"
#include "vcom_debug.h"

static volatile bool running = false;
static volatile bool shutdown_flag = false;

void csp_server_done()
{
    shutdown_flag = true;
}

void *csp_server(void *parameters)
{
    (void)parameters;

    if (running) {
        vd_warn("csp_server task already started!");
        return NULL;
    }

    running = true;
    csp_socket_t *sock = csp_socket(CSP_SO_NONE);
    csp_bind(sock, CSP_ANY);
    csp_listen(sock, CSP_CONNECTION_BACKLOG);

    csp_conn_t *conn = NULL;
    csp_packet_t *packet = NULL;

    if (!vcom_config.use_nii) {
        vd_info("csp_server task waiting for connections");
    }

    while (!shutdown_flag) {
        if (NULL == (conn = csp_accept(sock, 1000))) {
            continue; // check the shutdown flag and wait again
        }

        if (csp_conn_flags(conn) & CSP_FRDP) {
            /* TODO fork handling to a separate thread */
        }

        /* Read packet from the socket */
        while ((packet = csp_read(conn, 0)) != NULL && !shutdown_flag) {
            int port = csp_conn_dport(conn);

            /*
              If a packet buffer is re-used to send the reply, it MUST NOT be freed - CSP frees it internally.
              If we discard the packet, free it manually.
            */

            switch (port) {
                /* Custom port handlers go here */

                case CSP_REBOOT:
                    // Discard reboot request
                    csp_buffer_free(packet);
                    break;

                case CSP_CMP:;
                    // Discard memory access requests
                    struct csp_cmp_message *cmp = (struct csp_cmp_message *) packet->data;
                    if (cmp->type == CSP_CMP_REQUEST) {
                        if (cmp->code == CSP_CMP_PEEK || cmp->code == CSP_CMP_POKE) {
                            csp_buffer_free(packet);
                            break;
                        }
                    }
                    /* fall-through */
                default: {
                    csp_service_handler(conn, packet);
                    break;
                }
            }
        }

        csp_close(conn);
    }

    if (!vcom_config.use_nii) {
        vd_info("csp_server task done.");
    }

    return NULL;
}

