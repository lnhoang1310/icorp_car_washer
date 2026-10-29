#include "app_fsm.h"
#include "app_config.h"
#include "device_config.h"
#include "wash_session.h"
#include "session_store.h"
#include "light_control.h"
#include "io_expander.h"
#include "cloud_manager.h"
#include "ui_manager.h"
#include "audio_player.h"
#include "device_id.h"
#include "nvs_store.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "app_fsm";

#define APP_QUEUE_LEN        20
#define APP_TASK_STACK       6144
#define APP_TASK_PRIO        8
#define APP_TICK_MS          100
#define APP_HINT_INTERVAL_MS 5000
#define TIME_VALID_EPOCH     1672531200 /* 01/01/2023 */

/* Ma QR o man hinh dau: "CARWASH:AABBCCDDEEFF", AABBCCDDEEFF = MAC goc (eFuse) cua chinh thiet bi.
 * Chu cua hang quet ma nay de khai bao chuong len server va gan so khoang; khach dung App quet de rua. */
#define APP_QR_FMT  "CARWASH:%s"

/* Sau khi bam 1 nut bom (bot/nuoc/kho), bam lai CHINH nut do trong khoang nay bi bo qua
 * (chong bam dup tay lam bom vua bat lai tat). Nut khac van nhan ngay.
 * Muon doi: dinh nghia BTN_REPEAT_LOCK_MS trong app_config.h. 0 = tat tinh nang. */
/* Man "Ban co muon tiep tuc phien rua?" (sau khi co dien lai giua phien): khach khong thao tac gi
 * (khong cham man hinh, khong bam nut) trong khoang nay -> tu ket thuc phien. 0 = cho mai. */
#ifndef RESUME_ASK_TIMEOUT_S
#define RESUME_ASK_TIMEOUT_S 30
#endif

#ifndef BTN_REPEAT_LOCK_MS
#define BTN_REPEAT_LOCK_MS   1000
#endif

/* Chi so bom trong wash_session trung voi chi so relay trong io_expander */
_Static_assert((int)WS_FOAM == (int)IO_RELAY_FOAM, "map bom");
_Static_assert((int)WS_WATER == (int)IO_RELAY_WATER, "map bom");
_Static_assert((int)WS_DRY == (int)IO_RELAY_DRY, "map bom");

typedef enum
{
    ST_BOOT = 0,
    ST_IDLE,            /* man hinh dau: QR app + nut "Bam vao day" */
    ST_SELECT,          /* man chon goi (tra bang tai khoan ngan hang) */
    ST_OUT_OF_SERVICE,
    ST_CREATING_QR,
    ST_WAIT_PAYMENT,
    ST_PAID,
    ST_WASHING,
    ST_FINISHED,
    ST_NOTICE,
} app_state_t;

static const char *const k_state_name[] = {
    "BOOT", "IDLE", "SELECT", "OUT_OF_SERVICE", "CREATING_QR", "WAIT_PAYMENT", "PAID", "WASHING", "FINISHED", "NOTICE",
};

static QueueHandle_t s_queue = NULL;
static app_state_t s_state = ST_BOOT;

static ws_session_t s_ws;
static light_ctrl_t s_light;
static bool s_light_on = false;
static int s_applied_pump = -2; /* -2 = chua biet, ep ghi lan dau */

/* Don hang dang xu ly */
static char s_order_id[WS_ORDER_ID_LEN];
static devcfg_package_t s_pkg;
static int s_create_retries = 0;

/* Don vua huy/het han: van nhan neu tien ve tre trong khoang late_paid_grace_s */
static char s_late_order_id[WS_ORDER_ID_LEN];
static devcfg_package_t s_late_pkg;
static int64_t s_late_until_ms = 0;

static int64_t s_deadline_ms = 0; /* 0 = khong hen gio */
static int64_t s_last_welcome_ms = INT64_MIN / 2;
static int64_t s_last_hint_ms = INT64_MIN / 2;
static bool s_config_pending = false;

/* ---- Phien tam dung ----
 * Dung khan cap = ngat nguon (phan cung), firmware khong xu ly nut. Co dien lai giua phien -> phien
 * tam dung (ws o WS_ST_PAUSED, ca 2 dong ho su dung + do xe dung), hoi "Tiep tuc phien rua?".
 * Loi bus I/O (mat lien lac chip doc nut / relay) cung tam dung phien, hien man "Loi thiet bi". */
static bool s_resume_ask = false;     /* dang o man hoi tiep tuc */
static int64_t s_resume_since_ms = 0; /* moc tinh gio tu ket thuc o man hoi tiep tuc (0 = chua tinh) */
static bool s_fault_handled = false;  /* trang thai loi bus I/O da xu ly lan cuoi */

/* Thoi diem nhan gan nhat cua tung nut bom (chong bam dup) */
static int64_t s_btn_last_ms[WS_KIND_COUNT];

/* So giay dang hien tren man dang rua (su dung con lai, do xe con lai): doi la ve lai ngay,
 * de dong ho nhay TUNG giay ke ca khi bot tru nhanh gap 3 (khong nhay coc 3 giay 1 lan) */
static uint32_t s_shown_use_s = UINT32_MAX;
static uint32_t s_shown_park_s = UINT32_MAX;

/* Bao tien do phien len server */
static int64_t s_progress_last_ms = 0;

/* Ma cac phien vua ket thuc: server gui lai StartTransaction cu (giao muon) thi KHONG mo lai phien.
 * Luu NVS de mat dien / khoi dong lai van nho. */
#define APP_DONE_IDS 4
static char s_done_ids[APP_DONE_IDS][WS_ORDER_ID_LEN];
static int s_done_pos = 0;

static void done_load(void)
{
    size_t sz = sizeof(s_done_ids);
    if (nvs_store_get_blob("app", "done_ids", s_done_ids, &sz) != ESP_OK || sz != sizeof(s_done_ids))
        memset(s_done_ids, 0, sizeof(s_done_ids));
    for (int i = 0; i < APP_DONE_IDS; i++)
        s_done_ids[i][WS_ORDER_ID_LEN - 1] = 0;
    uint8_t pos = 0;
    nvs_store_get_u8("app", "done_pos", &pos);
    s_done_pos = pos % APP_DONE_IDS;
}

static void done_add(const char *id)
{
    if (!id || !id[0])
        return;
    strlcpy(s_done_ids[s_done_pos], id, WS_ORDER_ID_LEN);
    s_done_pos = (s_done_pos + 1) % APP_DONE_IDS;
    nvs_store_set_blob("app", "done_ids", s_done_ids, sizeof(s_done_ids));
    nvs_store_set_u8("app", "done_pos", (uint8_t)s_done_pos);
}

static bool done_has(const char *id)
{
    for (int i = 0; i < APP_DONE_IDS; i++)
        if (s_done_ids[i][0] && strcmp(s_done_ids[i], id) == 0)
            return true;
    return false;
}

