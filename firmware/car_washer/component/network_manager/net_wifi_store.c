#include "net_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "net_wifi_store";
#define NVS_NS "net_wifi"
#define NVS_KEY "list"

int net_wifi_store_load(net_wifi_credential_t list[NET_WIFI_MAX_SAVED])
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READONLY, &nvs) != ESP_OK)
        return 0;

    size_t required = sizeof(net_wifi_credential_t) * NET_WIFI_MAX_SAVED;
    esp_err_t err = nvs_get_blob(nvs, NVS_KEY, list, &required);
    nvs_close(nvs);
    if (err != ESP_OK)
        return 0;

    int count = 0;
    for (int i = 0; i < NET_WIFI_MAX_SAVED; i++)
    {
        if (strlen(list[i].ssid) > 0)
            count++;
    }
    return count;
}

static esp_err_t store_save_list(const net_wifi_credential_t list[NET_WIFI_MAX_SAVED])
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK)
        return err;

    err = nvs_set_blob(nvs, NVS_KEY, list, sizeof(net_wifi_credential_t) * NET_WIFI_MAX_SAVED);
    if (err == ESP_OK)
        err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

esp_err_t net_wifi_store_add(const char *ssid, const char *pass)
{
    if (!ssid)
        return ESP_ERR_INVALID_ARG;

    net_wifi_credential_t *list = calloc(NET_WIFI_MAX_SAVED, sizeof(net_wifi_credential_t));
    if (!list)
        return ESP_ERR_NO_MEM;
    net_wifi_store_load(list);

    int exist_index = -1;
    for (int i = 0; i < NET_WIFI_MAX_SAVED; i++)
    {
        if (strcmp(list[i].ssid, ssid) == 0 && strcmp(list[i].pass, pass ? pass : "") == 0)
        {
            exist_index = i;
            break;
        }
    }

    if (exist_index == 0)
    {
        ESP_LOGI(TAG, "SSID da o dau danh sach: %s", ssid);
        free(list);
        return ESP_OK;
    }

    if (exist_index > 0)
    {
        net_wifi_credential_t item = list[exist_index];
        for (int i = exist_index; i > 0; i--)
            list[i] = list[i - 1];
        list[0] = item;
        ESP_LOGI(TAG, "Dua SSID da co len dau: %s", ssid);
    }
    else
    {
        for (int i = NET_WIFI_MAX_SAVED - 1; i > 0; i--)
            list[i] = list[i - 1];
        memset(&list[0], 0, sizeof(list[0]));
        strncpy(list[0].ssid, ssid, sizeof(list[0].ssid) - 1);
        if (pass)
            strncpy(list[0].pass, pass, sizeof(list[0].pass) - 1);
        ESP_LOGI(TAG, "Luu SSID moi: %s", ssid);
    }

    esp_err_t err = store_save_list(list);
    free(list);
    return err;
}

esp_err_t net_wifi_store_remove(const char *ssid)
{
    if (!ssid || !ssid[0])
        return ESP_ERR_INVALID_ARG;
    /* 2 danh sach x 15 x 96 byte ~ 2.9 KB: cap phat heap, KHONG de tren stack task (tran stack -> hong heap) */
    net_wifi_credential_t *list = calloc(2 * NET_WIFI_MAX_SAVED, sizeof(net_wifi_credential_t));
    if (!list)
        return ESP_ERR_NO_MEM;
    net_wifi_credential_t *out = list + NET_WIFI_MAX_SAVED;
    net_wifi_store_load(list);

    int n = 0, removed = 0;
    for (int i = 0; i < NET_WIFI_MAX_SAVED; i++)
    {
        if (!list[i].ssid[0])
            continue;
        if (strncmp(list[i].ssid, ssid, sizeof(list[i].ssid)) == 0)
            removed++; /* xoa moi ban luu cua SSID nay (ke ca mat khau khac) */
        else
            out[n++] = list[i];
    }
    esp_err_t err = ESP_ERR_NOT_FOUND;
    if (removed)
    {
        ESP_LOGI(TAG, "Quen SSID: %s (%d ban luu)", ssid, removed);
        err = store_save_list(out);
    }
    free(list);
    return err;
}

esp_err_t net_wifi_store_clear_all(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK)
        return err;

    err = nvs_erase_key(nvs, NVS_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        err = ESP_OK;

    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "Da xoa toan bo danh sach Wi-Fi da luu");
    return err;
}

bool net_wifi_store_find_best(const net_wifi_credential_t *saved, int saved_count,
                               const wifi_ap_record_t *scan, int scan_count,
                               net_wifi_credential_t *out_best, int *out_index)
{
    int best_rssi = -999;
    bool found = false;

    for (int i = 0; i < saved_count; i++)
    {
        for (int j = 0; j < scan_count; j++)
        {
            if (strcmp(saved[i].ssid, (const char *)scan[j].ssid) == 0 && scan[j].rssi > best_rssi)
            {
                best_rssi = scan[j].rssi;
                *out_best = saved[i];
                *out_index = j;
                found = true;
            }
        }
    }
    return found;
}