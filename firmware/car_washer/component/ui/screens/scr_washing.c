#include "ui_screens.h"
#include "ui_theme.h"
#include <stdio.h>

/*
 * Man dang rua.
 *  - Dong ho lon : thoi gian su dung con lai cua goi (chi giam khi co bom chay,
 *                  bot giam nhanh gap 3 -- xem wash_session).
 *  - 3 the       : kich ban 1 (toc do): thoi gian DA TRU vao goi cua tung loai, DEM LEN;
 *                    tong 3 the + dong ho lon = thoi gian goi.
 *                  kich ban 2 (chia %): thoi gian CON LAI cua tung loai, DEM XUONG, het thi mo di;
 *                    dong ho lon = tong 3 the.
 *                  So lay tu ws_get_view() nen dong ho lon va 3 the luon khop nhau tung giay.
 *  - Dong duoi   : thoi gian do xe con lai (goi + 10 phut), dem nguoc theo thoi gian thuc.
 *  - Nut X do    : ket thuc som (co hop xac nhan).
 */

#define GAP       8
#define CARD_Y    100
#define CARD_H    100
#define CARD_W    ((UI_W - GAP * 4) / 3)
#define X_BTN     30

#define CONFIRM_TIMEOUT  10000 /* ms: khong ai bam thi tu dong dong hop xac nhan */
#define WARN_SHOW_MS     2500  /* ms: thoi gian hien bang canh bao "bam qua nhanh" */

typedef struct
{
    lv_obj_t *card;
    lv_obj_t *name;
    lv_obj_t *time;
    lv_obj_t *state;
} kind_ui_t;

static lv_obj_t *s_timer = NULL;
static lv_obj_t *s_park = NULL;
static kind_ui_t s_kind[WS_KIND_COUNT];

static lv_obj_t *s_confirm = NULL;   /* hop xac nhan ket thuc som, NULL = dang dong */
static uint32_t s_left_ms = 0;

/* Bang canh bao tam thoi, de len dong "thoi gian do xe" */
static lv_obj_t *s_warn = NULL;
static lv_obj_t *s_warn_text = NULL;
static lv_timer_t *s_warn_timer = NULL;

/* ================= Canh bao tam thoi ================= */

static void warn_hide(lv_timer_t *t)
{
    (void)t;
    s_warn_timer = NULL; /* timer 1 lan: LVGL tu xoa sau khi goi */
    if (s_warn)
        lv_obj_add_flag(s_warn, LV_OBJ_FLAG_HIDDEN);
}

/* Man hinh bi xoa (chuyen man) -> huy timer, bo con tro */
static void screen_deleted(lv_event_t *e)
{
    (void)e;
    if (s_warn_timer)
        lv_timer_delete(s_warn_timer);
    s_warn_timer = NULL;
    s_warn = NULL;
    s_warn_text = NULL;
    s_timer = NULL;
    s_park = NULL;
}

void scr_washing_warn(const char *text)
{
    if (!s_warn)
        return;
    lv_label_set_text(s_warn_text, text ? text : "");
    lv_obj_remove_flag(s_warn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_warn);
    if (s_confirm)
        lv_obj_move_foreground(s_confirm); /* hop xac nhan dang mo thi van nam tren cung */

    if (s_warn_timer)
        lv_timer_reset(s_warn_timer); /* bam nhanh lien tiep: dem lai tu dau */
    else
    {
        s_warn_timer = lv_timer_create(warn_hide, WARN_SHOW_MS, NULL);
        lv_timer_set_repeat_count(s_warn_timer, 1);
    }
}

/* ================= Hop xac nhan "Ket thuc" ================= */

static void confirm_deleted(lv_event_t *e)
{
    (void)e;
    s_confirm = NULL;
}

static void confirm_keep(lv_event_t *e)
{
    (void)e;
    if (s_confirm)
        lv_obj_delete_async(s_confirm);
}

static void confirm_end(lv_event_t *e)
{
    lv_obj_add_state(lv_event_get_target_obj(e), LV_STATE_DISABLED); /* chong bam 2 lan */
    ui_on_end_session();
}

static void open_confirm(lv_event_t *e)
{
    if (s_confirm)
        return;
    lv_obj_t *scr = lv_obj_get_screen(lv_event_get_target_obj(e));

    char t[12], msg[96];
    ui_fmt_mpss(t, sizeof(t), s_left_ms);
    snprintf(msg, sizeof(msg), "Thời gian còn lại là %s sẽ không được hoàn tiền", t);
    s_confirm = ui_confirm_dialog(scr, "Bạn muốn kết thúc phiên rửa xe?", msg,
                                  "Kết thúc", UI_C_DANGER, confirm_end,
                                  "Tiếp tục rửa", UI_C_PRIMARY, confirm_keep, CONFIRM_TIMEOUT);
    lv_obj_add_event_cb(s_confirm, confirm_deleted, LV_EVENT_DELETE, NULL);
}

/* ================= Man hinh ================= */

