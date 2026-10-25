#ifndef KISS_NET_H
#define KISS_NET_H

#include <pthread.h>

#include "csp/csp_interface.h"

#define KISS_NET_RX_TIMEOUT_MSEC        500
#define KISS_NET_RX_BUFFER_OVERHEAD     16

extern csp_iface_t csp_if_kiss_net;

/** KISS over network interface config */
typedef struct kiss_net_config {
    char *host_ip;
    uint16_t host_port;
} kiss_net_config_t;

/** KISS over network driver data */
typedef struct kiss_net_driver_data {
    int sock_fd;
    // The volatile is a lame attempt for atomicity. It is enough at the moment,
    // as it is only read in the thread. And it controls the termination of
    // while loop.
    volatile bool run;
    pthread_t rx_thread_handle;
} kiss_net_driver_data_t;

int kiss_net_start(kiss_net_config_t *);
void *kiss_net_rx(void *);
int kiss_net_done();

#endif
