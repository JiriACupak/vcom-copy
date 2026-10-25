/**
 * Generic implementation of a TCP socket server.
 */

#ifndef _SOCKET_SERVER_H_
#define _SOCKET_SERVER_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#ifndef ESP_PLATFORM
#define ESP_OK          0       /*!< esp_err_t value indicating success (no error) */
#define ESP_FAIL        (-1)      /*!< Generic esp_err_t code indicating failure */
#define ESP_ERR_NO_MEM              0x101   /*!< Out of memory */
#define ESP_ERR_INVALID_ARG         0x102   /*!< Invalid argument */
#define ESP_ERR_INVALID_STATE       0x103   /*!< Invalid state */
#define ESP_ERR_INVALID_SIZE        0x104   /*!< Invalid size */
#define ESP_ERR_NOT_FOUND           0x105   /*!< Requested resource not found */
#define ESP_ERR_NOT_SUPPORTED       0x106   /*!< Operation or feature not supported */
#define ESP_ERR_TIMEOUT             0x107   /*!< Operation timed out */
#else
#include <esp_err.h>
#include "sdkconfig.h"
#endif

typedef struct sockd_server *Tcpd_t;
typedef struct sockd_client *TcpdClient_t;
typedef int tcpd_err_t;

#define FD_NONE (-1)

/**
 * Socket read handler
 */
typedef tcpd_err_t (*tcpd_read_fn_t)(Tcpd_t serv, TcpdClient_t client, int sockfd);

/**
 * Socket open handler
 */
typedef tcpd_err_t (*tcpd_open_fn_t)(Tcpd_t serv, TcpdClient_t client);

/**
 * Socket close handler
 */
typedef tcpd_err_t (*tcpd_close_fn_t)(Tcpd_t serv, TcpdClient_t client);

/**
 * Function called during server shutdown to free the server context
 */
typedef void (*sockd_sctx_free_fn_t)(void * sctx);

/**
 * Server config structure
 */
typedef struct tcpd_config {
    uint16_t port;          //!< Server port
    uint16_t max_clients;   //!< Max number of connected clients
    bool close_lru;         //!< Close the least recently used client when a new connection is received
    bool start_immediately; //!< If true, start the server immediately after init, otherwise it starts paused

    void *sctx;                         //!< Server context (arbitrary user data accessible from the callbacks)
    sockd_sctx_free_fn_t sctx_free_fn;  //!< Context freeing function (no-op if NULL)

    tcpd_read_fn_t  read_fn;    //!< Callback to read data from a socket.
    tcpd_open_fn_t  open_fn;    //!< Callback to init a new client connection. Can set the client tag or handle.
    tcpd_close_fn_t close_fn;   //!< Callback when a client left or is kicked. Can free the client context.

    const char *task_name;  //!< Server task name
    uint32_t task_stack;    //!< Server stack size
    uint8_t task_prio;      //!< Server priority
} tcpd_config_t;

#define TCPD_INIT_DEFAULT() \
{                               \
    .port = 23,                 \
    .max_clients = 1,           \
    .close_lru = true,          \
    .start_immediately = true,  \
                                \
    .sctx = NULL,               \
    .sctx_free_fn = NULL,       \
                                \
    .read_fn = NULL,            \
    .open_fn = NULL,            \
    .close_fn = NULL,           \
                                \
    .task_name = "socksrv",     \
    .task_stack = 2048,         \
    .task_prio = 3,             \
}

struct tcpd_client_iter {
    Tcpd_t server;
    uint16_t next;
};

/** Initializer for the client iterator */
tcpd_err_t tcpd_iter_init(struct tcpd_client_iter *iter, Tcpd_t serv);

/** Iterate active clients. Returns NULL if no more clients were found. */
TcpdClient_t tcpd_client_iter_next(struct tcpd_client_iter *iterator);

/**
 * Get server context. The context was defined in the config object.
 *
 * @param serv - server handle
 * @return server context
 */
void *tcpd_get_server_ctx(Tcpd_t serv);

/**
 * Get client context, set by socksrv_set_client_ctx()
 *
 * @param client - client handle
 * @return context object
 */
void *tcpd_get_client_ctx(TcpdClient_t client);

