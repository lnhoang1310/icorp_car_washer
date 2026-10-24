#include "ui_screens.h"
#include "ui_theme.h"

void scr_qr_loading_build(lv_obj_t *scr, const devcfg_package_t *pkg)
{
    (void)pkg;
    ui_screen_base(scr, UI_C_BG);

    lv_obj_t *sp = lv_spinner_create(scr);
    lv_spinner_set_anim_params(sp, 1000, 270);
    lv_obj_set_size(sp, 56, 56);
    lv_obj_set_style_arc_width(sp, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(sp, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(sp, UI_C_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_arc_color(sp, UI_C_PRIMARY, LV_PART_INDICATOR);
    lv_obj_align(sp, LV_ALIGN_CENTER, 0, -28);

    lv_obj_t *t = ui_label(scr, &font_vn_22, UI_C_TEXT, "Vui lòng chờ");
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 36);
}