static void btn_lock_reset(void)
{
    for (int k = 0; k < WS_KIND_COUNT; k++)
        s_btn_last_ms[k] = INT64_MIN / 2;
}

/* ================= TIEN ICH ================= */

static inline int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static inline const device_config_t *cfg(void) { return device_config_get(); }

static uint32_t epoch_now(void)
{
    time_t t = time(NULL);
    return (t > TIME_VALID_EPOCH) ? (uint32_t)t : 0;
}

/* Mat lien lac bus I/O: khong doc duoc nut / khong dieu khien duoc bom */
static bool io_fault(void)
{
    return !io_expander_is_healthy();
}

static void set_state(app_state_t st)
{
    if (st != s_state)
        ESP_LOGI(TAG, "%s -> %s", k_state_name[s_state], k_state_name[st]);
    s_state = st;
    s_deadline_ms = 0;
}

static void set_deadline_s(uint32_t sec) { s_deadline_ms = now_ms() + (int64_t)sec * 1000; }

static void make_order_id(char out[WS_ORDER_ID_LEN])
{
    uint32_t seq = 0;
    nvs_store_get_u32("app", "order_seq", &seq); /* loi -> bat dau tu 0 */
    seq++;
    nvs_store_set_u32("app", "order_seq", seq);
    snprintf(out, WS_ORDER_ID_LEN, "%s-%lu-%04lu", device_id_get(),
             (unsigned long)epoch_now(), (unsigned long)(seq % 10000));
}

static audio_clip_t clip_on(ws_kind_t k)
{
    return k == WS_FOAM ? AUDIO_BUTTON_CLICK : k == WS_WATER ? AUDIO_BUTTON_CLICK : AUDIO_BUTTON_CLICK;
}

/* Noi dung ma QR mo app o man hinh dau */
static const char *app_qr_text(void)
{
    static char buf[32];
    snprintf(buf, sizeof(buf), APP_QR_FMT, device_id_get()); /* co dinh theo thiet bi, khong lay tu server */
    return buf;
}

/* Gui tien do phien len server (goi khi co thay doi va dinh ky) */
static void report_progress(void)
{
    s_progress_last_ms = now_ms();
    cloud_report_progress(&s_ws);
}

/* ================= RELAY ================= */

static void apply_pumps(void)
{
    int want = -1;
    if (s_state == ST_WASHING && ws_state(&s_ws) == WS_ST_RUNNING && ws_active_pump(&s_ws) != WS_NONE)
        want = (int)ws_active_pump(&s_ws);

    if (want == s_applied_pump)
        return;

    esp_err_t err = io_pump_select(want);
    if (err == ESP_OK)
    {
        s_applied_pump = want;
        return;
    }

    ESP_LOGE(TAG, "io_pump_select(%d) loi: %s", want, esp_err_to_name(err));
    s_applied_pump = -2; /* lan sau ghi lai */

    /* Khong bat duoc bom -> dong bo lai wash_session de khong tru thoi gian oan cho khach */
    if (want >= 0)
    {
        if (io_fault())
            ws_pause(&s_ws, true);
        else
            ws_press(&s_ws, (ws_kind_t)want); /* nhan lai = dung */
        io_pump_select(-1);
    }
}

static void update_light(uint32_t elapsed_ms)
{
    light_inputs_t in = {0};
    time_t t = time(NULL);
    in.time_valid = t > TIME_VALID_EPOCH;
    if (in.time_valid)
    {
        struct tm lt;
        localtime_r(&t, &lt);
        in.year = lt.tm_year + 1900;
        in.month = lt.tm_mon + 1;
        in.day = lt.tm_mday;
        in.minute_of_day = lt.tm_hour * 60 + lt.tm_min;
    }
    in.motion = io_input_is_active(IO_IN_MOTION);
    in.session_active = (s_state == ST_PAID || s_state == ST_WASHING);

    bool on = light_ctrl_update(&s_light, &in, elapsed_ms);
    if (on != s_light_on && io_light_set(on) == ESP_OK)
    {
        s_light_on = on;
        ESP_LOGI(TAG, "den %s (%02d:%02d, troi %s)", on ? "BAT" : "TAT", in.minute_of_day / 60,
                 in.minute_of_day % 60, s_light.is_dark ? "toi" : "sang");
    }

    /* Moi 30 giay ghi lai relay ra PCF: chip output bi reset (nhieu nguon) thi den / bom tu ve dung
     * trang thai, khong phai cho toi lan doi trang thai sau (voi den la toi 12 tieng). */
    static uint32_t refresh_ms = 0;
    refresh_ms += elapsed_ms;
    if (refresh_ms >= 30000)
    {
        refresh_ms = 0;
        io_relay_refresh();
    }
}

/* ================= CHUYEN TRANG THAI ================= */

static void apply_pending_config(void)
{
    if (!s_config_pending)
        return;
    s_config_pending = false;
    light_ctrl_set_config(&s_light, &cfg()->light);
    audio_set_volume(cfg()->volume);
    ESP_LOGI(TAG, "da ap dung cau hinh moi");
}

/* Vua khoi dong, chua ket noi server lan nao: hien "Dang ket noi..." toi da BOOT_CONNECT_GRACE_S giay
 * (tinh tu luc bat nguon) roi moi hien man canh bao "Mat ket noi". Da ket noi duoc 1 lan thi tu do
 * mat mang la canh bao ngay. */
#ifndef BOOT_CONNECT_GRACE_S
#define BOOT_CONNECT_GRACE_S 30
#endif
static bool s_boot_grace = true;

static void show_idle_screen(void)
{
    if (io_fault())
        ui_show_fault(cfg()->owner_phone);
    else if (s_state == ST_OUT_OF_SERVICE && s_boot_grace)
        ui_show_connecting();
    else if (s_state == ST_OUT_OF_SERVICE)
        ui_show_offline(cfg()->owner_phone);
    else
        ui_show_home(app_qr_text());
}

/* Man hinh cho phien dang rua (ST_WASHING), tuy phien dang chay / tam dung / loi bus */
static void show_session_screen(void)
{
    if (ws_state(&s_ws) != WS_ST_PAUSED)
    {
        s_resume_ask = false;
        ui_show_washing(&s_ws);
    }
    else if (io_fault())
    {
        s_resume_ask = false;
        ui_show_fault(cfg()->owner_phone);
    }
    else
    {
        s_resume_ask = true; /* cho khach bam "Tiep tuc rua" */
        ui_show_resume_ask(&s_ws);
    }
}

static void apply_pending_op_mode(void);
static void apply_pending_packages(void);

static void go_idle(void)
{
    ws_clear(&s_ws);
    s_resume_ask = false;
    s_order_id[0] = 0;
    apply_pending_config();
    apply_pending_op_mode(); /* chuong vua ve NORMAL */
    apply_pending_packages(); /* khach mua xong / huy */
    set_state(cloud_is_ready() ? ST_IDLE : ST_OUT_OF_SERVICE);
    apply_pumps();
    show_idle_screen();
}

