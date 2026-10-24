#include <string.h>
#include "net_manage.h"
#include "esp_event.h"
#include "esp_log.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "esp_eth.h"
#include "esp_wifi.h"
#include "esp_timer.h"

#if !CONFIG_ESP_NETIF_SET_DNS_PER_DEFAULT_NETIF
#warning "Bat menuconfig: Component config -> ESP NETIF Adapter -> Enable DNS server per interface (tu co nhieu mang)"
#endif

static const char *TAG = "net_manager";
ESP_EVENT_DEFINE_BASE(NETMGR_EVENT);

static esp_netif_t *s_netifs[NM_IF_MAX];
static bool s_has_ip[NM_IF_MAX];
static bool s_link_up[NM_IF_MAX];
static esp_netif_t *s_current_default = NULL;

/* Chong day mang chap chon: link len phai giu NET_ETH_STABLE_MS moi dung */
static bool s_eth_stable = false;
static uint32_t s_eth_gen = 0;          /* tang moi lan link doi -> bo qua hen gio cu */
static esp_timer_handle_t s_eth_timer = NULL;

static nm_if_t s_priority[NM_IF_MAX] = {NM_IF_ETH, NM_IF_WIFI, NM_IF_PPP};

void net_manager_set_priority(const nm_if_t *order, size_t count)
{
    if (count != NM_IF_MAX)
    {
        ESP_LOGE(TAG, "Priority list length mismatch");
        return;
    }
    memcpy(s_priority, order, sizeof(s_priority));
    ESP_LOGI(TAG, "Priority order updated");
    net_manager_evaluate();
}

static void apply_default_netif(esp_netif_t *new_default)
{
    if (s_current_default == new_default)
        return;

    s_current_default = new_default;

    if (new_default)
    {
        esp_netif_set_default_netif(new_default);
        /* DNS phai theo mang chinh: can CONFIG_ESP_NETIF_SET_DNS_PER_DEFAULT_NETIF=y, neu khong DNS cua
         * mang len SAU (thuong la 4G, DNS noi bo nha mang) ghi de len -> tra ten mien that bai tren ETH/WiFi */
        esp_netif_dns_info_t dns = {0};
        esp_netif_get_dns_info(new_default, ESP_NETIF_DNS_MAIN, &dns);
        ESP_LOGI(TAG, "Switch default interface -> %s (DNS " IPSTR ")", esp_netif_get_ifkey(new_default),
                 IP2STR(&dns.ip.u_addr.ip4));

        esp_event_post(NETMGR_EVENT, NETMGR_EVENT_ONLINE, &s_current_default, sizeof(s_current_default), portMAX_DELAY);
    }
    else
    {
        ESP_LOGW(TAG, "Offline");
        esp_event_post(NETMGR_EVENT, NETMGR_EVENT_OFFLINE, NULL, 0, portMAX_DELAY);
    }

    esp_event_post(NETMGR_EVENT, NETMGR_EVENT_DEFAULT_CHANGED, &s_current_default, sizeof(s_current_default), portMAX_DELAY);
}

void net_manager_update_ppp_netif(esp_netif_t *netif)
{
    s_netifs[NM_IF_PPP] = netif;
    ESP_LOGI(TAG, "PPP netif duoc cap nhat runtime");
}

static bool if_ready(nm_if_t t)
{
    return s_netifs[t] && s_link_up[t] && s_has_ip[t] && (t != NM_IF_ETH || s_eth_stable);
}

static esp_netif_t *choose_preferred_netif(void)
{
    for (int i = 0; i < NM_IF_MAX; i++)
    {
        nm_if_t t = s_priority[i];
        if (if_ready(t))
            return s_netifs[t];
    }
    return NULL;
}

/* Bao 1 mang vua mat de cloud_manager ket noi lai MQTT ngay neu dang chay tren mang do */
static void post_if_down(nm_if_t t)
{
    esp_netif_t *n = s_netifs[t];
    if (n)
        esp_event_post(NETMGR_EVENT, NETMGR_EVENT_IF_DOWN, &n, sizeof(n), 0);
}

static void eth_stable_timer_cb(void *arg)
{
    /* chay trong task esp_timer: chuyen ve event loop mac dinh de moi trang thai chi doi o 1 task */
    uint32_t g = s_eth_gen;
    esp_event_post(NETMGR_EVENT, NETMGR_EVENT_ETH_STABLE, &g, sizeof(g), 0);
}

static void eth_stable_restart(void)
{
    s_eth_gen++;
    s_eth_stable = false;
    if (!s_eth_timer)
    {
        esp_timer_create_args_t a = {.callback = eth_stable_timer_cb, .name = "nm_eth_stable"};
        if (esp_timer_create(&a, &s_eth_timer) != ESP_OK)
        {
            s_eth_stable = true; /* khong tao duoc hen gio -> dung luon, nhu truoc */
            return;
        }
    }
    esp_timer_stop(s_eth_timer);
    esp_timer_start_once(s_eth_timer, (uint64_t)NET_ETH_STABLE_MS * 1000ULL);
}

static void netmgr_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id != NETMGR_EVENT_ETH_STABLE || !data)
        return;
    uint32_t gen = *(uint32_t *)data;
    if (gen != s_eth_gen || !s_link_up[NM_IF_ETH])
        return; /* link da doi trong luc cho -> bo */
    s_eth_stable = true;
    ESP_LOGI(TAG, "ETH on dinh %d ms -> duoc dung", NET_ETH_STABLE_MS);
    net_manager_evaluate();
}

