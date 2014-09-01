#ifndef _AP_STR_H

#define _AP_STR_H

#include <stdio.h>

typedef struct
{
  char *buf;
  int buf_len;
  char *curr;
  char *separators;
} t_str_parse_rec;



extern void *getmem(int size, char *errmsg);//malloc with err checking. if errmsg != NULL it is reported via dosyslog and exit(1) called
extern int makestr(char **d, const char *s);// strdup with auto free/malloc d - destination ptr, s - source



// checks if designated buffer can hold the data expectedand resizes it if it is smaller than needed
extern int check_buf_size(char **buf, int *bufsize, int *bufpos, int needbytes);
// puts data into buffer, with size checking and buffer expanding on the fly
extern int put_to_buf(char **buf, int *bufsize, int *bufpos, void *src, int srclen);




// creates new parsing record from in_str, using user_separators if not NULL. default is space + tab
extern t_str_parse_rec *str_parse_init(char *in_str, char *user_separators);
extern void str_parse_end(t_str_parse_rec *r);// destroys parse record
extern char *str_parse_next_arg(t_str_parse_rec *r);// get next token from string
extern char *str_parse_remaining(t_str_parse_rec *r);// get remnants of string
extern char *str_parse_rollback(t_str_parse_rec *r, int roll_count);// rollback n tokens
extern char *str_parse_skip(t_str_parse_rec *r, int skip_count);// skip n tokens forward
// get next arg as boolean value (on/off 1/0 true/false). -1 in case of error
extern int str_parse_get_bool(t_str_parse_rec *r);

#endif
