//#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>

#ifndef ESP_PLATFORM

/* Posix with libcsp */

#include <csp/arch/csp_thread.h>
#include <csp/arch/csp_semaphore.h>
#define ThreadHandle csp_thread_handle_t
#define Mutex csp_mutex_t
#define mutex_lock(mutex, timeout) csp_mutex_lock(&(mutex), (timeout))
#define mutex_unlock(mutex) csp_mutex_unlock(&(mutex))
#define mutex_remove(mutex) csp_mutex_remove(&(mutex))
#define DEFINE_TASK(name) CSP_DEFINE_TASK(name)
#define thread_exit() do { csp_thread_exit(); return NULL; } while(0)
#define port_calloc(a,b) csp_calloc((a),(b))
#define port_free(p) csp_free(p)
#define port_thread_create(routine, thread_name, stack_size, parameters, priority, return_handle) \
    csp_thread_create(routine, thread_name, stack_size, parameters, priority, return_handle)
#define PORT_THREAD_CREATE_OK CSP_ERR_NONE
#define mutex_create_ok(handle) (CSP_SEMAPHORE_OK == csp_mutex_create(&(handle)))

#else

/* ESP_IDF (cspemu) */

#define ThreadHandle TaskHandle_t
#define Mutex QueueHandle_t
#define mutex_lock(mutex, timeout) xSemaphoreTake((mutex), (timeout))
#define mutex_unlock(mutex) xSemaphoreGive((mutex))
#define mutex_remove(mutex) vSemaphoreDelete((mutex))
#define DEFINE_TASK(name) void name(void *param)
#define thread_exit() do { vTaskDelete(NULL); return; } while(0)
#include <malloc.h>
#include "esp_log.h"
#define port_calloc(a,b) calloc((a),(b))
#define port_free(p) free(p)
#define port_thread_create(routine, thread_name, stack_size, parameters, priority, return_handle) \
    xTaskCreate(routine, thread_name, stack_size, parameters, priority, return_handle)
#define PORT_THREAD_CREATE_OK pdPASS
#define mutex_create_ok(handle) (NULL != (handle = xSemaphoreCreateMutex()))

#endif

#include "socket_server.h"

/**
 * Extra argument validations  - can be turned off in production to slightly
 * reduce code size and speed it up.
 */
#define TCPD_PARANOID_CHECKS 1

static const char *TAG = "tcpd";

#ifdef ESP_PLATFORM
#define TCPD_USE_RXLOCK 1
#include "esp_log.h"
#else
#define TCPD_USE_RXLOCK 0

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/fcntl.h>
#include <csp/arch/csp_malloc.h>

/* compat with the original csp emu code */

// TODO disable spammy logging
#define ESP_LOGE(tag, format, ...) fprintf(stderr, "[error] %s: "format"\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) fprintf(stderr, "[warn] %s: "format"\n", tag, ##__VA_ARGS__)
//#define ESP_LOGD(tag, format, ...) fprintf(stderr, "[debug] %s: "format"\n", tag, ##__VA_ARGS__)
//#define ESP_LOGI(tag, format, ...) fprintf(stderr, "[info] %s: "format"\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) do {} while(0)
#define ESP_LOGI(tag, format, ...) do {} while(0)

#define portMAX_DELAY UINT32_MAX
#endif

static void close_client(TcpdClient_t client, bool mutex_locked);

struct sockd_client {
    Tcpd_t serv; // backreference for easier API usage
    int fd;
    uint32_t usetime;
    void *cctx;
    uint32_t tag;
    struct sockaddr_in addr;
    // injected FDs are custom streams that can be used for reads, but can't be kicked due to LRU
    // this allows including e.g. stdin in the select
    bool injected;
};

struct sockd_server {
    struct tcpd_config config;
    int server_fd;
    fd_set active_fd_set;
    struct sockd_client *clients; // this is malloc'd
    uint32_t rxcnt; // activity counter (for tracking LRU sockets)
    ThreadHandle task;
#if TCPD_USE_RXLOCK
    Mutex rxMutex;
#endif
};

/**
 * Get client by FD. Returns NULL if not found.
 *
 * @param serv - server struct
 * @param sockfd
 * @return
 */
