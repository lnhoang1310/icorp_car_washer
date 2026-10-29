#include "net_wifi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "net_wifi";

static esp_netif_t *s_sta_netif = NULL;
static net_wifi_status_t s_status = NET_WIFI_STATUS_IDLE;

static bool s_sta_connecting = false;
static int s_scan_retry = 0;
static esp_timer_handle_t s_scan_timer = NULL;

static bool s_scan_running = false;
static bool s_scan_done = true;
static bool s_scan_requested_by_api = false;

static wifi_ap_record_t *s_scan_results = NULL;
static int s_scan_count = 0;

static bool s_api_connect_running = false;
static bool s_api_connect_success = false;
static bool s_api_connect_fail = false;
static int s_api_connect_attempts = 0;
static bool s_fallback_sta_used = false;
static wifi_config_t s_fallback_cfg;        /* WiFi mac dinh (app_config) -> khoi phuc neu ket noi qua web that bai */
static esp_netif_t *s_ap_netif = NULL;
static uint8_t s_last_reason = 0;           /* ly do mat ket noi STA gan nhat (wifi_err_reason_t) */
static char s_api_ssid[33];
static char s_api_pass[65];
static char s_forget_ssid[33];
#define API_CONNECT_MAX_ATTEMPTS 2

/* backoff scan */
static const int scan_backoff_ms[] = {5000, 10000, 30000, 60000, 120000, 300000, 600000};
#define SCAN_BACKOFF_COUNT (sizeof(scan_backoff_ms) / sizeof(scan_backoff_ms[0]))

static void request_scan(void);
static void schedule_next_scan(void);
static net_wifi_credential_t s_tmp_saved[NET_WIFI_MAX_SAVED];

/* ================= Ket noi nhanh luc khoi dong =================
 * Nho AP ket noi thanh cong gan nhat (SSID + BSSID + kenh). Khoi dong: neu SSID do van con trong danh sach
 * da luu -> ket noi THANG vao AP do tren dung kenh, bo qua buoc quet (~3 giay). That bai -> quet nhu cu. */
#define LAST_AP_NS "net_wifi_last"
#define LAST_AP_KEY "ap"
typedef struct
{
    char ssid[33];
    uint8_t bssid[6];
    uint8_t channel;
    uint8_t authmode;
} wifi_last_ap_t;
static bool s_fast_connect = false; /* dang thu ket noi nhanh */

static bool last_ap_load(wifi_last_ap_t *o)
{
    nvs_handle_t h;
    if (nvs_open(LAST_AP_NS, NVS_READONLY, &h) != ESP_OK)
        return false;
    size_t n = sizeof(*o);
    esp_err_t e = nvs_get_blob(h, LAST_AP_KEY, o, &n);
    nvs_close(h);
    o->ssid[sizeof(o->ssid) - 1] = '\0';
    return e == ESP_OK && n == sizeof(*o) && o->ssid[0];
}

static void last_ap_save_current(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK)
        return;
    wifi_last_ap_t cur = {0}, old = {0};
    strlcpy(cur.ssid, (const char *)ap.ssid, sizeof(cur.ssid));
    memcpy(cur.bssid, ap.bssid, 6);
    cur.channel = ap.primary;
    cur.authmode = (uint8_t)ap.authmode;
    if (last_ap_load(&old) && memcmp(&old, &cur, sizeof(cur)) == 0)
        return; /* khong doi -> khong ghi flash */
    nvs_handle_t h;
    if (nvs_open(LAST_AP_NS, NVS_READWRITE, &h) != ESP_OK)
        return;
    if (nvs_set_blob(h, LAST_AP_KEY, &cur, sizeof(cur)) == ESP_OK)
        nvs_commit(h);
    nvs_close(h);
}

/* s_tmp_saved da duoc nap (saved_count mang). true = da bat dau ket noi nhanh */
static bool try_fast_connect(int saved_count)
{
    wifi_last_ap_t last;
    if (!last_ap_load(&last))
        return false;
    for (int i = 0; i < saved_count; i++)
    {
        if (strncmp(s_tmp_saved[i].ssid, last.ssid, sizeof(s_tmp_saved[i].ssid)) != 0)
            continue;
        wifi_config_t cfg = {0};
        strncpy((char *)cfg.sta.ssid, last.ssid, sizeof(cfg.sta.ssid) - 1);
        strncpy((char *)cfg.sta.password, s_tmp_saved[i].pass, sizeof(cfg.sta.password));
        cfg.sta.threshold.authmode = (wifi_auth_mode_t)last.authmode;
        cfg.sta.bssid_set = true;
        memcpy(cfg.sta.bssid, last.bssid, 6);
        cfg.sta.channel = last.channel;
        ESP_LOGI(TAG, "Ket noi nhanh -> %s (kenh %d, khong quet)", last.ssid, last.channel);
        esp_wifi_set_config(WIFI_IF_STA, &cfg);
        s_fast_connect = true;
        s_sta_connecting = true;
        esp_wifi_connect();
        return true;
    }
    return false; /* mang cu da bi quen */
}

