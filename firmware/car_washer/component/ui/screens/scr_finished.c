#include "ui_screens.h"
#include "ui_theme.h"
#include <stdio.h>

void scr_finished_build(lv_obj_t *scr, const ws_session_t *s)
{
    ui_screen_base(scr, UI_C_BG);

    lv_obj_t *t = ui_label(scr, &font_vn_22, UI_C_TEXT, "Phiên rửa xe đã kết thúc");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *m = ui_label(scr, &font_vn_14, UI_C_TEXT, "Cảm ơn quý khách đã sử dụng dịch vụ");
    lv_obj_align(m, LV_ALIGN_TOP_MID, 0, 52);

    if (!s)
        return;

    lv_obj_t *cap = ui_label(scr, &font_vn_17, UI_C_TEXT, "Thời gian đã sử dụng");
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 92);

    /* Thoi gian da su dung cua tung loai (ca 2 kich ban), khop voi man dang rua */
    ws_view_t v;
    ws_get_view(s, &v);
    const int w = 96, gap = 8;
    int x0 = (UI_W - (3 * w + 2 * gap)) / 2;
    for (int k = 0; k < WS_KIND_COUNT; k++)
    {
        lv_obj_t *p = ui_panel(scr, UI_C_SURFACE, 8);
        lv_obj_set_size(p, w, 84);
        lv_obj_set_pos(p, x0 + k * (w + gap), 124);
        lv_obj_set_style_border_width(p, 2, 0);
        lv_obj_set_style_border_color(p, ui_kind_color((ws_kind_t)k), 0);

        lv_obj_t *n = ui_label(p, &font_vn_14, UI_C_TEXT, ui_kind_name((ws_kind_t)k));
        lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 12);

        char buf[12];
        ui_fmt_mmss_up(buf, sizeof(buf), v.used_s[k] * 1000u);
        lv_obj_t *v = ui_label(p, &font_vn_22, UI_C_TEXT, buf);
        lv_obj_align(v, LV_ALIGN_TOP_MID, 0, 38);
    }
}