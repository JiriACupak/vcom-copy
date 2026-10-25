#ifndef TCPCLIENT_H_
#define TCPCLIENT_H_

#include <stdbool.h>
#include <tcpp/tcpp.h>
#include "csp/csp_interface.h"

#define CSP_TCP_CLI_PORT    9209
#define CSP_TCP_CLI_MTU     500

int tcpcli_init();

struct tcpcli_config {
    /** IPv4 address with optional comma-separated port */
    const char* address_port;
    /** Default port, used if the address string does not specify it */
    uint16_t def_port;
    /** Send the TCPP hello packet after connecting */
    bool pp_hello;
    /** TCPP password, leave empty if unused */
    uint8_t pp_password[TCPP_PASSWORD_LEN + 1];
};

int tcpcli_start(const struct tcpcli_config *conf);
int tcpcli_done();

extern csp_iface_t csp_if_tcpcli;

#endif /* TCPCLIENT_H_ */
