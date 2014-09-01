#ifndef AP_ERROR_H
/*
  ap_error.h - internal!
  error info storage
*/

#include "ap_log.h"

#define ap_error_str_maxlen 1024

#ifndef AP_LOG_C
extern char ap_error_str[ap_error_str_maxlen];

extern void ap_error_set(char *fmt, ...);
extern void ap_error_clear(void);
#endif

#endif
