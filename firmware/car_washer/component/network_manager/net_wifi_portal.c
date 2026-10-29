#include "net_wifi_portal.h"
#include "net_wifi.h"
#include "net_manage.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <string.h>

static const char *TAG = "wifi_portal";

/* Trang web nhung vao firmware (main/CMakeLists.txt: EMBED_TXTFILES), tach 3 file:
 *   component/embedded_files/wifi_portal.html  khung trang      -> GET /
 *   component/embedded_files/wifi_portal.css   giao dien        -> GET /wifi_portal.css
 *   component/embedded_files/wifi_portal.js    xu ly, goi /api  -> GET /wifi_portal.js
 * Ten duong dan trung ten file -> mo file .html tren may tinh van xem thu duoc. */
extern const char portal_html_start[] asm("_binary_wifi_portal_html_start");
extern const char portal_html_end[] asm("_binary_wifi_portal_html_end");
extern const char portal_css_start[] asm("_binary_wifi_portal_css_start");
extern const char portal_css_end[] asm("_binary_wifi_portal_css_end");
extern const char portal_js_start[] asm("_binary_wifi_portal_js_start");
extern const char portal_js_end[] asm("_binary_wifi_portal_js_end");

#define PORTAL_SCAN_WAIT_MS  8000
#define PORTAL_SCAN_MAX      NET_WIFI_MAX_SAVED
#define PORTAL_BODY_MAX      256
#define DNS_PORT             53
#define DNS_BUF              512

static wifi_portal_config_t s_cfg;
static httpd_handle_t s_httpd = NULL;
static uint32_t s_ap_ip = 0;      /* network byte order */
static char s_ap_url[24];         /* "http://192.168.4.1/" */

/* ================= TIEN ICH ================= */

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!s)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, s);
    cJSON_free(s);
    return err;
}

static bool netif_has_ip(esp_netif_t *n, esp_netif_ip_info_t *out)
{
    esp_netif_ip_info_t ip = {0};
    if (!n || esp_netif_get_ip_info(n, &ip) != ESP_OK || ip.ip.addr == 0)
        return false;
    if (out)
        *out = ip;
    return true;
}

/* Mang tu dang dung de ra internet */
static const char *active_net(void)
{
    esp_netif_t *d = net_manager_get_default_netif();
    if (!netif_has_ip(d, NULL))
        return "none";
    if (d == net_wifi_get_netif())
        return "wifi";
    const char *key = esp_netif_get_ifkey(d);
    if (key && strncmp(key, "ETH", 3) == 0)
        return "eth";
    return "4g";
}

/* Loi ket noi gan nhat do trang web yeu cau: AUTH_FAIL / NO_AP / FAIL; NULL = khong co loi */
static const char *connect_error(void)
{
    if (net_wifi_api_connect_state() != NET_WIFI_API_CONNECT_FAILED)
        return NULL;
    switch (net_wifi_get_last_reason())
    {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
    case WIFI_REASON_AUTH_EXPIRE:
        return "AUTH_FAIL";
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "NO_AP";
    default:
        return "FAIL";
    }
}

/* ================= HTTP ================= */

/* Gui 1 file nhung. EMBED_TXTFILES them '\0' o cuoi -> tru 1 */
static esp_err_t send_embedded(httpd_req_t *req, const char *type, const char *start, const char *end)
{
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, start, end - start - 1);
}

static esp_err_t h_root(httpd_req_t *req)
{
    return send_embedded(req, "text/html; charset=utf-8", portal_html_start, portal_html_end);
}

static esp_err_t h_css(httpd_req_t *req)
{
    return send_embedded(req, "text/css; charset=utf-8", portal_css_start, portal_css_end);
}

static esp_err_t h_js(httpd_req_t *req)
{
    return send_embedded(req, "application/javascript; charset=utf-8", portal_js_start, portal_js_end);
}