/* ================= AP SSID theo MAC ================= */
static void build_ap_ssid(const char *prefix, char *out, size_t max_len)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, max_len, "%s%02X%02X%02X", prefix ? prefix : "DEVICE_", mac[3], mac[4], mac[5]);
}

/* ================= SCAN ================= */
static int get_scan_delay_ms(void)
{
    int idx = s_scan_retry;
    if (idx >= (int)SCAN_BACKOFF_COUNT)
        idx = SCAN_BACKOFF_COUNT - 1;
    return scan_backoff_ms[idx];
}

static void scan_timer_cb(void *arg)
{
    ESP_LOGI(TAG, "Auto scan fired (retry=%d)", s_scan_retry);
    request_scan();
}

static void schedule_next_scan(void)
{
    if (!s_scan_timer)
    {
        esp_timer_create_args_t args = {.callback = scan_timer_cb, .name = "net_wifi_backoff"};
        esp_timer_create(&args, &s_scan_timer);
    }
    int delay_ms = get_scan_delay_ms();
    ESP_LOGI(TAG, "Schedule next scan in %d ms", delay_ms);
    esp_timer_stop(s_scan_timer);
    esp_timer_start_once(s_scan_timer, (uint64_t)delay_ms * 1000ULL);
}

static void request_scan(void)
{
    if (s_scan_running)
        return;
    if (s_api_connect_running || s_sta_connecting)
    {
        ESP_LOGW(TAG, "Bo qua scan: STA dang connecting");
        return;
    }
    s_scan_running = true;
    s_scan_done = false;

    wifi_scan_config_t scan_cfg = {
        .scan_type = WIFI_SCAN_TYPE_PASSIVE,
        .scan_time.passive = 200,
    };
    esp_wifi_scan_start(&scan_cfg, false);
}

static void auto_connect(void)
{
    s_sta_connecting = true;
    if (s_scan_timer)
        esp_timer_stop(s_scan_timer);
    esp_wifi_connect();
}

static void save_wifi_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1000));
    wifi_config_t cfg;
    esp_wifi_get_config(WIFI_IF_STA, &cfg);
    net_wifi_store_add((char *)cfg.sta.ssid, (char *)cfg.sta.password);
    ESP_LOGI(TAG, "Da luu SSID: %s", (char *)cfg.sta.ssid);
    vTaskDelete(NULL);
}

static void handle_scan_done(void)
{
    if (!s_scan_results)
    {
        ESP_LOGE(TAG, "s_scan_results la NULL -- bo qua ket qua scan (xem loi cap phat luc init)");
        s_scan_running = false;
        s_scan_done = true;
        s_scan_requested_by_api = false;
        return;
    }

    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    if (ap_num > NET_WIFI_MAX_SAVED)
        ap_num = NET_WIFI_MAX_SAVED;
    esp_wifi_scan_get_ap_records(&ap_num, s_scan_results);
    s_scan_count = ap_num;

    ESP_LOGI(TAG, "===== SCAN RESULT (%d APs) =====", ap_num);
    for (int i = 0; i < ap_num; i++)
    {
        ESP_LOGI(TAG, "%d) SSID:%s RSSI:%d CH:%d",
                 i + 1, (char *)s_scan_results[i].ssid, s_scan_results[i].rssi, s_scan_results[i].primary);
    }

    s_scan_running = false;
    s_scan_done = true;

    /* Quét theo request từ web, không auto-connect */
    if (s_scan_requested_by_api)
    {
        s_scan_requested_by_api = false;
        ESP_LOGI(TAG, "Scan theo yeu cau API -> xong, khong auto-connect");

        wifi_ap_record_t current_ap;
        bool already_connected = (esp_wifi_sta_get_ap_info(&current_ap) == ESP_OK);
        if (!already_connected)
        {
            memset(s_tmp_saved, 0, sizeof(s_tmp_saved));
            if (net_wifi_store_load(s_tmp_saved) > 0)
            {
                s_scan_retry = 0; /* dang mat WiFi: 5 giay sau tu quet + ket noi lai (truoc day doi 10 phut) */
                schedule_next_scan();
            }
        }
        return;
    }

    /* Auto-connect */
    memset(s_tmp_saved, 0, sizeof(s_tmp_saved));
    int saved_count = net_wifi_store_load(s_tmp_saved);

    net_wifi_credential_t best;
    int best_idx = -1;
    if (net_wifi_store_find_best(s_tmp_saved, saved_count, s_scan_results, ap_num, &best, &best_idx))
    {
        wifi_config_t cfg = {0};
        strncpy((char *)cfg.sta.ssid, best.ssid, sizeof(cfg.sta.ssid) - 1);
        strncpy((char *)cfg.sta.password, best.pass, sizeof(cfg.sta.password) - 1);
        cfg.sta.threshold.authmode = s_scan_results[best_idx].authmode;
        cfg.sta.bssid_set = true;
        memcpy(cfg.sta.bssid, s_scan_results[best_idx].bssid, 6);
        cfg.sta.channel = s_scan_results[best_idx].primary;

        ESP_LOGI(TAG, "Auto-connect -> %s", best.ssid);
        esp_wifi_set_config(WIFI_IF_STA, &cfg);
        auto_connect();
    }
    else
    {
        ESP_LOGW(TAG, "Khong tim thay SSID da luu nao trong ket qua quet");
        s_scan_retry++;
        schedule_next_scan();
    }
}

