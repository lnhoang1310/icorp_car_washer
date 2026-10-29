#include "cloud_manager.h"
#include "cloud_protocol.h"
#include "mqtt_tx_queue.h"
#include "app_fsm.h"
#include "app_config.h"
#include "device_id.h"
#include "net_manage.h"
#include "ntp_time.h"
#include "peripheral_test.h" /* che do thu thao tac khong can server (PT_DEMO_PAYMENT) */

#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* ---- Tham so ket noi (ghi de duoc trong app_config.h) ---- */
#ifndef CLOUD_BROKER_USE_TLS
#define CLOUD_BROKER_USE_TLS 0 /* 1 = mqtts (can bat ESP-MQTT -> "Enable MQTT over SSL" trong menuconfig) */
#endif
#ifndef CLOUD_KEEPALIVE_S
#define CLOUD_KEEPALIVE_S 7
#endif
#ifndef CLOUD_RECONNECT_MS
#define CLOUD_RECONNECT_MS 5000
#endif
/* So khoang hien thi. Tai lieu (chot 12/10/2026): khoang tich hop trong tu controller = 0. */
#ifndef CLOUD_BAY_NUMBER
#define CLOUD_BAY_NUMBER 0
#endif
#ifndef HW_VERSION
#define HW_VERSION "1.0"
#endif
#ifndef FW_VERSION
#define FW_VERSION "1.0.0"
#endif

/* Trang thai server (backend): backend dang ky Last Will "OFFLINE" va publish "ONLINE" (deu retain)
 * len topic nay; moi tu cung subscribe. Server OFFLINE -> tu "Tam ngung phuc vu", khong nhan khach mua tai tu.
 * (De xuat bo sung giao thuc, can thong nhat voi doi backend.) */
#ifndef CLOUD_SERVER_STATUS_TOPIC
#define CLOUD_SERVER_STATUS_TOPIC "carwash/v1/server/status"
#endif
/* 0 = chua nhan duoc trang thai server (backend chua lam / chua tung publish) thi VAN coi la san sang,
 *     chi chan khi nhan dung "OFFLINE".
 * 1 = bat buoc phai nhan "ONLINE" moi phuc vu (dung khi backend da trien khai topic nay). */
#ifndef CLOUD_SERVER_STATUS_STRICT
#define CLOUD_SERVER_STATUS_STRICT 0
#endif

#if CLOUD_BROKER_USE_TLS
#include "esp_crt_bundle.h"
#endif

static const char *TAG = "cloud";

/* Chu ky gui Meter dinh ky (giay): "meterInterval" trong topic config (retained -> nhan lai moi lan ket noi),
 * chi giu trong RAM. Doc tu task app_fsm, ghi tu task MQTT: 1 bien 32-bit -> khong can khoa */
static volatile uint32_t s_meter_interval_s = CLOUD_METER_INTERVAL_S;

uint32_t cloud_meter_interval_s(void)
{
    return s_meter_interval_s;
}

#define TOPIC_LEN 64
#define TIME_VALID_EPOCH 1672531200 /* 01/01/2023 */

static esp_mqtt_client_handle_t s_client = NULL;
static SemaphoreHandle_t s_lock = NULL;
static volatile bool s_connected = false;
/* Da gui Boot tren ket noi hien tai. Moi goi up (Boot, Meter, Init, Cancel) deu mang timestamp UTC ->
 * chi gui khi SNTP da dong bo; truoc do tu o trang thai chua san sang (OUT_OF_SERVICE). */
static volatile bool s_booted = false;

typedef enum
{
    SRV_UNKNOWN = 0, /* chua nhan duoc gi tren topic trang thai server */
    SRV_ONLINE,
    SRV_OFFLINE,
} srv_state_t;
static volatile srv_state_t s_srv = SRV_UNKNOWN;
static bool s_ready_reported = false; /* gia tri cloud_is_ready() da bao cho app_fsm lan cuoi */

static char s_mac[CP_MAC_LEN]; /* MAC goc viet thuong, vd "80456b281774" */
static char s_topic_up[TOPIC_LEN];
static char s_topic_down[TOPIC_LEN];
static char s_topic_status[TOPIC_LEN];
static char s_topic_cfg[TOPIC_LEN];
static char s_client_id[32];