TcpdClient_t tcpd_client_by_fd(Tcpd_t serv, int sockfd)
{
#if TCPD_PARANOID_CHECKS
    if (!serv) {
        ESP_LOGE(TAG, "client_by_fd with no serv!");
        return NULL;
    }

    if (!serv->clients) {
        ESP_LOGE(TAG, "client_by_fd with no clients table!");
        return NULL;
    }
#endif

    for (int i = 0; i < serv->config.max_clients; i++) {
        if (serv->clients[i].fd == sockfd) {
            return &serv->clients[i];
        }
    }

    ESP_LOGE(TAG, "Client for fd %d not found!", sockfd);
    return NULL;
}

/**
 * Get client by tag. Returns NULL if not found.
 *
 * @param serv - server struct
 * @param sockfd
 * @return
 */
TcpdClient_t tcpd_client_by_tag(Tcpd_t serv, uint32_t tag)
{
#if TCPD_PARANOID_CHECKS
    if (!serv) {
        ESP_LOGE(TAG, "tcpd_client_by_tag with no serv!");
        return NULL;
    }

    if (!serv->clients) {
        ESP_LOGE(TAG, "tcpd_client_by_tag with no clients table!");
        return NULL;
    }
#endif

    for (int i = 0; i < serv->config.max_clients; i++) {
        if (serv->clients[i].tag == tag && serv->clients[i].fd != FD_NONE) {
            return &serv->clients[i];
        }
    }

    ESP_LOGE(TAG, "Client for tag %x not found!", tag);
    return NULL;
}

/**
 * Update client's LRU time to indicate its activity
 *
 * @param client - client pointer
 */
static inline void record_client_activity(TcpdClient_t client)
{
#if TCPD_PARANOID_CHECKS
    if (!client) {
        ESP_LOGE(TAG, "record_client_activity with no client!");
        return;
    }

    if (client->fd == FD_NONE) {
        ESP_LOGE(TAG, "record_client_activity with no FD!");
        return;
    }
#endif

    client->usetime = ++client->serv->rxcnt;
    ESP_LOGD(TAG, "fd %d used, time %d", client->fd, client->serv->rxcnt);
}

/**
 * Mark client slot as free
 *
 * @param client - client pointer
 */
static inline void mark_client_free(TcpdClient_t client)
{
#if TCPD_PARANOID_CHECKS
    if (!client) {
        ESP_LOGE(TAG, "mark_client_free with no client!");
        return;
    }
#endif

    client->fd = FD_NONE;
    client->usetime = 0;
    client->tag = 0;
    client->cctx = NULL;
}

/**
 * Create the main server socket
 */
static tcpd_err_t make_server_socket(int *pSock, uint16_t port)
{
#if TCPD_PARANOID_CHECKS
    if (!pSock) {
        ESP_LOGE(TAG, "make_server_socket with no pSock!");
        return ESP_ERR_INVALID_ARG;
    }
#endif

    int sock;
    struct sockaddr_in name;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "err in socket()");
        return ESP_FAIL;
    }

#ifndef ESP_PLATFORM
    // this should prevent failing to bind on POSIX when the program was recently hard-closed
    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (const char*)&one, sizeof(one));
#endif

    name.sin_family = AF_INET;
    name.sin_port = htons(port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *) &name, sizeof(name)) < 0) {
        ESP_LOGE(TAG, "err in bind()");
        return ESP_FAIL;
    }

    *pSock = sock;
    return ESP_OK;
}

/**
 * Get a client slot for a new incoming connection.
 * Frees the LRU slot (closing its connection) when all slots are full (and LRU closing is enabled)
 *
 * The client is cleared
 *
 * @param serv - server struct
 * @return client slot
 */
