#ifndef _NET_GSM_H_
#define _NET_GSM_H_

#include "esp_err.h"
#include "esp_netif.h"

typedef struct
{
    int uart_num;
    int tx_gpio;
    int rx_gpio;
    int reset_gpio;
    int pwrkey_gpio;
    const char *apn;
} net_gsm_config_t;

typedef void (*net_gsm_netif_updated_cb_t)(esp_netif_t *netif);
esp_err_t net_gsm_init(const net_gsm_config_t *cfg);
void net_gsm_set_netif_updated_cb(net_gsm_netif_updated_cb_t cb);
void net_gsm_start(void);
void net_gsm_stop(void);

esp_netif_t *net_gsm_get_netif(void);

#endif
