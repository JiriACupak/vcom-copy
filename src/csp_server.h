/**
 * CSP server task
 */

#ifndef CSP_SERVER_H
#define CSP_SERVER_H

/** Backlog queue len */
#define CSP_CONNECTION_BACKLOG 10

void csp_server_done(void);

void *csp_server(void *parameters);

#endif // CSP_SERVER_H
