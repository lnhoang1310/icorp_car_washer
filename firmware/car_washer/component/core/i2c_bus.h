#ifndef __I2C_BUS_H__
#define __I2C_BUS_H__

#include "esp_err.h"
#include "driver/i2c_master.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define I2C_BUS_MAX_INSTANCES 2

    esp_err_t i2c_bus_init(i2c_port_t port, int sda_gpio, int scl_gpio, uint32_t glitch_ignore_cnt);

    i2c_master_bus_handle_t i2c_bus_get_handle(i2c_port_t port);

    bool i2c_bus_is_ready(i2c_port_t port);

#ifdef __cplusplus
}
#endif

#endif