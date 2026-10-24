#ifndef __WIFI_PORTAL_H__
#define __WIFI_PORTAL_H__

/*
 * Trang cau hinh WiFi qua AP cua tu (captive portal).
 *   - Dien thoai/laptop vao WiFi cua tu -> may tu bat trang cau hinh (DNS tra moi ten mien ve IP cua AP,
 *     moi duong dan la -> chuyen huong 302 ve trang chu).
 *   - Trang (wifi_portal.html, nhung vao firmware) goi:
 *       GET  /api/status   trang thai mang, WiFi, IP, may chu
 *       GET  /api/scan     quet WiFi xung quanh
 *       POST /api/connect  {"ssid":"..","password":".."} -> ket noi, thanh cong thi luu vao NVS
 * Goi SAU net_wifi_init() (use_ap_mode = true). AP bat lien tuc, khong tat.
 */

#include "esp_err.h"
#include <stdbool.h>

typedef struct
{
    const char *fw_version;    /* hien duoi dong MAC */
    const char *hw_version;
    bool (*server_ready)(void); /* tu da ket noi may chu chua (NULL = khong bao) */
} wifi_portal_config_t;

esp_err_t wifi_portal_start(const wifi_portal_config_t *cfg);

#endif