#include "net_gsm.h"
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "../espressif_esp_modem_1.1.0/include/esp_modem_api.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "app_config.h"

static const char *TAG = "net_gsm";

static EventGroupHandle_t s_events = NULL;
static const int CONNECT_BIT = BIT0;
static const int STOP_CMD_BIT = BIT1;
static const int STOPPED_BIT = BIT2;
static const int MODEM_NEED_RESTART_BIT = BIT3;

/* Backoff khi PPP thất bại */
static const int gsm_backoff_ms[] = {5000};
#define GSM_BACKOFF_COUNT (sizeof(gsm_backoff_ms) / sizeof(gsm_backoff_ms[0]))
static int s_fail_count = 0;

static int gsm_get_backoff_ms(void)
{
    int idx = s_fail_count;
    if (idx >= (int)GSM_BACKOFF_COUNT)
        idx = GSM_BACKOFF_COUNT - 1;
    return gsm_backoff_ms[idx];
}

static SemaphoreHandle_t s_lock = NULL;
static bool s_started = false;
static TaskHandle_t s_task = NULL;

static net_gsm_config_t s_cfg;
static esp_modem_dce_t *s_dce = NULL;
static esp_netif_t *s_ppp_netif = NULL;
static net_gsm_netif_updated_cb_t s_netif_cb = NULL;

/* ================= Event handlers ================= */
static void on_ppp_status_changed(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == NETIF_PPP_ERRORUSER)
    {
        ESP_LOGW(TAG, "PPP error / user-interrupted event tu esp-netif");
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == IP_EVENT_PPP_GOT_IP)
    {
        gpio_set_level(LED_DEBUG_GPIO, 1);
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        s_ppp_netif = event->esp_netif;

        ESP_LOGI(TAG, "PPP connected, IP:" IPSTR, IP2STR(&event->ip_info.ip));

        if (s_netif_cb)
            s_netif_cb(s_ppp_netif);

        xEventGroupSetBits(s_events, CONNECT_BIT);
    }
    else if (id == IP_EVENT_PPP_LOST_IP)
    {
        ESP_LOGW(TAG, "PPP lost IP -> yeu cau restart modem");
        xEventGroupSetBits(s_events, MODEM_NEED_RESTART_BIT);
    }
}

