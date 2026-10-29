#ifndef __CLOUD_MANAGER_H__
#define __CLOUD_MANAGER_H__

/*
 * Giao dien giua ung dung va server (MQTT, tai lieu "IC CAR WASH - 001. Nghiep vu va giao tiep thiet bi").
 * app_fsm CHI goi cac ham duoi day, khong biet ben duoi la MQTT that hay gia lap (PT_DEMO_PAYMENT).
 *
 * Topic (mac = MAC goc viet thuong, bo dau ':'):
 *   carwash/v1/{mac}/up                     tram -> server : Boot, InitTransaction, CancelPayment, Meter
 *   carwash/v1/{mac}/down                   server -> tram : ShowPaymentCode, StartTransaction, RemoteStopTransaction
 *   carwash/v1/{mac}/status                 "ONLINE" / "OFFLINE" (Last Will), retain
 *   carwash/v1/{mac}/config                 cau hinh tu (retain): mode + danh sach goi dich vu
 * Moi goi gan voi chuong rua co payload.bayMac (MAC cua chuong) + payload.bayNumber (so hien thi, khoang tich hop = 0).
 *
 * Chieu nguoc lai (server -> tram) di qua su kien app_post_event():
 *   APP_EVT_CLOUD_READY, APP_EVT_CLOUD_QR, APP_EVT_CLOUD_START, APP_EVT_CLOUD_REMOTE_STOP,
 *   APP_EVT_CLOUD_OP_MODE, APP_EVT_CLOUD_PACKAGES
 *
 * Tat ca ham deu KHONG chan: chi dua ban tin vao hang doi MQTT roi return.
 */

#include "esp_err.h"
#include "device_config.h"
#include "wash_session.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CLOUD_ORDER_ID_LEN  WS_ORDER_ID_LEN
#define CLOUD_QR_MAX_LEN    384

/* Chu ky gui Meter khi chuong dang WASHING (giay): MAC DINH khi server chua gui "meterInterval"
 * trong topic config. Gioi han gia tri server gui: */
#define CLOUD_METER_INTERVAL_S 20
#define CLOUD_METER_INTERVAL_MIN_S 1
#define CLOUD_METER_INTERVAL_MAX_S 3600

/* Danh sach goi dich vu server cau hinh cho tu (topic config, truong "packages") */
#define CLOUD_MAX_PACKAGES 12
typedef struct
{
    uint32_t price;       /* VND */
    uint32_t duration_s;  /* packageDuration: tong thoi luong su dung dich vu (giay) */
    uint32_t park_s;      /* parkingTime: tong thoi gian do xe toi da (giay, >= duration_s) */
} cloud_pkg_t;

typedef struct
{
    uint8_t count;
    cloud_pkg_t p[CLOUD_MAX_PACKAGES];
} cloud_pkg_list_t;

/* Du lieu QR server tra ve. Duoc cap phat dong, NGUOI NHAN su kien phai free(). */
typedef struct
{
    char order_id[CLOUD_ORDER_ID_LEN]; /* ma don tram tu tao luc goi cloud_create_order() (de doi chieu) */
    uint32_t transaction_id;           /* ma phien server cap (0 = che do thu) */
    char qr[CLOUD_QR_MAX_LEN];         /* chuoi VietQR, tram tu ve ma */
    uint32_t amount;                   /* VND */
    uint16_t expire_in_s;              /* so giay con hieu luc (0 = dung qr_timeout_s trong cau hinh) */
    char bank_name[32];
    char account_name[48];
} cloud_qr_t;

typedef enum
{
    CLOUD_ORDER_PAID = 0,
    CLOUD_ORDER_FAILED,     /* loi he thong phia server/ngan hang */
    CLOUD_ORDER_EXPIRED,
    CLOUD_ORDER_CANCELLED,
} cloud_order_status_t;

esp_err_t cloud_init(void);

/* Server san sang nhan don chua (da ket noi MQTT) */
bool cloud_is_ready(void);

/* Khach mua tai tram: gui InitTransaction. Goi lai voi CUNG order_id = gui lai DUNG ban tin cu
 * (giu packageId/timestamp) de server khong tao phien trung. */
esp_err_t cloud_create_order(const char *order_id, const devcfg_package_t *pkg);

/* Huy cho thanh toan QR: gui CancelPayment. order_id la ma don tram tu tao (chua co QR)
 * hoac ma phien server cap (chuoi so). */
esp_err_t cloud_cancel_order(const char *order_id);

/* Giao thuc khong co lenh hoi trang thai don: server tu gui lai StartTransaction. Giu de tuong thich. */
esp_err_t cloud_query_order(const char *order_id);

/* Bao phien: ended=false -> Meter WASHING (vua bat dau), ended=true -> Meter NORMAL kem reason.
 * ESP_OK = ban tin da vao hang doi MQTT (dang ket noi). */
esp_err_t cloud_report_session(const ws_session_t *s, bool ended);

/* Meter WASHING cua phien dang rua: goi moi cloud_meter_interval_s() giay, khi bat/tat/doi bom,
 * khi nhan StartTransaction lap lai cua chinh phien dang rua. */
esp_err_t cloud_report_progress(const ws_session_t *s);

/* Chu ky gui Meter dinh ky dang dung (giay): theo "meterInterval" cua topic config, chua co -> mac dinh */
uint32_t cloud_meter_interval_s(void);

/* Canh bao / su kien van hanh: giao thuc chua co goi tin -> chi ghi log */
esp_err_t cloud_report_event(const char *event, const char *detail);

#endif