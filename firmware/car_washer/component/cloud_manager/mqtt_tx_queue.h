#ifndef __MQTT_TX_QUEUE_H__
#define __MQTT_TX_QUEUE_H__

/*
 * Hang doi ban tin QUAN TRONG cho server ACK (PUBACK), luu trong FRAM (vung FRAM_MAP_OUTBOX):
 * Meter ket thuc phien (co reason), CancelPayment.
 *   - Mat dien / mat mang: ban tin van nam trong FRAM, ket noi lai la gui lai NGUYEN VAN
 *     (cung packageId -> server nhan biet gui lap).
 *   - Chi xoa khi broker xac nhan PUBACK.
 * FRAM loi -> van chay voi hang doi trong RAM (mat khi mat dien).
 *
 * Khong tu khoa: nguoi goi (cloud_manager) giu mutex.
 */

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TXQ_SLOTS        16
#define TXQ_SLOT_SIZE    1024
#define TXQ_MAX_LEN      (TXQ_SLOT_SIZE - 16) /* tru phan header */

/* Doc FRAM, dem cac ban tin con cho gui tu lan chay truoc */
void txq_init(void);

/* Them ban tin (chuoi JSON). Tra ve so slot >= 0, -1 neu day / qua dai */
int txq_push(const char *json);

/* Doc ban tin o slot vao buf (ket thuc '\0'). false neu slot trong / loi */
bool txq_read(int slot, char *buf, size_t buf_len);

/* Xoa ban tin (da duoc ACK) */
void txq_remove(int slot);

/* Danh sach slot dang cho, sap theo thu tu da them (cu truoc). Tra ve so phan tu */
int txq_list(int out_slots[TXQ_SLOTS]);

int txq_count(void);

#endif