/* Don QR dang cho (chi 1 vi chi co 1 chuong) */
static struct
{
    char order_id[CLOUD_ORDER_ID_LEN]; /* ma don tram tu tao, "" = khong co */
    char *json;                        /* InitTransaction da gui (gui lai nguyen van khi retry) */
    uint32_t txn;                      /* transactionId server cap trong ShowPaymentCode, 0 = chua co */
    bool cancelled;                    /* khach huy truoc khi co QR -> co QR la gui CancelPayment ngay */
} s_init;

/* msg_id MQTT cua ban tin trong tung slot hang doi FRAM (-1 = chua gui trong lan ket noi nay) */
static int s_slot_msg[TXQ_SLOTS];

/* Anh chup phien gan nhat (de gui Meter sau Boot / ket noi lai) */
static ws_session_t s_snap;
static bool s_snap_valid = false;

static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

static void init_clear(void)
{
    cp_free(s_init.json);
    memset(&s_init, 0, sizeof(s_init));
}

/* ================= GUI ================= */

/* Ban tin thuong (khong luu): QoS 1, khong retain, khong chan. Mat mang thi bo. */
static esp_err_t publish_raw(const char *json)
{
    if (!json)
        return ESP_ERR_NO_MEM;
    if (!s_client || !s_booted)
    {
        ESP_LOGW(TAG, "%s, khong gui: %.80s", s_connected ? "chua dong bo gio (chua Boot)" : "chua ket noi", json);
        return ESP_ERR_INVALID_STATE;
    }
    int id = esp_mqtt_client_enqueue(s_client, s_topic_up, json, 0, 1, 0, true);
    if (id < 0)
    {
        ESP_LOGE(TAG, "enqueue loi (%d)", id);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "-> %s", json);
    return ESP_OK;
}

/* Gui roi giai phong chuoi */
static esp_err_t publish_take(char *json)
{
    esp_err_t err = publish_raw(json);
    cp_free(json);
    return err;
}

/* Ban tin quan trong: luu FRAM roi gui; xoa khi co PUBACK. ESP_OK = da luu an toan.
 * Hang doi day / FRAM loi -> van thu gui 1 lan, tra loi de app giu phien trong FRAM. Giai phong json. */
static esp_err_t send_important(char *json)
{
    if (!json)
        return ESP_ERR_NO_MEM;
    lock();
    int slot = txq_push(json);
    if (slot >= 0 && s_client && s_booted && s_srv != SRV_OFFLINE)
    {
        s_slot_msg[slot] = esp_mqtt_client_enqueue(s_client, s_topic_up, json, 0, 1, 0, true);
        ESP_LOGI(TAG, "-> [luu %d] %s", slot, json);
    }
    else if (slot >= 0)
    {
        /* Mat ket noi, hoac server OFFLINE (broker van nhan nhung khong co ai xu ly) -> giu FRAM */
        ESP_LOGW(TAG, "%s -> luu cho gui lai [%d]: %.80s",
                 !s_connected ? "dang mat ket noi" : !s_booted ? "chua dong bo gio" : "server OFFLINE", slot, json);
    }
    unlock();
    if (slot >= 0)
    {
        cp_free(json);
        return ESP_OK;
    }
    publish_take(json);
    return ESP_FAIL;
}

/* Ket noi lai: gui lai cac ban tin quan trong con ton (cu truoc) */
static void flush_important(void)
{
    char *buf = malloc(TXQ_SLOT_SIZE);
    if (!buf)
        return;
    lock();
    int slots[TXQ_SLOTS];
    int n = txq_list(slots);
    for (int i = 0; i < n && s_booted && s_srv != SRV_OFFLINE; i++)
    {
        if (!txq_read(slots[i], buf, TXQ_SLOT_SIZE))
            continue;
        s_slot_msg[slots[i]] = esp_mqtt_client_enqueue(s_client, s_topic_up, buf, 0, 1, 0, true);
        ESP_LOGI(TAG, "-> [gui lai %d] %s", slots[i], buf);
    }
    unlock();
    free(buf);
}

/* Broker xac nhan -> xoa khoi hang doi FRAM */
static void on_published(int msg_id)
{
    if (msg_id <= 0)
        return;
    lock();
    for (int i = 0; i < TXQ_SLOTS; i++)
        if (s_slot_msg[i] == msg_id)
        {
            txq_remove(i);
            s_slot_msg[i] = -1;
            ESP_LOGI(TAG, "server da nhan ban tin [%d], con %d cho gui", i, txq_count());
        }
    unlock();
}