static void go_notice(const char *title, const char *msg, int clip)
{
    set_state(ST_NOTICE);
    ui_show_notice(title, msg);
    if (clip >= 0)
        audio_play((audio_clip_t)clip);
    set_deadline_s(cfg()->notice_screen_s);
}

/* Khach bam "Bam vao day" o man hinh dau -> man chon goi (co hen gio tu quay ve) */
static void go_select(void)
{
    set_state(ST_SELECT);
    ui_show_packages(cfg()->packages, cfg()->package_count);
    set_deadline_s(cfg()->select_timeout_s);
}

static void remember_late(void)
{
    if (cfg()->late_paid_grace_s == 0 || !s_order_id[0])
        return;
    strlcpy(s_late_order_id, s_order_id, sizeof(s_late_order_id));
    s_late_pkg = s_pkg;
    s_late_until_ms = now_ms() + (int64_t)cfg()->late_paid_grace_s * 1000;
}

static void cancel_order(const char *title, const char *msg, int clip);

static void start_order(int index)
{
    const devcfg_package_t *p = device_config_package(index);
    if (!p)
    {
        ESP_LOGW(TAG, "chon goi sai chi so %d", index);
        return;
    }
    s_pkg = *p;
    make_order_id(s_order_id);
    s_create_retries = 0;

    ESP_LOGI(TAG, "tao don %s: %s (%lus rua, %lus do xe) %lu d", s_order_id, s_pkg.label,
             (unsigned long)s_pkg.duration_s, (unsigned long)s_pkg.park_s, (unsigned long)s_pkg.price);
    set_state(ST_CREATING_QR);
    ui_show_qr_loading(&s_pkg);
    if (cloud_create_order(s_order_id, &s_pkg) != ESP_OK)
    {
        /* Vua mat ket noi: bao loi ngay, khong bat khach cho het thoi gian */
        cancel_order("Lỗi kết nối", "Không tạo được mã thanh toán, vui lòng thử lại", AUDIO_CONNECTION_ERROR);
        return;
    }
    set_deadline_s(cfg()->create_order_timeout_s);
}

static void cancel_order(const char *title, const char *msg, int clip)
{
    cloud_cancel_order(s_order_id);
    remember_late();
    go_notice(title, msg, clip);
}

static void go_washing(bool restored);

/* direct = true: don tu App (da chon goi + tra tien tren dien thoai) -> vao thang man dang rua.
 * total_ms = thoi luong rua server bao (packageDuration); 0 = lay theo goi */
static void start_session(const char *order_id, const devcfg_package_t *pkg, uint32_t total_ms, bool direct)
{
    s_pkg = *pkg;
    strlcpy(s_order_id, order_id, sizeof(s_order_id));
    if (total_ms == 0)
        total_ms = pkg->duration_s * 1000u;
    /* parkingTime (tai lieu muc 4.4): lay theo goi. Server bao thoi luong khac goi (hiem) -> giu nguyen
     * phan do xe them cua goi. Khong biet goi -> 0 = thoi luong + park_extra_ms (5 phut). */
    uint32_t park_ms = 0;
    if (pkg->park_s >= pkg->duration_s && pkg->duration_s)
        park_ms = total_ms + (pkg->park_s - pkg->duration_s) * 1000u;

    if (!ws_start(&s_ws, &cfg()->wash, total_ms, park_ms, order_id, pkg->id, epoch_now()))
    {
        ESP_LOGE(TAG, "ws_start that bai (cau hinh sai?) -- don %s", order_id);
        cloud_report_event("session_start_failed", order_id);
        go_notice("Lỗi", "Không bắt đầu được phiên rửa xe, vui lòng liên hệ quản lý", AUDIO_CONNECTION_ERROR);
        return;
    }
    bool blocked = io_fault();
    if (blocked)
        ws_pause(&s_ws, true);

    session_store_save(&s_ws); /* luu NGAY: mat dien tu day tro di van giu tien cua khach */
    cloud_report_session(&s_ws, false); /* Meter WASHING: xac nhan da bat dau */
    s_progress_last_ms = now_ms();

    s_late_until_ms = 0;
    if (blocked)
    {
        /* Tien ve luc dang loi bus I/O: vao thang phien o trang thai tam dung,
         * het loi se hoi tiep tuc */
        set_state(ST_WASHING);
        show_session_screen();
        audio_play(AUDIO_PAY_OK);
        apply_pumps();
        return;
    }
    if (direct)
    {
        go_washing(false);
        return;
    }
    set_state(ST_PAID);
    ui_show_paid(&s_pkg);
    audio_play(AUDIO_PAY_OK);
    set_deadline_s(cfg()->paid_screen_s);
}

static void go_washing(bool restored)
{
    set_state(ST_WASHING);
    btn_lock_reset();
    show_session_screen();
    // if (ws_state(&s_ws) == WS_ST_PAUSED)
    //     audio_play(io_fault() ? AUDIO_OUT_OF_SERVICE : AUDIO_RESUME_ASK);
    // else
    //     audio_play(restored ? AUDIO_RESTORED : AUDIO_WASH_GUIDE);
    apply_pumps();
}

static void finish_session(void)
{
    apply_pumps(); /* ws da tat bom, dam bao relay cung tat */

    ESP_LOGI(TAG, "ket thuc phien %s (kich ban %u, ly do %u): da tru bot %lus nuoc %lus kho %lus (bom thuc %lus/%lus/%lus), con %lus su dung, %lus do xe",
             s_ws.order_id, s_ws.mode, s_ws.end_reason,
             (unsigned long)(s_ws.charged_ms[WS_FOAM] / 1000),
             (unsigned long)(s_ws.charged_ms[WS_WATER] / 1000),
             (unsigned long)(s_ws.charged_ms[WS_DRY] / 1000),
             (unsigned long)(s_ws.used_ms[WS_FOAM] / 1000),
             (unsigned long)(s_ws.used_ms[WS_WATER] / 1000),
             (unsigned long)(s_ws.used_ms[WS_DRY] / 1000),
             (unsigned long)(s_ws.session_left_ms / 1000), (unsigned long)(s_ws.park_left_ms / 1000));

    session_store_save(&s_ws); /* luu trang thai FINISHED truoc */
    done_add(s_ws.order_id);
    if (cloud_report_session(&s_ws, true) == ESP_OK)
        session_store_clear();
    /* khong gui duoc -> giu trong FRAM, lan khoi dong sau gui lai */

    s_resume_ask = false;
    set_state(ST_FINISHED);
    ui_show_finished(&s_ws);
    audio_play(s_ws.end_reason == WS_END_USER_STOP ? AUDIO_END_SESSION : AUDIO_END_SESSION);
    set_deadline_s(cfg()->finished_screen_s);
}

/* ================= LOI BUS I/O ================= */

