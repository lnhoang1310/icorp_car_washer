#ifndef __NET_ETHERNET_H__
#define __NET_ETHERNET_H__
#include "esp_err.h"
#include "esp_netif.h"
#include "driver/spi_master.h"
#include <stdint.h>


typedef struct
{
    spi_host_device_t spi_host;
    int sclk_gpio;
    int mosi_gpio;
    int miso_gpio;
    int cs_gpio;
    int int_gpio;
    int phy_rst_gpio;
    int clock_mhz;
    int phy_addr;
} net_eth_config_t;

esp_err_t net_ethernet_init(const net_eth_config_t *cfg, esp_netif_t **out_netif);

esp_err_t net_ethernet_start(void);
esp_err_t net_ethernet_stop(void);

void net_ethernet_deinit(void);

esp_netif_t *net_ethernet_get_netif(void);

#endif
