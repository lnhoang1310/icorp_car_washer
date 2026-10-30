#ifndef __CLOUD_PROTOCOL_H__
#define __CLOUD_PROTOCOL_H__

/*
 * Dong goi / doc goi tin giao thuc tram <-> server (JSON), KHONG lien quan MQTT.
 * Theo tai lieu "IC CAR WASH - 001. Nghiep vu va giao tiep thiet bi" (cap nhat 14/10/2026): khoang dinh danh
 * bang "bayMac" (MAC bo dieu khien khoang, 12 hex viet thuong), "bayNumber" chi de hien thi (khoang tich hop = 0).
 *
 * Goi len (up):  {"type":..,"packageId":<UUID>,"timestamp":<UTC ISO 8601>,"payload":{"bayMac":..,"bayNumber":..,...}}
 * Goi xuong (down): ShowPaymentCode, StartTransaction, RemoteStopTransaction (cung bao ngoai)
 * Cau hinh: config/operationMode {"mode":"SPEED"|"RATIO","foam":..,"water":..,"air":..,"vacuum":..}
 *
 * Cac ham cp_build_* tra ve chuoi JSON cap phat dong -> nguoi goi giai phong bang cp_free(). NULL = het bo nho.
 * Dung chung duoc cho tram trung tam lan khoang con (V2): bayMac truyen vao tung ham.
 */

#include "wash_session.h"
#include "cloud_manager.h"
#include <stdbool.h>
#include <stdint.h>

#define CP_MAC_LEN 13 /* 12 ky tu hex + '\0' */

/* ================= TIEN ICH ================= */

/* Chuan hoa MAC: bo ':' '-', viet thuong. false neu khong dung 12 ky tu hex */
bool cp_mac_normalize(const char *in, char out[CP_MAC_LEN]);

/* "2026-10-08T02:03:00Z" (hoac co phan giay le / mui gio +07:00) -> epoch UTC; -1 neu sai */
int64_t cp_iso_parse(const char *s);

/* transactionId tu chuoi so; 0 neu khong phai so duong */
uint32_t cp_txn_from_str(const char *s);

void cp_free(char *json);

/* ================= GOI LEN (up) ================= */

/* Boot: mac thiet bi, phien ban, operationMode DANG AP DUNG dang object day du
 * {"mode":"SPEED"|"RATIO","foam":..,"water":..,"air":..,"vacuum":..} (tai lieu C13 / J01).
 * raw[4] = bot, nuoc, khi, hut bui dung don vi server gui. */
char *cp_build_boot(const char *mac, const char *fw_ver, const char *hw_ver, uint8_t wash_mode, const float raw[4]);

/* InitTransaction: khach mua tai chuong */
char *cp_build_init(const char *bay_mac, int bay_number, uint32_t duration_s, uint32_t price);

/* CancelPayment: huy cho QR */
char *cp_build_cancel(const char *bay_mac, int bay_number, uint32_t txn);

/* Meter cua chuong. s = NULL hoac phien IDLE -> NORMAL, transactionId = 0.
 * Phien RUNNING/PAUSED -> WASHING; FINISHED -> NORMAL kem reason. */
char *cp_build_meter(const char *bay_mac, int bay_number, const ws_session_t *s);

/* ================= GOI XUONG (down) ================= */

typedef enum
{
    CP_DOWN_INVALID = 0,   /* JSON loi / thieu truong bat buoc */
    CP_DOWN_UNKNOWN,       /* loai goi chua ho tro (type_name) */
    CP_DOWN_SHOW_QR,       /* ShowPaymentCode: txn, code, expire_at */
    CP_DOWN_START,         /* StartTransaction: txn, duration_s */
    CP_DOWN_REMOTE_STOP,   /* RemoteStopTransaction: txn */
} cp_down_type_t;

typedef struct
{
    cp_down_type_t type;
    char type_name[24];             /* ten "type" trong goi (de ghi log) */
    char bay_mac[CP_MAC_LEN];       /* da chuan hoa; "" neu thieu */
    uint32_t txn;                   /* transactionId */
    uint32_t duration_s;            /* packageDuration (Start) */
    uint32_t price;                 /* (ShowPaymentCode, neu server gui) */
    int64_t expire_at;              /* expireAt epoch UTC, -1 = khong co */
    char code[CLOUD_QR_MAX_LEN];    /* chuoi QR (ShowPaymentCode) */
    bool code_truncated;            /* chuoi QR dai hon bo dem */
} cp_down_t;