/* Vua mat lien lac bus I/O */
static void on_fault(void)
{
    if (s_state == ST_PAID)
        set_state(ST_WASHING); /* phien da tra tien: xu ly chung voi phien dang rua */

    if (s_state == ST_WASHING)
    {
        if (ws_state(&s_ws) == WS_ST_RUNNING)
        {
            ws_pause(&s_ws, true); /* tat bom, dung dong ho phien */
            apply_pumps();
            session_store_save(&s_ws);
            report_progress();
        }
        show_session_screen();
    }
    else if (s_state == ST_CREATING_QR || s_state == ST_WAIT_PAYMENT)
    {
        /* Chua tra tien: huy don. Tien ve tre van duoc nhan (remember_late) */
        ESP_LOGW(TAG, "loi thiet bi khi dang thanh toan -> huy don %s", s_order_id);
        cloud_cancel_order(s_order_id);
        remember_late();
        go_idle(); /* go_idle -> show_idle_screen -> man loi thiet bi */
    }
    else if (s_state != ST_BOOT)
    {
        /* IDLE / SELECT / TAM NGUNG / NOTICE / FINISHED: chi doi man hinh, hen gio cua
         * SELECT/NOTICE/FINISHED van chay va se ve go_idle() (van hien man loi vi dang loi) */
        ui_show_fault(cfg()->owner_phone);
    }
    // audio_play(AUDIO_OUT_OF_SERVICE);
    cloud_report_event("io_fault", s_ws.order_id);
}

/* Bus I/O hoi phuc */
static void on_fault_cleared(void)
{
    cloud_report_event("io_fault_cleared", s_ws.order_id);
    if (s_state == ST_WASHING)
    {
        show_session_screen(); /* ws van o WS_ST_PAUSED -> man hoi tiep tuc */
        // audio_play(AUDIO_RESUME_ASK);
    }
    else if (s_state != ST_BOOT)
    {
        go_idle(); /* ve man hinh dau (hoac tam ngung) */
    }
}

/* Goi khi io_expander bao doi trang thai bus. Chi xu ly khi trang thai loi doi. */
static void apply_safety(void)
{
    bool fault = io_fault();
    if (fault == s_fault_handled)
        return;
    s_fault_handled = fault;
    if (fault)
        on_fault();
    else
        on_fault_cleared();
}

/* Khach bam "Tiep tuc rua" tren man hoi */
static void resume_session(void)
{
    if (s_state != ST_WASHING || !s_resume_ask || io_fault())
        return;
    s_resume_ask = false;
    ws_pause(&s_ws, false); /* dong ho phien chay lai tu day; bom van tat, khach nhan nut de bat */
    session_store_save(&s_ws);
    ui_show_washing(&s_ws);
    // audio_play(AUDIO_RESUME);
    report_progress();
    ESP_LOGI(TAG, "khach tiep tuc phien %s, con %lus", s_ws.order_id,
             (unsigned long)(s_ws.session_left_ms / 1000));
}

/* ================= XU LY SU KIEN ================= */

static void handle_button(ws_kind_t kind)
{
    if (s_state == ST_PAID)
        go_washing(false); /* khach bam nut ngay -> vao rua luon */

    if (s_state != ST_WASHING)
    {
        if (now_ms() - s_last_hint_ms > APP_HINT_INTERVAL_MS)
        {
            if (io_fault())
            {
                s_last_hint_ms = now_ms();
                // audio_play(AUDIO_OUT_OF_SERVICE);
            }
            else if (s_state == ST_IDLE || s_state == ST_SELECT)
            {
                s_last_hint_ms = now_ms();
                // audio_play(AUDIO_SELECT_PACKAGE);
            }
        }
        return;
    }

    if (s_resume_ask)
    {
        s_resume_since_ms = now_ms(); /* co nguoi -> tinh lai 30s */
        /* Da mo khoa nhung khach chua xac nhan: nut bom chua co tac dung */
        if (now_ms() - s_last_hint_ms > APP_HINT_INTERVAL_MS)
        {
            s_last_hint_ms = now_ms();
            // audio_play(AUDIO_RESUME_ASK);
        }
        return;
    }

    /* Bam lai cung nut qua nhanh -> coi nhu bam dup, bo qua (khong am thanh, khong doi bom) */
    int64_t t = now_ms();
    if (BTN_REPEAT_LOCK_MS > 0 && (unsigned)kind < WS_KIND_COUNT)
    {
        if (t - s_btn_last_ms[kind] < BTN_REPEAT_LOCK_MS)
        {
            ESP_LOGI(TAG, "bo qua nut %d: bam lai sau %lld ms (< %d ms)", (int)kind,
                     (long long)(t - s_btn_last_ms[kind]), BTN_REPEAT_LOCK_MS);
            return;
        }
        s_btn_last_ms[kind] = t;
    }

    ws_press_result_t r = ws_press(&s_ws, kind);
    switch (r)
    {
    case WS_PRESS_STARTED:
    case WS_PRESS_PENDING:
        audio_play(clip_on(kind));
        break;
    case WS_PRESS_STOPPED:
        audio_play(AUDIO_BUTTON_CLICK);
        break;
    case WS_PRESS_CANCELLED:
        /* Bam nut khac roi bam lai ngay trong luc cho dead-time: lenh chuyen bi huy,
         * khong bom nao chay -> bao cho khach biet de bam lai */
        // audio_play(AUDIO_TOO_FAST);
        ui_warn_too_fast();
        break;
    case WS_PRESS_EXHAUSTED: /* kich ban 2: loai nay da het phan, loai khac van con */
        // audio_play(AUDIO_KIND_EMPTY);
        break;
    case WS_PRESS_IGNORED:
        if (ws_state(&s_ws) == WS_ST_PAUSED && now_ms() - s_last_hint_ms > APP_HINT_INTERVAL_MS)
        {
            s_last_hint_ms = now_ms();
            // audio_play(io_fault() ? AUDIO_OUT_OF_SERVICE : AUDIO_RESUME_ASK);
        }
        break;
    }
    apply_pumps();
    session_store_save(&s_ws);
    ui_update_washing(&s_ws);
    if (r == WS_PRESS_STARTED || r == WS_PRESS_STOPPED || r == WS_PRESS_CANCELLED)
        report_progress();
}

static void handle_input(io_input_t in, bool active)
{
    switch (in)
    {
    case IO_IN_BTN_FOAM:
    case IO_IN_BTN_WATER:
    case IO_IN_BTN_DRY:
        if (active)
        {
            ui_wake();
            handle_button((ws_kind_t)in);
        }
        break;

    case IO_IN_MOTION:
        if (active)
        {
            ui_wake();
            if (s_state == ST_IDLE && !io_fault() &&
                now_ms() - s_last_welcome_ms > (int64_t)cfg()->welcome_interval_s * 1000)
            {
                s_last_welcome_ms = now_ms();
                // audio_play(AUDIO_WELCOME);
            }
        }
        break;

    case IO_IN_BUS_FAULT:
        apply_safety();
        break;

    default:
        break;
    }
}

