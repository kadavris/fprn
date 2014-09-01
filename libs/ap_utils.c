#ifndef AP_UTILS_C
#define AP_UTILS_C
#include "ap_utils.h"

//=========================================================
int ap_utils_timeval_set(struct timeval *tv, int mode, int msec)
{
  struct timeval tmp;


  if ( msec < 100 )
    return 0;

  switch( mode )
  {
    case AP_UTILS_TIMEVAL_ADD:
      tmp.tv_sec = msec / 1000;
      tmp.tv_usec = 1000 * (msec % 1000);
      timeradd(tv, &tmp, tv);
      break;

    case AP_UTILS_TIMEVAL_SUB:
      tmp.tv_sec = msec / 1000;
      tmp.tv_usec = 1000 * (msec % 1000);
      timersub(tv, &tmp, tv);
      break;

    case AP_UTILS_TIMEVAL_SET:
      gettimeofday(tv, 0);
      tmp.tv_sec = msec / 1000;
      tmp.tv_usec = 1000 * (msec % 1000);
      timeradd(tv, &tmp, tv);
      break;

    case AP_UTILS_TIMEVAL_SET_FROMZERO:
      tv->tv_sec = msec / 1000;
      tv->tv_usec = 1000 * (msec % 1000);
      break;

    default:
      return 0;
  }

  return 1;
}

//=========================================================
uint16_t count_crc16(void *mem, int len)
{
  uint16_t a, crc16;
  uint8_t *pch;

  pch = (uint8_t *)mem;
  crc16 = 0;

  while(len--)
  {
    crc16 ^= *pch;
    a = (crc16 ^ (crc16 << 4)) & 0x00FF;
    crc16 = (crc16 >> 8) ^ (a << 8) ^ (a << 3) ^ (a >> 4);
    ++pch;
  }

  return(crc16);
}

#endif