static esp_err_t h_status(httpd_req_t *req)
{
    cJSON *r = cJSON_CreateObject();
    uint8_t mac[6];
    char buf[24];

    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(r, "mac", buf);
    cJSON_AddStringToObject(r, "fw", s_cfg.fw_version ? s_cfg.fw_version : "");
    cJSON_AddStringToObject(r, "hw", s_cfg.hw_version ? s_cfg.hw_version : "");

    /* Trang thai Internet tung mang (cham xanh / do tren trang) */
    cJSON *links = cJSON_AddObjectToObject(r, "links");
    cJSON_AddBoolToObject(links, "eth", net_manager_type_ready(NM_IF_ETH));
    cJSON_AddBoolToObject(links, "4g", net_manager_type_ready(NM_IF_PPP));
    cJSON_AddBoolToObject(links, "wifi", net_manager_type_ready(NM_IF_WIFI));
    cJSON_AddStringToObject(r, "net", active_net());

    /* IP cua mang dang dung (Ethernet / WiFi / 4G) -> dong "Dia chi IP" tren trang */
    {
        esp_netif_ip_info_t nip;
        if (netif_has_ip(net_manager_get_default_netif(), &nip))
        {
            char ipb[16];
            snprintf(ipb, sizeof(ipb), IPSTR, IP2STR(&nip.ip));
            cJSON_AddStringToObject(r, "net_ip", ipb);
        }
        else
        {
            cJSON_AddStringToObject(r, "net_ip", "");
        }
    }

    /* WiFi STA: dang ket noi thi lay ten AP that, chua thi lay ten dang cau hinh */
    esp_netif_ip_info_t ip;
    bool has_ip = netif_has_ip(net_wifi_get_netif(), &ip);
    wifi_ap_record_t ap;
    bool assoc = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
    if (assoc)
    {
        cJSON_AddStringToObject(r, "ssid", (const char *)ap.ssid);
        cJSON_AddNumberToObject(r, "rssi", ap.rssi);
    }
    else
    {
        wifi_config_t wc = {0};
        esp_wifi_get_config(WIFI_IF_STA, &wc);
        cJSON_AddStringToObject(r, "ssid", (const char *)wc.sta.ssid);
    }
    cJSON_AddBoolToObject(r, "connected", assoc && has_ip);
    if (assoc && has_ip)
    {
        snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ip.ip));
        cJSON_AddStringToObject(r, "ip", buf);
    }
    else
    {
        cJSON_AddStringToObject(r, "ip", "");
    }
    cJSON_AddBoolToObject(r, "server", s_cfg.server_ready ? s_cfg.server_ready() : false);
    cJSON_AddBoolToObject(r, "busy", net_wifi_api_connect_state() == NET_WIFI_API_CONNECT_RUNNING);
    /* mang se thay WiFi neu ngat WiFi ("eth" / "4g"), "" = khong con mang nao */
    const char *other = net_manager_ready_other_name(net_wifi_get_netif());
    cJSON_AddStringToObject(r, "other", other ? other : "");

    const char *e = connect_error();
    if (e)
        cJSON_AddStringToObject(r, "err", e);
    return send_json(req, r);
}

static esp_err_t h_scan(httpd_req_t *req)
{
    if (net_wifi_api_connect_state() != NET_WIFI_API_CONNECT_RUNNING)
    {
        net_wifi_api_request_scan();
        for (int t = 0; t < PORTAL_SCAN_WAIT_MS / 100 && !net_wifi_api_is_scan_done(); t++)
            vTaskDelay(pdMS_TO_TICKS(100));
    }

    wifi_ap_record_t *aps = calloc(PORTAL_SCAN_MAX, sizeof(wifi_ap_record_t));
    if (!aps)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
    int n = net_wifi_api_get_scan_result(aps, PORTAL_SCAN_MAX);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++)
    {
        if (!aps[i].ssid[0])
            continue; /* mang an */
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ssid", (const char *)aps[i].ssid);
        cJSON_AddNumberToObject(o, "rssi", aps[i].rssi);
        cJSON_AddNumberToObject(o, "auth", aps[i].authmode);
        cJSON_AddItemToArray(arr, o);
    }
    free(aps);
    return send_json(req, arr);
}

