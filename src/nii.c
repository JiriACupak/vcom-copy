/**
 * \file nii.h
 * \brief Non Interactive Interpret - replacement of GOSH.
 * \author Petr Svoboda, 2015-02-11
 */
#include "nii.h"
#include "vcom_config.h"
#include "vcom_debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <console/console.h>

#define NII_MAX_MSG_SIZE  50000
#define NII_HOST_ADR_LEN  100
#define NII_WRP_MAX_LEN   5000

int nii_make_server_socket(unsigned short port)
{
    int server_sock;
    struct sockaddr_in name;
    int yes = 1;

    /* Create the socket. */
    server_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        vd_error("NII: socket");
        return -1;
    }

    // tento radek zpusobi, ze pri opakovanem restartu serveru, bude volani
    // funkce bind() uspesne, kdo neveri, at ho zakomentuje :))
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        vd_error("NII: setsockopt");
        return -1;
    }

    /* Give the socket a name. */
    name.sin_family = AF_INET;
    name.sin_port = htons(port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(server_sock, (struct sockaddr *) &name, sizeof(name)) < 0) {
        vd_error("NII: bind");
        return -1;
    }

    return server_sock;
}

int nii_read_from_client(int fd, char *buff, int *pos, int max)
{
    if (fd < 0 || buff == NULL || pos == NULL)
        return -1;

    int readed = recv(fd, &(buff[*pos]), max - *pos - 1, 0);
    if (readed < 0) { /* Read error. */
        return -2;
    }

    if (readed == 0) { /* End-of-file. */
        return 1;
    }

    *pos += readed;
    buff[*pos] = 0;
    return 0;
}

int line_is_complete(char *buff)
{
    char *pch = strchr(buff, '\r');
    if (pch != NULL) {
        *pch = 0;
        return 1;
    }
    pch = strchr(buff, '\n');
    if (pch != NULL) {
        *pch = 0;
        return 1;
    }
    return 0;
}

int non_interactive_interpret_start(unsigned short port, const char *logpath)
{
    struct timeval tim;
    int server_sock;
    fd_set active_fd_set;
    fd_set read_fd_set;
    (void)logpath;
    int fd, s_rv;
    struct sockaddr_in clisckadr;
    socklen_t size;
    int client_buff_pos = 0, cli_connected = 0;
    char client_buff[NII_MAX_MSG_SIZE];
    char client_name[NII_HOST_ADR_LEN];
    int out_pipe[2];
    char wrp_buff[NII_WRP_MAX_LEN+1]; // extra byte for final NULL
    int wrp_siz;

    if (pipe(out_pipe) != 0) { /* make a pipes */
        vd_error("NII: pipe ERROR");
        return -1;
    }

    if (dup2(out_pipe[1], STDOUT_FILENO) < 0) { /* redirect stdout to the pipe */
        vd_error("NII: dup2 ERROR");
        return -2;
    }

    if (close(out_pipe[1]) != 0 || setvbuf(stdout, NULL, _IONBF, 0)) { /* disable buffer for stdout */
        vd_error("NII: close or setvbuf ERROR");
        return -3;
    }

    /* Create the socket and set it up to accept connections. */
    server_sock = nii_make_server_socket(port);
    if (server_sock < 0)
        return -4;

    if (listen(server_sock, 1) < 0) {
        vd_error("NII: listen ERROR");
        return -5;
    }

    console_ctx_t *console_ctx = console_ctx_init(NULL, NULL, stdout);
    console_ctx->interactive = false;
    console_ctx->use_colors = (bool)vcom_config.colors;;

    /* Initialize the set of active sockets. */
    FD_ZERO (&active_fd_set);
    FD_SET (server_sock, &active_fd_set);

    //TODO: add reopened STDOUT into fdsets

    for (;;) {
        /* 500ms timeout */
        tim.tv_sec = 0;
        tim.tv_usec = 500000;
        /* Block until input arrives on one or more active sockets. */
        read_fd_set = active_fd_set;
        s_rv = select(FD_SETSIZE, &read_fd_set, NULL, NULL, &tim);

        if (s_rv == 0) {
            //idle tick
            continue;
        }

        if (s_rv < 0) {
            vd_error("NII: server select ERROR");
            return -6;
        }

        /* Service all the sockets with input pending. */
        for (fd = 0; fd < FD_SETSIZE; ++fd) {
            if (FD_ISSET (fd, &read_fd_set)) {
                if (fd == server_sock) {
                    /* Connection request on original socket. */
                    int novy;
                    size = sizeof(clisckadr);
                    novy = accept(server_sock,
                                  (struct sockaddr *) &clisckadr,
                                  &size);
                    if (novy < 0) {
                        vd_error("NII: server accept ERROR");
                        return -7;
                    }

                    if (cli_connected) {//occupied
                        vd_error("NII: rejecting: %s (multiple clients not allowed)!", inet_ntoa(clisckadr.sin_addr));
                        close(novy);
                    }
                    else {
                        FD_SET (novy, &active_fd_set); /* Add new client to the set */
                        strcpy(client_name, inet_ntoa(clisckadr.sin_addr));
                        client_buff_pos = 0;
                        memset(client_buff, 0, sizeof(unsigned char) * NII_MAX_MSG_SIZE);
                    }
                }
                else {
                    /* Data arriving on an already-connected socket. */
                    int crv = nii_read_from_client(fd, client_buff, &client_buff_pos, NII_MAX_MSG_SIZE);
                    if (crv < 0) {
                        FD_CLR (fd, &active_fd_set);
                        close(fd);
                    }
                    else if (crv > 0 || line_is_complete(client_buff)) {
                        vd_info("command: '%s' from %s\n", client_buff, client_name);

                        console_handle_cmd(console_ctx, client_buff, NULL, NULL);

                        vd_info(" reply: ");
                        int pending_bytes;
                        if (ioctl(out_pipe[0], FIONREAD, &pending_bytes) == 0 && pending_bytes > 0) {
                            do {
                                wrp_siz = read(out_pipe[0], wrp_buff, NII_WRP_MAX_LEN);
                                send(fd, wrp_buff, wrp_siz, 0);
                                wrp_buff[wrp_siz] = 0;
                                vd_info("%s", wrp_buff);
                            } while (wrp_siz == NII_WRP_MAX_LEN);
                        }
                        vd_info("\n");

                        client_buff_pos = 0;
                        memset(client_buff, 0, sizeof(unsigned char) * NII_MAX_MSG_SIZE);
                        FD_CLR (fd, &active_fd_set);
                        close(fd);
                    }
                }
            }
        }
    }


    /* Close all open sockets */
    for (fd = 0; fd < FD_SETSIZE; ++fd) {
        if (FD_ISSET (fd, &read_fd_set)) {
            FD_CLR (fd, &active_fd_set);
            close(fd);
        }
    }

    return 0;
}
