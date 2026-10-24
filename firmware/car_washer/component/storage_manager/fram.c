#include "fram.h"
#include "i2c_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "fram";

#define FRAM_DEFAULT_ADDR_7BIT 0x50
#define FRAM_DEFAULT_CLK_HZ 400000
#define FRAM_IO_TIMEOUT_MS 1000

static i2c_master_dev_handle_t s_dev = NULL;
static SemaphoreHandle_t s_lock = NULL;
static size_t s_capacity = 0;
static bool s_ready = false;

esp_err_t fram_init(const fram_config_t *cfg)
{
    if (!cfg)
        return ESP_ERR_INVALID_ARG;
    if (s_ready)
    {
        ESP_LOGW(TAG, "fram_init da goi roi, bo qua");
        return ESP_OK;
    }

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock)
        return ESP_ERR_NO_MEM;

    esp_err_t err = i2c_bus_init(cfg->i2c_port, cfg->sda_gpio, cfg->scl_gpio, 7);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "i2c_bus_init failed: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = cfg->i2c_addr_7bit ? cfg->i2c_addr_7bit : FRAM_DEFAULT_ADDR_7BIT,
        .scl_speed_hz = cfg->clk_speed_hz ? cfg->clk_speed_hz : FRAM_DEFAULT_CLK_HZ,
    };
    err = i2c_master_bus_add_device(i2c_bus_get_handle(cfg->i2c_port), &dev_cfg, &s_dev);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
        return err;
    }

    s_capacity = cfg->capacity_bytes;
    s_ready = true;
    ESP_LOGI(TAG, "fram_init OK (port=%d sda=%d scl=%d addr=0x%02X cap=%u byte)",
             cfg->i2c_port, cfg->sda_gpio, cfg->scl_gpio, dev_cfg.device_address, (unsigned)s_capacity);
    return ESP_OK;
}

bool fram_is_ready(void)
{
    return s_ready;
}

size_t fram_get_capacity(void)
{
    return s_capacity;
}

esp_err_t fram_read(uint16_t addr, void *buf, size_t len)
{
    if (!s_ready || !buf)
        return ESP_ERR_INVALID_STATE;

    uint8_t addr_buf[2] = {(uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF)};

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = i2c_master_transmit_receive(s_dev, addr_buf, sizeof(addr_buf),
                                                 (uint8_t *)buf, len, FRAM_IO_TIMEOUT_MS);
    xSemaphoreGive(s_lock);

    if (err != ESP_OK)
        ESP_LOGE(TAG, "read err addr=0x%04X len=%u: %s", addr, (unsigned)len, esp_err_to_name(err));
    return err;
}

esp_err_t fram_write(uint16_t addr, const void *buf, size_t len)
{
    if (!s_ready || !buf)
        return ESP_ERR_INVALID_STATE;

    if (len > 256)
    {
        ESP_LOGE(TAG, "write qua lon (%u byte), tach nho hon 256 byte/lan", (unsigned)len);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t tx[2 + 256];
    tx[0] = (uint8_t)(addr >> 8);
    tx[1] = (uint8_t)(addr & 0xFF);
    memcpy(&tx[2], buf, len);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = i2c_master_transmit(s_dev, tx, 2 + len, FRAM_IO_TIMEOUT_MS);
    xSemaphoreGive(s_lock);

    if (err != ESP_OK)
        ESP_LOGE(TAG, "write err addr=0x%04X len=%u: %s", addr, (unsigned)len, esp_err_to_name(err));
    return err;
}

bool fram_self_test(void)
{
    if (!s_ready)
        return false;

    static const uint32_t pattern = 0xA55A1234;
    uint32_t readback = 0;

    uint16_t test_addr = (s_capacity >= 4) ? (uint16_t)(s_capacity - 4) : 0;

    if (fram_write(test_addr, &pattern, sizeof(pattern)) != ESP_OK)
        return false;
    if (fram_read(test_addr, &readback, sizeof(readback)) != ESP_OK)
        return false;

    bool ok = (readback == pattern);
    if (!ok)
        ESP_LOGE(TAG, "self_test MISMATCH: wrote 0x%08lX doc lai 0x%08lX", (unsigned long)pattern, (unsigned long)readback);
    else
        ESP_LOGI(TAG, "self_test OK");
    return ok;
}