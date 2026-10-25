#ifndef TCPBRIDGE_H_
#define TCPBRIDGE_H_

#include "csp/csp_interface.h"

/**
 * Init function to register interface
 * @return CSP_ERR_NONE;
 */
int tcpbridge_init(int port);

int tcpbridge_start();
int tcpbridge_done();

/**
 * Interface definition
 */
extern csp_iface_t csp_if_tcpbridge;

#endif /* TCPBRIDGE_H_ */
