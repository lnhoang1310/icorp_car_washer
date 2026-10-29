#include <stdio.h>
#include "app_config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "device_id.h"
#include "net_manage.h"
#include "net_ethernet.h"
#include "net_gsm.h"
#include "net_wifi.h"
#include "net_wifi_portal.h"
#include "fram.h"
#include "rtc_ds3231.h"
#include "ntp_time.h"
#include "io_expander.h"
#include "device_config.h"
#include "app_fsm.h"
#include "cloud_manager.h"
#include "ui_manager.h"
#include "audio_player.h"
#include "peripheral_test.h"
#include "driver/gpio.h"
#include "esp_random.h"

/* 1 = chay test ngoai vi luc khoi dong (chon bai test trong peripheral_test.c) */
#define APP_RUN_PERIPHERAL_TEST 0

static const char *TAG = "main";

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS loi dinh dang -> xoa va khoi tao lai");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void init_storage_and_time(void)
{
    fram_config_t fcfg = {
        .i2c_port = FRAM_I2C_PORT,
        .sda_gpio = FRAM_SDA_GPIO,
        .scl_gpio = FRAM_SCL_GPIO,
        .clk_speed_hz = 0,
        .i2c_addr_7bit = FRAM_I2C_ADDR,
        .capacity_bytes = FRAM_CAPACITY_BYTES,
    };
    if (fram_init(&fcfg) != ESP_OK)
        ESP_LOGE(TAG, "FRAM loi -- phien rua se KHONG duoc luu khi mat dien");

    rtc_ds3231_config_t rcfg = {
        .i2c_port = DS3231_I2C_PORT,
        .sda_gpio = DS3231_SDA_GPIO,
        .scl_gpio = DS3231_SCL_GPIO,
        .i2c_addr_7bit = DS3231_I2C_ADDR,
    };
    if (rtc_ds3231_init(&rcfg) != ESP_OK)
        ESP_LOGE(TAG, "DS3231 loi -- gio chi co sau khi dong bo NTP");

    ntp_time_init(); /* dat mui gio VN + lay gio tu RTC */
}

static void init_network(void)
{
    ESP_LOGI(TAG, "=========== Network init ==========");
    net_eth_config_t eth_cfg = {
        .spi_host = ETH_SPI_HOST,
        .sclk_gpio = ETH_SPI_SCLK_GPIO,
        .mosi_gpio = ETH_SPI_MOSI_GPIO,
        .miso_gpio = ETH_SPI_MISO_GPIO,
        .cs_gpio = ETH_SPI_CS0_GPIO,
        .int_gpio = ETH_SPI_INT0_GPIO,
        .phy_rst_gpio = ETH_SPI_PHY_RST0_GPIO,
        .clock_mhz = CLOCK_SPI_MHZ,
        .phy_addr = 1,
    };
    esp_netif_t *eth_netif = NULL;
    if (net_ethernet_init(&eth_cfg, &eth_netif) != ESP_OK)
    {
        gpio_set_level(LED_DEBUG_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
        gpio_set_level(LED_DEBUG_GPIO, 0);
    }
    net_ethernet_start();

    net_wifi_config_t wifi_cfg = {
        .ap_ssid_prefix = WIFI_AP_PREFIX,
        .ap_password = WIFI_AP_PASSWORD,
        .fallback_sta_ssid = WIFI_SSID,
        .fallback_sta_password = WIFI_PASSWORD,
        .max_tx_power_dbm_x4 = 80,
        .use_ap_mode = true,
    };
    esp_netif_t *wifi_netif = net_wifi_init(&wifi_cfg);

    net_gsm_config_t gsm_cfg = {
        .uart_num = SIM_UART_NUM,
        .tx_gpio = SIM_TX_PIN,
        .rx_gpio = SIM_RX_PIN,
        .reset_gpio = SIM_RESET_PIN,
        .pwrkey_gpio = SIM_PEN_PIN,
        .apn = SIM_APN,
    };
    net_gsm_init(&gsm_cfg);
    net_gsm_set_netif_updated_cb(net_manager_update_ppp_netif);
    net_gsm_start();

    net_manager_init(eth_netif, wifi_netif, net_gsm_get_netif());
}

static uint32_t rand_ms(uint32_t min_ms, uint32_t max_ms)
{
    return min_ms + esp_random() % (max_ms - min_ms + 1);
}

static void led_debug_task(void *arg)
{
    while (1)
    {
        gpio_set_level(LED_DEBUG_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(rand_ms(20, 300)));
        gpio_set_level(LED_DEBUG_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(rand_ms(20, 300)));
    }
}

void app_main(void)
{
    /* NVS truoc: app_fsm_init() doc danh sach phien da ket thuc (done_ids) tu NVS */
    init_nvs();

    /* Tat relay cang som cang tot */
    ESP_ERROR_CHECK(app_fsm_init());
    if (io_expander_init(app_fsm_on_input, NULL) != ESP_OK)
        ESP_LOGE(TAG, "io_expander loi -- khong dieu khien duoc bom/den");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    device_id_init();
    ESP_LOGI(TAG, "device_id = %s", device_id_get());

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_DEBUG_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(LED_DEBUG_GPIO, 0);
    xTaskCreatePinnedToCore(led_debug_task, "led_debug_task", 1024, NULL, 1, NULL, 1);

#if APP_RUN_PERIPHERAL_TEST
    peripheral_test_run();
#endif

    init_storage_and_time();
    device_config_init();

    /* Man hinh TRUOC mang: 2 buffer ve LVGL phai nam trong RAM noi co DMA. Khoi tao mang truoc
     * thi WiFi/Ethernet/4G chiem mat vung RAM nay -> "Not enough memory for LVGL buffer" -> mat man hinh.
     * Chi mat ~0,4 giay; hien "Dang ket noi..." som hon. */
    ui_init();

    /* Mang + server truoc cam ung / am thanh: Ethernet can ~2 giay dam phan link, WiFi ket noi, 4G dang ky
     * mang... chay nen song song voi phan con lai -> co Internet som hon */
    init_network();
    ntp_time_start_sync();
    cloud_init();
    wifi_portal_config_t portal = {.fw_version = FW_VERSION, .hw_version = HW_VERSION, .server_ready = cloud_is_ready};
    wifi_portal_start(&portal);

    peripheral_test_after_ui(); /* hieu chinh cam ung neu chua co (xem PT_TOUCH_CALIB) */
    audio_init();

    /* 5. Chay may trang thai (tu khoi phuc phien neu mat dien giua chung) */
    ESP_ERROR_CHECK(app_fsm_start());
    ESP_LOGI(TAG, "khoi dong xong");
}