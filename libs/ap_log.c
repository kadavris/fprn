#define AP_LOG_C

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "ap_error.h"
#include "ap_log.h"
#include "ap_tcp.h"

int debug_to_tty = 0;
int debug_level = 0;

#define max_debug_handles 5
static int debug_handles[max_debug_handles];
static int debug_handles_count = 0;
static int debug_mutex = 0;

char ap_error_str[ap_error_str_maxlen]; // string representation of error

//=======================================================================
static int getlock(void)
{
  int i;


  for(i = 0; debug_mutex; ++i)
  {
    usleep(100000);//100ms

    if (i == 3)
    {
      if ( debug_to_tty )
        fputs("? WARNING! stale debug mutex!\n", stderr);

      return 0;
    }
  }

  ++debug_mutex;

  return 1;
}

//=================================================================
int is_debug_handle_internal(int fd)
{
  int i;


  for ( i = 0; i < debug_handles_count; ++i )
    if ( debug_handles[i] == fd )
      return 1;

  return 0;
}

//=================================================================
int add_debug_handle(int fd)
{
  if ( !getlock() )
    return 0;

  if ( is_debug_handle_internal(fd) )
  {
    debug_mutex = 0;
    return 1;
  }

  if ( debug_handles_count == max_debug_handles )
  {
    debug_mutex = 0;
    return 0;
  }

  debug_handles[debug_handles_count++] = fd;
  debug_mutex = 0;

  return 1;
}

//=================================================================
static int remove_debug_handle_internal(int fd)
{
  int i, ii;


  for ( i = 0; i < debug_handles_count; ++i )
  {
    if ( debug_handles[i] == fd )
    {
      for ( ii = i+1; ii < debug_handles_count; ++ii )
        debug_handles[ii - 1] = debug_handles[ii];

      --debug_handles_count;

      return 1;
    }
  }
  return 0;
}

//=================================================================
int remove_debug_handle(int fd)
{
  int retcode;


  if ( !getlock() )
    return 0;

  retcode = remove_debug_handle_internal(fd);
  debug_mutex = 0;

  return retcode;
}

//=================================================================
int is_debug_handle(int fd)
{
  int retcode;


  if ( !getlock() )
    return 0;

  retcode = is_debug_handle_internal(fd);
  debug_mutex = 0;

  return retcode;
}

//=================================================================
void debuglog_output(char *buf, int buflen) // internal. outputs ready message to debug channel(s)
{
  int i;

  if ( debug_to_tty )
    fputs(buf, stderr);

  for ( i = 0; i < debug_handles_count; ++i )
  {
    int n = ap_tcp_check_state(debug_handles[i]);

    if ( n == -1 || (n & 4) != 0 )
      remove_debug_handle_internal(debug_handles[i]);
    else
      write(debug_handles[i], buf, buflen); //don't use tcpsend,etc, as it is may try to show some debug and do the loop
  }
}

//=================================================================
void debuglog(char *fmt, ...)
{
  va_list vl;
  int buflen;
  char buf[1024];
  static int repeats = 0;    //this statics is for syslog-like "last msg repeated N times..."
  static char lastmsg[1024];
  static int lastlen = 0;
  static time_t last_time;


  if ( ! getlock() )
    return;

  va_start(vl, fmt);
  buflen = vsnprintf(buf, 1023, fmt, vl);
  va_end(vl);

  if ( lastlen == buflen && 0 == strncmp(lastmsg, buf, lastlen) ) // repeat ?
  {
    ++repeats;

    if ( last_time + 3 >= time(NULL) ) // 3 sec delay between reposts
    {
      buflen = sprintf(buf, "... Last message repeated %d time(s)\n", repeats);
      debuglog_output(buf, buflen);
      repeats = 0;
      last_time = time(NULL);

      goto end;
    }
  }

  if ( repeats > 0 ) // should we repost final N repeats for previous message?
  {
    lastlen = sprintf(lastmsg, "... and finally repeated %d time(s)\n", repeats);
    debuglog_output(lastmsg, lastlen);
  }

  strcpy(lastmsg, buf);
  lastlen = buflen;
  last_time = time(NULL);
  repeats = 0;

  debuglog_output(buf, buflen);

end:
  debug_mutex = 0;
}