static void fallback_connect_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "STA fallback used -> auto-connect");
    auto_connect();
    vTaskDelete(NULL);
}

/* ================= EVENT HANDLER ================= */
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        memset(s_tmp_saved, 0, sizeof(s_tmp_saved));
        int saved = net_wifi_store_load(s_tmp_saved);
        if (saved > 0)
        {
            if (!try_fast_connect(saved))
                request_scan();
        }
        else
        {
            if (s_fallback_sta_used)
            {
                ESP_LOGI(TAG, "STA fallback used -> auto-connect");
                xTaskCreate(fallback_connect_task, "net_wifi_fallback", 4096, NULL, tskIDLE_PRIORITY, NULL);
            }
            else
            {
                ESP_LOGW(TAG, "Khong co SSID da luu -> chua ket noi duoc");
                s_scan_retry++;
                schedule_next_scan();
            }
        }
    }
    else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Wi-Fi disconnected, reason=%d", d->reason);
        if (d->reason != WIFI_REASON_ASSOC_LEAVE)
            s_last_reason = d->reason;

        if (d->reason == WIFI_REASON_ASSOC_LEAVE)
        {
            s_fast_connect = false;
            s_sta_connecting = false;
        }
        else if (s_fast_connect && !s_api_connect_running)
        {
            /* AP cu doi kenh / thay router / mat song -> quet tim mang da luu ngay */
            ESP_LOGW(TAG, "Ket noi nhanh that bai -> quet tim mang da luu");
            s_fast_connect = false;
            s_sta_connecting = false;
            request_scan();
        }
        else if (s_api_connect_running)
        {
            s_api_connect_attempts++;
            ESP_LOGI(TAG, "api_connect_attempts=%d", s_api_connect_attempts);
            if (s_api_connect_attempts >= API_CONNECT_MAX_ATTEMPTS)
            {
                s_api_connect_fail = true;
                s_api_connect_running = false;
                s_sta_connecting = false;
                if (s_status == NET_WIFI_STATUS_WAITING_RESULT)
                    s_status = NET_WIFI_STATUS_FAILED;

                /* ket noi qua web that bai -> quay lai WiFi cu de tu khong mat mang */
                memset(s_tmp_saved, 0, sizeof(s_tmp_saved));
                if (net_wifi_store_load(s_tmp_saved) > 0)
                {
                    s_scan_retry = 0;
                    schedule_next_scan();
                }
                else if (s_fallback_sta_used)
                {
                    ESP_LOGI(TAG, "Ket noi qua web that bai -> quay lai WiFi mac dinh");
                    esp_wifi_set_config(WIFI_IF_STA, &s_fallback_cfg);
                    auto_connect();
                }
            }
            else
            {
                auto_connect();
            }
        }
        else
        {
            s_sta_connecting = false;
            if (net_wifi_store_load(s_tmp_saved) > 0)
            {
                s_scan_retry = 0;
                schedule_next_scan();
            }
            else if (s_fallback_sta_used)
            {
                ESP_LOGI(TAG, "Mat ket noi, thu lai fallback SSID");
                auto_connect();
            }
        }
    }
    else if (base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE)
    {
        handle_scan_done();
    }
    else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));

        s_scan_retry = 0;
        s_sta_connecting = false;
        s_fast_connect = false;
        if (s_scan_timer)
            esp_timer_stop(s_scan_timer);
        last_ap_save_current(); /* lan khoi dong sau ket noi thang vao AP nay */

        if (s_api_connect_running)
        {
            s_api_connect_success = true;
            s_api_connect_running = false;
            xTaskCreate(save_wifi_task, "net_wifi_save", 4096, NULL, tskIDLE_PRIORITY, NULL);
        }
        s_api_connect_attempts = 0;
        s_status = NET_WIFI_STATUS_CONNECTED;
    }
    else if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *ev = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " joined AP, AID=%d", MAC2STR(ev->mac), ev->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *ev = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " left AP, AID=%d", MAC2STR(ev->mac), ev->aid);
    }
}

