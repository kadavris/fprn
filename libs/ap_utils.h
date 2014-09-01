#ifndef AP_UTILS_H
#define AP_UTILS_H

#include <sys/time.h>
#include <stdint.h>

// for ap_utils_timeval_set()
// adds to current value
#define AP_UTILS_TIMEVAL_ADD 0
// set value to current time + offset
#define AP_UTILS_TIMEVAL_SET 1
// subtracts from current value
#define AP_UTILS_TIMEVAL_SUB 2
// set value to offset only
#define AP_UTILS_TIMEVAL_SET_FROMZERO 3

#ifndef AP_UTILS_C
extern int ap_utils_timeval_set(struct timeval *tv, int mode, int msec);
extern uint16_t count_crc16(void *mem, int len);
#endif
#endif