static TcpdClient_t get_new_client_slot(Tcpd_t serv)
{
#if TCPD_PARANOID_CHECKS
    if (!serv) {
        ESP_LOGE(TAG, "get_new_client_slot with no serv!");
        return NULL;
    }

    if (!serv->clients) {
        ESP_LOGE(TAG, "get_new_client_slot with no clients table!");
        return NULL;
    }
#endif

    ESP_LOGD(TAG, "find free slot");

    // find free or LRU slot
    uint32_t min = UINT32_MAX;
    TcpdClient_t client = NULL;
    int min_slotnum = -1;

    for (int i = 0; i < serv->config.max_clients; i++) {
        TcpdClient_t aClient = &serv->clients[i];
        if (aClient->fd == FD_NONE) {
            client = aClient;
            ESP_LOGD(TAG, "-> Found empty slot #%d", i);
            break;
        }

        // find LRU slot
        if (aClient->usetime < min && !aClient->injected) {
            client = aClient;
            min = aClient->usetime;
            min_slotnum = i;
        }
    }

#if TCPD_PARANOID_CHECKS
    if (!client) {
        ESP_LOGE(TAG, "get_new_client_slot no slot found!");
        return NULL;
    }
#endif

    // close old connection
    if (client->fd != FD_NONE) { // injected slot can never get here
        assert(!client->injected);

        if (serv->config.close_lru) {
            ESP_LOGD(TAG, "Out of client sockets, closing oldest (fd %d, #%d)", client->fd, min_slotnum);
            (void)min_slotnum;
            close(client->fd);
            FD_CLR (client->fd, &serv->active_fd_set);
        } else {
            ESP_LOGW(TAG, "No client slots left.");
            return NULL;
        }
    }

    client->serv = serv; // ensure the server reference is set
    mark_client_free(client);

    return client;
}

/* kick one client */
void tcpd_kick(TcpdClient_t client)
{
#if TCPD_PARANOID_CHECKS
    if (!client) {
        ESP_LOGE(TAG, "tcpd_kick with no client!");
        return;
    }

    if (client->fd == FD_NONE) {
        ESP_LOGE(TAG, "tcpd_kick with no FD!");
        return;
    }
#endif

    ESP_LOGW(TAG, "kicking client fd %d", client->fd);
    close_client(client, false);
}

/* kick all clients */
void tcpd_kick_all(Tcpd_t serv, bool with_injected)
{
#if TCPD_PARANOID_CHECKS
    if (!serv) {
        ESP_LOGE(TAG, "tcpd_kick_all with no server!");
        return;
    }
    if (serv->server_fd == FD_NONE) {
        ESP_LOGE(TAG, "tcpd_kick_all with no server FD!");
        return;
    }
#endif

    for (int i = 0; i < serv->config.max_clients; i++) {
        TcpdClient_t client = &serv->clients[i];
        if (client->fd != FD_NONE && (!client->injected || with_injected)) {
            tcpd_kick(client);
        }
    }
}

/* kick all clients */
int tcpd_kick_by_tag(Tcpd_t serv, uint32_t tag)
{
#if TCPD_PARANOID_CHECKS
    if (!serv) {
        ESP_LOGE(TAG, "tcpd_kick_by_tag with no server!");
        return -1;
    }
    if (serv->server_fd == FD_NONE) {
        ESP_LOGE(TAG, "tcpd_kick_by_tag with no server FD!");
        return -1;
    }
#endif

    int count = 0;
    for (int i = 0; i < serv->config.max_clients; i++) {
        TcpdClient_t client = &serv->clients[i];
        if (client->fd != FD_NONE && client->tag == tag) {
            tcpd_kick(client);
            count++;
        }
    }
    return count;
}

static bool fd_is_valid(int fd)
{
    if (fd == FD_NONE) {
        return false;
    }

    errno = 0;

    // always returns 0, but can set errno to EBADF or other
    fcntl(fd, F_GETFD);

    return errno == 0;
}

static tcpd_err_t read_from_client(TcpdClient_t client)
{
#if TCPD_PARANOID_CHECKS
    if (!client) {
        ESP_LOGE(TAG, "read_from_client with no client!");
        return ESP_ERR_INVALID_ARG;
    }

    if (!client->serv) {
        ESP_LOGE(TAG, "read_from_client with no client->serv!");
        return ESP_ERR_INVALID_ARG;
    }

    if (client->fd == FD_NONE) {
        ESP_LOGE(TAG, "read_from_client with no FD!");
        return ESP_ERR_INVALID_ARG;
    }
#endif

    if (client->serv->config.read_fn) {
        return client->serv->config.read_fn(client->serv, client, client->fd);
    } else {
        static uint8_t scratch[16];
        return read(client->fd, scratch, sizeof(scratch));
    }
}