static void handle_order_result(const char *order_id, cloud_order_status_t st)
{
    bool is_current = (s_state == ST_WAIT_PAYMENT || s_state == ST_CREATING_QR) &&
                      strcmp(order_id, s_order_id) == 0;

    if (is_current)
    {
        switch (st)
        {
        case CLOUD_ORDER_PAID:
            start_session(order_id, &s_pkg, 0, false);
            break;
        case CLOUD_ORDER_EXPIRED:
            remember_late();
            go_notice("Mã thanh toán đã hết hạn, vui lòng thực hiện lại", "", AUDIO_QR_TIMEOUT);
            break;
        case CLOUD_ORDER_CANCELLED:
        case CLOUD_ORDER_FAILED:
        default:
            go_notice("Giao dịch không thành công", "Vui lòng thực hiện lại", AUDIO_PAY_ERROR);
            break;
        }
        return;
    }

    if (st != CLOUD_ORDER_PAID)
    {
        ESP_LOGI(TAG, "bo qua ket qua %d cho don %s (khong phai don dang cho)", st, order_id);
        return;
    }

    /* Tien ve cho don da huy / het han */
    bool is_late = s_late_until_ms && strcmp(order_id, s_late_order_id) == 0;
    if (is_late && now_ms() <= s_late_until_ms &&
        (s_state == ST_IDLE || s_state == ST_SELECT || s_state == ST_NOTICE || s_state == ST_OUT_OF_SERVICE))
    {
        ESP_LOGW(TAG, "nhan tien tre cho don %s -> van bat dau phien", order_id);
        cloud_report_event("late_payment_served", order_id);
        start_session(order_id, &s_late_pkg, 0, false);
        return;
    }

    ESP_LOGW(TAG, "tien ve cho don %s nhung khong phuc vu duoc (trang thai %s) -> can hoan tien",
             order_id, k_state_name[s_state]);
    cloud_report_event("late_payment_unserved", order_id);
}

/* Server gui StartTransaction: order_id = transactionId, total_ms = thoi luong rua da mua.
 * Nhan dien theo ma phien (giao thuc khong co Ack, server gui lai toi da 7 lan / 35 giay). */
static void on_cloud_start(const char *oid, uint32_t total_ms, uint32_t package_id)
{
    /* 1. Don QR dang cho thanh toan tai tram */
    if ((s_state == ST_WAIT_PAYMENT || s_state == ST_CREATING_QR) && strcmp(oid, s_order_id) == 0)
    {
        ESP_LOGI(TAG, "don %s da thanh toan", oid);
        start_session(oid, &s_pkg, total_ms, false);
        return;
    }

    /* 2. Start lap lai cua chinh phien dang rua -> bao lai Meter ngay, khong reset gi */
    if ((s_state == ST_PAID || s_state == ST_WASHING) && strcmp(oid, s_ws.order_id) == 0)
    {
        ESP_LOGI(TAG, "Start lap lai phien %s dang rua -> gui lai Meter", oid);
        report_progress();
        return;
    }

    /* 3. Phien da ket thuc: Start cu giao muon -> bo qua */
    if (done_has(oid))
    {
        ESP_LOGW(TAG, "Start cua phien %s da ket thuc -> bo qua", oid);
        return;
    }

    bool free_bay = (s_state == ST_IDLE || s_state == ST_SELECT || s_state == ST_NOTICE ||
                     s_state == ST_OUT_OF_SERVICE || s_state == ST_FINISHED);
    if (!free_bay)
    {
        /* Chuong dang co phien/don khac: khong chuyen, khong thay phien */
        ESP_LOGW(TAG, "Start phien %s den luc chuong dang ban (%s, phien %s) -> khong chay", oid,
                 k_state_name[s_state], s_state == ST_WASHING || s_state == ST_PAID ? s_ws.order_id : s_order_id);
        cloud_report_event("start_rejected_busy", oid);
        return;
    }

    /* 4. Tien ve tre cho don QR vua huy / het han */
    if (s_late_until_ms && strcmp(oid, s_late_order_id) == 0 && now_ms() <= s_late_until_ms)
    {
        ESP_LOGW(TAG, "nhan tien tre cho don %s -> van bat dau phien", oid);
        cloud_report_event("late_payment_served", oid);
        ui_wake();
        start_session(oid, &s_late_pkg, total_ms, false);
        return;
    }

    /* 5. Don moi tu App (khach quet QR man hinh dau, tra bang vi) */
    /* StartTransaction chi co packageDuration -> tim goi cung thoi luong trong danh sach de lay
     * parkingTime + gia. Khong co goi nao trung: do xe = thoi luong + 5 phut. */
    devcfg_package_t pkg = {.id = package_id};
    bool found = false;
    for (int i = 0; i < cfg()->package_count && !found; i++)
        if ((package_id && cfg()->packages[i].id == package_id) ||
            (!package_id && cfg()->packages[i].duration_s * 1000u == total_ms))
        {
            pkg = cfg()->packages[i];
            found = true;
        }
    if (!found)
    {
        pkg.duration_s = total_ms / 1000u;
        pkg.park_s = 0;
        device_config_fill_package(&pkg, (int)package_id - 1);
        ESP_LOGW(TAG, "don App %s: thoi luong %lus khong co trong danh sach goi -> do xe = thoi luong + %u phut",
                 oid, (unsigned long)pkg.duration_s, (unsigned)WS_PARK_EXTRA_MIN);
    }
    if (s_state == ST_SELECT || s_state == ST_NOTICE)
        ESP_LOGW(TAG, "don App %s den khi khach dang thao tac tai tram (%s) -> uu tien don App", oid,
                 k_state_name[s_state]);
    ESP_LOGI(TAG, "don tu App %s: %lu giay", oid, (unsigned long)(total_ms / 1000));
    ui_wake();
    start_session(oid, &pkg, total_ms, true);
}

/* Server dung tu xa: chi dung khi dung ma phien dang rua */
static void on_remote_stop(const char *oid)
{
    if ((s_state == ST_WASHING || s_state == ST_PAID) && strcmp(oid, s_ws.order_id) == 0 &&
        (ws_state(&s_ws) == WS_ST_RUNNING || ws_state(&s_ws) == WS_ST_PAUSED))
    {
        ESP_LOGW(TAG, "server dung tu xa phien %s", oid);
        ws_cancel(&s_ws); /* ly do WS_END_CANCELLED -> Meter reason REMOTE_STOP */
        finish_session();
        return;
    }
    ESP_LOGW(TAG, "RemoteStop phien %s khong khop phien dang rua (%s) -> bo qua", oid,
             s_ws.order_id[0] ? s_ws.order_id : "khong co");
}

/* Mode trong topic config (tai lieu muc 6.4): chi AP DUNG khi chuong NORMAL (khong co phien).
 * Dang co phien (PAID / WASHING, ke ca tam dung) -> giu ban hop le MOI NHAT o trang thai cho,
 * ap dung o go_idle() sau khi phien ket thuc, truoc phien tiep theo. */
