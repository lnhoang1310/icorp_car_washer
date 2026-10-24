#include "ui_screens.h"
#include "ui_theme.h"

/* Thong bao loi / huy: nen do, tieu de vang, noi dung trang (co the rong) */
void scr_notice_build(lv_obj_t *scr, const char *title, const char *message)
{
    ui_error_screen(scr, title, message, NULL);
}