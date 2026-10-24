#ifndef __FRAM_H__
#define __FRAM_H__

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct
{
    int i2c_port;
    int sda_gpio;
    int scl_gpio;
    uint32_t clk_speed_hz;
    uint8_t i2c_addr_7bit;
    size_t capacity_bytes;
} fram_config_t;

esp_err_t fram_init(const fram_config_t *cfg);
bool fram_is_ready(void);
size_t fram_get_capacity(void);
esp_err_t fram_read(uint16_t addr, void *buf, size_t len);
esp_err_t fram_write(uint16_t addr, const void *buf, size_t len);

bool fram_self_test(void);

#endif