static void open_client(TcpdClient_t client, int fd, struct sockaddr_in *sockaddr)
{
#if TCPD_PARANOID_CHECKS
    if (!client) {
        ESP_LOGE(TAG, "open_client with no client!");
        return;
    }

    if (!sockaddr) {
        ESP_LOGE(TAG, "open_client with no sockaddr!");
        return;
    }
#endif

    client->fd = fd;
    memcpy(&client->addr, sockaddr, sizeof(struct sockaddr_in));

    ESP_LOGD(TAG, "+ Client %s:%d joined (fd %d)",
             inet_ntoa(sockaddr->sin_addr),
             ntohs(sockaddr->sin_port),
             client->fd);

    record_client_activity(client);
    FD_SET(client->fd, &client->serv->active_fd_set);

    if (client->serv->config.open_fn) {
        client->serv->config.open_fn(client->serv, client);
    }
}

TcpdClient_t tcpd_inject_client(Tcpd_t server, int fd)
{
#if TCPD_PARANOID_CHECKS
    if (!server) {
        ESP_LOGE(TAG, "tcpd_inject_client with no server!");
        return NULL;
    }
#endif

    TcpdClient_t client = get_new_client_slot(server);
    if (!client) { return NULL; }

    client->injected = true;
    client->fd = fd;

    ESP_LOGI(TAG, "+ Client injected (fd %d)", client->fd);
    FD_SET(fd, &server->active_fd_set);
    record_client_activity(client);

    if (server->config.open_fn) {
        server->config.open_fn(server, client);
    }

    return client;
}

static void close_client(TcpdClient_t client, bool mutex_locked)
{
#if TCPD_PARANOID_CHECKS
    if (!client) {
        ESP_LOGE(TAG, "close_client with no client!");
        return;
    }
#endif

    if (client->fd == FD_NONE) {
        ESP_LOGW(TAG, "- Client became invalid while preparing to close!");
    } else {
        if (!client->serv) {
            ESP_LOGE(TAG, "close_client with no serv!");
            return;
        }

#if TCPD_USE_RXLOCK
        // We must guard this to prevent a lwip crash - https://github.com/espressif/esp-lwip/issues/13
        if (!mutex_locked) {
            mutex_lock(client->serv->rxMutex, portMAX_DELAY);
        }
#else
    (void)mutex_locked;
#endif

        FD_CLR(client->fd, &client->serv->active_fd_set);

        if (!client->injected) {
            close(client->fd);
        }

#if TCPD_USE_RXLOCK
        if (!mutex_locked) {
            mutex_unlock(client->serv->rxMutex);
        }
#endif

        ESP_LOGD(TAG, "- Closing client (fd %d)", client->fd);
    }

    if (client->serv->config.close_fn) {
        client->serv->config.close_fn(client->serv, client);
    }
    mark_client_free(client);
}