static esp_err_t send_meter(const ws_session_t *s)
{
    return publish_take(cp_build_meter(s_mac, CLOUD_BAY_NUMBER, s));
}

static esp_err_t send_cancel_payment(uint32_t txn)
{
    return send_important(cp_build_cancel(s_mac, CLOUD_BAY_NUMBER, txn));
}

/* ================= NHAN ================= */

static void on_show_payment_code(const cp_down_t *d)
{
    lock();
    bool match = s_init.order_id[0] && (s_init.txn == 0 || s_init.txn == d->txn);
    if (!match)
    {
        unlock();
        ESP_LOGW(TAG, "ShowPaymentCode phien %lu khong khop don dang cho -> bo qua", (unsigned long)d->txn);
        return;
    }
    s_init.txn = d->txn;
    if (s_init.cancelled)
    {
        /* Khach da huy truoc khi QR ve: huy luon phien tren server */
        ESP_LOGW(TAG, "QR phien %lu ve sau khi khach huy -> CancelPayment", (unsigned long)d->txn);
        init_clear();
        unlock();
        send_cancel_payment(d->txn);
        return;
    }

    cloud_qr_t *qr = calloc(1, sizeof(cloud_qr_t));
    if (!qr)
    {
        unlock();
        return;
    }
    strlcpy(qr->order_id, s_init.order_id, sizeof(qr->order_id));
    unlock();

    qr->transaction_id = d->txn;
    strlcpy(qr->qr, d->code, sizeof(qr->qr));
    if (d->code_truncated)
        ESP_LOGW(TAG, "chuoi QR dai hon %u ky tu -> bi cat", CLOUD_QR_MAX_LEN - 1);
    qr->amount = d->price;

    /* Han thanh toan: chi tin expireAt khi gio tram da dong bo; 0 = dung qr_timeout_s */
    time_t now = time(NULL);
    if (d->expire_at > 0 && now > TIME_VALID_EPOCH)
    {
        int64_t left = d->expire_at - (int64_t)now;
        qr->expire_in_s = left <= 0 ? 1 : left > 65535 ? 65535
                                                       : (uint16_t)left;
    }

    app_evt_t ev = {.type = APP_EVT_CLOUD_QR, .data.qr = qr};
    if (!app_post_event(&ev))
        free(qr);
}

static void on_start_transaction(const cp_down_t *d)
{
    lock();
    if (s_init.txn == d->txn)
        init_clear(); /* don QR da duoc thanh toan */
    unlock();

    app_evt_t ev = {.type = APP_EVT_CLOUD_START};
    snprintf(ev.data.start.order_id, sizeof(ev.data.start.order_id), "%lu", (unsigned long)d->txn);
    ev.data.start.duration_s = d->duration_s;
    app_post_event(&ev);
}

static void on_remote_stop(const cp_down_t *d)
{
    app_evt_t ev = {.type = APP_EVT_CLOUD_REMOTE_STOP};
    snprintf(ev.data.result.order_id, sizeof(ev.data.result.order_id), "%lu", (unsigned long)d->txn);
    app_post_event(&ev);
}

static void on_down(const char *data, int len)
{
    cp_down_t *d = malloc(sizeof(cp_down_t));
    if (!d)
        return;
    cp_parse_down(data, len, d);

    if (d->type == CP_DOWN_INVALID)
        ESP_LOGW(TAG, "down '%s': JSON loi / thieu truong -> bo qua", d->type_name);
    else if (strcmp(d->bay_mac, s_mac) != 0)
        ESP_LOGW(TAG, "down %s: bayMac '%s' khong phai cua tram -> bo qua", d->type_name, d->bay_mac);
    else if (d->type == CP_DOWN_SHOW_QR)
        on_show_payment_code(d);
    else if (d->type == CP_DOWN_START)
        on_start_transaction(d);
    else if (d->type == CP_DOWN_REMOTE_STOP)
        on_remote_stop(d);
    else
        ESP_LOGW(TAG, "down: loai goi '%s' chua ho tro", d->type_name);
    free(d);
}

