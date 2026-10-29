#include "ui_manager.h"
#include "app_config.h"
#include "ui_theme.h"
#include "ui_screens.h"
#include "display_driver.h"
#include "app_fsm.h"

#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "ui";

#define UI_LOCK_TIMEOUT_MS 1000
#define UI_DIM_AFTER_MS    60000 /* khong ai cham / khong co nguoi -> giam sang */
#define UI_BL_FULL         100
/* Co cam bien chuyen dong thi giam sau (khach den gan se tu sang lai),
 * khong co thi giu 50% de khach van nhin thay man hinh tu xa */
#if IN_USE_MOTION_SENSOR
#define UI_BL_DIM          15
#else
#define UI_BL_DIM          50
#endif

typedef enum
{
    SCR_NONE = 0,
    SCR_HOME,
    SCR_PACKAGES,
    SCR_QR_LOADING,
    SCR_QR,
    SCR_PAID,
    SCR_WASHING,
    SCR_FAULT,
    SCR_RESUME,
    SCR_FINISHED,
    SCR_NOTICE,
    SCR_OFFLINE,
    SCR_CONNECTING,
} ui_scr_t;

static lv_obj_t *s_scr = NULL;
static ui_scr_t s_cur = SCR_NONE;
static bool s_ready = false;
static uint8_t s_bl = 0;

/* ================= Tien ich ================= */

static bool lock(void)
{
    if (!s_ready)
        return false;
    if (!lvgl_port_lock(UI_LOCK_TIMEOUT_MS))
    {
        ESP_LOGW(TAG, "khong lay duoc khoa LVGL");
        return false;
    }
    return true;
}

static void unlock(void) { lvgl_port_unlock(); }

static void set_bl(uint8_t pct)
{
    if (pct != s_bl)
    {
        s_bl = pct;
        // display_set_backlight(pct);
    }
}

/* Xoa man hinh cu truoc khi ve man moi */
static void begin(ui_scr_t id)
{
    lv_obj_clean(s_scr);
    s_cur = id;
}

/* Giam sang khi o man chon goi / tam ngung lau khong co ai; cac man khac luon sang */
static void dim_timer_cb(lv_timer_t *t)
{
    (void)t;
    bool idle_screen = (s_cur == SCR_HOME || s_cur == SCR_OFFLINE);
    uint32_t inactive = lv_display_get_inactive_time(NULL);
    set_bl((idle_screen && inactive > UI_DIM_AFTER_MS) ? UI_BL_DIM : UI_BL_FULL);
}

/* ================= Thao tac nguoi dung (goi tu task LVGL) ================= */

void ui_on_manual(void)
{
    app_evt_t e = {.type = APP_EVT_UI_MANUAL};
    app_post_event(&e);
}

void ui_on_back(void)
{
    app_evt_t e = {.type = APP_EVT_UI_BACK};
    app_post_event(&e);
}

void ui_on_package_selected(int index)
{
    app_evt_t e = {.type = APP_EVT_UI_SELECT_PACKAGE, .data.u32 = (uint32_t)index};
    app_post_event(&e);
}

void ui_on_cancel(void)
{
    app_evt_t e = {.type = APP_EVT_UI_CANCEL};
    app_post_event(&e);
}

void ui_on_end_session(void)
{
    app_evt_t e = {.type = APP_EVT_UI_END_SESSION};
    app_post_event(&e);
}

void ui_on_resume(void)
{
    app_evt_t e = {.type = APP_EVT_UI_RESUME};
    app_post_event(&e);
}

/* ================= API ================= */

esp_err_t ui_init(void)
{
    esp_err_t err = display_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "display_init loi: %s -- chay khong man hinh", esp_err_to_name(err));
        return err;
    }

    if (!lvgl_port_lock(0))
        return ESP_FAIL;
    ui_theme_init();
    s_scr = lv_screen_active();
    ui_screen_base(s_scr, UI_C_BG);
    lv_timer_create(dim_timer_cb, 1000, NULL);
    lvgl_port_unlock();

    s_ready = true;
    set_bl(UI_BL_FULL);
    return ESP_OK;
}