/**
* TCP server's main thread
*
* @param arg
*/
DEFINE_TASK(socksrv_thread)
{
    Tcpd_t serv = param;

#if TCPD_PARANOID_CHECKS
    if (!serv) {
        ESP_LOGE(TAG, "socksrv_thread with no serv!");
        thread_exit();
    }

    if (!serv->clients) {
        ESP_LOGE(TAG, "socksrv_thread with no clients table!");
        thread_exit();
    }

    if (0 == serv->config.port) {
        ESP_LOGE(TAG, "socksrv_thread with zero port!");
        thread_exit();
    }
#endif

    fd_set read_fd_set;
    struct sockaddr_in clientname;

    /* Create the socket and set it up to accept connections. */
    tcpd_err_t rv = make_server_socket(&serv->server_fd, serv->config.port);
    if (rv != ESP_OK) {
        ESP_LOGE(TAG, "failed to start TCP server");
        thread_exit();
    }

    if (listen(serv->server_fd, 4) < 0) {
        ESP_LOGE(TAG, "err listen");
        thread_exit();
    }

    ESP_LOGD(TAG, "Started TCP server on port %d", serv->config.port);

    /* Initialize the set of active sockets. */
    FD_ZERO (&serv->active_fd_set);
    FD_SET (serv->server_fd, &serv->active_fd_set);

    while (1) {
#if TCPD_USE_RXLOCK
        mutex_lock(serv->rxMutex, portMAX_DELAY);
#endif

        /* Block until input arrives on one or more active sockets. */
        struct timeval selectto = {.tv_usec = 100000};
        read_fd_set = serv->active_fd_set;
        if (select(FD_SETSIZE, &read_fd_set, NULL, NULL, &selectto) < 0) {
            ESP_LOGE(TAG, "err in select()");

            // try to clean up
            for (int i = 0; i < serv->config.max_clients; i++) {
                TcpdClient_t client = &serv->clients[i];
                if (client->fd != FD_NONE) {
                    if (!fd_is_valid(client->fd)) {
                        close_client(client, true);
                        FD_CLR(client->fd, &read_fd_set);
                    }
                }
            }

            if (!fd_is_valid(serv->server_fd)) {
#if TCPD_USE_RXLOCK
                mutex_unlock(serv->rxMutex);
#endif
            }
            break;
        }

        /* Service all the sockets with input pending. */
        for (int fd = 0; fd < FD_SETSIZE; ++fd) {
            if (FD_ISSET (fd, &read_fd_set)) {
                if (fd == serv->server_fd) {
                    /* Connection request on original socket. */
                    socklen_t size = sizeof(clientname);
                    int newfd = accept(serv->server_fd, (struct sockaddr *) &clientname, &size);
                    if (newfd < 0) {
                        ESP_LOGE(TAG, "err in accept()");
#if TCPD_USE_RXLOCK
                        mutex_unlock(serv->rxMutex);
#endif
                        continue;
                    }

                    struct timeval sockto1 = {.tv_sec = 5};
                    setsockopt(newfd, SOL_SOCKET, SO_RCVTIMEO, (char *) &sockto1, sizeof(sockto1));
                    struct timeval sockto2 = {.tv_sec = 5};
                    setsockopt(newfd, SOL_SOCKET, SO_SNDTIMEO, (char *) &sockto2, sizeof(sockto2));

                    TcpdClient_t slot = get_new_client_slot(serv);
                    if (slot) {
                        open_client(slot, newfd, &clientname);
                    } else {
                        // reject the connection
                        close(newfd);
                    }
                } else if (FD_ISSET(fd, &serv->active_fd_set)) {
                    /* Data arriving for an already connected client */
                    TcpdClient_t client = tcpd_client_by_fd(serv, fd);
                    if (client) {
                        if (ESP_OK != read_from_client(client)) {
                            close_client(client, true);
                        }
                    }
                }
            } /* end isset */
        } /* end fd iteration */

#if TCPD_USE_RXLOCK
        mutex_unlock(serv->rxMutex);
#endif
    } /* end server loop */

    thread_exit();
}

tcpd_err_t tcpd_broadcast(Tcpd_t serv, const uint8_t *data, ssize_t len)
{
#if TCPD_PARANOID_CHECKS
    if (!serv) {
        ESP_LOGE(TAG, "tcpd_broadcast with no serv!");
        return ESP_ERR_INVALID_ARG;
    }

    if (serv->server_fd == FD_NONE) {
        ESP_LOGE(TAG, "tcpd_broadcast with no server FD!");
        return ESP_ERR_INVALID_ARG;
    }

    if (!data) {
        ESP_LOGE(TAG, "tcpd_send with no data!");
        return ESP_ERR_INVALID_ARG;
    }
#endif

    if (len < 0) { len = strlen((const char *) data); }

    // send it to all clients
    for (int i = 0; i < serv->config.max_clients; i++) {
        TcpdClient_t client = &serv->clients[i];
        if (client->fd != FD_NONE) {
            tcpd_send(client, data, len);
        }
    }

    return ESP_OK;
}

