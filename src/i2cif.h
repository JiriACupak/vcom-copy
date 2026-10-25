/**
 * I2C CSP interface
 * Connect to a system I2C bus as a master/slave device
 *
 * Created on 2021/07/18.
 */

#ifndef VCOM_I2CDEV_H
#define VCOM_I2CDEV_H

#include <stdint.h>

#define I2C_MTU 512

/** I2C interface config; only needs to live through the init call */
struct i2cif_config {
    char *bus_device;
    char *slave_addr_file;
    char *slave_data_file;
};

int i2cif_start(const struct i2cif_config *cfg);

extern csp_iface_t csp_if_i2c;

#endif //VCOM_I2CDEV_H
