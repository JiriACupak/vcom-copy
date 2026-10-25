/**
 * \file stcpsc.c
 * \brief Simple-to-use TCP server/client
 * \author Petr Svoboda, 2014-10-11
 */

#include "posix/stcpsc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <semaphore.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "vcom_debug.h"

#define STCP_MAX_MSG_SIZE  1000
#define STCP_HOST_ADR_LEN  100

#define STCP_READ_ERR_NO         0
#define STCP_READ_ERR_CLOSE      STCP_DISCONNECT_REASON_CLOSED
#define STCP_READ_ERR_READ       STCP_DISCONNECT_REASON_ERROR
#define STCP_READ_ERR_INTERNAL   STCP_DISCONNECT_REASON_INTERNAL
#define STCP_READ_ERR_MTU        STCP_DISCONNECT_REASON_MTU
#define STCP_READ_ERR_KICK       STCP_DISCONNECT_REASON_KICK

#define STCP_READ_STATE_SIZ1 0
#define STCP_READ_STATE_SIZ2 1
#define STCP_READ_STATE_DATA 2

struct simple_tcp_queue;
typedef struct simple_tcp_queue simple_tcp_queue_t;

struct simple_tcp_queue {
    int cli_fd, state, awaiting, mtu, aloc;
    simple_msg_t msg;
    msg_received_fce_t cli_rec_fce;
    simple_tcp_queue_t *next;
};

/* Server stuff */
volatile int srv_done = 0;
unsigned short srv_port = 1234;
int srv_mtu = -1;
simple_tcp_queue_t *srv_queue = NULL;
pthread_t server_thread = 0;
fd_set active_fd_set;

msg_received_fce_t srv_rec_fce = NULL;
cli_connected_fce_t srv_conn_fce = NULL;
cli_disconnected_fce_t srv_disconn_fce = NULL;

/* Client stuff */
sem_t cli_conn_sem;
int tcpcli_portno;
int client_mtu = -1;
volatile int tcpcli_sockfd = -1;
volatile int cli_thread_done = 0;
pthread_t client_thread;
char cli_host_address[STCP_HOST_ADR_LEN + 1];

msg_received_fce_t cli_rec_fce = NULL;


int make_server_socket()
{
    int server_sock;
    struct sockaddr_in name;
    int yes = 1;

    /* Create the socket. */
    server_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        vd_error("TCPBR: socket");
        return -1;
    }

    // tento radek zpusobi, ze pri opakovanem restartu serveru, bude volani
    // funkce bind() uspesne, kdo neveri, at ho zakomentuje :))
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        vd_error("TCPBR: setsockopt");
        return -1;
    }

    /* Give the socket a name. */
    name.sin_family = AF_INET;
    name.sin_port = htons(srv_port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(server_sock, (struct sockaddr *) &name, sizeof(name)) < 0) {
        vd_error("TCPBR: bind");
        return -1;
    }

    return server_sock;
}

void close_client(int fd, int reason)
{
    simple_tcp_queue_t *toDel = srv_queue;
    simple_tcp_queue_t **prev = &srv_queue;
    while (toDel) {
        if (toDel->cli_fd == fd) { /* match! */
            simple_tcp_queue_t *next = toDel->next;
            if (toDel->msg.data)
                free(toDel->msg.data);
            free(toDel);
            *prev = next;
            break;
        }
        /* iterate */
        prev = &(toDel->next);
        toDel = toDel->next;
    }
    FD_CLR (fd, &active_fd_set); /* Remove the client from the set */
    close(fd);
    if (srv_disconn_fce)
        srv_disconn_fce(fd, reason);
}

int read_from_client(simple_tcp_queue_t *cli)
{
    if (cli == NULL)
        return STCP_READ_ERR_INTERNAL;

    unsigned char *p, buff[STCP_MAX_MSG_SIZE + 2];
    int readed = recv(cli->cli_fd, buff, STCP_MAX_MSG_SIZE + 2, 0);

    if (readed < 0) { /* Read error. */
        return STCP_READ_ERR_READ;
    }

    if (readed == 0) { /* End-of-file. */
        return STCP_READ_ERR_CLOSE;
    }

    p = buff;

    while (readed > 0) {
        if (cli->state == STCP_READ_STATE_SIZ1) {
            cli->awaiting = (unsigned char) *p;
            cli->state = STCP_READ_STATE_SIZ2;
            readed--;
            p++;

            if (readed < 1)
                return STCP_READ_ERR_NO;
        }

        if (cli->state == STCP_READ_STATE_SIZ2) {
            cli->awaiting <<= 8;
            cli->awaiting += (unsigned char) *p;
            cli->state = STCP_READ_STATE_DATA;
            readed--;
            p++;

            if (cli->mtu > 0) {
                if (cli->awaiting > cli->mtu)
                    return STCP_READ_ERR_MTU;
            }

            cli->msg.len = 0;
            if (cli->aloc < cli->awaiting) {
                if (cli->msg.data)
                    free(cli->msg.data);
                cli->msg.data = (unsigned char *) malloc(cli->awaiting);
                cli->aloc = cli->awaiting;
            }

            if (readed < 1)
                return STCP_READ_ERR_NO;
        }

        if (cli->state == STCP_READ_STATE_DATA) {
            int copy_siz = readed;
            if (copy_siz > cli->awaiting)
                copy_siz = cli->awaiting;

            memcpy(&(cli->msg.data[cli->msg.len]), p, copy_siz);
            cli->msg.len += copy_siz;
            cli->awaiting -= copy_siz;
            p += copy_siz;
            readed -= copy_siz;

            if (cli->awaiting > 0)
                return STCP_READ_ERR_NO; /* rest of data next time */

            if (cli->cli_rec_fce) {
                if (cli->cli_rec_fce(cli->cli_fd, &(cli->msg)))
                    return STCP_READ_ERR_KICK;
            }

            cli->state = STCP_READ_STATE_SIZ1;
        }
    }

    return STCP_READ_ERR_NO;
}

