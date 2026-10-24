#include "device_id.h"
#include "esp_mac.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "device_id";

static bool s_ready = false;
static uint8_t s_mac[6];
static char s_id_str[13];

void device_id_init(void)
{
    if (s_ready)
        return;

    /* MAC GOC do Espressif ghi vao eFuse luc san xuat: khong doi theo cau hinh WiFi/Ethernet,
     * khong doi khi nap lai firmware -> dung lam dinh danh co dinh cua chuong rua. */
    esp_err_t err = esp_read_mac(s_mac, ESP_MAC_EFUSE_FACTORY);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_read_mac failed: %s -- dung MAC gia 000000000000", esp_err_to_name(err));
        memset(s_mac, 0, sizeof(s_mac));
    }

    snprintf(s_id_str, sizeof(s_id_str), "%02X%02X%02X%02X%02X%02X",
             s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5]);

    s_ready = true;
    ESP_LOGI(TAG, "device_id = %s", s_id_str);
}

const char *device_id_get(void)
{
    if (!s_ready)
        device_id_init();
    return s_id_str;
}

void device_id_get_mac(uint8_t out_mac[6])
{
    if (!s_ready)
        device_id_init();
    if (out_mac)
        memcpy(out_mac, s_mac, 6);
}