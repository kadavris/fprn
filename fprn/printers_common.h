#ifndef PRINTERS_COMMON_H
#define PRINTERS_COMMON_H

/*
   read printer bytes with timeouts
   timeout in millisec (at least 50 there will be)
   adds data past dev->bufptr
   return bytes read if any
    0 if timeout
   -1 if error
*/
extern int read_bytes(struct t_device *dev, int need_bytes, int timeout);

// return count of bytes written
extern int write_bytes(struct t_device *dev, void *buf, int count, char *error_msg_fmt, ...);

/*
   decodes config "options speed ..." line and and fills array of numeric values to try on init
   out_speeds_array - ptr to array of speeds to try (B* as in termios.h). zero at the end
   in_default_speeds_list can be NULL
*/
extern void process_config_options_speed(char *in_config_line, int **out_speeds_array, const char *in_default_speeds_list);

#endif