/* Topic config (retain): {"mode":"S#3#1#2#1","packages":"10000#600#900|..."}. Hai phan doc lap:
 * phan nao sai -> giu cau hinh dang dung cua phan do, phan kia van ap dung.
 * app_fsm quyet dinh LUC NAO ap dung (mode chi doi khi chuong NORMAL, tai lieu muc 6.4). */
static void on_config(const char *data, int len)
{
    cp_config_t *c = malloc(sizeof(*c));
    if (!c)
        return;
    cp_parse_config(data, len, c);
    ESP_LOGI(TAG, "<- config %.*s", len, data);
    if (!c->json_ok)
    {
        ESP_LOGW(TAG, "config khong phai JSON hop le -> giu cau hinh dang dung");
        free(c);
        return;
    }

    if (c->mode_ok)
    {
        app_evt_t ev = {.type = APP_EVT_CLOUD_OP_MODE};
        ev.data.op_mode.mode = c->mode.mode;
        memcpy(ev.data.op_mode.rate_pct, c->mode.rate_pct, sizeof(c->mode.rate_pct));
        memcpy(ev.data.op_mode.permille, c->mode.permille, sizeof(c->mode.permille));
        memcpy(ev.data.op_mode.raw, c->mode.raw, sizeof(c->mode.raw));
        app_post_event(&ev);
    }
    else
    {
        ESP_LOGW(TAG, "config: mode %s -> giu mode dang dung (dung: \"S#3#1#2#1\" / \"R#20#50#20#10\")",
                 c->has_mode ? "KHONG hop le" : "khong co");
    }

    if (c->packages_ok)
    {
        cloud_pkg_list_t *pl = malloc(sizeof(*pl));
        if (pl)
        {
            *pl = c->packages;
            app_evt_t ev = {.type = APP_EVT_CLOUD_PACKAGES, .data.packages = pl};
            if (!app_post_event(&ev))
                free(pl);
        }
    }
    else
    {
        ESP_LOGW(TAG, "config: packages %s -> giu danh sach goi dang dung (dung: \"10000#600#900|20000#900#1200\")",
                 c->has_packages ? "KHONG hop le" : "khong co");
    }

    /* meterInterval: khong co truong / sai -> giu chu ky dang dung. Ap dung ngay (ke ca dang rua) */
    if (c->meter_interval_ok)
    {
        if (c->meter_interval_s != s_meter_interval_s)
            ESP_LOGI(TAG, "config: chu ky Meter %lu -> %lu giay", (unsigned long)s_meter_interval_s,
                     (unsigned long)c->meter_interval_s);
        s_meter_interval_s = c->meter_interval_s;
    }
    else if (c->has_meter_interval)
    {
        ESP_LOGW(TAG, "config: meterInterval KHONG hop le (so nguyen %d..%d giay) -> giu %lu giay",
                 CLOUD_METER_INTERVAL_MIN_S, CLOUD_METER_INTERVAL_MAX_S, (unsigned long)s_meter_interval_s);
    }

    /* hotline: khong co truong -> giu so dang dung; sai dinh dang -> giu so dang dung */
    if (c->hotline_ok)
    {
        app_evt_t ev = {.type = APP_EVT_CLOUD_HOTLINE};
        strlcpy(ev.data.phone, c->hotline, sizeof(ev.data.phone));
        app_post_event(&ev);
    }
    else if (c->has_hotline)
    {
        ESP_LOGW(TAG, "config: hotline KHONG hop le (chi gom so va + - . ( ), toi da %d ky tu) -> giu so dang dung",
                 DEVCFG_PHONE_LEN - 1);
    }
    free(c);
}

/* Bao app_fsm khi "san sang phuc vu" doi (ket noi broker + trang thai server) */
static bool ready_now(void)
{
    if (!s_booted)
        return false;
#if CLOUD_SERVER_STATUS_STRICT
    return s_srv == SRV_ONLINE;
#else
    return s_srv != SRV_OFFLINE;
#endif
}

static void report_ready(bool force)
{
    bool r = ready_now();
    if (!force && r == s_ready_reported)
        return;
    s_ready_reported = r;
    app_evt_t ev = {.type = APP_EVT_CLOUD_READY, .data.b = r};
    app_post_event(&ev);
}

