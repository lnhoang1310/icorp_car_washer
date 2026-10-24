#ifndef _NET_WIFI_H_
#define _NET_WIFI_H_
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <stdbool.h>
#include <stdint.h>

#define NET_WIFI_MAX_SAVED 15

typedef enum
{
    NET_WIFI_STATUS_IDLE = 0,
    NET_WIFI_STATUS_CONNECTING,
    NET_WIFI_STATUS_WAITING_RESULT,
    NET_WIFI_STATUS_CONNECTED,
    NET_WIFI_STATUS_FAILED,
} net_wifi_status_t;

typedef struct
{
    char ssid[32];
    char pass[64];
} net_wifi_credential_t;

typedef struct
{
    const char *ap_ssid_prefix;
    const char *ap_password;
    const char *fallback_sta_ssid;
    const char *fallback_sta_password;
    bool use_ap_mode;
    int8_t max_tx_power_dbm_x4;
} net_wifi_config_t;

esp_netif_t *net_wifi_init(const net_wifi_config_t *cfg);
esp_netif_t *net_wifi_get_netif(void);
net_wifi_status_t net_wifi_get_status(void);

void net_wifi_api_request_scan(void);
bool net_wifi_api_is_scan_done(void);
int net_wifi_api_get_scan_result(wifi_ap_record_t *out, int max);
void net_wifi_api_connect(const char *ssid, const char *pass);

typedef enum
{
    NET_WIFI_API_CONNECT_IDLE = 0,
    NET_WIFI_API_CONNECT_RUNNING,
    NET_WIFI_API_CONNECT_SUCCESS,
    NET_WIFI_API_CONNECT_FAILED,
} net_wifi_api_connect_state_t;

net_wifi_api_connect_state_t net_wifi_api_connect_state(void);

/* Ngat WiFi dang dung va QUEN mang ssid (xoa khoi danh sach da luu, tat ca WiFi mac dinh trong
 * app_config neu trung ten -- toi lan khoi dong sau). Tu khong tu ket noi lai mang nay; con mang
 * da luu khac thi van tu quet + ket noi nhu binh thuong. Khong chan: lam trong task rieng. */
void net_wifi_api_forget(const char *ssid);

/* netif cua AP (NULL neu use_ap_mode = false) */
esp_netif_t *net_wifi_get_ap_netif(void);

/* Ly do STA mat ket noi gan nhat (wifi_err_reason_t, 0 = chua co). Dung de bao "sai mat khau"/"khong thay mang" */
uint8_t net_wifi_get_last_reason(void);

/* ==== Quản lý danh sách Wi-Fi đã lưu trong NVS (FIFO, mới nhất lên đầu) ==== */
int net_wifi_store_load(net_wifi_credential_t list[NET_WIFI_MAX_SAVED]);
esp_err_t net_wifi_store_add(const char *ssid, const char *pass);
esp_err_t net_wifi_store_clear_all(void);
/* Xoa SSID khoi danh sach da luu. ESP_ERR_NOT_FOUND neu khong co */
esp_err_t net_wifi_store_remove(const char *ssid);
bool net_wifi_store_find_best(const net_wifi_credential_t *saved, int saved_count,
                              const wifi_ap_record_t *scan, int scan_count,
                              net_wifi_credential_t *out_best, int *out_index);

#endif