static void build_card(lv_obj_t *scr, ws_kind_t k)
{
    kind_ui_t *u = &s_kind[k];
    u->card = ui_panel(scr, UI_C_SURFACE, 8);
    lv_obj_set_size(u->card, CARD_W, CARD_H);
    lv_obj_set_pos(u->card, GAP + k * (CARD_W + GAP), CARD_Y);
    lv_obj_set_style_border_width(u->card, 1, 0);
    lv_obj_set_style_border_color(u->card, UI_C_BORDER, 0);

    u->name = ui_label(u->card, &font_vn_17, UI_C_TEXT, ui_kind_name(k));
    lv_obj_align(u->name, LV_ALIGN_TOP_MID, 0, 10);

    u->time = ui_label(u->card, &font_vn_22, UI_C_TEXT, "00:00");
    lv_obj_align(u->time, LV_ALIGN_TOP_MID, 0, 36);

    u->state = ui_label(u->card, &font_vn_14, UI_C_TEXT2, "");
    lv_obj_align(u->state, LV_ALIGN_TOP_MID, 0, 70);
}

void scr_washing_build(lv_obj_t *scr, const ws_session_t *s)
{
    ui_screen_base(scr, UI_C_BG);
    s_confirm = NULL;

    lv_obj_t *cap = ui_label(scr, &font_vn_17, UI_C_TEXT, "Thời gian sử dụng còn lại");
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_add_event_cb(cap, screen_deleted, LV_EVENT_DELETE, NULL);

    /* Nut X do goc tren phai: ket thuc som */
    lv_obj_t *x = ui_button(scr, X_BTN, X_BTN, UI_SYM_CLOSE, &font_vn_17, UI_C_DANGER, UI_C_TEXT, open_confirm);
    lv_obj_set_style_radius(x, 6, 0);
    lv_obj_set_ext_click_area(x, 12);
    lv_obj_set_pos(x, UI_W - GAP - X_BTN, 6);

    s_timer = ui_label(scr, &font_num_56, UI_C_TEXT, "00:00");
    lv_obj_align(s_timer, LV_ALIGN_TOP_MID, 0, 32);

    for (int k = 0; k < WS_KIND_COUNT; k++)
        build_card(scr, (ws_kind_t)k);

    s_park = ui_label(scr, &font_vn_17, UI_C_TEXT, "");
    lv_obj_align(s_park, LV_ALIGN_BOTTOM_MID, 0, -8);

    s_warn = ui_panel(scr, UI_C_WARNING, 8);
    lv_obj_set_size(s_warn, UI_W - 2 * GAP, 32);
    lv_obj_align(s_warn, LV_ALIGN_BOTTOM_MID, 0, -3);
    lv_obj_add_flag(s_warn, LV_OBJ_FLAG_HIDDEN);
    s_warn_text = ui_label(s_warn, &font_vn_17, UI_C_BG, "");
    lv_obj_center(s_warn_text);

    scr_washing_update(s);
}

void scr_washing_update(const ws_session_t *s)
{
    if (!s_timer || !s)
        return;

    ws_view_t v;
    ws_get_view(s, &v);

    char buf[56], t[12];
    ui_fmt_mmss_up(buf, sizeof(buf), v.left_s * 1000u);
    lv_label_set_text(s_timer, buf);
    lv_obj_set_style_text_color(s_timer, v.left_s <= 60 ? UI_C_DANGER : UI_C_TEXT, 0);
    s_left_ms = v.left_s * 1000u;

    /* Thoi gian do xe con lai, dem nguoc */
    ui_fmt_mmss_up(t, sizeof(t), v.park_s * 1000u);
    snprintf(buf, sizeof(buf), "Tổng thời gian đỗ xe: %s", t);
    lv_label_set_text(s_park, buf);
    lv_obj_set_style_text_color(s_park, s->park_left_ms <= 60000 ? UI_C_DANGER : UI_C_TEXT, 0);

    ws_kind_t act = (ws_kind_t)s->active;
    ws_kind_t pend = (ws_kind_t)s->pending;
    for (int k = 0; k < WS_KIND_COUNT; k++)
    {
        kind_ui_t *u = &s_kind[k];
        bool running = (act == (ws_kind_t)k);
        bool waiting = (pend == (ws_kind_t)k);
        bool empty = v.exhausted[k]; /* chi co o kich ban 2 */

        ui_fmt_mmss_up(buf, sizeof(buf), v.card_s[k] * 1000u);
        lv_label_set_text(u->time, buf);
        lv_label_set_text(u->state, empty ? "Đã hết" : running ? "Đang sử dụng" : waiting ? "Đang chuyển" : "Đang chờ");
        /* The dang sang (dang chay): chu doi sang mau nen cho noi tren nen xanh */
        lv_obj_set_style_text_color(u->state, running ? UI_C_BG : UI_C_TEXT2, 0);
        lv_obj_set_style_text_color(u->time, running ? UI_C_BG : empty ? UI_C_TEXT2 : UI_C_TEXT, 0);
        lv_obj_set_style_text_color(u->name, running ? UI_C_BG : empty ? UI_C_TEXT2 : UI_C_TEXT, 0);

        lv_obj_set_style_bg_color(u->card, running ? UI_C_PRIMARY : UI_C_SURFACE, 0);
        lv_obj_set_style_border_color(u->card, (running || waiting) ? UI_C_PRIMARY : UI_C_BORDER, 0);
        lv_obj_set_style_border_width(u->card, waiting ? 2 : 1, 0);
    }
}