/* Topic trang thai server: "ONLINE" / "OFFLINE" (chuoi thuan), rong = da xoa retain */
static void on_server_status(const char *data, int len)
{
    while (len > 0 && (data[len - 1] == ' ' || data[len - 1] == '\r' || data[len - 1] == '\n'))
        len--;
    srv_state_t st = SRV_UNKNOWN;
    if (len == 6 && strncasecmp(data, "ONLINE", 6) == 0)
        st = SRV_ONLINE;
    else if (len == 7 && strncasecmp(data, "OFFLINE", 7) == 0)
        st = SRV_OFFLINE;
    else if (len > 0)
    {
        ESP_LOGW(TAG, "trang thai server khong hop le: %.*s -> bo qua", len, data);
        return;
    }
    srv_state_t old = s_srv;
    if (st != old)
        ESP_LOGW(TAG, "SERVER %s", st == SRV_ONLINE ? "ONLINE" : st == SRV_OFFLINE ? "OFFLINE -> tam ngung phuc vu"
                                                                                   : "chua ro trang thai");
    s_srv = st;
    if (old == SRV_OFFLINE && st != SRV_OFFLINE && s_booted)
        flush_important(); /* server song lai: gui cac goi quan trong giu trong luc server OFFLINE */
    report_ready(false);
}

/* ================= MQTT ================= */

/* Mang dang cho ket noi MQTT (mang chinh luc ket noi thanh cong). Ket noi TCP gan voi IP cua mang nay:
 * mang nay mat (rut day / rot WiFi / mat 4G) thi ket noi chac chan chet -> ket noi lai NGAY qua mang
 * khac thay vi cho het keepalive (~10 giay). Mang khac len/xuong khong dong toi MQTT. */
static esp_netif_t *s_mqtt_netif = NULL;
static volatile bool s_fast_reconnect = false;

/* Ket noi that bai lien tiep tren CUNG mang chinh (DNS khong phan giai / TCP khong mo duoc).
 * Du CLOUD_NET_FAIL_MAX lan -> bao net_manager tu xu ly (khoi dong lai ETH / doi mang). 1 = bao ngay lan dau. */
#ifndef CLOUD_NET_FAIL_MAX
#define CLOUD_NET_FAIL_MAX 1
#endif
static uint8_t s_conn_fail = 0;

/* Vua co mang chinh moi trong luc MQTT dang thu ket noi do dang -> thu lai ngay khi lan do ket thuc */
static volatile bool s_net_kick = false;

static void on_netmgr_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == NETMGR_EVENT_DEFAULT_CHANGED)
    {
        s_conn_fail = 0; /* mang chinh moi -> dem lai tu dau */
        return;
    }
    if (id == NETMGR_EVENT_ONLINE)
    {
        /* Co mang chinh (luc khoi dong / doi mang) ma chua vao broker -> thu ngay, khong cho CLOUD_RECONNECT_MS */
        if (s_client && !s_connected)
        {
            if (esp_mqtt_client_reconnect(s_client) == ESP_OK)
                ESP_LOGI(TAG, "co mang -> ket noi broker ngay");
            else
                s_net_kick = true; /* dang ket noi do dang -> thu lai ngay khi lan nay ket thuc */
        }
        return;
    }
    if (id != NETMGR_EVENT_IF_DOWN || !data || !s_client)
        return;
    esp_netif_t *down = *(esp_netif_t **)data;
    if (!down || down != s_mqtt_netif)
        return;
    if (s_connected)
    {
        ESP_LOGW(TAG, "mang dang chay MQTT (%s) vua mat -> ket noi lai ngay qua mang khac",
                 esp_netif_get_ifkey(down));
        s_fast_reconnect = true;
        esp_mqtt_client_disconnect(s_client); /* -> MQTT_EVENT_DISCONNECTED -> reconnect ngay */
    }
    else
    {
        esp_mqtt_client_reconnect(s_client); /* dang cho ket noi lai -> bo qua thoi gian cho */
    }
}

/* Boot, cac ban tin quan trong con ton (mat mang / mat dien truoc do), roi Meter hien tai.
 * Chi chay khi DA ket noi broker VA SNTP da dong bo, dung 1 lan moi ket noi. Goi tu on_connected (task MQTT)
 * va tu ntp_time_on_sync (task esp_timer) -> co ca 2 thu tu "ket noi truoc / gio truoc". */
