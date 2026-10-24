#include "ui_screens.h"
#include "ui_theme.h"
#include <stdio.h>
#include <string.h>

/* Man 320x240. Thanh tieu de thap de ma QR con du lon cho app ngan hang doc.
 * LVGL ve QR theo he so nguyen (moi module = N diem anh): QR_SIZE 196 cho 4 px/module
 * voi ma toi 49 module (VietQR thuong 45-49), 3 px/module voi ma dai hon. */
#define HEAD_H   28
#define QR_BOX   208
#define QR_SIZE  196
#define MARGIN   6

static lv_obj_t *s_countdown = NULL;

static void cancel_clicked(lv_event_t *e)
{
    (void)e;
    ui_on_cancel();
}

static void qr_deleted(lv_event_t *e)
{
    (void)e;
    s_countdown = NULL;
}

void scr_qr_build(lv_obj_t *scr, const cloud_qr_t *qr, const devcfg_package_t *pkg)
{
    (void)pkg;
    ui_screen_base(scr, UI_C_BG);

    lv_obj_t *h = ui_panel(scr, UI_C_SURFACE, 0);
    lv_obj_set_size(h, UI_W, HEAD_H);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t *ht = ui_label(h, &font_vn_17, UI_C_TEXT, "Quét mã QR để thanh toán");
    lv_obj_center(ht);

    /* ---- Ma QR tren nen trang (can vien trang de app ngan hang doc duoc) ---- */
    int top = HEAD_H + (UI_H - HEAD_H - QR_BOX) / 2;
    lv_obj_t *box = ui_panel(scr, lv_color_white(), 8);
    lv_obj_set_size(box, QR_BOX, QR_BOX);
    lv_obj_set_pos(box, MARGIN, top);

    lv_obj_t *code = lv_qrcode_create(box);
    lv_qrcode_set_size(code, QR_SIZE);
    lv_qrcode_set_dark_color(code, lv_color_black());
    lv_qrcode_set_light_color(code, lv_color_white());
    lv_qrcode_update(code, qr->qr, strlen(qr->qr));
    lv_obj_center(code);

    /* ---- Cot ben phai: thoi gian cho + nut huy ---- */
    int x = MARGIN + QR_BOX + MARGIN;
    int w = UI_W - x - MARGIN;

    lv_obj_t *cap = ui_label(scr, &font_vn_14, UI_C_TEXT, "Thời gian chờ thanh toán");
    lv_obj_set_width(cap, w);
    lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(cap, x, top + 34);

    s_countdown = ui_label(scr, &font_vn_22, UI_C_WARNING, "");
    lv_obj_set_width(s_countdown, w);
    lv_obj_set_style_text_align(s_countdown, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_countdown, x, top + 96);
    lv_obj_add_event_cb(s_countdown, qr_deleted, LV_EVENT_DELETE, NULL);
    scr_qr_update(qr->expire_in_s);

    lv_obj_t *btn = ui_button(scr, w, 44, "Hủy", &font_vn_17, UI_C_DANGER, UI_C_TEXT, cancel_clicked);
    lv_obj_set_pos(btn, x, top + QR_BOX - 44 - 8);
}

void scr_qr_update(uint32_t sec_left)
{
    if (!s_countdown)
        return;
    char mmss[8];
    ui_fmt_mmss(mmss, sizeof(mmss), sec_left * 1000u);
    lv_label_set_text(s_countdown, mmss);
    lv_obj_set_style_text_color(s_countdown, sec_left <= 30 ? UI_C_DANGER : UI_C_WARNING, 0);
}