#ifndef __DEVICE_CONFIG_H__
#define __DEVICE_CONFIG_H__

/*
 * Cau hinh van hanh cua tram: goi dich vu, toc do tru thoi gian tung loai bom, thoi gian cho, den,
 * so dien thoai chu cua hang, ma QR mo app...
 * Luu trong NVS (namespace "app", key "devcfg"). Server gui mode + danh sach goi qua topic
 * carwash/v1/{mac}/config (retain), app_fsm ap dung roi luu lai o day.
 * Khoi dong khong co mang van chay duoc voi cau hinh da luu (hoac mac dinh).
 */

#include "esp_err.h"
#include "wash_session.h"
#include "light_control.h"
#include <stdint.h>
#include <stdbool.h>

#define DEVCFG_MAX_PACKAGES 12 /* man chon goi cuon duoc, 2 goi / hang */
#define DEVCFG_LABEL_LEN    24
#define DEVCFG_PHONE_LEN    20
#define DEVCFG_APP_QR_LEN   160

#define DEVCFG_PKG_DURATION_MIN_S   60u
#define DEVCFG_PKG_DURATION_MAX_S   (99u * 60u + 59u) /* dong ho man hinh co 2 chu so phut */
#define DEVCFG_PKG_PARK_EXTRA_MAX_S (2u * 3600u)      /* do xe toi da = thoi luong + 2 gio */

/* 1 goi dich vu = "price#packageDuration#parkingTime" server gui trong topic config */
typedef struct
{
    uint32_t id;                    /* so thu tu goi trong danh sach (1..), chi dung noi bo */
    uint32_t duration_s;            /* packageDuration: tong thoi luong su dung dich vu (giay) */
    uint32_t park_s;                /* parkingTime: tong thoi gian do xe toi da (giay, >= duration_s) */
    uint32_t price;                 /* VND */
    char label[DEVCFG_LABEL_LEN];   /* ten hien thi, UTF-8 (tu sinh: "Gói 10 phút") */
} devcfg_package_t;

typedef struct
{
    uint32_t magic;
    uint16_t layout_ver;            /* doi cau truc -> tang so nay */
    uint32_t config_ver;            /* version do server cap */

    uint8_t package_count;
    devcfg_package_t packages[DEVCFG_MAX_PACKAGES];

    /* Kich ban tinh thoi gian (wash.mode): 1 = theo toc do (wash.rate_pct), 2 = chia phan tram
     * (wash.permille). Server doi duoc ca kich ban lan cac he so; ap dung tu PHIEN KE TIEP. */
    ws_config_t wash;
    light_cfg_t light;

    uint16_t create_order_timeout_s; /* cho server tra QR */
    uint16_t qr_timeout_s;           /* han thanh toan toi da */
    uint16_t late_paid_grace_s;      /* nhan tien tre sau khi huy/het han (0 = tat) */
    uint16_t paid_screen_s;          /* man hinh "thanh toan thanh cong" */
    uint16_t finished_screen_s;      /* man hinh ket thuc */
    uint16_t notice_screen_s;        /* man hinh thong bao loi/huy */
    uint16_t welcome_interval_s;     /* toi thieu giua 2 lan loa chao */
    uint8_t volume;                  /* 0..100 */

    uint16_t progress_interval_s;    /* (KHONG dung nua: chu ky Meter co dinh CLOUD_METER_INTERVAL_S = 20s theo giao thuc) */
    uint16_t select_timeout_s;       /* o man chon goi qua lau khong chon -> ve man hinh dau */
    /* So dien thoai chu cua hang, hien tren man "Mat ket noi internet" / "Loi thiet bi".
     * Server gui xuong (lay tu tai khoan app cua chu cua hang). Rong = chua co. */
    char owner_phone[DEVCFG_PHONE_LEN];
    /* (Du phong, hien KHONG dung: ma QR man hinh dau co dinh "CARWASH:<MAC goc>", xem app_fsm.c) */
    char app_qr[DEVCFG_APP_QR_LEN];
    /* operationMode dang ap dung, dung nguyen gia tri server gui (bot, nuoc, khi, hut bui):
     * SPEED = he so, RATIO = phan tram. Bao lai trong goi Boot (tai lieu muc C13 / J01). */
    float op_raw[4];
} device_config_t;

/* Doc tu NVS; loi hoac chua co -> dung mac dinh */
void device_config_init(void);

/* Con tro toi cau hinh dang dung (chi doc) */
const device_config_t *device_config_get(void);

/* Kiem tra + ap dung + luu NVS */
esp_err_t device_config_set(const device_config_t *cfg);

/* Kiem tra + ap dung, KHONG luu NVS (mat khi khoi dong lai) -- dung cho che do thu */
esp_err_t device_config_set_volatile(const device_config_t *cfg);

bool device_config_validate(const device_config_t *cfg);

/* Tim goi theo chi so (0..count-1); NULL neu sai */
const devcfg_package_t *device_config_package(int index);

/* Dien id = index + 1 va ten hien thi ("Gói 10 phút", "Gói 7 phút 30 giây") cho tung goi */
void device_config_fill_package(devcfg_package_t *p, int index);

#endif