void ui_show_home(const char *app_qr)
{
    if (!lock())
        return;
    begin(SCR_HOME);
    scr_home_build(s_scr, app_qr);
    unlock();
}

void ui_show_packages(const devcfg_package_t *pkgs, int count)
{
    if (!lock())
        return;
    begin(SCR_PACKAGES);
    scr_packages_build(s_scr, pkgs, count);
    unlock();
}

void ui_show_qr_loading(const devcfg_package_t *pkg)
{
    if (!lock())
        return;
    begin(SCR_QR_LOADING);
    scr_qr_loading_build(s_scr, pkg);
    unlock();
}

void ui_show_qr(const cloud_qr_t *qr, const devcfg_package_t *pkg)
{
    if (!qr || !lock())
        return;
    begin(SCR_QR);
    scr_qr_build(s_scr, qr, pkg);
    lv_display_trigger_activity(NULL);
    unlock();
}

void ui_update_qr_countdown(uint32_t sec_left)
{
    if (!lock())
        return;
    if (s_cur == SCR_QR)
        scr_qr_update(sec_left);
    unlock();
}

void ui_show_paid(const devcfg_package_t *pkg)
{
    if (!lock())
        return;
    begin(SCR_PAID);
    scr_paid_build(s_scr, pkg);
    unlock();
}

void ui_show_washing(const ws_session_t *s)
{
    if (!lock())
        return;
    begin(SCR_WASHING);
    scr_washing_build(s_scr, s);
    unlock();
}

void ui_update_washing(const ws_session_t *s)
{
    if (!lock())
        return;
    if (s_cur == SCR_WASHING)
        scr_washing_update(s);
    unlock();
}

void ui_warn_too_fast(void)
{
    if (!lock())
        return;
    if (s_cur == SCR_WASHING)
        scr_washing_warn(UI_SYM_WARNING " Thao tác quá nhanh, bấm lại");
    unlock();
}

void ui_show_fault(const char *phone)
{
    if (!lock())
        return;
    begin(SCR_FAULT);
    scr_fault_build(s_scr, phone);
    lv_display_trigger_activity(NULL);
    unlock();
}

void ui_show_resume_ask(const ws_session_t *s)
{
    if (!lock())
        return;
    begin(SCR_RESUME);
    scr_resume_build(s_scr, s);
    lv_display_trigger_activity(NULL);
    unlock();
}

void ui_show_finished(const ws_session_t *s)
{
    if (!lock())
        return;
    begin(SCR_FINISHED);
    scr_finished_build(s_scr, s);
    unlock();
}

void ui_show_notice(const char *title, const char *message)
{
    if (!lock())
        return;
    begin(SCR_NOTICE);
    scr_notice_build(s_scr, title, message);
    unlock();
}

void ui_show_offline(const char *phone)
{
    if (!lock())
        return;
    begin(SCR_OFFLINE);
    scr_offline_build(s_scr, phone);
    unlock();
}

void ui_show_connecting(void)
{
    if (!lock())
        return;
    if (s_cur != SCR_CONNECTING) /* ve lai se lam giat vong quay */
    {
        begin(SCR_CONNECTING);
        scr_connecting_build(s_scr);
    }
    unlock();
}

uint32_t ui_inactive_ms(void)
{
    if (!s_ready)
        return UINT32_MAX; /* khong co man hinh: chi tinh theo nut bam */
    if (!lock())
        return 0;
    uint32_t t = lv_display_get_inactive_time(NULL);
    unlock();
    return t;
}

void ui_wake(void)
{
    if (!lock())
        return;
    lv_display_trigger_activity(NULL);
    unlock();
    set_bl(UI_BL_FULL);
}