#include "net_ethernet.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_eth.h"
#include "driver/gpio.h"

static const char *TAG = "net_ethernet";

static esp_eth_handle_t s_eth_handle = NULL;
static esp_eth_netif_glue_handle_t s_eth_glue = NULL;
static esp_netif_t *s_eth_netif = NULL;
static net_eth_config_t s_cfg;

/* ================= SPI / MAC / PHY bring-up ================= */
static esp_err_t spi_bus_init(void)
{
    esp_err_t ret = gpio_install_isr_service(0);
    if (ret == ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(TAG, "GPIO ISR handler already installed, bo qua");
        ret = ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "GPIO ISR install failed");

    spi_bus_config_t buscfg = {
        .miso_io_num = s_cfg.miso_gpio,
        .mosi_io_num = s_cfg.mosi_gpio,
        .sclk_io_num = s_cfg.sclk_gpio,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    return spi_bus_initialize(s_cfg.spi_host, &buscfg, SPI_DMA_CH_AUTO);
}

static esp_err_t eth_init_spi(esp_eth_handle_t *out_handle)
{
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = s_cfg.phy_addr;
    phy_config.reset_gpio_num = s_cfg.phy_rst_gpio;

    spi_device_interface_config_t spi_devcfg = {
        .mode = 0,
        .clock_speed_hz = s_cfg.clock_mhz * 1000 * 1000,
        .queue_size = 20,
        .spics_io_num = s_cfg.cs_gpio,
    };

    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(s_cfg.spi_host, &spi_devcfg);
    w5500_config.int_gpio_num = s_cfg.int_gpio;

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
    if (!mac || !phy)
    {
        ESP_LOGE(TAG, "khong tao duoc mac/phy w5500");
        return ESP_FAIL;
    }

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    esp_err_t err = esp_eth_driver_install(&eth_config, &eth_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "driver install failed: %s", esp_err_to_name(err));
        mac->del(mac);
        phy->del(phy);
        return err;
    }

    uint8_t mac_addr[ETH_ADDR_LEN];
    esp_read_mac(mac_addr, ESP_MAC_ETH);
    err = esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "set MAC failed: %s", esp_err_to_name(err));
        esp_eth_driver_uninstall(eth_handle);
        return err;
    }

    *out_handle = eth_handle;
    return ESP_OK;
}

/* ================= Event handlers ================= */
static void eth_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case ETHERNET_EVENT_CONNECTED:
    {
        uint8_t mac_addr[6] = {0};
        esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Link up, MAC " MACSTR, MAC2STR(mac_addr));
        break;
    }
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Link down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Driver started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Driver stopped");
        break;
    default:
        break;
    }
}

static void got_ip_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR " mask:" IPSTR " gw:" IPSTR,
             IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.netmask), IP2STR(&event->ip_info.gw));
}

/* ================= Public API ================= */
esp_err_t net_ethernet_init(const net_eth_config_t *cfg, esp_netif_t **out_netif)
{
    if (!cfg)
        return ESP_ERR_INVALID_ARG;
    s_cfg = *cfg;

    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL),
        TAG, "register ETH_EVENT failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL),
        TAG, "register IP_EVENT failed");

    ESP_RETURN_ON_ERROR(spi_bus_init(), TAG, "SPI bus init failed");
    ESP_RETURN_ON_ERROR(eth_init_spi(&s_eth_handle), TAG, "eth_init_spi failed");

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_cfg);
    s_eth_glue = esp_eth_new_netif_glue(s_eth_handle);
    ESP_RETURN_ON_FALSE(s_eth_netif != NULL, ESP_FAIL, TAG, "esp_netif_new failed");

    ESP_RETURN_ON_ERROR(esp_netif_attach(s_eth_netif, s_eth_glue), TAG, "netif attach failed");

    if (out_netif)
        *out_netif = s_eth_netif;

    ESP_LOGI(TAG, "net_ethernet_init OK (spi_host=%d cs=%d int=%d)",
             s_cfg.spi_host, s_cfg.cs_gpio, s_cfg.int_gpio);
    return ESP_OK;
}

esp_err_t net_ethernet_start(void)
{
    if (!s_eth_handle)
        return ESP_ERR_INVALID_STATE;
    return esp_eth_start(s_eth_handle);
}

esp_err_t net_ethernet_stop(void)
{
    if (!s_eth_handle)
        return ESP_ERR_INVALID_STATE;
    return esp_eth_stop(s_eth_handle);
}

void net_ethernet_deinit(void)
{
    if (s_eth_handle)
    {
        esp_eth_stop(s_eth_handle);
        esp_eth_driver_uninstall(s_eth_handle);
        s_eth_handle = NULL;
    }
    if (s_eth_netif)
    {
        esp_netif_destroy(s_eth_netif);
        s_eth_netif = NULL;
    }
    if (s_eth_glue)
    {
        esp_eth_del_netif_glue(s_eth_glue);
        s_eth_glue = NULL;
    }
    spi_bus_free(s_cfg.spi_host);
}

esp_netif_t *net_ethernet_get_netif(void)
{
    return s_eth_netif;
}