static void boot_sequence(void)
{
    lock();
    bool go = s_connected && !s_booted && ntp_time_is_synced();
    if (go)
        s_booted = true;
    unlock();
    if (!go)
        return;

    publish_take(cp_build_boot(s_mac, FW_VERSION, HW_VERSION, device_config_get()->wash.mode, device_config_get()->op_raw));
    flush_important();
    ws_session_t snap;
    lock();
    bool have = s_snap_valid;
    if (have)
        snap = s_snap;
    unlock();
    send_meter(have ? &snap : NULL);

    report_ready(true);
}

static void on_time_synced(void)
{
    if (s_connected && !s_booted)
        ESP_LOGI(TAG, "SNTP vua dong bo -> gui Boot");
    boot_sequence();
}

static void on_connected(void)
{
    esp_mqtt_client_subscribe(s_client, s_topic_down, 1);
    esp_mqtt_client_subscribe(s_client, s_topic_cfg, 1);
    esp_mqtt_client_subscribe(s_client, CLOUD_SERVER_STATUS_TOPIC, 1); /* retain -> nhan ngay trang thai hien tai */
    esp_mqtt_client_enqueue(s_client, s_topic_status, "ONLINE", 6, 1, 1, true); /* chuoi, khong co timestamp */
    s_booted = false;
    s_connected = true;

    if (!ntp_time_is_synced())
        ESP_LOGW(TAG, "da ket noi broker nhung CHUA dong bo gio SNTP -> cho dong bo roi moi gui Boot");
    boot_sequence();
}