static bool s_op_pending = false;
static app_evt_t s_op_pending_evt;

static bool bay_in_session(void)
{
    return s_state == ST_PAID || s_state == ST_WASHING;
}

static void apply_op_mode(const app_evt_t *e)
{
    device_config_t c = *cfg();
    c.wash.mode = e->data.op_mode.mode;
    if (c.wash.mode == WS_MODE_RATE)
        memcpy(c.wash.rate_pct, e->data.op_mode.rate_pct, sizeof(c.wash.rate_pct));
    else
        memcpy(c.wash.permille, e->data.op_mode.permille, sizeof(c.wash.permille));
    memcpy(c.op_raw, e->data.op_mode.raw, sizeof(c.op_raw));

    if (memcmp(&c.wash, &cfg()->wash, sizeof(c.wash)) == 0 && memcmp(c.op_raw, cfg()->op_raw, sizeof(c.op_raw)) == 0)
        return; /* nhan lai cau hinh retained giong het -> khong ghi NVS */
    if (device_config_set(&c) != ESP_OK)
    {
        ESP_LOGW(TAG, "cau hinh operationMode bi tu choi");
        return;
    }
    ESP_LOGI(TAG, "ap dung operationMode: %s bot %u nuoc %u kho %u (%s)",
             c.wash.mode == WS_MODE_RATE ? "SPEED" : "RATIO",
             c.wash.mode == WS_MODE_RATE ? c.wash.rate_pct[0] : c.wash.permille[0],
             c.wash.mode == WS_MODE_RATE ? c.wash.rate_pct[1] : c.wash.permille[1],
             c.wash.mode == WS_MODE_RATE ? c.wash.rate_pct[2] : c.wash.permille[2],
             c.wash.mode == WS_MODE_RATE ? "%" : "phan nghin");
}

static void on_op_mode(const app_evt_t *e)
{
    if (bay_in_session())
    {
        s_op_pending_evt = *e; /* nhieu ban trong luc rua -> giu ban moi nhat */
        s_op_pending = true;
        ESP_LOGI(TAG, "operationMode moi -> CHO, ap dung khi phien %s ket thuc", s_ws.order_id);
        return;
    }
    s_op_pending = false;
    apply_op_mode(e);
}

static void apply_pending_op_mode(void)
{
    if (!s_op_pending)
        return;
    s_op_pending = false;
    apply_op_mode(&s_op_pending_evt);
}

/* Danh sach goi trong topic config. Tai lieu C12 chua chot thoi diem co hieu luc -> firmware:
 * - khach DANG MUA (man chon goi / cho QR / cho thanh toan): giu ban moi nhat o trang thai cho,
 *   ap dung o go_idle() khi khach mua xong / huy / het gio -> man hinh khong doi giua chung;
 * - con lai: ap dung ngay. Phien dang rua da sao gia/thoi luong/do xe luc bat dau nen khong bi anh huong. */
static bool s_pkgs_pending = false;
static cloud_pkg_list_t s_pkgs_pending_list;

static bool customer_buying(void)
{
    return s_state == ST_SELECT || s_state == ST_CREATING_QR || s_state == ST_WAIT_PAYMENT;
}

static void apply_packages(const cloud_pkg_list_t *pl)
{
    device_config_t c = *cfg();
    c.package_count = pl->count;
    memset(c.packages, 0, sizeof(c.packages));
    for (int i = 0; i < pl->count; i++)
    {
        c.packages[i].price = pl->p[i].price;
        c.packages[i].duration_s = pl->p[i].duration_s;
        c.packages[i].park_s = pl->p[i].park_s;
        device_config_fill_package(&c.packages[i], i);
    }
    if (c.package_count == cfg()->package_count &&
        memcmp(c.packages, cfg()->packages, sizeof(c.packages)) == 0)
        return; /* nhan lai config retained giong het -> khong ghi NVS */
    if (device_config_set(&c) != ESP_OK)
    {
        ESP_LOGW(TAG, "danh sach goi bi tu choi");
        return;
    }
    ESP_LOGI(TAG, "ap dung danh sach %u goi:", c.package_count);
    for (int i = 0; i < c.package_count; i++)
        ESP_LOGI(TAG, "  %d) %lu d, %lus rua, %lus do xe", i + 1, (unsigned long)c.packages[i].price,
                 (unsigned long)c.packages[i].duration_s, (unsigned long)c.packages[i].park_s);
}

static void on_packages(const cloud_pkg_list_t *pl)
{
    if (customer_buying())
    {
        s_pkgs_pending_list = *pl; /* nhieu ban trong luc khach mua -> giu ban moi nhat */
        s_pkgs_pending = true;
        ESP_LOGI(TAG, "danh sach goi moi -> CHO, ap dung khi khach mua xong (%s)", k_state_name[s_state]);
        return;
    }
    s_pkgs_pending = false;
    apply_packages(pl);
}

static void apply_pending_packages(void)
{
    if (!s_pkgs_pending)
        return;
    s_pkgs_pending = false;
    apply_packages(&s_pkgs_pending_list);
}

/* So dien thoai hien tren man canh bao (topic config, truong "hotline"). Luu NVS; dang hien man canh bao thi ve lai */
static void on_hotline(const char *phone)
{
    if (strcmp(phone, cfg()->owner_phone) == 0)
        return; /* nhan lai config retained giong het -> khong ghi NVS */
    device_config_t c = *cfg();
    strlcpy(c.owner_phone, phone, sizeof(c.owner_phone));
    if (device_config_set(&c) != ESP_OK)
    {
        ESP_LOGW(TAG, "luu hotline loi");
        return;
    }
    ESP_LOGI(TAG, "hotline: \"%s\"", phone);
    if (s_state == ST_OUT_OF_SERVICE || (s_state == ST_IDLE && io_fault()))
        show_idle_screen();
    else if (s_state == ST_WASHING && io_fault())
        show_session_screen();
}