tcpd_err_t tcpd_send(TcpdClient_t client, const uint8_t *data, ssize_t len)
{
#if TCPD_PARANOID_CHECKS
    if (!client) {
        ESP_LOGE(TAG, "tcpd_send with no client!");
        return ESP_ERR_INVALID_ARG;
    }

    if (!client->serv) {
        ESP_LOGE(TAG, "tcpd_send with no client->serv!");
        return ESP_ERR_INVALID_ARG;
    }

    if (client->fd == FD_NONE) {
        ESP_LOGE(TAG, "tcpd_send with no FD!");
        return ESP_ERR_INVALID_ARG;
    }

    if (!data) {
        ESP_LOGE(TAG, "tcpd_send with no data!");
        return ESP_ERR_INVALID_ARG;
    }
#endif

    int fd = client->fd;

    if (client->injected) {
        if (fd == STDIN_FILENO) {
            fd = STDOUT_FILENO;
        } else if (O_RDONLY == fcntl(fd, F_GETFL)) {
            // read-only file, do not try to write (maybe injected stdin)
            return ESP_OK;
        }
    }

    if (len < 0) { len = strlen((const char *) data); }

    int rv = write(fd, data, (size_t) len);
    if (rv <= 0) {
        ESP_LOGE(TAG, "Client sock write failed");
        close_client(client, false);
        return ESP_FAIL;
    } else {
        record_client_activity(client);
    }

    return ESP_OK;
}

void *tcpd_get_server_ctx(Tcpd_t serv)
{
#if TCPD_PARANOID_CHECKS
    if (!serv) {
        ESP_LOGE(TAG, "tcpd_get_server_ctx on NULL!");
        return NULL;
    }
#endif
    return serv->config.sctx;
}

void *tcpd_get_client_ctx(TcpdClient_t client)
{
#if TCPD_PARANOID_CHECKS
    if (!client) {
        ESP_LOGE(TAG, "tcpd_get_client_ctx on NULL!");
        return NULL;
    }
#endif
    return client->cctx;
}

void tcpd_set_client_ctx(TcpdClient_t client, void *cctx)
{
#if TCPD_PARANOID_CHECKS
    if (!client) {
        ESP_LOGE(TAG, "tcpd_set_client_ctx on NULL!");
        return;
    }
#endif
    client->cctx = cctx;
}

uint32_t tcpd_get_client_tag(TcpdClient_t client)
{
#if TCPD_PARANOID_CHECKS
    if (!client) {
        ESP_LOGE(TAG, "tcpd_get_client_tag on NULL!");
        return 0;
    }
#endif
    return client->tag;
}

const struct sockaddr_in * tcpd_get_client_addr(TcpdClient_t client)
{
#if TCPD_PARANOID_CHECKS
    if (!client) {
        ESP_LOGE(TAG, "tcpd_get_client_addr on NULL!");
        return NULL;
    }
#endif
    return &client->addr;
}

void tcpd_set_client_tag(TcpdClient_t client, uint32_t tag)
{
#if TCPD_PARANOID_CHECKS
    if (!client) {
        ESP_LOGE(TAG, "tcpd_set_client_tag on NULL!");
        return;
    }
#endif
    client->tag = tag;
}

int tcpd_get_client_fd(TcpdClient_t client)
{
    return (client == NULL ? FD_NONE : client->fd);
}