/* Doc than POST (JSON nho). NULL = loi (da tra loi cho trinh duyet neu can) */
static cJSON *read_json_body(httpd_req_t *req)
{
    char body[PORTAL_BODY_MAX + 1];
    int len = req->content_len;
    if (len <= 0 || len > PORTAL_BODY_MAX)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return NULL;
    }
    int got = 0;
    while (got < len)
    {
        int r = httpd_req_recv(req, body + got, len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT)
            continue;
        if (r <= 0)
            return NULL;
        got += r;
    }
    body[got] = '\0';
    cJSON *j = cJSON_Parse(body);
    if (!j)
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
    return j;
}

static esp_err_t h_connect(httpd_req_t *req)
{
    cJSON *j = read_json_body(req);
    if (!j)
        return ESP_OK;
    const cJSON *js = cJSON_GetObjectItem(j, "ssid");
    const cJSON *jp = cJSON_GetObjectItem(j, "password");
    const char *ssid = cJSON_IsString(js) ? js->valuestring : NULL;
    const char *pass = cJSON_IsString(jp) ? jp->valuestring : "";

    const char *err = NULL;
    if (!ssid || !ssid[0] || strlen(ssid) > 32)
        err = "Tên WiFi không hợp lệ.";
    else if (strlen(pass) > 63 || (pass[0] && strlen(pass) < 8))
        err = "Mật khẩu phải từ 8 đến 63 ký tự.";
    else if (net_wifi_api_connect_state() == NET_WIFI_API_CONNECT_RUNNING)
        err = "Tủ đang kết nối, vui lòng chờ.";

    if (!err)
    {
        ESP_LOGI(TAG, "yeu cau ket noi WiFi \"%s\"", ssid);
        net_wifi_api_connect(ssid, pass);
    }
    cJSON_Delete(j);

    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", err == NULL);
    if (err)
        cJSON_AddStringToObject(r, "msg", err);
    return send_json(req, r);
}

/* POST /api/forget {"ssid":".."}: ngat WiFi va quen mang (tu khong tu ket noi lai mang nay) */
static esp_err_t h_forget(httpd_req_t *req)
{
    cJSON *j = read_json_body(req);
    if (!j)
        return ESP_OK;
    const cJSON *js = cJSON_GetObjectItem(j, "ssid");
    const char *ssid = cJSON_IsString(js) ? js->valuestring : NULL;

    const char *err = NULL;
    if (!ssid || !ssid[0] || strlen(ssid) > 32)
        err = "Tên WiFi không hợp lệ.";
    else if (net_wifi_api_connect_state() == NET_WIFI_API_CONNECT_RUNNING)
        err = "Tủ đang kết nối WiFi, vui lòng chờ.";

    if (!err)
    {
        ESP_LOGW(TAG, "yeu cau ngat + quen WiFi \"%s\"", ssid);
        net_wifi_api_forget(ssid);
    }
    cJSON_Delete(j);

    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", err == NULL);
    if (err)
        cJSON_AddStringToObject(r, "msg", err);
    return send_json(req, r);
}

/* Moi duong dan la (trang kiem tra mang cua Android/iOS/Windows...) -> chuyen ve trang chu.
 * iOS can co noi dung trong phan hoi thi moi nhan ra captive portal. */
static esp_err_t h_404(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", s_ap_url);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "Redirect");
}

static esp_err_t start_http(void)
{
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.stack_size = 6144;
    c.max_open_sockets = 5; /* lwip chi co 16 socket, con cho MQTT/DNS/SNTP */
    c.lru_purge_enable = true;
    c.max_uri_handlers = 12; /* 7 duong dan + du phong */
    c.core_id = 0;
    c.recv_wait_timeout = 5;
    c.send_wait_timeout = 5;

    esp_err_t err = httpd_start(&s_httpd, &c);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_start loi: %s", esp_err_to_name(err));
        return err;
    }
    static const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = h_root},
        {.uri = "/wifi_portal.css", .method = HTTP_GET, .handler = h_css},
        {.uri = "/wifi_portal.js", .method = HTTP_GET, .handler = h_js},
        {.uri = "/api/status", .method = HTTP_GET, .handler = h_status},
        {.uri = "/api/scan", .method = HTTP_GET, .handler = h_scan},
        {.uri = "/api/connect", .method = HTTP_POST, .handler = h_connect},
        {.uri = "/api/forget", .method = HTTP_POST, .handler = h_forget},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++)
        httpd_register_uri_handler(s_httpd, &uris[i]);
    httpd_register_err_handler(s_httpd, HTTPD_404_NOT_FOUND, h_404);

    /* chuyen huong sinh ra rat nhieu log canh bao vo hai */
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);
    return ESP_OK;
}

