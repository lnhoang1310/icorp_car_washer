#ifndef __PERIPHERAL_TEST_H__
#define __PERIPHERAL_TEST_H__

#include "esp_err.h"
#include "device_config.h"
#include <stdbool.h>

void peripheral_test_run(void);

/* Goi sau ui_init(): hieu chinh cam ung (PT_TOUCH_CALIB trong peripheral_test.c) */
void peripheral_test_after_ui(void);

/* ===== Thu thao tac khi chua co server (bat/tat bang PT_DEMO_PAYMENT trong peripheral_test.c) =====
 * Bat len thi cloud_manager chuyen sang gia lap: tram nhan khach, hien ma QR thu,
 * tu bao "da thanh toan" sau PT_DEMO_PAY_AFTER_S giay. KHONG co tien that. */
bool pt_demo_enabled(void);
esp_err_t pt_demo_create_order(const char *order_id, const devcfg_package_t *pkg);
void pt_demo_cancel_order(const char *order_id);
void pt_demo_query_order(const char *order_id);

#endif