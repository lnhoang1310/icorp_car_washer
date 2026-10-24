#ifndef __DEVICE_ID_H__
#define __DEVICE_ID_H__

#include <stdint.h>

/* Sinh và cache định danh duy nhất cho thiết bị từ MAC */

void device_id_init(void);
const char *device_id_get(void);
void device_id_get_mac(uint8_t out_mac[6]);

#endif