static void handle_event(const app_evt_t *e)
{
    switch (e->type)
    {
    case APP_EVT_INPUT:
        handle_input(e->data.input.in, e->data.input.active);
        break;

    case APP_EVT_UI_TOUCH:
        ui_wake();
        if (s_resume_ask)
            s_resume_since_ms = now_ms(); /* co nguoi -> tinh lai 30s */
        break;

    case APP_EVT_UI_MANUAL:
        ui_wake();
        if (s_state != ST_IDLE)
            break;
        if (io_fault())
        {
            // audio_play(AUDIO_OUT_OF_SERVICE);
            break;
        }
        if (!cloud_is_ready())
        {
            go_idle(); /* chuyen sang man mat ket noi */
            break;
        }
        go_select();
        break;

    case APP_EVT_UI_BACK:
        ui_wake();
        if (s_state == ST_SELECT)
            go_idle();
        break;

    case APP_EVT_UI_SELECT_PACKAGE:
        ui_wake();
        if (s_state != ST_SELECT)
            break;
        if (io_fault())
        {
            // audio_play(AUDIO_OUT_OF_SERVICE);
            break;
        }
        if (!cloud_is_ready())
        {
            go_idle(); /* chuyen sang tam ngung */
            break;
        }
        start_order((int)e->data.u32);
        break;

    case APP_EVT_UI_CANCEL:
        if (s_state == ST_CREATING_QR || s_state == ST_WAIT_PAYMENT)
            cancel_order("Đã hủy thành công", "", -1);
        break;

    case APP_EVT_UI_RESUME:
        ui_wake();
        resume_session();
        break;

    case APP_EVT_UI_END_SESSION:
        ui_wake();
        /* Tu man dang rua (nut "Ket thuc") hoac man hoi tiep tuc sau khi co dien lai (nut "Huy phien").
         * ws_finish_early nhan ca trang thai WS_ST_PAUSED. */
        if (s_state == ST_WASHING && ws_finish_early(&s_ws))
        {
            ESP_LOGI(TAG, "khach %s phien %s, con %lus chua dung", s_resume_ask ? "huy" : "ket thuc som",
                     s_ws.order_id, (unsigned long)(s_ws.session_left_ms / 1000));
            finish_session();
        }
        break;

    case APP_EVT_CLOUD_READY:
        ESP_LOGW(TAG, "server %s (trang thai %s)", e->data.b ? "DA KET NOI" : "MAT KET NOI", k_state_name[s_state]);
        if (e->data.b)
            s_boot_grace = false;
        if (s_state == ST_IDLE || s_state == ST_OUT_OF_SERVICE || (s_state == ST_SELECT && !e->data.b))
            go_idle();
        else if (!e->data.b && s_state == ST_CREATING_QR)
            /* Chua co QR ma mat mang: huy luon (QR ve sau khi co mang se bi huy tren server) */
            cancel_order("Lỗi kết nối", "Không tạo được mã thanh toán, vui lòng thử lại", AUDIO_CONNECTION_ERROR);
        else if (e->data.b && s_state == ST_WAIT_PAYMENT)
            cloud_query_order(s_order_id);
        /* WAIT_PAYMENT ma mat mang: GIU man QR -- khach co the da chuyen tien, co mang lai server
         * gui StartTransaction. Het han thi huy nhu binh thuong (CancelPayment luu, co mang gui).
         * Dang rua: phien chay tiep binh thuong, Meter ket thuc duoc luu va gui khi co mang. */
        break;

    case APP_EVT_CLOUD_QR:
    {
        cloud_qr_t *qr = e->data.qr;
        if (qr && s_state == ST_CREATING_QR && strcmp(qr->order_id, s_order_id) == 0)
        {
            uint32_t sec = qr->expire_in_s;
            if (sec == 0 || sec > cfg()->qr_timeout_s)
                sec = cfg()->qr_timeout_s;
            /* Tu day don mang ma phien server cap (transactionId): CancelPayment, StartTransaction,
             * Meter deu dung ma nay */
            if (qr->transaction_id)
                snprintf(s_order_id, sizeof(s_order_id), "%lu", (unsigned long)qr->transaction_id);
            set_state(ST_WAIT_PAYMENT);
            ui_show_qr(qr, &s_pkg);
            audio_play(AUDIO_SCAN_QR);
            set_deadline_s(sec);
        }
        else if (qr)
        {
            ESP_LOGW(TAG, "bo qua QR cua don %s", qr->order_id);
        }
        free(qr);
        break;
    }

    case APP_EVT_CLOUD_ORDER_RESULT:
        handle_order_result(e->data.result.order_id, e->data.result.status);
        break;

    case APP_EVT_CLOUD_APP_ORDER: /* che do thu */
        on_cloud_start(e->data.app_order.order_id, (uint32_t)e->data.app_order.minutes * 60000u,
                       e->data.app_order.package_id);
        break;

    case APP_EVT_CLOUD_START:
        on_cloud_start(e->data.start.order_id, e->data.start.duration_s * 1000u, 0);
        break;

    case APP_EVT_CLOUD_REMOTE_STOP:
        on_remote_stop(e->data.result.order_id);
        break;

    case APP_EVT_CLOUD_OP_MODE:
        on_op_mode(e);
        break;

    case APP_EVT_CLOUD_HOTLINE:
        on_hotline(e->data.phone);
        break;

    case APP_EVT_CLOUD_PACKAGES:
        if (e->data.packages)
        {
            on_packages(e->data.packages);
            free(e->data.packages);
        }
        break;

    case APP_EVT_CONFIG_UPDATED:
        s_config_pending = true;
        if (s_state == ST_IDLE || s_state == ST_OUT_OF_SERVICE)
            go_idle();
        break;
    }
}

static void handle_timeout(void)
{
    switch (s_state)
    {
    case ST_CREATING_QR:
        if (s_create_retries < 1)
        {
            s_create_retries++;
            ESP_LOGW(TAG, "chua nhan QR, gui lai yeu cau don %s", s_order_id);
            cloud_create_order(s_order_id, &s_pkg); /* cung order_id -> server khong tao trung */
            set_deadline_s(cfg()->create_order_timeout_s);
        }
        else
        {
            cancel_order("Lỗi kết nối", "Không tạo được mã thanh toán, vui lòng thử lại", AUDIO_CONNECTION_ERROR);
        }
        break;
    case ST_WAIT_PAYMENT:
        cancel_order("Mã thanh toán đã hết hạn, vui lòng thực hiện lại", "", AUDIO_QR_TIMEOUT);
        break;
    case ST_PAID:
        go_washing(false);
        break;
    case ST_SELECT: /* khong chon goi nao -> ve man hinh dau */
    case ST_FINISHED:
    case ST_NOTICE:
        go_idle();
        break;
    default:
        s_deadline_ms = 0;
        break;
    }
}

