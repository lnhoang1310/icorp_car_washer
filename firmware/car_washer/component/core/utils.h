#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdint.h>

int fuel_type_to_index(const char *fuel_type);

/* device_package_id: mã định danh cho mỗi phiên bơm */
uint64_t make_device_package_id(uint32_t epoch_sec, uint8_t pump_idx, uint16_t seq);

void decode_device_package_id(uint64_t pkgid,
                               uint32_t *epoch_sec,
                               uint8_t *pump_idx,
                               uint16_t *seq);

#endif
