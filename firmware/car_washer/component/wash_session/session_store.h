#ifndef __SESSION_STORE_H__
#define __SESSION_STORE_H__

/*
 * Luu phien rua xuong FRAM de khoi phuc khi mat dien.
 * Dung 2 slot A/B ghi xen ke + so thu tu + CRC32: neu mat dien dung luc dang ghi
 * thi slot kia van con ban hop le gan nhat.
 *
 * Vung FRAM: FRAM_MAP_SESSION_BASE (xem fram_map.h)
 */

#include "esp_err.h"
#include "wash_session.h"
#include "fram_map.h"
#include <stdbool.h>

#define SESSION_STORE_FRAM_BASE  FRAM_MAP_SESSION_BASE
#define SESSION_STORE_SLOT_SIZE  FRAM_MAP_SESSION_SLOT

/* Yeu cau fram_init() da goi truoc */
esp_err_t session_store_save(const ws_session_t *s);

/* true neu co phien hop le; out duoc dien ban moi nhat */
bool session_store_load(ws_session_t *out);

/* Xoa (goi sau khi phien ket thuc VA da gui bao cao len server thanh cong) */
esp_err_t session_store_clear(void);

#endif