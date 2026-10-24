#include "utils.h"
#include <stddef.h>

int fuel_type_to_index(const char *fuel_type)
{
    if (!fuel_type || fuel_type[0] == 0)
        return -1;

    char c = fuel_type[0];

    /* '1'..'9' -> 1..9 */
    if (c >= '1' && c <= '9')
        return c - '0';

    /* 'A'..'Z' -> 10..35 */
    if (c >= 'A' && c <= 'Z')
        return 10 + (c - 'A');

    return -1; /* invalid */
}

#define PKGID_SEQ_BITS 12
#define PKGID_PUMP_BITS 5

#define PKGID_SEQ_MASK ((1U << PKGID_SEQ_BITS) - 1U)   /* 0x0FFF */
#define PKGID_PUMP_MASK ((1U << PKGID_PUMP_BITS) - 1U) /* 0x001F */
#define PKGID_TIME_SHIFT (PKGID_SEQ_BITS + PKGID_PUMP_BITS)

uint64_t make_device_package_id(uint32_t epoch_sec, uint8_t pump_idx, uint16_t seq)
{
    return ((uint64_t)epoch_sec << PKGID_TIME_SHIFT) |
           ((uint64_t)(pump_idx & PKGID_PUMP_MASK) << PKGID_SEQ_BITS) |
           (uint64_t)(seq & PKGID_SEQ_MASK);
}

void decode_device_package_id(uint64_t pkgid,
                               uint32_t *epoch_sec,
                               uint8_t *pump_idx,
                               uint16_t *seq)
{
    if (epoch_sec)
        *epoch_sec = (uint32_t)(pkgid >> PKGID_TIME_SHIFT);

    if (pump_idx)
        *pump_idx = (uint8_t)((pkgid >> PKGID_SEQ_BITS) & PKGID_PUMP_MASK);

    if (seq)
        *seq = (uint16_t)(pkgid & PKGID_SEQ_MASK);
}
