#pragma once

#include "driver/uart.h"
#include "driver/i2c_types.h"

/* =====================================================================
 *  ETHERNET W5500 (SPI3)
 * ===================================================================== */
#define CLOCK_SPI_MHZ          (40)
#define ETH_SPI_HOST           (SPI3_HOST)
#define ETH_SPI_SCLK_GPIO      (14)
#define ETH_SPI_MOSI_GPIO      (13)
#define ETH_SPI_MISO_GPIO      (11)
#define ETH_SPI_CS0_GPIO       (9)
#define ETH_SPI_INT0_GPIO      (10)
#define ETH_SPI_PHY_RST0_GPIO  (12)

/* =====================================================================
 *  I2C BUS CHUNG: FRAM + DS3231 + 2x PCF8574AT
 *  Tat ca thiet bi dung chung 1 bus -> chi khai bao 1 lan o day,
 *  cac ten cu (FRAM_*, DS3231_*) giu lai de code hien tai khong phai sua.
 * ===================================================================== */
#define I2C_MAIN_PORT          (I2C_NUM_1)
#define I2C_MAIN_SDA_GPIO      (1)
#define I2C_MAIN_SCL_GPIO      (2)

#define FRAM_I2C_PORT          I2C_MAIN_PORT
#define FRAM_SDA_GPIO          I2C_MAIN_SDA_GPIO
#define FRAM_SCL_GPIO          I2C_MAIN_SCL_GPIO    
#define FRAM_I2C_ADDR          (0x50)
#define FRAM_CAPACITY_BYTES    (32 * 1024)

#define DS3231_I2C_PORT        I2C_MAIN_PORT
#define DS3231_SDA_GPIO        I2C_MAIN_SDA_GPIO
#define DS3231_SCL_GPIO        I2C_MAIN_SCL_GPIO
#define DS3231_I2C_ADDR        (0x68)

#define PCF8574AT_I2C_PORT     I2C_MAIN_PORT
#define PCF8574AT_SDA_GPIO     I2C_MAIN_SDA_GPIO
#define PCF8574AT_SCL_GPIO     I2C_MAIN_SCL_GPIO
#define PCF8574AT_CLK_HZ       (400000)
#define PCF8574AT_OUT_I2C_ADDR (0x38)
#define PCF8574AT_IN_I2C_ADDR  (0x39)
#define PCF8574AT_IN_INT_GPIO  (15)   /* INT open-drain, active-low: can pull-up */

/* ---- Ban do INPUT (PCF 0x39) ---- */
#define IN_BIT_BTN_FOAM        (0)
#define IN_BIT_BTN_WATER       (1)
#define IN_BIT_BTN_DRY         (2)
/* P3: bo trong (dung khan cap = ngat nguon bang phan cung, firmware khong doc) */
#define IN_BIT_MOTION          (4)
#define IN_USED_MASK           (0x17)

/* Muc tac dong cua tung input: 1 = active-low (tac dong khi chan bi keo xuong GND),
 *                              0 = active-high (tac dong khi chan len 3.3V).
 * Nut nhan dung dien tro keo len san co trong PCF8574 -> nut noi xuong GND la active-low. */
#define IN_BTN_FOAM_ACTIVE_LOW   (1)
#define IN_BTN_WATER_ACTIVE_LOW  (1)
#define IN_BTN_DRY_ACTIVE_LOW    (1)
#define IN_MOTION_ACTIVE_LOW     (0)   /* PIR (HC-SR501...): xuat muc cao khi co nguoi */

/* 1 = co lap cam bien chuyen dong o P3, 0 = khong lap (bo qua chan P3).
 * Khi khong dung: den chi bat khi troi toi VA dang co phien rua; khong co loa chao;
 * man hinh chi giam sang xuong 50% (thay vi 15%) de khach van nhin thay tu xa. */
#define IN_USE_MOTION_SENSOR     (0)

#define IN_ACTIVE_LOW_MASK  (((IN_BTN_FOAM_ACTIVE_LOW) << IN_BIT_BTN_FOAM) |   \
                             ((IN_BTN_WATER_ACTIVE_LOW) << IN_BIT_BTN_WATER) | \
                             ((IN_BTN_DRY_ACTIVE_LOW) << IN_BIT_BTN_DRY) |     \
                             ((IN_MOTION_ACTIVE_LOW) << IN_BIT_MOTION))
