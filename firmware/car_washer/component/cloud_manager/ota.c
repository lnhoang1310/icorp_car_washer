#include "ota.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <inttypes.h>

#if CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
#include "esp_efuse.h"
#endif

static const char *TAG = "ota";

#define OTA_URL_MAX_LEN 256

static char s_url[OTA_URL_MAX_LEN];
static volatile bool s_running = false;
static uint8_t s_percent = 0;

static ota_progress_cb_t s_cb = NULL;
static void *s_cb_ctx = NULL;

static void report(const char *state, uint8_t percent)
{
    s_percent = percent;
    if (s_cb)
        s_cb(state, percent, s_cb_ctx);
}

static void https_ota_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;
    if (base != ESP_HTTPS_OTA_EVENT)
        return;

    switch (event_id)
    {
    case ESP_HTTPS_OTA_START:
        ESP_LOGI(TAG, "OTA started");
        break;
    case ESP_HTTPS_OTA_CONNECTED:
        ESP_LOGI(TAG, "Connected to server");
        break;
    case ESP_HTTPS_OTA_GET_IMG_DESC:
        ESP_LOGI(TAG, "Reading image description");
        break;
    case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
        ESP_LOGI(TAG, "Verifying chip id of new image: %d", *(esp_chip_id_t *)event_data);
        break;
    case ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION:
        ESP_LOGI(TAG, "Boot partition updated, next partition: %d", *(esp_partition_subtype_t *)event_data);
        break;
    case ESP_HTTPS_OTA_FINISH:
        ESP_LOGI(TAG, "OTA finish");
        break;
    case ESP_HTTPS_OTA_ABORT:
        ESP_LOGW(TAG, "OTA abort");
        break;
    default:
        break;
    }
}

static esp_err_t http_client_init_cb(esp_http_client_handle_t http_client)
{
    (void)http_client;
    return ESP_OK;
}

static void ota_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Bat dau OTA tu %s", s_url);
    report("start", 0);

    esp_http_client_config_t http_cfg = {
        .url = s_url,
        .skip_cert_common_name_check = true,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
        .http_client_init_cb = http_client_init_cb,
    };

    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_cfg, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_https_ota_begin that bai: %s", esp_err_to_name(err));
        goto fail;
    }

    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(handle, &app_desc);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Doc app_desc that bai: %s", esp_err_to_name(err));
        goto abort_ota;
    }
    ESP_LOGI(TAG, "Firmware moi: version=%s", app_desc.version);

#if CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
    {
        uint32_t hw_sec_version = esp_efuse_read_secure_version();
        if (app_desc.secure_version < hw_sec_version)
        {
            ESP_LOGE(TAG, "Secure version qua thap: %" PRIu32 " < %" PRIu32, app_desc.secure_version, hw_sec_version);
            goto abort_ota;
        }
    }
#endif

    int64_t last_report_us = 0;
    while (1)
    {
        err = esp_https_ota_perform(handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS)
            break;

        int img_size = esp_https_ota_get_image_size(handle);
        if (img_size <= 0)
            continue;

        uint8_t percent = (uint8_t)((int64_t)esp_https_ota_get_image_len_read(handle) * 100 / img_size);
        int64_t now = esp_timer_get_time();
        if (percent != s_percent && (now - last_report_us) >= 5000000LL)
        {
            last_report_us = now;
            ESP_LOGI(TAG, "OTA %u%%", percent);
            report("updating", percent);
        }
    }

    if (!esp_https_ota_is_complete_data_received(handle))
    {
        ESP_LOGE(TAG, "Chua nhan du du lieu firmware");
        goto abort_ota;
    }

    err = esp_https_ota_finish(handle);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "OTA thanh cong -- reboot sau 2s");
        report("success", 100);
        s_running = false;
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }

    if (err == ESP_ERR_OTA_VALIDATE_FAILED)
        ESP_LOGE(TAG, "Anh firmware khong hop le (validate that bai)");
    ESP_LOGE(TAG, "esp_https_ota_finish that bai: 0x%x", err);
    goto fail;

abort_ota:
    esp_https_ota_abort(handle);

fail:
    ESP_LOGE(TAG, "OTA THAT BAI -- giu nguyen firmware hien tai");
    report("failed", s_percent);
    s_running = false;
    vTaskDelete(NULL);
}

void ota_manager_init(ota_progress_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;

    esp_err_t err = esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, https_ota_event_handler, NULL);
    if (err != ESP_OK)
        ESP_LOGW(TAG, "esp_event_handler_register that bai: %s", esp_err_to_name(err));

#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    /* Xác nhận fw mới ok -> ko rollback */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK && ota_state == ESP_OTA_IMG_PENDING_VERIFY)
    {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
            ESP_LOGI(TAG, "Firmware hop le, da huy rollback");
        else
            ESP_LOGE(TAG, "Huy rollback that bai");
    }
#endif

    ESP_LOGI(TAG, "ota_manager_init OK");
}

esp_err_t ota_manager_start(const char *url)
{
    if (!url || strlen(url) == 0)
        return ESP_ERR_INVALID_ARG;
    if (s_running)
    {
        ESP_LOGW(TAG, "Dang co 1 OTA chay roi -- bo qua yeu cau moi");
        return ESP_ERR_INVALID_STATE;
    }

    strlcpy(s_url, url, sizeof(s_url));
    s_running = true;
    s_percent = 0;

    if (xTaskCreate(ota_task, "ota_task", 8192, NULL, 5, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Tao ota_task that bai");
        s_running = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool ota_manager_is_running(void) { return s_running; }
uint8_t ota_manager_get_percent(void) { return s_percent; }