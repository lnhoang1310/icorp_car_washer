#include <string.h>
#include "net_manage.h"
#include "esp_event.h"
#include "esp_log.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "esp_eth.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/dns.h"
#include "lwip/ip4.h"
#include "net_ethernet.h"

#if !CONFIG_ESP_NETIF_SET_DNS_PER_DEFAULT_NETIF
#warning "Bat menuconfig: Component config -> ESP NETIF Adapter -> Enable DNS server per interface (tu co nhieu mang)"
#endif

static const char *TAG = "net_manager";
ESP_EVENT_DEFINE_BASE(NETMGR_EVENT);

static esp_netif_t *s_netifs[NM_IF_MAX];
static bool s_has_ip[NM_IF_MAX];
static bool s_link_up[NM_IF_MAX];
static esp_netif_t *s_current_default = NULL;

/* Mang co IP nhung khong toi duoc server (cloud_manager bao) */
#define NM_PENALTY_MS (120 * 1000)  /* tam bo qua mang loi / tat ETH bao lau */
#define NM_ETH_REPLUG_MS 1500       /* "rut day" ETH bao lau khi khoi dong lai lan dau */
static uint8_t s_strikes[NM_IF_MAX];
static int64_t s_bad_until[NM_IF_MAX]; /* esp_timer_get_time() (us); 0 = khong bi phat */
static esp_timer_handle_t s_penalty_timer = NULL;
static volatile bool s_eth_restarting = false;
static const char *const s_if_names[NM_IF_MAX] = {[NM_IF_ETH] = "ETH", [NM_IF_WIFI] = "WiFi", [NM_IF_PPP] = "4G"};


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
    return s_netifs[t] && s_link_up[t] && s_has_ip[t];
}

static bool penalized(nm_if_t t)
{
    return s_bad_until[t] != 0 && esp_timer_get_time() < s_bad_until[t];
}

static esp_netif_t *choose_preferred_netif(void)
{
    /* Uu tien mang khong bi phat; neu chi con mang bi phat thi van dung (con hon khong co mang) */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < NM_IF_MAX; i++)
        {
            nm_if_t t = s_priority[i];
            if (if_ready(t) && (pass == 1 || !penalized(t)))
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
        ESP_LOGI(TAG, "ETH LINK UP");
        s_link_up[NM_IF_ETH] = true;
    }
    else if (id == ETHERNET_EVENT_DISCONNECTED || id == ETHERNET_EVENT_STOP)
    {
        /* esp_eth_stop() khong phat DISCONNECTED -> xu ly STOP nhu rut day */
        ESP_LOGW(TAG, "ETH LINK DOWN%s", id == ETHERNET_EVENT_STOP ? " (driver dung)" : "");
        bool was_ready = if_ready(NM_IF_ETH);
        s_link_up[NM_IF_ETH] = false;
        s_has_ip[NM_IF_ETH] = false;
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
    esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_STOP, &eth_event_handler, NULL);

    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &wifi_event_handler, NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_event_handler, NULL);

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

/* ================= Mang co IP nhung khong toi duoc server ================= */

static int type_of(esp_netif_t *n)
{
    for (int t = 0; t < NM_IF_MAX; t++)
        if (n && s_netifs[t] == n)
            return t;
    return -1;
}

/* Chay trong task lwIP: thu tu netif trong lwIP + mang ma goi DNS se di ra */
static esp_err_t route_probe(void *ctx)
{
    char *out = ctx;
    size_t len = 0;
    out[0] = '\0';
    for (struct netif *n = netif_list; n && len < 150; n = n->next)
    {
        int w = snprintf(out + len, 160 - len, "%c%c%d(%s%s) ", n->name[0], n->name[1], n->num,
                         ip4addr_ntoa(netif_ip4_addr(n)), netif_is_link_up(n) ? "" : ",no-link");
        if (w < 0)
            break;
        len += (size_t)w;
        if (len >= 160)
            return ESP_OK; /* da cat bot */
    }
    const ip_addr_t *d = dns_getserver(0);
    struct netif *r = (d && IP_IS_V4(d) && !ip_addr_isany(d)) ? ip4_route(ip_2_ip4(d)) : NULL;
    if (len < 150)
        snprintf(out + len, 160 - len, "| DNS %s -> %c%c", d ? ipaddr_ntoa(d) : "?",
                 r ? r->name[0] : '-', r ? r->name[1] : '-');
    return ESP_OK;
}