static bool topic_is(const esp_mqtt_event_t *e, const char *t)
{
    return e->topic_len == (int)strlen(t) && memcmp(e->topic, t, e->topic_len) == 0;
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t e = data;
    switch ((esp_mqtt_event_id_t)id)
    {
    case MQTT_EVENT_CONNECTED:
        s_mqtt_netif = net_manager_get_default_netif();
        ESP_LOGI(TAG, "da ket noi broker (qua %s)", s_mqtt_netif ? esp_netif_get_ifkey(s_mqtt_netif) : "?");
        s_conn_fail = 0;
        s_net_kick = false;
        net_manager_report_reachable();
        on_connected();
        break;

    case MQTT_EVENT_DISCONNECTED:
        if (s_connected)
        {
            ESP_LOGW(TAG, "mat ket noi broker");
            s_connected = false;
            s_booted = false;
            lock();
            for (int i = 0; i < TXQ_SLOTS; i++)
                s_slot_msg[i] = -1; /* chua ACK -> ket noi lai gui lai */
            unlock();
            report_ready(true);
        }
        if (s_fast_reconnect || s_net_kick)
        {
            s_fast_reconnect = false;
            s_net_kick = false;
            esp_mqtt_client_reconnect(s_client); /* khong cho CLOUD_RECONNECT_MS */
        }
        break;

    case MQTT_EVENT_PUBLISHED:
        on_published(e->msg_id);
        break;

    case MQTT_EVENT_DATA:
        if (e->current_data_offset != 0 || e->data_len != e->total_data_len)
        {
            ESP_LOGW(TAG, "ban tin %d byte vuot bo dem -> bo qua", e->total_data_len);
            break;
        }
        ESP_LOGI(TAG, "<- %.*s: %.*s", e->topic_len, e->topic, e->data_len, e->data);
        if (topic_is(e, s_topic_down))
            on_down(e->data, e->data_len);
        else if (topic_is(e, CLOUD_SERVER_STATUS_TOPIC))
            on_server_status(e->data, e->data_len);
        else if (topic_is(e, s_topic_cfg))
            on_config(e->data, e->data_len);
        break;

    case MQTT_EVENT_ERROR:
        if (e->error_handle && e->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
            ESP_LOGE(TAG, "broker tu choi ket noi (ma %d) -- kiem tra user/password", e->error_handle->connect_return_code);
        else
            ESP_LOGW(TAG, "loi MQTT (sock errno %d)", e->error_handle ? e->error_handle->esp_transport_sock_errno : 0);
        /* Loi khi dang ket noi (chua vao duoc broker) trong luc CO mang chinh -> mang do co the khong ra Internet */
        if (!s_connected && e->error_handle && e->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT &&
            net_manager_get_default_netif() && ++s_conn_fail >= CLOUD_NET_FAIL_MAX)
        {
            s_conn_fail = 0;
            net_manager_report_unreachable();
        }
        break;

    default:
        break;
    }
}

/* Che do thu: khong gui, chi in ra goi tin LE RA se gui len server (de kiem tra noi dung / chu ky) */
static void demo_log(const char *what, char *json)
{
    ESP_LOGI(TAG, "[DEMO] %s -> server (khong gui that): %s", what, json ? json : "(het bo nho)");
    cp_free(json);
}

/* ================= API ================= */

esp_err_t cloud_init(void)
{
    if (pt_demo_enabled())
    {
        ESP_LOGW(TAG, "!!! CHE DO THU (PT_DEMO_PAYMENT = 1): thanh toan GIA LAP, khong ket noi server !!!");
        cp_mac_normalize(device_id_get(), s_mac); /* de in goi tin mau dung bayMac */
        return ESP_OK;
    }
    if (s_client)
        return ESP_OK;

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock)
        return ESP_ERR_NO_MEM;
    for (int i = 0; i < TXQ_SLOTS; i++)
        s_slot_msg[i] = -1;
    txq_init(); /* FRAM da khoi tao truoc cloud_init */

    cp_mac_normalize(device_id_get(), s_mac);
    snprintf(s_topic_up, sizeof(s_topic_up), "carwash/v1/%s/up", s_mac);
    snprintf(s_topic_down, sizeof(s_topic_down), "carwash/v1/%s/down", s_mac);
    snprintf(s_topic_status, sizeof(s_topic_status), "carwash/v1/%s/status", s_mac);
    snprintf(s_topic_cfg, sizeof(s_topic_cfg), "carwash/v1/%s/config", s_mac);
    snprintf(s_client_id, sizeof(s_client_id), "carwash-%s", s_mac);

    esp_mqtt_client_config_t c = {
        .broker.address.hostname = CLOUD_BROKER_HOST,
        .broker.address.port = CLOUD_BROKER_PORT,
#if CLOUD_BROKER_USE_TLS
        .broker.address.transport = MQTT_TRANSPORT_OVER_SSL,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
#else
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
#endif
        .credentials.username = CLOUD_BROKER_USERNAME,
        .credentials.authentication.password = CLOUD_BROKER_PASSWORD,
        .credentials.client_id = s_client_id,
        .session.keepalive = CLOUD_KEEPALIVE_S,
        .session.disable_clean_session = false, /* khong giu hang doi lenh cu khi ket noi lai */
        .session.last_will = {
            .topic = s_topic_status,
            .msg = "OFFLINE",
            .msg_len = 7,
            .qos = 1,
            .retain = 1,
        },
#if CONFIG_MQTT_PROTOCOL_5
        .session.protocol_ver = MQTT_PROTOCOL_V_5,
#else
        .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1,
#endif
        .network.reconnect_timeout_ms = CLOUD_RECONNECT_MS,
        .network.timeout_ms = 10000,
        .buffer.size = 2048,
        .buffer.out_size = 2048,
        .task.stack_size = 6144,
    };

    s_client = esp_mqtt_client_init(&c);
    if (!s_client)
        return ESP_FAIL;

#if CONFIG_MQTT_PROTOCOL_5
    esp_mqtt5_connection_property_config_t prop = {
        .session_expiry_interval = 0, /* Session Expiry = 0: khong giu lenh cu */
    };
    esp_mqtt5_client_set_connect_property(s_client, &prop);
#else
    ESP_LOGW(TAG, "menuconfig chua bat MQTT 5.0 -> dung MQTT 3.1.1");
#endif

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    ntp_time_on_sync(on_time_synced);
    esp_event_handler_register(NETMGR_EVENT, NETMGR_EVENT_IF_DOWN, on_netmgr_event, NULL);
    esp_event_handler_register(NETMGR_EVENT, NETMGR_EVENT_DEFAULT_CHANGED, on_netmgr_event, NULL);
    esp_event_handler_register(NETMGR_EVENT, NETMGR_EVENT_ONLINE, on_netmgr_event, NULL);
    esp_err_t err = esp_mqtt_client_start(s_client);
    ESP_LOGI(TAG, "broker %s:%d%s, client %s, bayMac %s, khoang %d", CLOUD_BROKER_HOST, CLOUD_BROKER_PORT,
             CLOUD_BROKER_USE_TLS ? " (TLS)" : "", s_client_id, s_mac, CLOUD_BAY_NUMBER);
    return err;
}