int read_from_srv_fd(int filedes)
{
    simple_tcp_queue_t *cli = srv_queue;
    while (cli) {
        if (cli->cli_fd == filedes)
            break;
        cli = cli->next;
    }
    return read_from_client(cli);
}

void *srv_thread_function(void *arg)
{
    struct timeval tim;
    int server_sock;
    fd_set read_fd_set;
    int fd, s_rv;
    struct sockaddr_in clientname;
    socklen_t size;

    /* unused */
    (void) arg;

    /* Create the socket and set it up to accept connections. */
    server_sock = make_server_socket();
    if (server_sock < 0)
        return NULL;

    if (listen(server_sock, 1) < 0) {
        vd_error("TCPBR: listen ERROR");
        return NULL;
    }

    /* Initialize the set of active sockets. */
    FD_ZERO (&active_fd_set);
    FD_SET (server_sock, &active_fd_set);

    while (!srv_done) {
        /* 200ms timeout */
        tim.tv_sec = 0;
        tim.tv_usec = 200000;
        /* Block until input arrives on one or more active sockets. */
        read_fd_set = active_fd_set;
        s_rv = select(FD_SETSIZE, &read_fd_set, NULL, NULL, &tim);

        if (s_rv == 0)
            continue;

        if (s_rv < 0) {
            vd_error("TCPBR: server select ERROR");
            return NULL;
        }

        /* Service all the sockets with input pending. */
        for (fd = 0; fd < FD_SETSIZE; ++fd) {
            if (FD_ISSET (fd, &read_fd_set)) {
                if (fd == server_sock) {
                    /* Connection request on original socket. */
                    int novy;
                    size = sizeof(clientname);
                    novy = accept(server_sock,
                                  (struct sockaddr *) &clientname,
                                  &size);
                    if (novy < 0) {
                        vd_error("TCPBR: server accept ERROR");
                        return NULL;
                    }

                    FD_SET (novy, &active_fd_set); /* Add new client to the set */
                    simple_tcp_queue_t *cliq = (simple_tcp_queue_t *) malloc(sizeof(simple_tcp_queue_t));
                    cliq->cli_fd = novy;
                    cliq->state = STCP_READ_STATE_SIZ1;
                    cliq->next = srv_queue;
                    cliq->awaiting = 0;
                    cliq->mtu = srv_mtu;
                    cliq->aloc = 0;
                    cliq->msg.len = 0;
                    cliq->msg.data = NULL;
                    cliq->cli_rec_fce = srv_rec_fce;
                    srv_queue = cliq;

                    if (srv_conn_fce) {
                        if (srv_conn_fce(novy, inet_ntoa(clientname.sin_addr)))
                            close_client(novy, STCP_DISCONNECT_REASON_KICK);
                    }
                }
                else {
                    /* Data arriving on an already-connected socket. */
                    int crv = read_from_srv_fd(fd);
                    if (crv != STCP_READ_ERR_NO)
                        close_client(fd, crv);
                }
            }
        }
    }

    /* Close all open sockets */
    for (fd = 0; fd < FD_SETSIZE; ++fd) {
        if (FD_ISSET (fd, &read_fd_set))
            close_client(fd, STCP_DISCONNECT_REASON_SERVER_CLOSE);
    }
    return NULL;
}

