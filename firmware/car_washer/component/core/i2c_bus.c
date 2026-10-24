#include "i2c_bus.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "i2c_bus";

typedef struct
{
    bool used;
    i2c_port_t port;
    int sda_gpio, scl_gpio;
    i2c_master_bus_handle_t handle;
} i2c_bus_slot_t;

static i2c_bus_slot_t s_slots[I2C_BUS_MAX_INSTANCES] = {0};

static i2c_bus_slot_t *find_slot(i2c_port_t port)
{
    for (int i = 0; i < I2C_BUS_MAX_INSTANCES; i++)
        if (s_slots[i].used && s_slots[i].port == port)
            return &s_slots[i];
    return NULL;
}

static i2c_bus_slot_t *find_free_slot(void)
{
    for (int i = 0; i < I2C_BUS_MAX_INSTANCES; i++)
        if (!s_slots[i].used)
            return &s_slots[i];
    return NULL;
}

esp_err_t i2c_bus_init(i2c_port_t port, int sda_gpio, int scl_gpio, uint32_t glitch_ignore_cnt)
{
    i2c_bus_slot_t *slot = find_slot(port);
    if (slot)
    {
        if (slot->sda_gpio == sda_gpio && slot->scl_gpio == scl_gpio)
            return ESP_OK;

        ESP_LOGE(TAG, "port %d da init voi sda=%d scl=%d, khong the doi sang sda=%d scl=%d",
                 port, slot->sda_gpio, slot->scl_gpio, sda_gpio, scl_gpio);
        return ESP_ERR_INVALID_STATE;
    }

    slot = find_free_slot();
    if (!slot)
    {
        ESP_LOGE(TAG, "het slot bus (toi da %d bus dong thoi)", I2C_BUS_MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = port,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = glitch_ignore_cnt ? glitch_ignore_cnt : 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &slot->handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "i2c_new_master_bus failed (port=%d): %s", port, esp_err_to_name(err));
        return err;
    }

    slot->used = true;
    slot->port = port;
    slot->sda_gpio = sda_gpio;
    slot->scl_gpio = scl_gpio;

    ESP_LOGI(TAG, "i2c_bus_init OK (port=%d sda=%d scl=%d)", port, sda_gpio, scl_gpio);
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_get_handle(i2c_port_t port)
{
    i2c_bus_slot_t *slot = find_slot(port);
    return slot ? slot->handle : NULL;
}

bool i2c_bus_is_ready(i2c_port_t port)
{
    return find_slot(port) != NULL;
}