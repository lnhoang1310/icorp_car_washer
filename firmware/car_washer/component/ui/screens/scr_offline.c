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