void *cli_thread_function(void *arg)
{
    struct timeval tim;
    struct sockaddr_in host_addr;
    struct hostent *host_ent;
    fd_set active_fd_set;
    fd_set read_fd_set;
    int fd, s_rv;
    char host_ip_str[INET_ADDRSTRLEN];

    /* unused */
    (void) arg;

    tcpcli_sockfd = -1;

    /* Create a socket point */
    tcpcli_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcpcli_sockfd < 0) {
        vd_error("TCPCL: error opening socket");
        sem_post(&cli_conn_sem);
        return NULL;
    }

    host_ent = gethostbyname(cli_host_address);
    if (host_ent == NULL) {
        vd_error("TCPCL: error '%s': no such host", cli_host_address);
        tcpcli_sockfd = -1;
        sem_post(&cli_conn_sem);
        return NULL;
    }

    memset(&host_addr, 0, sizeof(host_addr));
    host_addr.sin_family = AF_INET;
    host_addr.sin_port = htons(tcpcli_portno);
    memcpy(&host_addr.sin_addr.s_addr, host_ent->h_addr, host_ent->h_length);
    inet_ntop(AF_INET, &(host_addr.sin_addr), host_ip_str, INET_ADDRSTRLEN);

    /* Now connect to the server */
    if (connect(tcpcli_sockfd, (struct sockaddr *) &host_addr, sizeof(host_addr)) < 0) {
        vd_error("TCPCL: error connect to: %s:%d", host_ip_str, tcpcli_portno);
        tcpcli_sockfd = -1;
        sem_post(&cli_conn_sem);
        return NULL;
    }
    simple_tcp_queue_t *client_que;

    vd_info("TCPCL: connected to %s:%d", host_ip_str, tcpcli_portno);
    sem_post(&cli_conn_sem);

    client_que = (simple_tcp_queue_t *) malloc(sizeof(simple_tcp_queue_t));
    client_que->cli_fd = tcpcli_sockfd;
    client_que->state = STCP_READ_STATE_SIZ1;
    client_que->next = NULL;
    client_que->awaiting = 0;
    client_que->mtu = client_mtu;
    client_que->aloc = 0;
    client_que->msg.len = 0;
    client_que->msg.data = NULL;
    client_que->cli_rec_fce = cli_rec_fce;

    /* Initialize the set of active sockets. */
    FD_ZERO (&active_fd_set);
    FD_SET (tcpcli_sockfd, &active_fd_set);

    while (!cli_thread_done) {
        /* 200ms timeout */
        tim.tv_sec = 0;
        tim.tv_usec = 200000;
        /* Block until input arrives on one or more active sockets. */
        read_fd_set = active_fd_set;
        s_rv = select(FD_SETSIZE, &read_fd_set, NULL, NULL, &tim);

        if (s_rv == 0)
            continue;

        if (s_rv < 0) {
            vd_error("TCPBR: server select ERROR");
            return NULL;
        }

        /* Service all the sockets with input pending. */
        for (fd = 0; fd < FD_SETSIZE; ++fd) {
            if (FD_ISSET (fd, &read_fd_set) && fd == tcpcli_sockfd) {
                if (read_from_client(client_que) != STCP_READ_ERR_NO)
                    cli_thread_done = 1;
            }
        }
    }

    free(client_que);
    close(tcpcli_sockfd);
    tcpcli_sockfd = -1;
    return NULL;
}

int simple_tcp_server_start(unsigned short port, int mtu, msg_received_fce_t rec, cli_connected_fce_t con, cli_disconnected_fce_t dis)
{
    srv_done = 0;
    srv_port = port;
    srv_mtu = mtu;
    srv_rec_fce = rec;
    srv_conn_fce = con;
    srv_disconn_fce = dis;
    srv_queue = NULL;
    if (pthread_create(&server_thread, NULL, srv_thread_function, NULL)) {
        srv_done = 1;
        return -1;
    }
    return 0;
}

int simple_tcp_server_stop()
{
    if (srv_done || !server_thread)
        return 0;
    srv_done = 1;
    if (pthread_join(server_thread, NULL))
        return -1;
    return 0;
}

int simple_tcp_client_start(const char *address_port, int mtu, int def_port, msg_received_fce_t rec)
{
    char *portp = strchr(address_port, ':');
    if (portp) {
        *portp = '\0';
        portp++;
        tcpcli_portno = atoi((portp));
    }
    else
        tcpcli_portno = def_port;

    cli_thread_done = 0;
    client_mtu = mtu;
    cli_rec_fce = rec;
    strncpy(cli_host_address, address_port, STCP_HOST_ADR_LEN);

    if (sem_init(&cli_conn_sem, 0, 0) < 0) {
        vd_error("TCPCL: sem_init");
        return -1;
    }

    if (pthread_create(&client_thread, NULL, cli_thread_function, NULL)) {
        cli_thread_done = 1;
        return -1;
    }

    sem_wait(&cli_conn_sem);
    sem_destroy(&cli_conn_sem);

    return tcpcli_sockfd;
}

int simple_tcp_client_done()
{
    if (cli_thread_done)
        return 0;
    cli_thread_done = 1;
    if (pthread_join(client_thread, NULL))
        return -1;
    return 0;
}

int simple_tcp_send(int fd, const simple_msg_t *msg)
{
    char buff[STCP_MAX_MSG_SIZE];
    buff[0] = 0xFF & ((msg->len) >> 8);
    buff[1] = 0xFF & msg->len;

    size_t unused;

    if (msg->len < STCP_MAX_MSG_SIZE - 2) { //small message send in one TCP packet
        memcpy(&(buff[2]), msg->data, msg->len);
        unused = write(fd, buff, msg->len + 2);
        return 0;
    }

    unused = write(fd, buff, 2);
    unused = write(fd, msg->data, msg->len);
    (void)unused;
    return 0;
}