/* Doc goi down. Luon dien out->type (INVALID neu loi). */
void cp_parse_down(const char *data, int len, cp_down_t *out);

/* ================= CAU HINH TU: carwash/v1/{mac}/config (retain) =================
 * Tai lieu muc 6 (cap nhat 23/10/2026):
 *   {"mode": "S#3#1#2#1", "packages": "10000#600#900|20000#900#1200|30000#1200#1500"}
 *   mode     = mode#foam#water#air#vacuum. S = SPEED (he so > 0), R = RATIO (so nguyen > 0, tong 100).
 *              Gia tri 0 = bo loai do (vd "R#20#50#30#0", "S#3#0#2#0"); can it nhat 1 trong bot/nuoc/khi > 0
 *   packages = cac goi ngan cach '|', moi goi price#packageDuration#parkingTime (VND, giay, giay)
 *   meterInterval = chu ky gui Meter dinh ky khi dang rua (giay, so nguyen
 *                   CLOUD_METER_INTERVAL_MIN_S..CLOUD_METER_INTERVAL_MAX_S)
 *   hotline  = so dien thoai hien tren man canh bao ("Mat ket noi" / "Loi thiet bi"), "" = khong hien so
 * Hai truong doc DOC LAP: truong nao sai thi bo truong do, truong kia van dung. */

typedef struct
{
    uint8_t mode;                       /* WS_MODE_RATE (SPEED) / WS_MODE_SHARE (RATIO) */
    uint16_t rate_pct[WS_KIND_COUNT];   /* SPEED: he so x100 */
    uint16_t permille[WS_KIND_COUNT];   /* RATIO: phan nghin, tong 1000 */
    float raw[4];                       /* gia tri goc: foam, water, air, vacuum */
} cp_op_mode_t;

#define CP_MAX_PACKAGES          CLOUD_MAX_PACKAGES
#define CP_PKG_DURATION_MIN_S    60u
#define CP_PKG_DURATION_MAX_S    (99u * 60u + 59u) /* dong ho man hinh co 2 chu so phut */
#define CP_PKG_PARK_EXTRA_MAX_S  (2u * 3600u) /* do xe toi da = thoi luong rua + 2 gio */
#define CP_PKG_PRICE_MAX         100000000u

typedef cloud_pkg_t cp_pkg_t;
typedef cloud_pkg_list_t cp_pkg_list_t;

typedef struct
{
    bool json_ok;                    /* payload la JSON object */
    bool has_mode, mode_ok;          /* co truong mode / mode hop le */
    bool has_packages, packages_ok;  /* co truong packages / danh sach hop le */
    bool has_hotline, hotline_ok;    /* co truong hotline / so hop le (rong cung hop le = xoa so) */
    char hotline[DEVCFG_PHONE_LEN];
    bool has_meter_interval, meter_interval_ok; /* co truong meterInterval / so nguyen trong gioi han */
    uint32_t meter_interval_s;
    cp_op_mode_t mode;
    cp_pkg_list_t packages;
} cp_config_t;

/* "S#3#1#2#1" / "R#20#50#20#10" (chap nhan ca "SPEED#.."/"RATIO#.."). Thieu/thua gia tri -> false.
 * V1 chua co hut bui: RATIO chia lai phan vacuum cho bot/nuoc/kho theo ti le cua chung. */
bool cp_parse_mode_str(const char *str, cp_op_mode_t *out);

/* "10000#600#900|20000#900#1200": 1..12 goi; 60 <= packageDuration <= 5999 giay (99:59),
 * packageDuration <= parkingTime <= packageDuration + 2 gio, gia > 0. Mot goi sai -> ca danh sach sai. */
bool cp_parse_packages_str(const char *str, cp_pkg_list_t *out);

/* So dien thoai: toi da DEVCFG_PHONE_LEN-1 ky tu, chi gom so va + - . ( ) khoang trang. "" hop le */
bool cp_parse_hotline(const char *str, char out[DEVCFG_PHONE_LEN]);

/* Doc ca goi config. Luon dien out (xem has_ / _ok). */
void cp_parse_config(const char *data, int len, cp_config_t *out);

#endif