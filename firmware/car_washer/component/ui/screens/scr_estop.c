#include "ui_screens.h"
#include "ui_theme.h"

/*
 * Dung khan cap: bam nut 1 lan -> dung, bam LAI 1 lan -> mo.
 * Loi thiet bi (mat lien lac chip doc nut): khong mo bang nut duoc, hien so dien thoai chu cua hang.
 */
void scr_estop_build(lv_obj_t *scr, bool io_fault, const char *phone)
{
    if (!io_fault)
        ui_error_screen(scr, "ĐANG DỪNG KHẨN CẤP", "Để mở lại, vui lòng ấn lại nút", "\"Dừng khẩn cấp\"");
    else if (phone && phone[0])
        ui_error_screen(scr, "LỖI THIẾT BỊ", "Vui lòng liên hệ số điện thoại:", phone);
    else
        ui_error_screen(scr, "LỖI THIẾT BỊ", "Vui lòng liên hệ quản lý", NULL);
}