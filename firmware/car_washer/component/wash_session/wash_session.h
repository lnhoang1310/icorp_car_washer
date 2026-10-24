#ifndef __WASH_SESSION_H__
#define __WASH_SESSION_H__

/*
 * Logic phien rua xe - thuan C, khong phu thuoc FreeRTOS/ESP-IDF.
 *
 * Goi X phut = "thoi gian su dung" (total_ms). Co 2 KICH BAN tinh thoi gian, chon bang cfg.mode
 * (server cau hinh duoc, doi qua lai duoc; phien dang chay giu kich ban luc bat dau):
 *
 *   KICH BAN 1 - WS_MODE_RATE (toc do): 3 loai dung chung 1 quy thoi gian. Quy chi bi tru khi co
 *     bom DANG BAT, voi toc do rieng tung loai (rate_pct): mac dinh bot 300% (bom 1 phut tru
 *     3 phut), nuoc 100%, xit kho 200%.
 *   KICH BAN 2 - WS_MODE_SHARE (chia phan tram): quy thoi gian chia san cho tung loai theo
 *     permille (mac dinh bot 20%, nuoc 50%, kho 30%). Moi loai chi bi tru phan cua minh, toc do 1x;
 *     loai nao het phan thi khong bom duoc nua, cac loai khac van dung tiep.
 *
 * Chung cho ca 2 kich ban:
 * - "Thoi gian do xe" (park_left_ms) = parkingTime cua goi (server cau hinh); goi khong co parkingTime
 *   thi = thoi gian goi + park_extra_ms (mac dinh WS_PARK_EXTRA_MIN = 5 phut),
 *   dem nguoc theo thoi gian thuc tu luc bat dau phien; DUNG LAI khi phien tam dung.
 * - Ket thuc khi: dung het thoi gian su dung, HOAC het thoi gian do xe.
 * - charged_ms[] = thoi gian DA TRU vao goi cua tung loai; tong charged_ms + session_left_ms = total_ms.
 *   used_ms[] = thoi gian bom THUC cua tung loai. Ca 2 dung de bao cao len server.
 * - So hien tren man hinh lay qua ws_get_view(): dong ho lon va 3 o luon khop nhau tung giay.
 * - Khong bao gio bat 2 bom cung luc; chuyen bom phai cho dead-time.
 *
 * Module khong tu dieu khien relay: sau moi lan goi ws_press()/ws_tick()/ws_pause(),
 * lop tren doc ws_active_pump() va ghi relay tuong ung.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    WS_FOAM = 0,
    WS_WATER = 1,
    WS_DRY = 2,
    WS_KIND_COUNT = 3,
    WS_NONE = 0xFF,
} ws_kind_t;

typedef enum
{
    WS_MODE_RATE = 1,   /* kich ban 1: tru theo toc do tung loai */
    WS_MODE_SHARE = 2,  /* kich ban 2: chia phan tram thoi gian cho tung loai */
} ws_mode_t;

typedef enum
{
    WS_ST_IDLE = 0,     /* chua co phien */
    WS_ST_RUNNING,      /* dang trong phien */
    WS_ST_PAUSED,       /* tam dung (co dien lai giua phien / loi bus): tat bom, dong ho phien dung lai */
    WS_ST_FINISHED,     /* da ket thuc, cho lop tren doc bao cao roi ws_clear() */
} ws_state_t;

typedef enum
{
    WS_END_NONE = 0,
    WS_END_TIME_UP,     /* het thoi gian do xe */
    WS_END_ALL_USED,    /* dung het thoi gian su dung cua goi */
    WS_END_CANCELLED,   /* huy boi quan tri/server */
    WS_END_USER_STOP,   /* khach bam "Ket thuc" som */
} ws_end_reason_t;

typedef enum
{
    WS_PRESS_STARTED = 0,   /* bom bat ngay */
    WS_PRESS_PENDING,       /* dang cho dead-time, se tu bat */
    WS_PRESS_STOPPED,       /* nhan lai nut dang chay -> dung */
    WS_PRESS_CANCELLED,     /* nhan lai nut dang cho dead-time -> huy chuyen, khong bom nao chay
                             * (khach bam 2 lan qua nhanh) */
    WS_PRESS_EXHAUSTED,     /* het thoi gian su dung / (kich ban 2) loai nay da het phan */
    WS_PRESS_IGNORED,       /* khong trong phien / dang tam dung / tham so sai */
} ws_press_result_t;