#define IN_DEBOUNCE_MS           (50)

/* ---- Ban do OUTPUT (PCF 0x38) ---- */
#define OUT_BIT_RELAY_FOAM     (0)
#define OUT_BIT_RELAY_WATER    (1)
#define OUT_BIT_RELAY_DRY      (2)
#define OUT_BIT_RELAY_LIGHT    (3)
#define OUT_RELAY_ACTIVE_LOW   (0)    /* PCF8574 len muc HIGH khi cap nguon -> relay nen kich muc LOW */
#define OUT_PUMP_DEADTIME_MS   (300)  /* thoi gian cho giua tat bom nay va bat bom khac */

/* =====================================================================
 *  TFT 3.2" 240x320 (dat ngang 320x240) + TOUCH XPT2046 (dung chung SPI2)
 * ===================================================================== */
#define TFT_SPI_HOST           (SPI2_HOST)
#define TFT_SPI_MHZ            (20)   /* chay on dinh roi moi thu tang len 26/40; day dai/cam board test thi giu 10-20 */
#define TFT_SCK_GPIO           (8)
#define TFT_MOSI_GPIO          (3)
#define TFT_MISO_GPIO          (18)
#define TFT_CS_GPIO            (19)
#define TFT_DC_GPIO            (38)
#define TFT_RST_GPIO           (20)
#define TFT_BACKLIGHT_GPIO     (41)
#define TFT_BACKLIGHT_ON_LEVEL (1)

/* Chip dieu khien man hinh. Nhieu module 3.2" 240x320 ghi ILI9488 nhung thuc te la ILI9341.
 * Luc khoi dong, log "display: doc ID man hinh" se cho biet chip that -> dat cho dung. */
#define LCD_CTRL_ILI9488       (1)
#define LCD_CTRL_ILI9341       (2)
#define LCD_CONTROLLER         LCD_CTRL_ILI9341

/* 1 = ngay sau khi khoi tao, to ca man do -> xanh la -> xanh duong -> trang -> den (moi mau 0.7 s),
 * khong qua LVGL. Dung de kiem tra day/chip. Chay on roi thi dat 0. */
#define LCD_SELF_TEST          (0)

/* Man 3.2" TFT SPI 240x320, dat nam ngang -> 320x240 */
#define LCD_H_RES              (320)
#define LCD_V_RES              (240)
#define LCD_DRAW_BUF_LINES     (80)   /* ILI9341: 2 buffer 320x40x2 byte trong RAM noi (DMA) */
/* Do lech vung hien thi. ILI9341 co bo nho dung bang man 240x320 nen de 0.
 * Chi dung khi hinh bi lech/co soc rac o 1 canh: lech ngang -> LCD_OFFSET_X, lech doc -> LCD_OFFSET_Y. */
#define LCD_OFFSET_X           (0)
#define LCD_OFFSET_Y           (0)

/* Huong man hinh (nam ngang).
 * LCD_ROTATE_180 = 1: man hinh lap NGUOC (xoay 180 do) -> lat ca hinh lan cam ung, KHONG can hieu chinh
 * cam ung lai (hieu chinh da luu van dung). Lap xuoi thi dat 0.
 * Neu hinh bi soi guong trai-phai: dao LCD_MIRROR_X. Tren-duoi: dao LCD_MIRROR_Y. */
#define LCD_ROTATE_180         (1)
#define LCD_SWAP_XY            (1)
#if LCD_CONTROLLER == LCD_CTRL_ILI9488
/* Driver ILI9488 (atanisoft) DAO NGUOC y nghia mirror_x; mac dinh tuong ung MADCTL = 0x28 */
#define LCD_MIRROR_X           (1 ^ LCD_ROTATE_180)
#define LCD_MIRROR_Y           (0 ^ LCD_ROTATE_180)
#else
#define LCD_MIRROR_X           (0 ^ LCD_ROTATE_180)
#define LCD_MIRROR_Y           (0 ^ LCD_ROTATE_180)
#endif
#define LCD_BGR_ORDER          (1)    /* mau do/xanh duong bi dao -> doi thanh 0 */
#define LCD_INVERT_COLOR       (0)    /* man IPS thuong can 1 */

