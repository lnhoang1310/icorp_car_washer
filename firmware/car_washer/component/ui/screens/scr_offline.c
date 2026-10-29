#include "ui_screens.h"
#include "ui_theme.h"

/* Mat ket noi server/internet: hien so dien thoai chu cua hang (lay tu server, luu trong cau hinh) */
void scr_offline_build(lv_obj_t *scr, const char *phone)
{
    if (phone && phone[0])
        ui_error_screen(scr, "Mất kết nối internet", "Vui lòng liên hệ số điện thoại:", phone);
    else
        ui_error_screen(scr, "Mất kết nối internet", "Vui lòng liên hệ quản lý", NULL);
}

/* Vua khoi dong, chua ket noi server lan nao: man cho (khong phai canh bao) */
void scr_connecting_build(lv_obj_t *scr)
{
    ui_screen_base(scr, UI_C_BG);

    lv_obj_t *sp = lv_spinner_create(scr);
    lv_spinner_set_anim_params(sp, 1000, 270);
    lv_obj_set_size(sp, 64, 64);
    lv_obj_set_style_arc_width(sp, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(sp, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(sp, UI_C_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_arc_color(sp, UI_C_ACCENT, LV_PART_INDICATOR);
    lv_obj_align(sp, LV_ALIGN_CENTER, 0, -36);

    lv_obj_t *ic = ui_label(scr, &font_vn_22, UI_C_ACCENT, UI_SYM_WIFI);
    lv_obj_align_to(ic, sp, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *t = ui_label(scr, &font_vn_22, UI_C_TEXT, "Đang kết nối…");
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t *m = ui_label(scr, &font_vn_17, UI_C_TEXT2, "Vui lòng chờ trong giây lát");
    lv_obj_set_width(m, UI_W - 32);
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(m, LV_ALIGN_CENTER, 0, 62);
}