static void handle_tick(uint32_t dt_ms, bool second_boundary)
{
    if (s_boot_grace && now_ms() >= (int64_t)BOOT_CONNECT_GRACE_S * 1000)
    {
        s_boot_grace = false;
        ESP_LOGW(TAG, "sau %d giay khoi dong van chua ket noi duoc server -> hien man mat ket noi", BOOT_CONNECT_GRACE_S);
        if (s_state == ST_OUT_OF_SERVICE)
            show_idle_screen();
    }

    if (s_state == ST_WASHING)
    {
        /* ws_tick chi chay dong ho khi phien o RUNNING: dang tam dung / dang hoi tiep tuc thi
         * ca thoi gian su dung lan thoi gian do xe deu dung */
        uint32_t ev = ws_tick(&s_ws, dt_ms);

        // if (ev & WS_EVT_PUMP_EXHAUSTED) /* kich ban 2: bom dang chay vua het phan -> da tu tat */
        //     audio_play(AUDIO_KIND_EMPTY);
        // if (ev & WS_EVT_PARK_1MIN)
        //     audio_play(AUDIO_PARK_ONE_MIN);
        // if (ev & WS_EVT_WARN_1MIN)
        //     audio_play(AUDIO_ONE_MIN_LEFT);
        if (ev & WS_EVT_PUMP_ON)
            ui_update_washing(&s_ws);

        apply_pumps();

        if (ev & WS_EVT_FINISHED)
        {
            finish_session();
            return;
        }
        if (second_boundary)
            session_store_save(&s_ws);

        /* Man hoi tiep tuc: khong ai thao tac trong RESUME_ASK_TIMEOUT_S -> tu ket thuc phien */
        if (!s_resume_ask || RESUME_ASK_TIMEOUT_S == 0)
            s_resume_since_ms = 0;
        else if (!s_resume_since_ms)
            s_resume_since_ms = now_ms();
        else if (now_ms() - s_resume_since_ms >= (int64_t)RESUME_ASK_TIMEOUT_S * 1000 &&
                 ui_inactive_ms() >= (uint32_t)RESUME_ASK_TIMEOUT_S * 1000 && ws_finish_early(&s_ws))
        {
            ESP_LOGW(TAG, "khong ai tra loi man hoi tiep tuc sau %ds -> tu ket thuc phien %s, con %lus",
                     RESUME_ASK_TIMEOUT_S, s_ws.order_id, (unsigned long)(s_ws.session_left_ms / 1000));
            s_resume_since_ms = 0;
            finish_session();
            return;
        }

        ws_view_t v;
        ws_get_view(&s_ws, &v);
        if (v.left_s != s_shown_use_s || v.park_s != s_shown_park_s)
        {
            s_shown_use_s = v.left_s;
            s_shown_park_s = v.park_s;
            ui_update_washing(&s_ws);
        }

        /* Meter len server: ngay khi bom tu bat/tu tat, va moi cloud_meter_interval_s() giay (server cau hinh
         * qua "meterInterval", mac dinh CLOUD_METER_INTERVAL_S) khi chuong WASHING (ke ca luc tam dung) */
        bool changed = ev & (WS_EVT_PUMP_ON | WS_EVT_PUMP_EXHAUSTED);
        if (changed || now_ms() - s_progress_last_ms >= (int64_t)cloud_meter_interval_s() * 1000)
            report_progress();
    }

    if (second_boundary && s_state == ST_WAIT_PAYMENT && s_deadline_ms)
    {
        int64_t left = s_deadline_ms - now_ms();
        ui_update_qr_countdown(left > 0 ? (uint32_t)((left + 999) / 1000) : 0);
    }

    if (s_deadline_ms && now_ms() >= s_deadline_ms)
        handle_timeout();
}

/* ================= KHOI PHUC SAU MAT DIEN ================= */

static void restore_session(void)
{
    ws_session_t tmp;
    if (!session_store_load(&tmp))
    {
        go_idle();
        return;
    }

    if (ws_state(&tmp) == WS_ST_FINISHED)
    {
        ESP_LOGW(TAG, "phien %s da xong nhung chua bao cao -> gui lai", tmp.order_id);
        if (cloud_report_session(&tmp, true) == ESP_OK)
            session_store_clear();
        go_idle();
        return;
    }

    if (ws_state(&tmp) != WS_ST_RUNNING && ws_state(&tmp) != WS_ST_PAUSED)
    {
        session_store_clear();
        go_idle();
        return;
    }

    s_ws = tmp;
    ws_on_restore(&s_ws);
    /* Vua co dien lai (mat dien / bam nut dung khan cap = ngat nguon) giua phien: TAM DUNG phien
     * (tat bom, dung ca dong ho su dung lan do xe) -> go_washing hien man "Ban co muon tiep tuc
     * phien rua xe khong?", dong ho chi chay lai khi khach bam "Tiep tuc rua" */
    if (ws_state(&s_ws) == WS_ST_RUNNING)
        ws_pause(&s_ws, true);

    strlcpy(s_order_id, s_ws.order_id, sizeof(s_order_id));
    for (int i = 0; i < cfg()->package_count; i++)
        if (cfg()->packages[i].id == s_ws.package_id)
            s_pkg = cfg()->packages[i];

    ESP_LOGW(TAG, "KHOI PHUC phien %s: con %lus su dung, %lus do xe", s_ws.order_id,
             (unsigned long)(s_ws.session_left_ms / 1000), (unsigned long)(s_ws.park_left_ms / 1000));
    cloud_report_event("session_restored", s_ws.order_id);
    go_washing(true);
    report_progress(); /* de Meter sau Boot bao dung chuong dang WASHING */
}

/* ================= TASK ================= */

static void app_task(void *arg)
{
    /* Cho io_expander doc lan dau (toi da 1s) de biet loi bus truoc khi quyet dinh */
    for (int i = 0; i < 20 && !io_expander_is_healthy(); i++)
        vTaskDelay(pdMS_TO_TICKS(50));
    vTaskDelay(pdMS_TO_TICKS(IN_DEBOUNCE_MS + 20));
    s_fault_handled = io_fault();

    restore_session();

    int64_t last_tick = now_ms();
    uint32_t acc_ms = 0;

    for (;;)
    {
        app_evt_t e;
        int64_t t = now_ms();
        int64_t wait = APP_TICK_MS - (t - last_tick);
        if (wait < 0)
            wait = 0;

        if (xQueueReceive(s_queue, &e, pdMS_TO_TICKS(wait)) == pdTRUE)
            handle_event(&e);

        t = now_ms();
        if (t - last_tick >= APP_TICK_MS)
        {
            uint32_t dt = (uint32_t)(t - last_tick);
            last_tick = t;
            acc_ms += dt;
            bool second = acc_ms >= 1000;
            handle_tick(dt, second);
            if (second)
            {
                update_light(acc_ms);
                acc_ms = 0;
            }
        }
    }
}

/* ================= API ================= */

bool app_post_event(const app_evt_t *evt)
{
    if (!s_queue || !evt)
        return false;
    if (xQueueSend(s_queue, evt, 0) != pdTRUE)
    {
        ESP_LOGW(TAG, "hang doi day, bo su kien %d", evt->type);
        return false;
    }
    return true;
}

void app_fsm_on_input(io_input_t in, bool active, void *ctx)
{
    (void)ctx;
    app_evt_t e = {.type = APP_EVT_INPUT};
    e.data.input.in = in;
    e.data.input.active = active;
    app_post_event(&e);
}

esp_err_t app_fsm_init(void)
{
    if (s_queue)
        return ESP_OK;
    s_queue = xQueueCreate(APP_QUEUE_LEN, sizeof(app_evt_t));
    return s_queue ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t app_fsm_start(void)
{
    if (!s_queue)
        return ESP_ERR_INVALID_STATE;

    ws_clear(&s_ws);
    btn_lock_reset();
    done_load();
    light_ctrl_init(&s_light, &cfg()->light);
    audio_set_volume(cfg()->volume);

    if (xTaskCreatePinnedToCore(app_task, "app_fsm", APP_TASK_STACK, NULL, APP_TASK_PRIO, NULL, 0) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}