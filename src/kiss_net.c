#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <csp/csp_buffer.h>
#include <csp/interfaces/csp_if_kiss.h>

#include "kiss_net.h"
#include "vcom_debug.h"

int kiss_net_start(kiss_net_config_t *config)
{
    int ret = CSP_ERR_NONE;
    int err = -1;
    int sock_fd = -1;
    struct sockaddr_in server_addr;
    kiss_net_driver_data_t *driver_data = csp_if_kiss_net.driver_data;

    // create and configure socket
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        err = errno;
        vd_error("%s: socket creation failed: %s", __func__, strerror(err));
        return CSP_ERR_DRIVER;
    }
    memset(&server_addr, 0, sizeof(server_addr));

    int yes = 1;
    ret = setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (ret < 0) {
        err = errno;
        vd_error("%s: reuse addr setsockopt error: %s", __func__, strerror(err));
        close(sock_fd);
        return CSP_ERR_DRIVER;
    }

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = KISS_NET_RX_TIMEOUT_MSEC * 1000;
    ret = setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (ret < 0) {
        err = errno;
        vd_error("%s: timeval setsockopt error: %s", __func__, strerror(err));
        close(sock_fd);
        return CSP_ERR_DRIVER;
    }

    // assign IP and port
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(config->host_ip);
    server_addr.sin_port = htons(config->host_port);

    // connect the socket
    vd_info("Connecting KISS_NET to the server %s:%d",
            config->host_ip, config->host_port);
    ret = connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (ret) {
        err = errno;
        vd_error("%s: connection with the server %s:%d failed: %s",
                 __func__, config->host_ip, config->host_port, strerror(err));
        close(sock_fd);
        return CSP_ERR_DRIVER;
    }

    driver_data->sock_fd = sock_fd;

    ret = csp_kiss_add_interface(&csp_if_kiss_net);

    if (ret != CSP_ERR_NONE) {
        vd_error("%s: cannot add KISS_NET interface: %d", __func__, ret);
        kiss_net_done();
        return ret;
    }

    driver_data->run = true;
    ret = pthread_create(&driver_data->rx_thread_handle, NULL, kiss_net_rx, driver_data);
    if (ret != 0) {
        vd_error("%s: cannot create KISS_NET receive thread: %d", __func__, ret);
        kiss_net_done();
        return CSP_ERR_DRIVER;
    }

    return CSP_ERR_NONE;
}

static int kiss_net_tx(void *tx_driver_data, const uint8_t *data, size_t len)
{
    kiss_net_driver_data_t *driver_data = tx_driver_data;
    size_t off = 0;

    while (off < len) {
        ssize_t ret = send(driver_data->sock_fd, data + off, len - off, MSG_NOSIGNAL);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            vd_error("%s: error sending data: %s", __func__, strerror(errno));
            return CSP_ERR_DRIVER;
        }
        else if (ret == 0) {
            vd_error("%s: send returned 0", __func__);
            return CSP_ERR_DRIVER;
        }

        off += (size_t) ret;
    }

    return CSP_ERR_NONE;
}

void *kiss_net_rx(void *rx_driver_data)
{
    int ret = -1;
    int err = -1;

    uint8_t buf[csp_buffer_size() + KISS_NET_RX_BUFFER_OVERHEAD];
    memset(buf, 0, sizeof(buf));

    kiss_net_driver_data_t *driver_data = rx_driver_data;

    while (driver_data->run) {
        ret = recv(driver_data->sock_fd, buf, sizeof(buf), 0);

        if (ret < 0) {
            err = errno;

            // The configured socket timeout has been reached, so continue
            if (err == EAGAIN || err == EWOULDBLOCK) {
                continue;
            }
            else {
                vd_error("KISS_NET error during receive: %s", strerror(err));
                break;
            }
        }
        else if (ret == 0) {
            vd_error("KISS_NET: remote host has closed the connection");
            break;
        }
        else {
            csp_debug(CSP_PROTOCOL, "%s: received data, len %d", __func__, ret);
            csp_kiss_rx(&csp_if_kiss_net, buf, ret, NULL);
        }
    }

    return NULL;
}

int kiss_net_done()
{
    int ret = -1;
    int final_ret = CSP_ERR_NONE;
    int err = -1;
    kiss_net_driver_data_t *driver_data = csp_if_kiss_net.driver_data;

    vd_debug("Closing KISS_NET interface");

    driver_data->run = false;

    if (driver_data->sock_fd >= 0) {
        ret = shutdown(driver_data->sock_fd, SHUT_RDWR);
        if (ret != 0) {
            err = errno;
            vd_error("Error shutting down KISS_NET socket: %s", strerror(err));
            final_ret = CSP_ERR_DRIVER;
        }
    }

    if (driver_data->rx_thread_handle != 0) {
        ret = pthread_join(driver_data->rx_thread_handle, NULL);
        if (ret != 0) {
            vd_error("Error terminating KISS_NET receive thread: %d", ret);
            final_ret = CSP_ERR_DRIVER;
        }
        driver_data->rx_thread_handle = 0;
    }

    ret = 0;
    if (driver_data->sock_fd >= 0) {
        ret = close(driver_data->sock_fd);
        driver_data->sock_fd = -1;
    }

    if (ret) {
        err = errno;
        vd_error("Error closing KISS_NET socket: %s", strerror(err));
        final_ret = CSP_ERR_DRIVER;
    }

    return final_ret;
}

kiss_net_driver_data_t kiss_net_driver_data = {
    .sock_fd = -1,
    .rx_thread_handle = 0,
    .run = false,
};

csp_kiss_interface_data_t kiss_net_interface_data = {
    .tx_func = kiss_net_tx,
};

csp_iface_t csp_if_kiss_net = {
    .name = "KISS_NET",
    .interface_data = &kiss_net_interface_data,
    .driver_data = &kiss_net_driver_data,
    // Let the KISS interface decide the MTU
    .mtu = 0,
};