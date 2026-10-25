#include <csp/csp.h>
#include "i2cif.h"

#include <linux/i2c.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <csp/csp_endian.h>
#include <pthread.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <poll.h>

static int i2cif_tx(const csp_route_t *ifroute, csp_packet_t *packet);

static int open_i2c_device(const char *device);

static int do_write(int fd, uint8_t address, const uint8_t *data, uint16_t len);

struct i2cif_private {
    int bus_fd;
    int slave_fd;
    pthread_t slave_thread_handle;
};

static struct i2cif_private priv = {};

/* Interface definition */
csp_iface_t csp_if_i2c = {
    .name = "I2C",
    .nexthop = i2cif_tx,
    .mtu = I2C_MTU,
};

static int set_slave_addr(const char *slave_addr_file)
{
    int fd = open(slave_addr_file, O_WRONLY);
    if (fd < 0) {
        csp_debug(CSP_ERROR, "Error opening I2C slave address file: %d", fd);
        return CSP_ERR_DRIVER;
    }

    int addr = csp_get_address();
    csp_debug(CSP_PROTOCOL, "Setting I2C slave addr: %d\n", addr);

    char buf[10];
    int len = sprintf(buf, "%d\n", addr);
    int wn = write(fd, buf, len);
    if (wn != len) {
        csp_debug(CSP_ERROR, "Error setting I2C slave address");
        close(fd);
        return CSP_ERR_DRIVER;
    }

    close(fd);
    return 0;
}

void * slave_receive_thread(void *unused)
{
    (void) unused;

    struct pollfd pfd = {};
    pfd.fd = priv.slave_fd;
    pfd.events = POLLPRI;
    csp_debug(CSP_PROTOCOL, "wait for slave data");

    while (1) {
        // Wait for data
        int r = poll(&pfd, 1, 1000);
        if (r < 0) {
            csp_debug(CSP_ERROR, "i2c if slave-data read error: %d", r);
            break;
        }
        if (r == 0 || !(pfd.revents & POLLPRI)) {
            // timeout or spurious wup
            continue;
        }

        csp_packet_t *packet = csp_buffer_get(I2C_MTU);
        lseek(priv.slave_fd, 0, SEEK_SET);
        r = read(priv.slave_fd, (void*)&packet->id, I2C_MTU);
        if (r <= 0) {
            csp_buffer_free(packet);
            if (r < 0) {
                break; // Did not receive anything
            }
            continue;
        }

        csp_debug(CSP_PROTOCOL, "I2C rx frame of len %d", r);

        packet->id.ext = csp_ntoh32(packet->id.ext);
        packet->length = r - sizeof(csp_id_t);
        csp_qfifo_write(packet, &csp_if_i2c, NULL);
    }

    csp_debug(CSP_ERROR, "I2C slave rx thread ends!");
    close(priv.slave_fd);
    priv.slave_fd = 0;

    return NULL;
}

int i2cif_start(const struct i2cif_config *cfg)
{
    csp_debug(CSP_PROTOCOL, "Setting up I2C driver:\ndev=%s\nslave-addr=%s\nslave-data=%s\n",
              cfg->bus_device, cfg->slave_addr_file, cfg->slave_data_file);

    priv.bus_fd = 0;
    priv.slave_fd = 0;

    int bus_fd = open_i2c_device(cfg->bus_device);
    if (bus_fd < 0) {
        csp_debug(CSP_ERROR, "Error opening I2C bus device: %d", bus_fd);
        return CSP_ERR_DRIVER;
    }
    priv.bus_fd = bus_fd;

    int suc = set_slave_addr(cfg->slave_addr_file);
    if (suc != 0) {
        return suc;
    }

    int slave_fd = open(cfg->slave_data_file, O_RDONLY|O_NONBLOCK);
    if (slave_fd < 0) {
        csp_debug(CSP_ERROR, "Error opening I2C slave data file: %d", slave_fd);
        close(priv.bus_fd);
        priv.bus_fd = 0;
        return CSP_ERR_DRIVER;
    }
    priv.slave_fd = slave_fd;

    if (CSP_ERR_NONE != csp_iflist_add(&csp_if_i2c)) {
        close(priv.bus_fd);
        close(priv.slave_fd);
        priv.bus_fd = 0;
        priv.slave_fd = 0;
        return CSP_ERR_DRIVER;
    }

    suc = pthread_create(&priv.slave_thread_handle, NULL, slave_receive_thread, NULL);
    assert(suc == 0);

    return 0;
}

/** CSP nexhop function */
static int i2cif_tx(const csp_route_t *ifroute, csp_packet_t *packet)
{
    if (packet == NULL || packet->length > I2C_MTU) {
        return CSP_ERR_DRIVER;
    }

    csp_debug(CSP_PROTOCOL, "Sending packet via I2C");

    uint8_t via = (ifroute->via == CSP_NO_VIA_ADDRESS) ?
                  (uint8_t) packet->id.dst :
                  ifroute->via;

    packet->id.ext = csp_hton32(packet->id.ext);
    int suc = do_write(priv.bus_fd, via, (void *) &packet->id, packet->length + sizeof(csp_id_t));
    if (suc != 0) { return suc; } // Do not free in this case - it's freed by the caller.

    csp_buffer_free(packet);

    return CSP_ERR_NONE;
}

// Opens the specified I2C device.  Returns a non-negative file descriptor
// on success, or -1 on failure.
static int open_i2c_device(const char *device)
{
    int fd = open(device, O_RDWR);
    if (fd == -1) {
        perror(device);
        return -1;
    }
    return fd;
}

// Write N bytes to a device
static int do_write(int fd, uint8_t address, const uint8_t *data, uint16_t len)
{
    struct i2c_msg message = {address, 0, len, (uint8_t *) data};
    struct i2c_rdwr_ioctl_data ioctl_data = {&message, 1};
    int result = ioctl(fd, I2C_RDWR, &ioctl_data);
    if (result != 1) {
        perror("failed to write bytes");
        return CSP_ERR_TX;
    }
    return 0;
}