/* ================= PUBLIC API ================= */
esp_netif_t *net_wifi_init(const net_wifi_config_t *cfg)
{
    if (!cfg)
        return NULL;
    esp_netif_t *ap_netif = NULL;
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (cfg->use_ap_mode)
        ap_netif = esp_netif_create_default_wifi_ap();
    s_ap_netif = ap_netif;

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&init_cfg);

    s_scan_results = heap_caps_calloc(NET_WIFI_MAX_SAVED, sizeof(wifi_ap_record_t), MALLOC_CAP_8BIT);
    if (!s_scan_results)
        ESP_LOGE(TAG, "Khong cap phat duoc s_scan_results -- tinh nang scan wifi se khong hoat dong");

    char ap_ssid[32] = {0};
    if (cfg->use_ap_mode)
    {
        wifi_config_t ap_cfg = {
            .ap = {
                .channel = 1,
                .max_connection = 4,
                .authmode = WIFI_AUTH_WPA2_PSK,
                .pmf_cfg = {.capable = true, .required = false},
            },
        };
        build_ap_ssid(cfg->ap_ssid_prefix, ap_ssid, sizeof(ap_ssid));
        strncpy((char *)ap_cfg.ap.ssid, ap_ssid, sizeof(ap_cfg.ap.ssid) - 1);
        ap_cfg.ap.ssid_len = strlen(ap_ssid);
        strncpy((char *)ap_cfg.ap.password, cfg->ap_password ? cfg->ap_password : "12345678",
                sizeof(ap_cfg.ap.password) - 1);

        esp_wifi_set_mode(WIFI_MODE_APSTA);
        esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    }
    else
    {
        esp_wifi_set_mode(WIFI_MODE_STA);
    }

    if (cfg->fallback_sta_ssid && strlen(cfg->fallback_sta_ssid) > 0)
    {
        wifi_config_t sta_cfg = {.sta = {.threshold.authmode = WIFI_AUTH_WPA2_PSK}};
        strncpy((char *)sta_cfg.sta.ssid, cfg->fallback_sta_ssid, sizeof(sta_cfg.sta.ssid) - 1);
        if (cfg->fallback_sta_password)
            strncpy((char *)sta_cfg.sta.password, cfg->fallback_sta_password, sizeof(sta_cfg.sta.password) - 1);
        esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
        s_fallback_cfg = sta_cfg;
        s_fallback_sta_used = true;
    }

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);

    esp_wifi_start();

    /* Turn off power save */
    esp_wifi_set_ps(WIFI_PS_NONE);

    if (cfg->max_tx_power_dbm_x4 > 0)
        esp_wifi_set_max_tx_power(cfg->max_tx_power_dbm_x4);

    if (ap_netif)
    {
        esp_netif_ip_info_t ap_ip = {0};
        esp_netif_get_ip_info(ap_netif, &ap_ip);
        ESP_LOGI(TAG, "AP SSID=%s IP:" IPSTR, ap_ssid, IP2STR(&ap_ip.ip));
    }
    else
    {
        ESP_LOGI(TAG, "WiFi chi chay STA (use_ap_mode = false)");
    }

    return s_sta_netif;
}

net_wifi_status_t net_wifi_get_status(void)
{
    return s_status;
}

esp_netif_t *net_wifi_get_netif(void)
{
    return s_sta_netif;
}

void net_wifi_api_request_scan(void)
{
    s_scan_requested_by_api = true;
    request_scan();
}

bool net_wifi_api_is_scan_done(void)
{
    return s_scan_done;
}

int net_wifi_api_get_scan_result(wifi_ap_record_t *out, int max)
{
    if (!out || !s_scan_results)
        return 0;
    int n = s_scan_count < max ? s_scan_count : max;
    memcpy(out, s_scan_results, n * sizeof(wifi_ap_record_t));
    return n;
}