bool cloud_is_ready(void)
{
    if (pt_demo_enabled())
        return true;
    return ready_now();
}

esp_err_t cloud_create_order(const char *order_id, const devcfg_package_t *pkg)
{
    if (pt_demo_enabled())
    {
        demo_log("InitTransaction", cp_build_init(s_mac, CLOUD_BAY_NUMBER, pkg->duration_s, pkg->price));
        return pt_demo_create_order(order_id, pkg);
    }
    if (!s_client)
        return ESP_ERR_INVALID_STATE;

    lock();
    if (strcmp(s_init.order_id, order_id) != 0 || !s_init.json)
    {
        /* Don moi */
        init_clear();
        s_init.json = cp_build_init(s_mac, CLOUD_BAY_NUMBER, pkg->duration_s, pkg->price);
        strlcpy(s_init.order_id, order_id, sizeof(s_init.order_id));
    }
    /* Gui lai: dung nguyen van (cung packageId, timestamp) */
    esp_err_t err = s_init.json ? publish_raw(s_init.json) : ESP_ERR_NO_MEM;
    unlock();
    return err;
}

esp_err_t cloud_cancel_order(const char *order_id)
{
    if (pt_demo_enabled())
    {
        pt_demo_cancel_order(order_id);
        return ESP_OK;
    }
    if (!s_client || !order_id || !order_id[0])
        return ESP_ERR_INVALID_ARG;

    uint32_t txn = 0;
    lock();
    if (s_init.order_id[0] && strcmp(s_init.order_id, order_id) == 0)
    {
        if (s_init.txn)
        {
            txn = s_init.txn;
            init_clear();
        }
        else
        {
            s_init.cancelled = true; /* chua co QR: co QR ve thi huy */
        }
    }
    else
    {
        txn = cp_txn_from_str(order_id);
        if (txn && txn == s_init.txn)
            init_clear();
    }
    unlock();

    if (!txn)
        return ESP_OK;
    return send_cancel_payment(txn);
}

esp_err_t cloud_query_order(const char *order_id)
{
    if (pt_demo_enabled())
    {
        pt_demo_query_order(order_id);
        return ESP_OK;
    }
    /* Server tu gui lai StartTransaction theo lich (7 luot / 35 giay) -> khong can hoi */
    return ESP_OK;
}

esp_err_t cloud_report_session(const ws_session_t *s, bool ended)
{
    if (pt_demo_enabled())
    {
        /* Che do thu: coi nhu da gui -> khong de phien thu nam lai trong FRAM */
        demo_log(ended ? "Meter ket thuc" : "Meter bat dau", cp_build_meter(s_mac, CLOUD_BAY_NUMBER, s));
        return ESP_OK;
    }
    if (!s_lock)
        return ESP_ERR_INVALID_STATE;

    lock();
    if (ended)
        s_snap_valid = false; /* chuong ve NORMAL */
    else
    {
        s_snap = *s;
        s_snap_valid = true;
    }
    unlock();

    if (!ended)
        return send_meter(s); /* mat thi server gui lai Start -> tram bao lai Meter */
    /* Meter ket thuc: luu FRAM, gui den khi server nhan duoc */
    return send_important(cp_build_meter(s_mac, CLOUD_BAY_NUMBER, s));
}

esp_err_t cloud_report_progress(const ws_session_t *s)
{
    if (pt_demo_enabled())
    {
        demo_log("Meter", cp_build_meter(s_mac, CLOUD_BAY_NUMBER, s));
        return ESP_OK;
    }
    if (!s_lock)
        return ESP_ERR_INVALID_STATE;

    lock();
    s_snap = *s;
    s_snap_valid = (s->state == WS_ST_RUNNING || s->state == WS_ST_PAUSED);
    unlock();
    return send_meter(s);
}

esp_err_t cloud_report_event(const char *event, const char *detail)
{
    /* Giao thuc chua co goi canh bao rieng */
    ESP_LOGW(TAG, "su kien: %s (%s)", event, detail ? detail : "");
    return ESP_OK;
}