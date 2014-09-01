/** \file printers_common.c
* \brief Fiscal printers daemon's common printing-related functions
*
* V1.200. Written by Andrej Pakhutin
****************************************************/

#define PRINTERS_COMMON_C
#include "fprnconfig.h"
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

//===========================================================================
/** \brief Reads data from printer until done or error
 *
 * \param dev struct t_device * - ptr to printer device data structure
 * \param need_bytes int - how many byte we need to receive
 * \param timeout int - total timeout for the whole transaction in milliseconds (at least 50 there will be)
 * \return int - actual read count or -1 in case of error
 *
 * cyclically queries the printer for the data available and reads it into dev->buf
 * adds data into dev->buf, past dev->bufptr
*/
int read_bytes(struct t_device *dev, int need_bytes, int timeout)
{
struct timeval tvstart, tvnow, tvdiff;
int n, received_count;

  timeout *= 1000; // to microseconds
  gettimeofday(&tvstart, NULL);
  received_count = 0;

  for(;;)
  {
    // it's better to sleep immediately as this func anyway called right after write() w/o delays
    usleep(10000);

    n = dev->buf_size - dev->buf_ptr;

    if (n > need_bytes)
       n = need_bytes;

    n = read(dev->fd, dev->buf + dev->buf_ptr, n);

    if (n > 0)
    {
      received_count += n;

      if (debug_level > 9 )
      {
        debuglog("read_bytes(): got %d (%d of %d) bytes:%c", n, received_count, need_bytes, (received_count > 9? '\n' : ' ') );
        memdump(dev->buf + dev->buf_ptr, n);
      }

      dev->buf_ptr += n;
    }
    else // n <= 0
    {
      if (n < 0 && errno != EAGAIN && debug_level > 5)
        dosyslog(LOG_ERR, "* debug: read_bytes(): got %d code reding from printer id: %d (err: %m)\n", n, dev->id);
    }

    if (received_count == need_bytes)
      return need_bytes;

    gettimeofday(&tvnow, NULL);
    timersub(&tvnow, &tvstart, &tvdiff);

    if (tvdiff.tv_sec * 1000000 + tvdiff.tv_usec >= timeout)
    {
      if (debug_level > 10)
        debuglog("* debug: read_bytes(): timeout (%ldms of %dms) from printer id: %d (%d bytes read so far)\n", (tvdiff.tv_sec * 1000000 + tvdiff.tv_usec)/1000, timeout/1000, dev->id, received_count);

      if (n < 0 && errno != EAGAIN)
        return n;

      return 0;
    }
  }//for(;;)
}

//===========================================================================
/** \brief Sends some prepared data to printer.
 *
 * \param dev struct t_device * - ptr to printer device data structure
 * \param buf void * - source of data
 * \param count int - bytes count
 * \param error_msg_fmt char* - NULL or printf - like format for possible error logging
 * \param ... - optional arguments for error logging message
 * \return int - actual count of bytes sent
 *
*/
int write_bytes(struct t_device *dev, void *buf, int count, char *error_msg_fmt, ...)
{
va_list va;
char str[1000];
int n;

  // commented out because some printers have very short receive timeout thus dropping even init sequence
  //usleep(1000);//let it have time to think
  n = write(dev->fd, buf, count);

  if ( count == n )
  {
    if (debug_level > 10 )
    {
      debuglog("* debug: successfully wrote dev %d, %d byte(s):%c", dev->id, count, (count > 10?'\n':' '));
      memdump(buf, count);
    }

    return count;
  }

  if ( error_msg_fmt != NULL )
  {
    va_start(va, error_msg_fmt);
    vsnprintf(str, 999, error_msg_fmt, va);
    dosyslog(LOG_ERR, str);
    va_end(va);
  }
  else
  {
    dosyslog(LOG_ERR, "!ERROR: dev #%d/%s: %d bytes write error: %m", dev->id, dev->tty, count);
  }

  return n;
}

