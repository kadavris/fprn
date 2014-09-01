/*
  apstr.c: string manipulation and alike functions. written by Andrej Pakhutin for his own use primarily.
*/
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/socket.h>

#define _AP_STR_C
#include "ap_log.h"
#include "ap_str.h"

//=================================================================
/*
  strdup with auto free/malloc d - destination ptr, s - source
  should we simplify this via realloc?
*/
int makestr(char **d, const char *s)
{
  if ( *d != NULL )
    free(*d);

  *d = NULL;

  if ( s == NULL )
    return 0;

  if ( NULL == (*d = (char*)malloc(strlen(s) + 1) ) )
    return 0;

  strcpy( (char*)(*d), s );

  return 1;
}

//=================================================================
void *getmem(int size, char *errmsg)
{
  void *ptr;


  ptr = malloc(size);

  if ( ptr == NULL )
  {
    if (errmsg != NULL)
      dosyslog(LOG_ERR, errmsg);

    exit(1);
  }

  return ptr;
}

//==========================================================
// copies data from src to buf. increasing size of the buf if it is smaller than needed
int check_buf_size(char **buf, int *bufsize, int *bufpos, int needbytes)
{
  int n;


  if ( *bufsize - *bufpos < needbytes )
  {
    n = *bufsize + needbytes - (*bufsize - *bufpos);

    if ( NULL == realloc(*buf, n) )
      return 0;

    *bufsize = n;
  }

  return 1;
}

//==========================================================
// copies data from src to buf. calling check_buf_size to adjust if needed
int put_to_buf(char **buf, int *bufsize, int *bufpos, void *src, int srclen)
{
  if ( ! check_buf_size(buf, bufsize, bufpos, srclen) )
    return 0;

  memcpy(*buf + *bufpos, src, srclen);
  *bufpos += srclen;

  return 1;
}

//====================================================================
//====================================================================
//====================================================================
t_str_parse_rec *str_parse_init(char *in_str, char *user_separators)
{
  t_str_parse_rec *r;


  if( NULL == (r = getmem(sizeof(t_str_parse_rec), NULL)) )
    return NULL;

  r->separators = NULL;

  if ( ! makestr(&r->separators, ( user_separators != NULL ) ? user_separators : " \t" ) )
  {
    free(r);
    return NULL;
  }

  r->buf_len = strlen(in_str) + 1;

  if( NULL == (r->buf = getmem(r->buf_len, NULL)) )
  {
    free(r);
    return NULL;
  }

  strcpy(r->buf, in_str);

  r->curr = strsep(&r->buf, r->separators);

  return r;
}

//====================================================================
void str_parse_end(t_str_parse_rec *r)
{
  free(r->buf);
  free(r->separators);
  free(r);
}

//====================================================================
char *str_parse_next_arg(t_str_parse_rec *r)
{
  return (r->curr = strsep(&r->buf, r->separators));
}

//====================================================================
char *str_parse_remaining(t_str_parse_rec *r)
{
  return r->buf;
}

//====================================================================
char *str_parse_rollback(t_str_parse_rec *r, int roll_count)
{
  if ( roll_count <= 0 )
    return r->curr;

  for(; roll_count; --roll_count)
  {
    if( r->curr == r->buf )
      break;

    *(r->curr - 1) = *(r->separators);

    for(; r->curr > r->buf; r->curr--)
    {
      if( *r->curr == '\0' ) // reached end of token
      {
        r->curr++;

        // backed up one token - looping for next if more pending
        break;
      }
    }
  }

  return r->curr;
}

//====================================================================
char *str_parse_skip(t_str_parse_rec *r, int skip_count)
{
  char *s;


  while( skip_count-- && NULL != (s = str_parse_next_arg(r)) );

  return s;
}

//====================================================================
int str_parse_set_separators(t_str_parse_rec *r, char *separators)
{
  if ( ! makestr(&(r->separators), ( separators != NULL ) ? separators : " \t" ) )
    return 0;

  return 1;
}

//====================================================================
int str_parse_get_bool(t_str_parse_rec *r)
{
#define GET_BOOL_KW_COUNT 6
  static const struct
  {
    const char *s;
    const int result;
  } keywords[GET_BOOL_KW_COUNT] = {
    { "on", 1 },
    { "off", 0 },
    { "1", 1 },
    { "0", 0 },
    { "true", 1 },
    { "false", 0 }
  };
  char *s;
  int i;


  s = str_parse_next_arg(r);

  if ( s == NULL )
    return -1;

  for( i = 0; i < GET_BOOL_KW_COUNT; ++i )
    if ( 0 == strcasecmp(keywords[i].s, s ) )
      return keywords[i].result;

  return -1;
}
