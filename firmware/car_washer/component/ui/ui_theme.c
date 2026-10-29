#include "ui_theme.h"
#include <stdio.h>
#include <stdbool.h>

void ui_theme_init(void)
{
    lv_display_t *disp = lv_display_get_default();
    lv_theme_t *th = lv_theme_default_init(disp, UI_C_ACCENT, UI_C_WARNING, true, &font_vn_17);
    lv_display_set_theme(disp, th);
}

void ui_screen_base(lv_obj_t *scr, lv_color_t bg)
{
    lv_obj_set_style_bg_color(scr, bg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_text_color(scr, UI_C_TEXT, 0);
    lv_obj_set_style_text_font(scr, &font_vn_17, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text ? text : "");
    return l;
}

lv_obj_t *ui_panel(lv_obj_t *parent, lv_color_t bg, int radius)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_style_bg_color(p, bg, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p, radius, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return p;
}

lv_obj_t *ui_header(lv_obj_t *scr, const char *title)
{
    lv_obj_t *h = ui_panel(scr, UI_C_SURFACE, 0);
    lv_obj_set_size(h, UI_W, UI_HEADER_H);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *bar = ui_panel(h, UI_C_ACCENT, 0);
    lv_obj_set_size(bar, 4, UI_HEADER_H);
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *t = ui_label(h, &font_vn_22, UI_C_TEXT, title);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 14, 0);
    return h;
}

void ui_fmt_mmss(char *buf, size_t len, uint32_t ms)
{
    uint32_t s = (ms + 999) / 1000;
    snprintf(buf, len, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

void ui_fmt_mmss_up(char *buf, size_t len, uint32_t ms)
{
    uint32_t s = ms / 1000;
    snprintf(buf, len, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

void ui_fmt_mpss(char *buf, size_t len, uint32_t ms)
{
    uint32_t s = (ms + 999) / 1000;
    snprintf(buf, len, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

void ui_fmt_money(char *buf, size_t len, uint32_t vnd)
{
    char digits[16];
    int n = snprintf(digits, sizeof(digits), "%lu", (unsigned long)vnd);
    size_t o = 0;
    for (int i = 0; i < n && o + 2 < len; i++)
    {
        if (i > 0 && (n - i) % 3 == 0)
            buf[o++] = '.';
        buf[o++] = digits[i];
    }
    /* "đ" = 0xC4 0x91 trong UTF-8 */
    if (o + 3 <= len)
    {
        buf[o++] = (char)0xC4;
        buf[o++] = (char)0x91;
    }
    buf[o < len ? o : len - 1] = 0;
}

lv_color_t ui_kind_color(ws_kind_t k)
{
    switch (k)
    {
    case WS_FOAM:  return UI_C_FOAM;
    case WS_WATER: return UI_C_WATER;
    case WS_DRY:   return UI_C_DRY;
    default:       return UI_C_TEXT2;
    }
}

const char *ui_kind_name(ws_kind_t k)
{
    switch (k)
    {
    case WS_FOAM:  return "Bọt tuyết";
    case WS_WATER: return "Nước";
    case WS_DRY:   return "Xì khô";
    default:       return "";
    }
}

/* ================= Nut bam ================= */

lv_obj_t *ui_button(lv_obj_t *parent, int w, int h, const char *text, const lv_font_t *font,
                    lv_color_t bg, lv_color_t fg, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_color(b, lv_color_mix(bg, lv_color_black(), 190), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, UI_C_SURFACE2, LV_STATE_DISABLED);
    lv_obj_set_ext_click_area(b, 6);
    if (cb)
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = ui_label(b, font, fg, text);
    lv_obj_center(l);
    return b;
}

/* ================= Man loi nen do ================= */

void ui_error_screen(lv_obj_t *scr, const char *title, const char *message, const char *strong)
{
    ui_screen_base(scr, UI_C_ERROR_BG);

    bool has_msg = message && message[0];
    bool has_strong = strong && strong[0];

    /* Xep doc, canh giua ca khoi theo chieu cao man hinh */
    lv_obj_t *col = lv_obj_create(scr);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, UI_W - 24, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 4, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(col);

    lv_obj_t *ic = ui_label(col, &font_num_56, UI_C_WARNING, UI_SYM_WARNING);
    lv_obj_set_style_pad_bottom(ic, 6, 0);

    lv_obj_t *t = ui_label(col, &font_vn_22, UI_C_WARNING, title);
    lv_obj_set_width(t, UI_W - 24);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);

    if (has_msg)
    {
        lv_obj_t *m = ui_label(col, &font_vn_17, UI_C_TEXT, message);
        lv_obj_set_width(m, UI_W - 32);
        lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    }
    if (has_strong)
    {
        lv_obj_t *b = ui_label(col, &font_vn_22, UI_C_TEXT, strong);
        lv_obj_set_width(b, UI_W - 32);
        lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
    }
}

/* ================= Hop xac nhan ================= */

static void dialog_deleted(lv_event_t *e)
{
    lv_timer_t *t = lv_event_get_user_data(e);
    if (t)
        lv_timer_delete(t);
}

static void dialog_timeout(lv_timer_t *t)
{
    lv_obj_t *ov = lv_timer_get_user_data(t);
    /* Timer nay bi xoa trong dialog_deleted() khi lop phu bi xoa */
    lv_obj_delete(ov);
}

static void dialog_close_clicked(lv_event_t *e)
{
    lv_obj_t *ov = lv_event_get_user_data(e);
    lv_obj_delete_async(ov); /* dang o trong su kien cua nut con -> xoa sau */
}

lv_obj_t *ui_confirm_dialog(lv_obj_t *scr, const char *title, const char *message,
                            const char *left_text, lv_color_t left_bg, lv_event_cb_t left_cb,
                            const char *right_text, lv_color_t right_bg, lv_event_cb_t right_cb,
                            uint32_t timeout_ms)
{
    /* Lop phu toi, chan cham xuong cac nut phia duoi */
    lv_obj_t *ov = lv_obj_create(scr);
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, UI_W, UI_H);
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_style_bg_color(ov, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_70, 0);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);

    lv_timer_t *tm = NULL;
    if (timeout_ms)
        tm = lv_timer_create(dialog_timeout, timeout_ms, ov);
    lv_obj_add_event_cb(ov, dialog_deleted, LV_EVENT_DELETE, tm);

    lv_obj_t *box = ui_panel(ov, UI_C_BG, 10);
    lv_obj_set_size(box, 300, 212);
    lv_obj_center(box);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_border_color(box, UI_C_BORDER, 0);

    /* Nut X goc phai */
    lv_obj_t *x = ui_button(box, 26, 26, UI_SYM_CLOSE, &font_vn_17, UI_C_TEXT, UI_C_BG, NULL);
    lv_obj_set_style_radius(x, 5, 0);
    lv_obj_set_ext_click_area(x, 10);
    lv_obj_align(x, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_add_event_cb(x, dialog_close_clicked, LV_EVENT_CLICKED, ov);

    lv_obj_t *t = ui_label(box, &font_vn_22, UI_C_TEXT, title);
    lv_obj_set_width(t, 230);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t *m = ui_label(box, &font_vn_14, UI_C_TEXT, message);
    lv_obj_set_width(m, 272);
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(m, t, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    lv_obj_t *l = ui_button(box, 132, 42, left_text, &font_vn_17, left_bg, UI_C_TEXT, left_cb);
    lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_t *r = ui_button(box, 132, 42, right_text, &font_vn_17, right_bg, UI_C_TEXT, right_cb);
    lv_obj_align(r, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    return ov;
}