//=================================================================
void dosyslog(int priority, char *fmt, ...)
{
  va_list vl;
  char buf[1024];


  va_start(vl, fmt);
  vsnprintf(buf, 1023, fmt, vl);
  va_end(vl);

  syslog(priority, buf);

  if ( debug_level )
    debuglog(buf);
}

//=================================================================
void ap_error_set(char *fmt, ...)
{
  va_list vl;
  int msg_len;


  if ( ! getlock() )
    return;

  va_start(vl, fmt);
  msg_len = vsnprintf(ap_error_str, ap_error_str_maxlen, fmt, vl);
  va_end(vl);

  if (debug_level)
    debuglog_output(ap_error_str, msg_len);

  debug_mutex = 0;
}

//=================================================================
void ap_error_clear(void)
{
  ap_error_str[0] = '\0';
}

//=================================================================
const char *ap_error_get(void)
{
  return (const char *)ap_error_str;
}

//==========================================================
// fprintf for standard filehandles
int hprintf(int fh, char *fmt, ...)
{
  va_list vl;
  int blen,retcode;
  char buf[1024];


  va_start(vl, fmt);

  blen = vsnprintf(buf, 1024, fmt, vl);
  retcode = write(fh, buf, blen);

  va_end(vl);
  return retcode;
}

// fputs for standard filehandles
int hputs(char *str, int fh)
{
  return write(fh, str, strlen(str));
}

// fputc for standard filehandles
int hputc(char c, int fh)
{
  return write(fh, &c, 1);
}

//==========================================================
// memory hex dump with printable characters shown
void memdumpfd(int fh, void *p, int len)
{
  int i, addr, showaddr;
  unsigned char *s;


  if (len == 0)
     return;

  showaddr = (len > 48);// > 3 lines of data
  addr = 0;

  s = (unsigned char*)p;

  for (; len > 0; len -= 16, addr += 16)
  {
    if (showaddr)
      hprintf(fh, "%04x(%4d):  ", addr, addr);

    int linelen = len > 16 ? 16 : len;

    for (i = 0; i < linelen; ++i)
      hprintf(fh, "%02x %c ", s[i], isprint(s[i]) ? s[i] : '.');

    /*
    for(i = 0; i < linelen; ++i) hprintf(fh, "%02x ", s[i]);
    for(i = 16 - linelen; i > 0; --i) hputs("   ", fh);

    hprintf(fh, "\n\t");
    for(i = 0; i < linelen; ++i) hprintf(fh, " %c ", isprint(s[i]) ? s[i] : '.');
    */
    hputc('\n', fh);
    s += linelen;
  }
}

// dumps to debug channel if any
void memdump(void *p, int len)
{
  int i;


  if (debug_to_tty)
    memdumpfd(fileno(stderr), p, len);

  for ( i = 0; i < debug_handles_count; ++i )
    if (debug_handles[i] != 0)
      memdumpfd(debug_handles[i], p, len);
}

//==========================================================
// memory bits dump
void memdumpbfd(int fh, void *p, int len)
{
  int i, mask;
  unsigned char *s;


  if (len == 0)
    return;

  s = (unsigned char*)p;

  for (; len > 0; len -= 4)
  {
    hputs("\t", fh);
    int size = len > 4 ? 4 : len;

    for(i = 0; i < size; ++i)
    {
      hprintf(fh, "0x%02x: ", s[i]);

      for (mask = 128; mask != 0; mask >>= 1)
        hputc( s[i] & mask ? '1' : '0', fh );

      hputs("   ", fh);
    }
    hputs("\n", fh);
  }
}

// dumps to debug channel if any
void memdumpb(void *p, int len)
{
  int i;


  if (debug_to_tty)
    memdumpbfd(fileno(stderr), p, len);

  for ( i = 0; i < debug_handles_count; ++i )
    if (debug_handles[i] != 0)
      memdumpbfd(debug_handles[i], p, len);
}