/* Co su kien tra ve tu ws_tick() (OR bit) */
#define WS_EVT_PUMP_ON        (1u << 0) /* 1 bom vua bat sau dead-time */
#define WS_EVT_PUMP_EXHAUSTED (1u << 4) /* (kich ban 2) bom dang chay vua het phan cua minh -> da tat */
#define WS_EVT_PARK_1MIN      (1u << 1) /* thoi gian do xe vua qua moc con 60 giay */
#define WS_EVT_FINISHED       (1u << 2) /* phien vua ket thuc */
#define WS_EVT_WARN_1MIN      (1u << 3) /* thoi gian su dung vua qua moc con 60 giay */

#define WS_ORDER_ID_LEN 32
#define WS_SNAPSHOT_MAGIC 0x57534835u /* "WSH5" -- doi cau truc thi doi so nay */

typedef struct
{
    uint8_t mode;                     /* ws_mode_t */
    uint16_t rate_pct[WS_KIND_COUNT]; /* kich ban 1: toc do tru khi bom chay, 100 = 1x, 300 = 3x */
    uint16_t permille[WS_KIND_COUNT]; /* kich ban 2: phan chia cho tung loai, tong phai = 1000 */
    uint32_t deadtime_ms;
    uint32_t park_extra_ms;           /* thoi gian do xe = thoi gian goi + khoang nay */
} ws_config_t;

#define WS_RATE_PCT_MIN 10
#define WS_RATE_PCT_MAX 1000
#define WS_PARK_EXTRA_MAX_MS (2u * 3600u * 1000u)
#define WS_PARK_EXTRA_MIN (5u) /* phut cong them vao thoi gian do xe: parkingTime = goi + 5 phut */

/* Kich ban 1; bot x3, nuoc x1, xit kho x2; (kich ban 2: bot 20%, nuoc 50%, kho 30%); do xe them WS_PARK_EXTRA_MIN phut */
#define WS_CONFIG_DEFAULT { .mode = WS_MODE_RATE, .rate_pct = {300, 100, 200}, .permille = {200, 500, 300}, \
                            .deadtime_ms = 300, .park_extra_ms = WS_PARK_EXTRA_MIN * 60u * 1000u }

/* Kiem tra cau hinh (dung chung cho ws_start va device_config) */
bool ws_config_valid(const ws_config_t *cfg);

/* Toan bo trang thai phien. La POD de ghi thang xuong FRAM. */
typedef struct
{
    uint32_t magic;
    char order_id[WS_ORDER_ID_LEN];
    uint32_t package_id;
    uint32_t start_epoch;                 /* gio bat dau (epoch giay), de bao cao */

    uint32_t total_ms;                    /* thoi gian su dung cua goi */
    uint32_t session_left_ms;             /* thoi gian su dung con lai (dong ho lon) */
    uint32_t park_total_ms;               /* tong thoi gian do xe */
    uint32_t park_left_ms;                /* thoi gian do xe con lai */
    uint32_t used_ms[WS_KIND_COUNT];      /* thoi gian bom THUC cua tung loai (bao cao) */
    uint32_t charged_ms[WS_KIND_COUNT];   /* thoi gian DA TRU vao goi cua tung loai (chinh xac, bao cao) */
    uint32_t shown_s[WS_KIND_COUNT];      /* kich ban 1: so giay hien tren o tung loai, dong bo voi dong ho lon */
    uint16_t rate_pct[WS_KIND_COUNT];     /* toc do tru dang ap dung (kich ban 2: deu = 100) */
    uint32_t budget_ms[WS_KIND_COUNT];    /* phan thoi gian cua tung loai (kich ban 1: = total_ms) */
    uint8_t mode;                         /* ws_mode_t, sao tu cau hinh luc bat dau phien */
    uint16_t charge_rem;                  /* phan du khi tinh tru (don vi 1/100 ms) */
    uint32_t switch_count;                /* so lan bat bom, de thong ke */

    uint32_t deadtime_ms;
    uint32_t off_elapsed_ms;              /* thoi gian tu luc tat bom gan nhat (bao hoa) */

    uint8_t state;                        /* ws_state_t */
    uint8_t active;                       /* ws_kind_t: bom dang BAT relay */
    uint8_t pending;                      /* ws_kind_t: bom cho het dead-time */
    uint8_t end_reason;                   /* ws_end_reason_t */
} ws_session_t;