/**
 * Set client context. If allocated, it should be freed by the client close function.
 *
 * @param client - client handle
 * @param cctx - context object
 */
void tcpd_set_client_ctx(TcpdClient_t client, void *cctx);

/**
 * Get client tag.
 *
 * @param client - client handle
 * @return tag value
 */
uint32_t tcpd_get_client_tag(TcpdClient_t client);

/**
 * Set client tag. Tag may be used alongside the client context e.g. to distinguish
 * context type.
 *
 * @param client - client handle
 * @param tag - tag value
 */
void tcpd_set_client_tag(TcpdClient_t client, uint32_t tag);

/**
 * Get client IP address
 *
 * @param client - client
 * @return address struct or NULL
 */
const struct sockaddr_in * tcpd_get_client_addr(TcpdClient_t client);

/**
 * Get client FD
 *
 * @param client
 * @return fd
 */
int tcpd_get_client_fd(TcpdClient_t client);

/**
 * Kick a single client.
 * This may be called even when the server is stopped.
 *
 * The client handle should be considered invalid after this call,
 * as it may be reused for another incoming connection.
 * Set it to NULL for safety.
 *
 * @param client - client handle (obtained e.g. as an argument in the receive function)
 */
void tcpd_kick(TcpdClient_t client);

/**
 * Kick all connected clients.
 * This may be called even when the server is stopped.
 *
 * @param serv - server handle
 */
void tcpd_kick_all(Tcpd_t serv, bool with_injected);

/* kick all clients. Returns kicked count, or -1 on err */
int tcpd_kick_by_tag(Tcpd_t serv, uint32_t tag);

/**
 * Inject a client with a custom FD (e.g. STDIN, other UART socket...).
 *
 * Injecting STDIN will automatically use STDOUT for outgoing messages.
 *
 * @param server
 * @param fd
 * @return the client, NULL on failure
 */
TcpdClient_t tcpd_inject_client(Tcpd_t server, int fd);

/**
 * Initialize and start the socket server.
 *
 * @param config - config struct (will be copied into the server handle, can be only on stack)
 * @param handle - pointer where to store the server handle.
 * @return success
 */
tcpd_err_t tcpd_init(const tcpd_config_t *config, Tcpd_t *handle);

/**
 * Shutdown the server, close open sockets and free all allocated memory.
 * Client contexts can be freed in the close_fn, if it was defined in server config.
 * It will be called for all still open sockets.
 *
 * The server context will be freed using the user-provided free function (set in config)
 *
 * The server handle should be considered invalid after calling this function.
 * The same applies to all existing client handles. Set it to NULL for safety.
 *
 * @param serv - server handle
 */
void tcpd_shutdown(Tcpd_t serv);

/**
 * Stop the server loop. It won't accept any connection requests nor data.
 * A stopped server can be resumed again.
 *
 * Does nothing if the server is already stopped.
 *
 * @param serv - server handle
 */
void tcpd_suspend(Tcpd_t serv);

/**
 * Start the server after it has been stopped.
 *
 * Does nothing if the server is already running.
 *
 * The server runs immediately after init,
 * so this does not need to be called to start it.
 *
 * @param serv - server handle
 */
void tcpd_resume(Tcpd_t serv);

/**
 * Send data to all connected clients
 *
 * @param serv - server handle
 * @param buffer - data to send
 * @param len - data length; if negative, treat data as a string and use strlen()
 */
tcpd_err_t tcpd_broadcast(Tcpd_t serv, const uint8_t *data, ssize_t len);

/**
 * Send a message to a single client
 *
 * @param client - client handle
 * @param data - bytes to send
 * @param len - length or -1 for strlen
 * @return success
 */
tcpd_err_t tcpd_send(TcpdClient_t client, const uint8_t *data, ssize_t len);

/**
 * Get client slot by FD. Returns NULL if not found.
 *
 * @param serv - server struct
 * @param sockfd
 * @return
 */
TcpdClient_t tcpd_client_by_fd(Tcpd_t serv, int sockfd);

/**
 * Get client by tag. Returns NULL if not found.
 *
 * @param serv - server struct
 * @param sockfd
 * @return
 */
TcpdClient_t tcpd_client_by_tag(Tcpd_t serv, uint32_t tag);

#endif //_SOCKET_SERVER_H_