//===========================================================================
/** \brief Helper for process_config_options_speed() - decodes serial speed value and finds it index
 *
 * \param s char * - string value of speed to convert
 * \param instr const char * - error message deobfuscator text
 * \return int - index in io_speeds[]
 *
 * converts serial speed value i.e. 2400,4800,etc into corresponding index in speeds array
 * aborts program execution on error
*/
int find_speed(char *s, const char *instr)
{
int speed, i;

  speed = atoi(s);

  for ( i = 0; i <= max_io_speeds_index; ++i )
  {
    if ( speed != io_speeds_printable[i] )
      continue;

    return i;
  }

  if ( i == max_io_speeds_index )
  {
    dosyslog(LOG_ERR, "find_speed(): wrong number '%s' in '%s'", s, instr);
    exit(1);
  }

  return 0;
}

//===========================================================================
/** \brief Processes "options speed ..." config line parameter
 *
 * \param in_config_line const char * - ptr to the beginning of string with list of values
 * \param out_speeds_array int ** - output array with allowed speeds indexes list
 * \param in_default_speeds_list const char * - string with list of default values
 * \return void
 *
 * decodes config "options speed ..." line and fills the array of indexes corresponding to the desired speed values.
 * The indexes is for io_speeds[] array.
 * The values must be comma separated, standard serial line speeds like 2400,4800,etc. Ranges allowed in form of lowspeed-highspeed
 * e.g. "options speed 1200,4800-19200,57600". No spaces allowed.
 * The last value of out_speeds_array will be zero.
 * in_default_speeds_list can be NULL. it is used in case of empty list in input line
 * Terminates execution of program on fatal error.
*/
void process_config_options_speed(const char *in_config_line, int **out_speeds_array, const char *in_default_speeds_list)
{
char *s, *range, *in_ptr;
int i, speed, range_end;

  if ( *out_speeds_array == NULL )
    *out_speeds_array = malloc((max_io_speeds_index + 1) * sizeof(int));

  int try_idx = 0; // index in this array

  range_end = -1;
  in_ptr = NULL;
  makestr(&in_ptr, in_config_line);

  while ( NULL != (s = strsep(&in_ptr, ",")) )
  {
    if ( try_idx == max_io_speeds_index )
    {
      dosyslog(LOG_ERR, "process_options_speed(): too much variants in '%s'. think more, use less.", in_config_line);
      exit(1);
    }

    while ( *s == '\t' || *s == ' ' )
      ++s;

    // detecting ranges: speed-speed
    range = strchr(s, '-');

    speed = find_speed(s, in_config_line);

    if ( range == NULL )
    {
      (*out_speeds_array)[try_idx++] = speed;
      continue;
    }

    // filling range
    while ( *range == '\t' || *range == ' ' )
       ++range;

    range_end = find_speed(range, in_config_line);

    if ( speed <= range_end )
    {
      dosyslog(LOG_ERR, "process_options_speed(): bad range '%s' of '%s'.", s, in_config_line);
      exit(1);
    }

    for ( i = speed; i <= range_end; ++i )
    {
      if ( try_idx == max_io_speeds_index )
      {
        dosyslog(LOG_ERR, "process_options_speed(): too much variants in range '%s' of '%s'.", s, in_config_line);
        exit(1);
      }

      (*out_speeds_array)[try_idx++] = i;
    }
  }

  (*out_speeds_array)[try_idx] = 0;

  if ( try_idx > 0 )
    return;

  if ( NULL == in_default_speeds_list )
  {
    dosyslog(LOG_ERR, "process_options_speed(): no speeds set. default speed list is empty");
    exit(1);
  }

  if ( debug_level > 4 )
    dosyslog(LOG_NOTICE, "process_options_speed(): no speeds set. using default scan range: %s.", in_default_speeds_list);

  process_config_options_speed((char*)in_default_speeds_list, out_speeds_array, NULL);
}