void net_manager_evaluate(void)
{
    apply_default_netif(choose_preferred_netif());
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base != IP_EVENT)
        return;

    if (id == IP_EVENT_ETH_GOT_IP)
    {
        ESP_LOGI(TAG, "ETH GOT IP");
        s_has_ip[NM_IF_ETH] = true;
        s_link_up[NM_IF_ETH] = true;
    }
    else if (id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *info = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "WIFI GOT IP: " IPSTR, IP2STR(&info->ip_info.ip));
        s_has_ip[NM_IF_WIFI] = true;
        s_link_up[NM_IF_WIFI] = true;
    }
    else if (id == IP_EVENT_PPP_GOT_IP)
    {
        ESP_LOGI(TAG, "PPP GOT IP");
        s_has_ip[NM_IF_PPP] = true;
        s_link_up[NM_IF_PPP] = true;
    }
    else if (id == IP_EVENT_PPP_LOST_IP)
    {
        ESP_LOGW(TAG, "PPP LOST IP");
        s_has_ip[NM_IF_PPP] = false;
        s_link_up[NM_IF_PPP] = false;
        post_if_down(NM_IF_PPP);
    }
    else
    {
        return;
    }
    net_manager_evaluate();
}

static void eth_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base != ETH_EVENT)
        return;

    if (id == ETHERNET_EVENT_CONNECTED)
    {
        ESP_LOGI(TAG, "ETH LINK UP -> cho on dinh %d ms", NET_ETH_STABLE_MS);
        s_link_up[NM_IF_ETH] = true;
        eth_stable_restart();
    }
    else if (id == ETHERNET_EVENT_DISCONNECTED)
    {
        ESP_LOGW(TAG, "ETH LINK DOWN");
        bool was_ready = if_ready(NM_IF_ETH);
        s_link_up[NM_IF_ETH] = false;
        s_has_ip[NM_IF_ETH] = false;
        s_eth_stable = false;
        s_eth_gen++;
        if (s_eth_timer)
            esp_timer_stop(s_eth_timer);
        if (was_ready)
            post_if_down(NM_IF_ETH);
    }
    else
    {
        return;
    }
    net_manager_evaluate();
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base != WIFI_EVENT)
        return;

    if (id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW(TAG, "WIFI STA DISCONNECTED");
        bool was_ready = if_ready(NM_IF_WIFI);
        s_link_up[NM_IF_WIFI] = false;
        s_has_ip[NM_IF_WIFI] = false;
        if (was_ready)
            post_if_down(NM_IF_WIFI);
        net_manager_evaluate();
    }
    else if (id == WIFI_EVENT_STA_CONNECTED)
    {
        ESP_LOGI(TAG, "WIFI STA CONNECTED");
        s_link_up[NM_IF_WIFI] = true;
    }
}

void net_manager_init(esp_netif_t *eth_netif, esp_netif_t *wifi_netif, esp_netif_t *ppp_netif)
{
    ESP_LOGI(TAG, "net_manager_init");
    memset(s_netifs, 0, sizeof(s_netifs));
    memset(s_has_ip, 0, sizeof(s_has_ip));
    memset(s_link_up, 0, sizeof(s_link_up));

    s_netifs[NM_IF_ETH] = eth_netif;
    s_netifs[NM_IF_WIFI] = wifi_netif;
    s_netifs[NM_IF_PPP] = ppp_netif;

    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &ip_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_GOT_IP, &ip_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_LOST_IP, &ip_event_handler, NULL);

    esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_CONNECTED, &eth_event_handler, NULL);
    esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, &eth_event_handler, NULL);

    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &wifi_event_handler, NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_event_handler, NULL);
    esp_event_handler_register(NETMGR_EVENT, NETMGR_EVENT_ETH_STABLE, &netmgr_event_handler, NULL);

    net_manager_evaluate();
}

esp_netif_t *net_manager_get_default_netif(void)
{
    return s_current_default;
}

bool net_manager_is_primary_available(void)
{
    return (s_link_up[NM_IF_ETH] && s_has_ip[NM_IF_ETH]) ||
           (s_link_up[NM_IF_WIFI] && s_has_ip[NM_IF_WIFI]);
}

bool net_manager_netif_ready(esp_netif_t *n)
{
    for (int t = 0; t < NM_IF_MAX; t++)
        if (n && s_netifs[t] == n)
            return if_ready((nm_if_t)t);
    return false;
}

bool net_manager_has_ready_other(esp_netif_t *exclude)
{
    for (int t = 0; t < NM_IF_MAX; t++)
        if (s_netifs[t] != exclude && if_ready((nm_if_t)t))
            return true;
    return false;
}

const char *net_manager_ready_other_name(esp_netif_t *exclude)
{
    static const char *const names[NM_IF_MAX] = {[NM_IF_ETH] = "eth", [NM_IF_WIFI] = "wifi", [NM_IF_PPP] = "4g"};
    for (int i = 0; i < NM_IF_MAX; i++)
    {
        nm_if_t t = s_priority[i];
        if (s_netifs[t] != exclude && if_ready(t))
            return names[t];
    }
    return NULL;
}

bool net_manager_type_ready(nm_if_t t)
{
    return t < NM_IF_MAX && if_ready(t);
}