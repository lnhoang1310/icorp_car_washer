#include "ui_screens.h"
#include "ui_theme.h"

/*
 * Loi thiet bi: mat lien lac bus I/O (chip doc nut / relay) -> khong phuc vu duoc,
 * hien so dien thoai chu cua hang. Tu het khi bus hoi phuc.
 * (Dung khan cap la ngat nguon bang phan cung, khong co man hinh rieng.)
 */
void scr_fault_build(lv_obj_t *scr, const char *phone)
{
    if (phone && phone[0])
        ui_error_screen(scr, "LỖI THIẾT BỊ", "Vui lòng liên hệ số điện thoại:", phone);
    else
        ui_error_screen(scr, "LỖI THIẾT BỊ", "Vui lòng liên hệ quản lý", NULL);
}