/* ================= DNS: moi ten mien -> IP cua AP ================= */

static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "DNS: khong tao duoc socket");
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(DNS_PORT), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        ESP_LOGE(TAG, "DNS: bind cong 53 loi");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    static uint8_t buf[DNS_BUF];
    while (1)
    {
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen);
        if (n < 12)
            continue;
        if ((buf[2] & 0x80) || buf[4] != 0 || buf[5] != 1)
            continue; /* khong phai cau hoi / khong dung 1 cau hoi */

        /* bo qua ten mien (chuoi nhan) -> toi QTYPE/QCLASS */
        int p = 12;
        while (p < n && buf[p] != 0)
        {
            if (buf[p] & 0xC0)
                break; /* cau hoi khong dung nen ten -> bo */
            p += buf[p] + 1;
        }
        if (p >= n || buf[p] != 0 || p + 5 > n)
            continue;
        p += 1;
        uint16_t qtype = (buf[p] << 8) | buf[p + 1];
        p += 4; /* QTYPE + QCLASS */

        /* phan hoi: giu ID + cau hoi, bat co QR/RA, xoa phan con lai */
        buf[2] = 0x84 | (buf[2] & 0x01);            /* QR=1, AA=1, RD giu nguyen */
        buf[3] = 0x80;                              /* RA=1, RCODE=0 */
        buf[6] = buf[7] = buf[8] = buf[9] = buf[10] = buf[11] = 0;
        int len = p;
        if (qtype == 1 /* A */ && len + 16 <= (int)sizeof(buf))
        {
            buf[7] = 1; /* ANCOUNT = 1 */
            uint8_t *a = buf + len;
            a[0] = 0xC0; a[1] = 0x0C;               /* tro ve ten trong cau hoi */
            a[2] = 0; a[3] = 1;                     /* TYPE A */
            a[4] = 0; a[5] = 1;                     /* CLASS IN */
            a[6] = 0; a[7] = 0; a[8] = 0; a[9] = 60; /* TTL 60 s */
            a[10] = 0; a[11] = 4;
            memcpy(a + 12, &s_ap_ip, 4);
            len += 16;
        }
        sendto(sock, buf, len, 0, (struct sockaddr *)&from, flen);
    }
}

/* ================= API ================= */

esp_err_t wifi_portal_start(const wifi_portal_config_t *cfg)
{
    esp_netif_t *ap = net_wifi_get_ap_netif();
    if (!ap)
    {
        ESP_LOGW(TAG, "AP chua bat (use_ap_mode = false) -> khong chay trang cau hinh");
        return ESP_ERR_INVALID_STATE;
    }
    if (cfg)
        s_cfg = *cfg;

    esp_netif_ip_info_t ip = {0};
    esp_netif_get_ip_info(ap, &ip);
    if (ip.ip.addr == 0)
        IP4_ADDR(&ip.ip, 192, 168, 4, 1);
    s_ap_ip = ip.ip.addr;
    snprintf(s_ap_url, sizeof(s_ap_url), "http://" IPSTR "/", IP2STR(&ip.ip));

    /* Android 11+/iOS 14+: DHCP option 114 bao thang dia chi trang cau hinh */
    static char uri[24];
    snprintf(uri, sizeof(uri), "http://" IPSTR, IP2STR(&ip.ip));
    esp_netif_dhcps_stop(ap);
    esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, uri, strlen(uri));
    esp_netif_dhcps_start(ap);

    esp_err_t err = start_http();
    if (err != ESP_OK)
        return err;
    if (xTaskCreatePinnedToCore(dns_task, "portal_dns", 3072, NULL, 4, NULL, 0) != pdPASS)
        ESP_LOGE(TAG, "khong tao duoc task DNS -> may se khong tu mo trang");

    ESP_LOGI(TAG, "trang cau hinh WiFi: %s", s_ap_url);
    return ESP_OK;
}