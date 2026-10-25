/**
 * TCP P2P CSP connection
 *
 * Created on 2020/10/02.
 */

#ifndef VCOM_TCPP_H
#define VCOM_TCPP_H

#include <socket_server.h>
#include <csp/csp_interface.h>

// TCP port (default)
#define TCPP_PORT 9211
// Iface MTU
#define TCPP_MTU 500
// Length of the password field
#define TCPP_PASSWORD_LEN 8
// Magic, identifies the "hello" packet
#define TCPP_HELLO_MAGIC 0xfce9f929ul

/** State struct allocated for each connected client */
struct tcpp_client_ctx {
    uint8_t addr;
    uint16_t frame_len;
    uint16_t write_pos;
    uint8_t rxbuf[TCPP_MTU];
};

/** Server context struct */
struct tcpp_server_context {
    uint8_t password[TCPP_PASSWORD_LEN];
};

extern csp_iface_t csp_if_tcpp;
extern Tcpd_t g_tcpp_server;

struct __attribute__((packed)) tcpp_hello_frame {
    uint8_t addr;
    uint8_t _pad1;
    uint32_t magic;
    uint8_t password[TCPP_PASSWORD_LEN];
};

struct tcpp_config {
    uint8_t password[TCPP_PASSWORD_LEN + 1]; // +1 for terminator
    /** Allowed peer address range - start */
    uint8_t allowed_addr_range_start;
    /** Allowed peer address range - end (inclusive) */
    uint8_t allowed_addr_range_end;
};

/**
 * Start the server
 *
 * @return 0=success
 */
int tcpp_start(const struct tcpp_config *cfg);

void tcpp_shutdown(void);

#endif //VCOM_TCPP_H