tcpd_err_t tcpd_init(const tcpd_config_t *config, Tcpd_t *handle)
{
#if TCPD_PARANOID_CHECKS
    if (!config) {
        ESP_LOGE(TAG, "tcpd_init with no config!");
        return ESP_ERR_INVALID_ARG;
    }

    if (!handle) {
        ESP_LOGE(TAG, "tcpd_init with no handle ptr!");
        return ESP_ERR_INVALID_ARG;
    }
#endif

    Tcpd_t serv = port_calloc(sizeof(struct sockd_server), 1);
    if (!serv) {
        ESP_LOGE(TAG, "server alloc failed");
        return ESP_ERR_NO_MEM;
    }

    memcpy(&serv->config, config, sizeof(struct tcpd_config));

    serv->clients = port_calloc(sizeof(struct sockd_client), config->max_clients);
    if (!serv->clients) {
        port_free(serv);
        ESP_LOGE(TAG, "clients alloc failed");
        return ESP_ERR_NO_MEM;
    }

    // gracefully shutdown all clients (this calls the close function to free user ctx)
    for (int i = 0; i < serv->config.max_clients; i++) {
        mark_client_free(&serv->clients[i]);
    }

#if TCPD_USE_RXLOCK
    if (!mutex_create_ok(serv->rxMutex)) {
        ESP_LOGE(TAG, "error creating mutex");
        port_free(serv->clients);
        port_free(serv);
        return ESP_FAIL;
    }
#endif

    if (!config->start_immediately) {
        tcpd_suspend(serv);
    }
    if (PORT_THREAD_CREATE_OK != port_thread_create(socksrv_thread,
                              config->task_name,
                              config->task_stack,
                              serv,
                              config->task_prio,
                              &serv->task)
    ) {
        ESP_LOGE(TAG, "error creating server thread");
#if TCPD_USE_RXLOCK
        mutex_remove(serv->rxMutex);
#endif
        port_free(serv->clients);
        port_free(serv);
        return ESP_FAIL;
    }

    *handle = serv;
    return ESP_OK;
}

void tcpd_shutdown(Tcpd_t serv)
{
#if TCPD_PARANOID_CHECKS
    if (!serv) {
        ESP_LOGE(TAG, "tcpd_shutdown with no handle!");
        return;
    }
#endif
    close(serv->server_fd);

    // this can fail if the server is already stopped, that's OK
    tcpd_suspend(serv);

    // gracefully shutdown all clients (this calls the close function to free user ctx)
    for (int i = 0; i < serv->config.max_clients; i++) {
        TcpdClient_t client = &serv->clients[i];
        if (client->fd != FD_NONE && !client->injected) {
            close_client(client, true);
        }
    }

    if (serv->config.sctx_free_fn && serv->config.sctx) {
        serv->config.sctx_free_fn(serv->config.sctx);
    }

    // XXX task kill is not implemented in CSP
#ifdef ESP_PLATFORM
    vTaskDelete(serv->task);
#endif

#if TCPD_USE_RXLOCK
    mutex_remove(serv->rxMutex);
#endif
    port_free(serv->clients);
    port_free(serv);
}

void tcpd_suspend(Tcpd_t serv)
{
#if TCPD_PARANOID_CHECKS
    if (!serv) {
        ESP_LOGE(TAG, "tcpd_stop with no handle!");
        return;
    }
#endif

    // Since these were implemented using the lock, they now don't work.
#if TCPD_USE_RXLOCK
    mutex_lock(serv->rxMutex, portMAX_DELAY);
#endif
}

void tcpd_resume(Tcpd_t serv)
{
#if TCPD_PARANOID_CHECKS
    if (!serv) {
        ESP_LOGE(TAG, "tcpd_start with no handle!");
        return;
    }
#endif

#if TCPD_USE_RXLOCK
    mutex_unlock(&serv->rxMutex);
#endif
}

tcpd_err_t tcpd_iter_init(struct tcpd_client_iter *iter, Tcpd_t serv) {
    if (!serv) {
        ESP_LOGE(TAG, "tcpd_iter_init NULL server!");
        return -1;
    }

    iter->server = serv;
    iter->next = 0;
    return 0;
}

TcpdClient_t tcpd_client_iter_next(struct tcpd_client_iter *iterator)
{
#if TCPD_PARANOID_CHECKS
    if (!iterator) {
        ESP_LOGE(TAG, "tcpd_client_iter_next NULL iterator!");
        return NULL;
    }
    if (!iterator->server) {
        ESP_LOGE(TAG, "tcpd_client_iter_next NULL server!");
        return NULL;
    }
#endif

    const uint16_t maxnum = iterator->server->config.max_clients;
    struct sockd_client * const clients = iterator->server->clients;

    for(; iterator->next < maxnum; iterator->next++) {
        TcpdClient_t client = &clients[iterator->next];
        if (client->fd != FD_NONE) {
            iterator->next++;
            return client;
        }
    }

    return NULL;
}
