#include "rtc_ds3231.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "rtc_ds3231";

#define DS3231_ADDR 0x68
#define DS3231_CLK_HZ 100000

static i2c_master_dev_handle_t s_dev = NULL;
static bool s_present = false;

static uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t dec2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

esp_err_t rtc_ds3231_init(const rtc_ds3231_config_t *cfg)
{
    if (!cfg)
        return ESP_ERR_INVALID_ARG;

    esp_err_t err = i2c_bus_init(cfg->i2c_port, cfg->sda_gpio, cfg->scl_gpio, 7);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "i2c_bus_init failed: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = cfg->i2c_addr_7bit ? cfg->i2c_addr_7bit : DS3231_ADDR,
        .scl_speed_hz = DS3231_CLK_HZ,
    };
    err = i2c_master_bus_add_device(i2c_bus_get_handle(cfg->i2c_port), &dev_cfg, &s_dev);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t reg = 0x00, dummy;
    err = i2c_master_transmit_receive(s_dev, &reg, 1, &dummy, 1, 1000);
    s_present = (err == ESP_OK);

    if (s_present)
        ESP_LOGI(TAG, "DS3231 connected (port=%d sda=%d scl=%d)", cfg->i2c_port, cfg->sda_gpio, cfg->scl_gpio);
    else
        ESP_LOGW(TAG, "DS3231 khong phan hoi (co gan chip khong / dung dia chi 0x68 khong?)");

    return ESP_OK;
}

bool rtc_ds3231_is_present(void) { return s_present; }

esp_err_t rtc_ds3231_get_time(struct tm *out)
{
    if (!s_present || !out)
        return ESP_ERR_INVALID_STATE;

    uint8_t reg = 0x00;
    uint8_t data[7];
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, data, sizeof(data), 1000);
    if (err != ESP_OK)
        return err;

    memset(out, 0, sizeof(*out));
    out->tm_sec = bcd2dec(data[0] & 0x7F);
    out->tm_min = bcd2dec(data[1]);
    out->tm_hour = bcd2dec(data[2] & 0x3F);
    out->tm_wday = bcd2dec(data[3]);
    out->tm_mday = bcd2dec(data[4]);
    out->tm_mon = bcd2dec(data[5]) - 1;
    out->tm_year = bcd2dec(data[6]) + 100;
    return ESP_OK;
}

esp_err_t rtc_ds3231_set_time(const struct tm *t)
{
    if (!s_present || !t)
        return ESP_ERR_INVALID_STATE;

    uint8_t data[8];
    data[0] = 0x00;
    data[1] = dec2bcd((uint8_t)t->tm_sec);
    data[2] = dec2bcd((uint8_t)t->tm_min);
    data[3] = dec2bcd((uint8_t)t->tm_hour);
    data[4] = dec2bcd((uint8_t)(t->tm_wday ? t->tm_wday : 1));
    data[5] = dec2bcd((uint8_t)t->tm_mday);
    data[6] = dec2bcd((uint8_t)(t->tm_mon + 1));
    data[7] = dec2bcd((uint8_t)(t->tm_year - 100));

    return i2c_master_transmit(s_dev, data, sizeof(data), 1000);
}

bool rtc_ds3231_lost_power(void)
{
    if (!s_present)
        return true;

    uint8_t reg = 0x0F, status;
    if (i2c_master_transmit_receive(s_dev, &reg, 1, &status, 1, 1000) != ESP_OK)
        return true;

    return (status & 0x80) != 0;
}