static void log_net_state(void)
{
    for (int t = 0; t < NM_IF_MAX; t++)
    {
        if (!s_netifs[t])
            continue;
        esp_netif_ip_info_t ip = {0};
        esp_netif_dns_info_t dns = {0};
        esp_netif_get_ip_info(s_netifs[t], &ip);
        esp_netif_get_dns_info(s_netifs[t], ESP_NETIF_DNS_MAIN, &dns);
        ESP_LOGW(TAG, "  %-4s link=%d ip=" IPSTR " gw=" IPSTR " dns=" IPSTR "%s%s", s_if_names[t], s_link_up[t],
                 IP2STR(&ip.ip), IP2STR(&ip.gw), IP2STR(&dns.ip.u_addr.ip4),
                 s_netifs[t] == s_current_default ? " <- mang chinh" : "", penalized(t) ? " (dang bi bo qua)" : "");
    }
    char buf[160];
    if (esp_netif_tcpip_exec(route_probe, buf) == ESP_OK)
        ESP_LOGW(TAG, "  lwIP: %s", buf);
}

static void eth_restart_task(void *arg)
{
    uint32_t off_ms = (uint32_t)(uintptr_t)arg;
    ESP_LOGW(TAG, "ETH: tat W5500 %lu giay roi bat lai (nhu rut/cam lai day)", (unsigned long)(off_ms / 1000));
    net_ethernet_stop();
    vTaskDelay(pdMS_TO_TICKS(off_ms));
    esp_err_t e = net_ethernet_start();
    ESP_LOGI(TAG, "ETH: bat lai W5500 (%s)", esp_err_to_name(e));
    s_eth_restarting = false;
    vTaskDelete(NULL);
}

static void eth_restart(uint32_t off_ms)
{
    if (s_eth_restarting)
        return;
    s_eth_restarting = true;
    if (xTaskCreate(eth_restart_task, "eth_rst", 3072, (void *)(uintptr_t)off_ms, 5, NULL) != pdPASS)
        s_eth_restarting = false;
}

static void penalty_timer_cb(void *arg)
{
    ESP_LOGI(TAG, "het thoi gian bo qua mang loi -> chon lai mang theo uu tien");
    net_manager_evaluate();
}

void net_manager_report_unreachable(void)
{
    esp_netif_t *cur = s_current_default;
    int t = type_of(cur);
    if (t < 0)
        return;
    if (s_strikes[t] < 255)
        s_strikes[t]++;
    ESP_LOGW(TAG, "%s co IP nhung KHONG toi duoc server (lan %u). Trang thai mang:", s_if_names[t], s_strikes[t]);
    log_net_state();

    bool other = net_manager_has_ready_other(cur);
    if (t == NM_IF_ETH)
    {
        /* Lan 1: khoi dong lai nhanh. Tu lan 2: con mang khac thi tat ETH lau hon de chay bang mang do */
        eth_restart(s_strikes[t] >= 2 && other ? NM_PENALTY_MS : NM_ETH_REPLUG_MS);
        return;
    }
    if (!other)
    {
        ESP_LOGW(TAG, "khong con mang nao khac -> giu %s, tiep tuc thu", s_if_names[t]);
        return;
    }
    s_bad_until[t] = esp_timer_get_time() + (int64_t)NM_PENALTY_MS * 1000;
    ESP_LOGW(TAG, "tam bo qua %s %d giay, chuyen sang mang khac", s_if_names[t], NM_PENALTY_MS / 1000);
    if (!s_penalty_timer)
    {
        const esp_timer_create_args_t a = {.callback = penalty_timer_cb, .name = "nm_penalty"};
        esp_timer_create(&a, &s_penalty_timer);
    }
    if (s_penalty_timer)
    {
        esp_timer_stop(s_penalty_timer);
        esp_timer_start_once(s_penalty_timer, (uint64_t)NM_PENALTY_MS * 1000 + 100000);
    }
    net_manager_evaluate();
}

void net_manager_report_reachable(void)
{
    int t = type_of(s_current_default);
    if (t >= 0)
    {
        s_strikes[t] = 0;
        s_bad_until[t] = 0;
    }
}