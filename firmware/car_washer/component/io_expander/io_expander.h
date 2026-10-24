#ifndef __IO_EXPANDER_H__
#define __IO_EXPANDER_H__

/*
 * 2x PCF8574AT:
 *   - 0x39 INPUT : nut bot / nuoc / kho, cam bien chuyen dong (co chan INT)
 *   - 0x38 OUTPUT: relay bot / nuoc / kho / den
 *
 * An toan duoc xu ly NGAY trong module nay, khong phu thuoc lop tren:
 *   - Init: ghi 0xFF (tat het relay) truoc moi thao tac khac.
 *   - Dung khan cap = ngat nguon bang phan cung, module nay khong doc nut dung khan.
 *   - Doc INPUT loi lien tiep -> tat 3 relay bom, bao IO_IN_BUS_FAULT.
 *   - Chi cho phep toi da 1 relay bom bat tai 1 thoi diem.
 */

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    IO_IN_BTN_FOAM = 0,
    IO_IN_BTN_WATER,
    IO_IN_BTN_DRY,
    IO_IN_MOTION,
    IO_IN_COUNT,
    IO_IN_BUS_FAULT = 0x80, /* gia input: active=true khi mat lien lac PCF input */
} io_input_t;

typedef enum
{
    IO_RELAY_FOAM = 0,
    IO_RELAY_WATER,
    IO_RELAY_DRY,
    IO_RELAY_LIGHT,
    IO_RELAY_COUNT,
} io_relay_t;

/* Goi tu task io (khong phai ISR). Nen chi day event vao queue roi return nhanh. */
typedef void (*io_input_cb_t)(io_input_t in, bool active, void *ctx);

esp_err_t io_expander_init(io_input_cb_t cb, void *ctx);

bool io_input_is_active(io_input_t in);
bool io_expander_is_healthy(void);

/* Bat 1 bom duy nhat (cac bom khac tu tat). pump = IO_RELAY_FOAM/WATER/DRY, hoac -1 de tat het bom.
 * Tra ESP_ERR_INVALID_STATE neu dang loi bus. */
esp_err_t io_pump_select(int pump);

esp_err_t io_light_set(bool on);

/* Tat moi relay (ke ca den) */
esp_err_t io_relay_all_off(void);

/* Ghi lai trang thai relay dang luu ra PCF. Goi dinh ky: neu chip output bi reset (nhieu nguon, cam lai day)
 * thi relay se tu ve dung trang thai thay vi tat cho toi lan doi trang thai sau. */
esp_err_t io_relay_refresh(void);

/* Mask logic cac relay dang bat (bit = io_relay_t), khong phai muc chan */
uint8_t io_relay_get_mask(void);

/* Ghi truc tiep trang thai nhieu relay (bit = io_relay_t). CHI dung cho kiem tra / bao tri:
 * bo qua quy tac "chi 1 bom tai 1 thoi diem" va dead-time. Van ton trong an toan:
 * mat lien lac PCF input -> bit bom bi bo, chi ghi relay den,
 * va tra ve ESP_ERR_INVALID_STATE de nguoi goi biet. */
esp_err_t io_relay_write_raw(uint8_t mask);

#endif