/* Ngat WiFi dang dung (neu co) roi moi doi cau hinh + ket noi: lam trong task rieng vi phai cho
 * su kien ngat ket noi xu ly xong. */
static void api_connect_task(void *arg)
{
    if (s_scan_timer)
        esp_timer_stop(s_scan_timer);
    if (s_scan_running)
    {
        esp_wifi_scan_stop();
        s_scan_running = false;
        s_scan_done = true;
    }
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, s_api_ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, s_api_pass, sizeof(cfg.sta.password) - 1);
    /* mang khong mat khau -> cho phep OPEN; co mat khau -> toi thieu WPA2 */
    cfg.sta.threshold.authmode = s_api_pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    esp_wifi_set_config(WIFI_IF_STA, &cfg);

    ESP_LOGI(TAG, "Ket noi theo yeu cau web -> %s", s_api_ssid);
    auto_connect();
    vTaskDelete(NULL);
}

void net_wifi_api_connect(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0] || s_api_connect_running)
        return;

    strncpy(s_api_ssid, ssid, sizeof(s_api_ssid) - 1);
    s_api_ssid[sizeof(s_api_ssid) - 1] = '\0';
    strncpy(s_api_pass, pass ? pass : "", sizeof(s_api_pass) - 1);
    s_api_pass[sizeof(s_api_pass) - 1] = '\0';

    s_api_connect_running = true;
    s_api_connect_success = false;
    s_api_connect_fail = false;
    s_api_connect_attempts = 0;
    s_last_reason = 0;
    s_status = NET_WIFI_STATUS_WAITING_RESULT;

    if (xTaskCreate(api_connect_task, "net_wifi_api", 3072, NULL, tskIDLE_PRIORITY + 2, NULL) != pdPASS)
    {
        s_api_connect_running = false;
        s_api_connect_fail = true;
        s_status = NET_WIFI_STATUS_FAILED;
    }
}

static void api_forget_task(void *arg)
{
    if (s_scan_timer)
        esp_timer_stop(s_scan_timer);
    if (s_scan_running)
    {
        esp_wifi_scan_stop();
        s_scan_running = false;
        s_scan_done = true;
    }
    net_wifi_store_remove(s_forget_ssid);
    if (s_fallback_sta_used && strncmp((const char *)s_fallback_cfg.sta.ssid, s_forget_ssid, 32) == 0)
    {
        s_fallback_sta_used = false; /* WiFi mac dinh trong app_config: tat toi lan khoi dong sau */
        ESP_LOGW(TAG, "Quen WiFi mac dinh %s (toi lan khoi dong sau)", s_forget_ssid);
    }
    s_api_connect_running = false;
    s_api_connect_success = false;
    s_api_connect_fail = false;
    s_status = NET_WIFI_STATUS_IDLE;

    /* reason ASSOC_LEAVE -> event handler KHONG tu ket noi lai */
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(300));
    wifi_config_t empty = {0};
    esp_wifi_set_config(WIFI_IF_STA, &empty);
    ESP_LOGI(TAG, "Da ngat va quen WiFi \"%s\"", s_forget_ssid);

    /* Con mang da luu khac -> van tu ket noi nhu binh thuong */
    memset(s_tmp_saved, 0, sizeof(s_tmp_saved));
    if (net_wifi_store_load(s_tmp_saved) > 0)
    {
        s_scan_retry = 0;
        schedule_next_scan();
    }
    vTaskDelete(NULL);
}

void net_wifi_api_forget(const char *ssid)
{
    if (!ssid || !ssid[0])
        return;
    strncpy(s_forget_ssid, ssid, sizeof(s_forget_ssid) - 1);
    s_forget_ssid[sizeof(s_forget_ssid) - 1] = '\0';
    xTaskCreate(api_forget_task, "net_wifi_forget", 4096, NULL, tskIDLE_PRIORITY + 2, NULL);
}

esp_netif_t *net_wifi_get_ap_netif(void)
{
    return s_ap_netif;
}

uint8_t net_wifi_get_last_reason(void)
{
    return s_last_reason;
}

net_wifi_api_connect_state_t net_wifi_api_connect_state(void)
{
    if (s_api_connect_running)
        return NET_WIFI_API_CONNECT_RUNNING;
    if (s_api_connect_success)
        return NET_WIFI_API_CONNECT_SUCCESS;
    if (s_api_connect_fail)
        return NET_WIFI_API_CONNECT_FAILED;
    return NET_WIFI_API_CONNECT_IDLE;
}