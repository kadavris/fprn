#ifndef AP_LOG_H
#define AP_LOG_H

#include <syslog.h>


#ifndef AP_LOG_C
extern int debug_to_tty;
extern int debug_level;
#endif

//register file/stream handle for debug output
extern int add_debug_handle(int fd);

//removes handle from debug output
extern int remove_debug_handle(int fd);

//checks if handle is registered for debug output
extern int is_debug_handle(int fd);

//logs message to debug channel(s)
extern void debuglog(char *fmt, ...);

//syslog wrapper. also call debuglog if level is set
extern void dosyslog(int priority, char *fmt, ...);

// returns string with info on last error occured within ap_* function calls
extern const char *ap_error_get(void);


extern int hprintf(int fh, char *fmt, ...); //like fprintf for int handles
extern int hputs(char *str, int fh); //like fputs for int handles
extern int hputc(char c, int fh);//like fputc for int handles


extern void memdumpfd(int fh, void *p, int len); //hexdump/printable chars into specified filehandle
extern void memdump(void *p, int len);// hexdump/printable chars to debug channel(s)
extern void memdumpbfd(int fh, void *p, int len);// bitdump into specified filehandle
extern void memdumpb(void *p, int len);// bitdump into debug channel(s)


#endif
