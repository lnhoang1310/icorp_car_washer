#ifndef __APP_EVENTS_H__
#define __APP_EVENTS_H__

/*
 * Toan bo su kien di vao app_fsm. Moi module (io, ui, cloud, mang) chi BAO su kien,
 * app_fsm la noi duy nhat ra quyet dinh.
 */

#include "io_expander.h"
#include "cloud_manager.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    /* Tu io_expander */
    APP_EVT_INPUT = 0,           /* data.input */

    /* Tu man hinh cam ung */
    APP_EVT_UI_MANUAL,           /* bam "Bam vao day" o man hinh dau -> sang man chon goi */
    APP_EVT_UI_BACK,             /* bam mui ten quay ve o man chon goi -> ve man hinh dau */
    APP_EVT_UI_SELECT_PACKAGE,   /* data.u32 = chi so goi (0..count-1) */
    APP_EVT_UI_CANCEL,           /* khach bam huy o man hinh QR */
    APP_EVT_UI_TOUCH,            /* cham man hinh bat ky -> danh thuc */
    APP_EVT_UI_END_SESSION,      /* khach xac nhan "Ket thuc" som o man dang rua */
    APP_EVT_UI_RESUME,           /* khach bam "Tiep tuc rua" sau khi mo khoa dung khan */

    /* Tu cloud */
    APP_EVT_CLOUD_READY,         /* data.b = true/false */
    APP_EVT_CLOUD_QR,            /* data.qr (cap phat dong, app_fsm free) */
    APP_EVT_CLOUD_ORDER_RESULT,  /* data.result */
    APP_EVT_CONFIG_UPDATED,      /* device_config da doi */
    /* Khach chon goi + thanh toan tren App (quet ma QR o man hinh dau): server bao tram bat dau phien.
     * data.app_order -- chi con dung cho che do thu (PT_DEMO_PAYMENT) */
    APP_EVT_CLOUD_APP_ORDER,
    /* Server gui StartTransaction (da thanh toan QR ngan hang hoac vi App). data.start
     * order_id = transactionId dang chuoi so. app_fsm tu phan biet: don QR dang cho / tien tre /
     * Start lap cua phien dang rua / don App moi. */
    APP_EVT_CLOUD_START,
    APP_EVT_CLOUD_REMOTE_STOP,   /* server dung tu xa. data.result.order_id = transactionId */
    APP_EVT_CLOUD_OP_MODE,       /* mode moi trong topic config (da kiem tra hop le). data.op_mode */
    APP_EVT_CLOUD_HOTLINE,       /* so dien thoai canh bao trong topic config. data.phone ("" = khong hien so) */
    APP_EVT_CLOUD_PACKAGES,      /* danh sach goi moi trong topic config. data.packages (cap phat dong, app_fsm free) */
} app_evt_type_t;

typedef struct
{
    app_evt_type_t type;
    union
    {
        struct
        {
            io_input_t in;
            bool active;
        } input;
        struct
        {
            char order_id[CLOUD_ORDER_ID_LEN];
            cloud_order_status_t status;
        } result;
        struct
        {
            char order_id[CLOUD_ORDER_ID_LEN];
            uint32_t package_id;    /* trung id goi trong cau hinh thi dung ten/gia cua goi do */
            uint16_t minutes;       /* thoi luong goi khach da mua */
        } app_order;
        struct
        {
            char order_id[CLOUD_ORDER_ID_LEN];
            uint32_t duration_s;    /* packageDuration: thoi luong rua da mua (giay) */
        } start;
        struct
        {
            uint8_t mode;           /* ws_mode_t */
            uint16_t rate_pct[WS_KIND_COUNT];
            uint16_t permille[WS_KIND_COUNT];
            float raw[4];           /* gia tri goc server gui: bot, nuoc, khi, hut bui */
        } op_mode;
        cloud_qr_t *qr;
        cloud_pkg_list_t *packages;
        char phone[DEVCFG_PHONE_LEN];
        uint32_t u32;
        bool b;
    } data;
} app_evt_t;

#endif