/* Xoa sach, ve IDLE */
void ws_clear(ws_session_t *s);

/* Bat dau phien moi. park_total_ms = tong thoi gian do xe toi da (parkingTime);
 * 0 = total_ms + cfg->park_extra_ms. Tra ve false neu tham so khong hop le
 * (park_total_ms < total_ms hoac lon hon total_ms + WS_PARK_EXTRA_MAX_MS). */
bool ws_start(ws_session_t *s, const ws_config_t *cfg, uint32_t total_ms, uint32_t park_total_ms,
              const char *order_id, uint32_t package_id, uint32_t start_epoch);

/* Nguoi dung nhan nut cua 1 loai bom */
ws_press_result_t ws_press(ws_session_t *s, ws_kind_t kind);

/* Goi dinh ky (vd 100 ms) voi so ms troi qua ke tu lan goi truoc. Tra ve WS_EVT_* */
uint32_t ws_tick(ws_session_t *s, uint32_t elapsed_ms);

/* Tam dung: active=true -> tat bom, dung ca 2 dong ho (su dung + do xe); false -> tiep tuc (bom van tat, phai nhan lai) */
void ws_pause(ws_session_t *s, bool active);

/* Huy phien (quan tri) */
void ws_cancel(ws_session_t *s);

/* Khach chu dong ket thuc som: tat bom, ket thuc voi ly do WS_END_USER_STOP.
 * Thoi gian con lai khong duoc hoan. Tra ve false neu khong co phien dang chay. */
bool ws_finish_early(ws_session_t *s);

/* So giay con lai lam tron len (kich ban 1: dong ho lon) */
static inline uint32_t ws_left_shown_s(const ws_session_t *s) { return (s->session_left_ms + 999u) / 1000u; }

/* Phan con lai cua 1 loai (ms). Kich ban 1: bang thoi gian su dung con lai. */
uint32_t ws_kind_left_ms(const ws_session_t *s, ws_kind_t kind);

/* Cac so HIEN THI (giay), dam bao dong ho lon va 3 o khop nhau tung giay:
 *   kich ban 1: card_s[] DEM LEN (da tru cua tung loai); left_s + tong card_s = thoi gian goi
 *   kich ban 2: card_s[] DEM XUONG (con lai cua tung loai); left_s = tong card_s
 * used_s[] luon la "da su dung" cua tung loai (cho man ket thuc). */
typedef struct
{
    uint8_t mode;                       /* ws_mode_t */
    uint32_t left_s;                    /* dong ho lon */
    uint32_t park_s;                    /* thoi gian do xe con lai */
    uint32_t card_s[WS_KIND_COUNT];
    uint32_t used_s[WS_KIND_COUNT];
    bool exhausted[WS_KIND_COUNT];      /* kich ban 2: loai nay da het phan */
} ws_view_t;

void ws_get_view(const ws_session_t *s, ws_view_t *v);

/* Truy van */
static inline ws_kind_t ws_active_pump(const ws_session_t *s) { return (ws_kind_t)s->active; }
static inline ws_kind_t ws_pending_pump(const ws_session_t *s) { return (ws_kind_t)s->pending; }
static inline ws_state_t ws_state(const ws_session_t *s) { return (ws_state_t)s->state; }
bool ws_is_available(const ws_session_t *s, ws_kind_t kind);

/* Kiem tra snapshot doc tu FRAM co hop le khong (magic, gia tri trong gioi han) */
bool ws_validate(const ws_session_t *s);

/* Sau khi khoi phuc tu FRAM (mat dien): tat moi bom, giu nguyen thoi gian con lai */
void ws_on_restore(ws_session_t *s);

const char *ws_kind_name(ws_kind_t k);

#ifdef __cplusplus
}
#endif

#endif