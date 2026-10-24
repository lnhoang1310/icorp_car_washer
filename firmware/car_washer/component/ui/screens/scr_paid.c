#include "ui_screens.h"
#include "ui_theme.h"

void scr_paid_build(lv_obj_t *scr, const devcfg_package_t *pkg)
{
    (void)pkg;
    ui_screen_base(scr, UI_C_BG);

    lv_obj_t *circle = ui_panel(scr, UI_C_SUCCESS, LV_RADIUS_CIRCLE);
    lv_obj_set_size(circle, 76, 76);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, -52);
    lv_obj_t *ic = ui_label(circle, &font_num_56, UI_C_TEXT, UI_SYM_OK);
    lv_obj_center(ic);

    lv_obj_t *t = ui_label(scr, &font_vn_22, UI_C_TEXT, "Thanh toán thành công.");
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 22);

    lv_obj_t *s = ui_label(scr, &font_vn_22, UI_C_TEXT, "Bắt đầu thao tác rửa xe");
    lv_obj_align(s, LV_ALIGN_CENTER, 0, 60);
}