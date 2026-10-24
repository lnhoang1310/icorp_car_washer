#ifndef __FRAM_MAP_H__
#define __FRAM_MAP_H__

/*
 * Ban do dia chi FRAM 32 KB (0x0000 - 0x7FFF).
 * MOI module luu vao FRAM phai lay dia chi tu day, khong tu dat so.
 * Doi bo cuc -> tang FRAM_MAP_LAYOUT_VERSION de firmware biet du lieu cu khong con hop le.
 */

#define FRAM_MAP_LAYOUT_VERSION   (1)

/* Header: version bo cuc, bo dem khoi dong */
#define FRAM_MAP_HEADER_BASE      (0x0000)
#define FRAM_MAP_HEADER_SIZE      (0x0100)

/* Vung test cua peripheral_test.c (ghi de thoai mai) */
#define FRAM_MAP_TEST_BASE        (0x0100)
#define FRAM_MAP_TEST_SIZE        (0x0100)

/* Snapshot phien rua: 2 slot A/B */
#define FRAM_MAP_SESSION_BASE     (0x1000)
#define FRAM_MAP_SESSION_SLOT     (0x0100)
#define FRAM_MAP_SESSION_SIZE     (2 * FRAM_MAP_SESSION_SLOT)

/* Outbox MQTT: hang doi vong cac tin cho server ACK */
#define FRAM_MAP_OUTBOX_BASE      (0x2000)
#define FRAM_MAP_OUTBOX_SIZE      (0x4000)

/* Bo dem tich luy: gio chay tung bom, so lan dong relay, doanh thu */
#define FRAM_MAP_COUNTERS_BASE    (0x6000)
#define FRAM_MAP_COUNTERS_SIZE    (0x0100)

/* 0x6100 - 0x7FFF: du phong */
#define FRAM_MAP_END              (0x8000)

_Static_assert(FRAM_MAP_SESSION_BASE + FRAM_MAP_SESSION_SIZE <= FRAM_MAP_OUTBOX_BASE, "FRAM: session de len outbox");
_Static_assert(FRAM_MAP_OUTBOX_BASE + FRAM_MAP_OUTBOX_SIZE <= FRAM_MAP_COUNTERS_BASE, "FRAM: outbox de len counters");
_Static_assert(FRAM_MAP_COUNTERS_BASE + FRAM_MAP_COUNTERS_SIZE <= FRAM_MAP_END, "FRAM: vuot 32 KB");

#endif