#define TOUCH_SPI_MHZ          (2)    /* XPT2046 toi da ~2.5 MHz */
#define TOUCH_SCK_GPIO         TFT_SCK_GPIO
#define TOUCH_MOSI_GPIO        TFT_MOSI_GPIO
#define TOUCH_MISO_GPIO        TFT_MISO_GPIO
#define TOUCH_CS_GPIO          (21)
#define TOUCH_IRQ_GPIO         (42)
/* 0 = doc cam ung dinh ky (khuyen dung). 1 = cho chan IRQ bao cham: phai bat them
 * CONFIG_XPT2046_INTERRUPT_MODE trong menuconfig, neu khong chip khong bao IRQ va cam ung chet. */
#define TOUCH_USE_IRQ          (0)
/* Do nhay cam ung (luc an, don vi ADC 0..4095). Nho hon = nhay hon, qua nho de bi cham ao.
 * Log luc hieu chinh in "luc=..." cua moi lan cham, dung de chon nguong. */
#define TOUCH_Z_PRESS          (200)  /* bat dau tinh la cham */
#define TOUCH_Z_RELEASE        (120)  /* dang cham, luc giam duoi muc nay moi tinh la nha */
#define TOUCH_RELEASE_READS    (2)    /* so lan doc lien tiep khong thay cham moi tinh la nha tay */
#define TOUCH_PRESS_READS      (2)    /* so lan doc lien tiep (~33 ms/lan) phai thay cham, cung vi tri, moi tinh la cham */
#define TOUCH_MAX_SPREAD       (200)  /* ADC: cac lan doc lech nhau hon muc nay = nhieu, bo qua */

#define TOUCH_CALIB_LOG        (0)
/* Hieu chinh cam ung tinh theo man hinh lap XUOI (LCD_ROTATE_180 = 0); lap nguoc firmware tu doi. */
#define TOUCH_SWAP_XY          (1)
#define TOUCH_INVERT_X         (1)
#define TOUCH_INVERT_Y         (1)
#define TOUCH_RAW_X_MIN        (20)
#define TOUCH_RAW_X_MAX        (3900)
#define TOUCH_RAW_Y_MIN        (20)
#define TOUCH_RAW_Y_MAX        (3800)

#define MAX98357_BCLK_GPIO     (6)
#define MAX98357_LRCLK_GPIO    (7)
#define MAX98357_DIN_GPIO      (5)
#define MAX98357_SD_MODE_GPIO  (4)    /* LOW = shutdown, dung de tat amp khi khong phat -> het tieng "xe" */
#define MAX98357_SD_ON_LEVEL   (0)
#define AUDIO_SAMPLE_RATE      (16000)

#define SIM_RESET_PIN          (45)   /* CHU Y: GPIO45 la chan strapping VDD_SPI, xem ghi chu */
#define SIM_PEN_PIN            (-1)
#define SIM_TX_PIN             (47)
#define SIM_RX_PIN             (48)
#define SIM_UART_NUM           (UART_NUM_2)
#define SIM_APN                ("v-internet")

// #define WIFI_SSID              ("TP-Link_FF1E")
// #define WIFI_PASSWORD          ("61907140")   /* "" = mang mo */

// #define WIFI_SSID              ("Lâm Hoàng")
// #define WIFI_PASSWORD          ("12345678")

#define WIFI_SSID              ("")
#define WIFI_PASSWORD          ("")

#define WIFI_AP_PREFIX         ("XTECH - ")
#define WIFI_AP_PASSWORD       ("12345678")

#define HW_VERSION           ("V1.0")
#define FW_VERSION           ("V1.0.0")

#define CLOUD_BROKER_HOST      ("uat-mqtt-carwash.icorp.vn")
#define CLOUD_BROKER_PORT      (1883)
#define CLOUD_BROKER_USERNAME  ("device")
#define CLOUD_BROKER_PASSWORD  ("device_2026")

// #define CLOUD_BROKER_HOST      ("broker.hivemq.com")
// #define CLOUD_BROKER_PORT      (1883)
// #define CLOUD_BROKER_USERNAME  ("test")   /* broker cong cong khong kiem tra, de gi cung duoc */
// #define CLOUD_BROKER_PASSWORD  ("test")

#define LED_DEBUG_GPIO         (46)