/* ================= PPP session lifecycle ================= */
static void modem_power_pulse(void)
{
    if (s_cfg.pwrkey_gpio >= 0)
    {
        ESP_LOGI(TAG, "pulse pwrkey");
        gpio_set_level(s_cfg.pwrkey_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
        gpio_set_level(s_cfg.pwrkey_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (s_cfg.reset_gpio >= 0)
    {
        ESP_LOGI(TAG, "pulse reset");
        gpio_set_level(s_cfg.reset_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(2500));
        gpio_set_level(s_cfg.reset_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static bool ppp_session_open(void)
{
    modem_power_pulse();

    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG(s_cfg.apn);
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_PPP();
    s_ppp_netif = esp_netif_new(&netif_cfg);
    if (!s_ppp_netif)
    {
        ESP_LOGE(TAG, "esp_netif_new(PPP) failed");
        return false;
    }

    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte_config.uart_config.port_num = s_cfg.uart_num;
    dte_config.uart_config.tx_io_num = s_cfg.tx_gpio;
    dte_config.uart_config.rx_io_num = s_cfg.rx_gpio;
    dte_config.uart_config.rts_io_num = -1;
    dte_config.uart_config.cts_io_num = -1;
    dte_config.uart_config.flow_control = ESP_MODEM_FLOW_CONTROL_NONE;
    dte_config.uart_config.rx_buffer_size = 1024;
    dte_config.uart_config.tx_buffer_size = 1024;
    dte_config.uart_config.event_queue_size = 30;
    dte_config.task_stack_size = 4096;
    dte_config.task_priority = 5;
    dte_config.dte_buffer_size = 512;

    s_dce = esp_modem_new(&dte_config, &dce_config, s_ppp_netif);
    if (!s_dce)
    {
        ESP_LOGE(TAG, "esp_modem_new failed");
        return false;
    }

    xEventGroupClearBits(s_events, CONNECT_BIT);

    int rssi = 0, ber = 0;
    bool signal_ok = false;
    for (int tries = 0; tries < 20; tries++)
    {
        esp_err_t err = esp_modem_get_signal_quality(s_dce, &rssi, &ber);
        if (err == ESP_OK && rssi > 0 && rssi < 99)
        {
            ESP_LOGI(TAG, "Signal quality OK: rssi=%d ber=%d", rssi, ber);
            signal_ok = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!signal_ok)
    {
        ESP_LOGE(TAG, "Khong bat duoc song sau 20 lan thu");
        return false;
    }

    esp_err_t err = esp_modem_set_mode(s_dce, ESP_MODEM_MODE_DATA);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "set DATA mode failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

static void ppp_session_close(void)
{
    if (s_dce)
    {
        esp_modem_destroy(s_dce);
        s_dce = NULL;
    }
    if (s_ppp_netif)
    {
        esp_netif_destroy(s_ppp_netif);
        s_ppp_netif = NULL;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
}

static void gsm_task(void *arg)
{
    for (;;)
    {
        if (!ppp_session_open())
        {
            ppp_session_close();
            int delay_ms = gsm_get_backoff_ms();
            if (s_fail_count < GSM_BACKOFF_COUNT - 1)
                s_fail_count++;
            ESP_LOGW(TAG, "Modem/song chua san sang, thu lai sau %d ms (fail_count=%d)", delay_ms, s_fail_count);

            EventBits_t bits = xEventGroupWaitBits(s_events, STOP_CMD_BIT, pdTRUE, pdTRUE, pdMS_TO_TICKS(delay_ms));
            if (bits & STOP_CMD_BIT)
            {
                xEventGroupSetBits(s_events, STOPPED_BIT);
                vTaskDelete(NULL);
            }
            continue;
        }

        EventBits_t connected = xEventGroupWaitBits(s_events, CONNECT_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(6000));
        if (!(connected & CONNECT_BIT))
        {
            int delay_ms = gsm_get_backoff_ms();
            if (s_fail_count < GSM_BACKOFF_COUNT - 1)
                s_fail_count++;
            ESP_LOGW(TAG, "PPP handshake timeout -> restart modem, cho %d ms truoc lan sau (fail_count=%d)",
                     delay_ms, s_fail_count);
            ppp_session_close();

            EventBits_t bits = xEventGroupWaitBits(s_events, STOP_CMD_BIT, pdTRUE, pdTRUE, pdMS_TO_TICKS(delay_ms));
            if (bits & STOP_CMD_BIT)
            {
                xEventGroupSetBits(s_events, STOPPED_BIT);
                vTaskDelete(NULL);
            }
            continue;
        }

        s_fail_count = 0;
        EventBits_t bits = xEventGroupWaitBits(s_events, STOP_CMD_BIT | MODEM_NEED_RESTART_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        ppp_session_close();

        if (bits & STOP_CMD_BIT)
        {
            xEventGroupSetBits(s_events, STOPPED_BIT);
            vTaskDelete(NULL);
        }
    }
}

/* ================= Public API ================= */
esp_err_t net_gsm_init(const net_gsm_config_t *cfg)
{
    if (!cfg)
        return ESP_FAIL;
    s_cfg = *cfg;

    gpio_config_t io_conf = {
        .pin_bit_mask = 0,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (s_cfg.reset_gpio >= 0)
        io_conf.pin_bit_mask |= (1ULL << s_cfg.reset_gpio);
    if (s_cfg.pwrkey_gpio >= 0)
        io_conf.pin_bit_mask |= (1ULL << s_cfg.pwrkey_gpio);
    if (io_conf.pin_bit_mask)
        gpio_config(&io_conf);

    esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, NULL);
    esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, &on_ppp_status_changed, NULL);

    s_events = xEventGroupCreate();
    s_lock = xSemaphoreCreateMutex();

    ESP_LOGI(TAG, "net_gsm_init OK (uart=%d tx=%d rx=%d apn=%s)",
             s_cfg.uart_num, s_cfg.tx_gpio, s_cfg.rx_gpio, s_cfg.apn ? s_cfg.apn : "(null)");
    return ESP_OK;
}

void net_gsm_set_netif_updated_cb(net_gsm_netif_updated_cb_t cb)
{
    s_netif_cb = cb;
}

void net_gsm_start(void)
{
    if (s_started)
    {
        ESP_LOGW(TAG, "da start roi, bo qua");
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    xTaskCreatePinnedToCore(gsm_task, "net_gsm_task", 4096, NULL, 1, &s_task, 0);
    s_started = true;
    xSemaphoreGive(s_lock);
}

void net_gsm_stop(void)
{
    if (!s_started)
    {
        ESP_LOGW(TAG, "chua start, bo qua");
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    xEventGroupSetBits(s_events, STOP_CMD_BIT);
    xEventGroupWaitBits(s_events, STOPPED_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
    s_started = false;
    s_task = NULL;
    xSemaphoreGive(s_lock);
}

esp_netif_t *net_gsm_get_netif(void)
{
    return s_ppp_netif;
}