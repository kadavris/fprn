/** \file maria301.c
* \brief Fiscal printers daemon's driver for maria301 main module
*
* V1.000a. Written by Andrej Pakhutin
* !WARNING! This driver is incomplete and not tested at all
****************************************************/
#define MARIA301_C
#include <fcntl.h>
#include "maria301.h"

const char *default_speeds_list = "115200,57600,38400,19200,9600,4800,2400";

const int each_speed_tries = 2;
const int standard_answer_timeout = 10000; //msec. win driver table std = 10000

//===========================================================================
/** \brief Maria301 driver's internal. CRC counting. taken from printer's programming manual for complemence sake
 *
 * \param mem void* - ptr to data
 * \param len int - data length
 * \return uint16_t
 *
*/
static uint16_t count_crc16(void *mem, int len)
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

//===========================================================================
/** \brief Device registration in daemon's list
 *
 * \param device_index int
 * \return int - boolean success
 *
 * Called within config reader on 'device' line. This is done once per driver type
*/
int maria301_register_device(int device_index)
{
struct t_device *dev;
struct t_driver_data *dd;

  dev = &devices[device_index];

  dev->buf_size = 1024;
  dev->buf = getmem(dev->buf_size, "maria301_register_device: malloc on devices.buf");
  dev->buf_ptr = 0;
  dev->psbuf_size = 10000;
  dev->psbuf = getmem(dev->psbuf_size, "maria301_register_device: malloc on php state buf");
  dev->psbuf_ptr = 0;

  dev->driver_data = getmem(sizeof(struct t_driver_data), "maria301_register_device: driver_data malloc");

  dd = dev->driver_data;
  dd->buf_size = 1024;
  dd->buf = getmem(dd->buf_size, "maria301_register_device: driver_data buf malloc");
  dd->buf_ptr = 0;

  dd->use_crc = 0;

  process_config_options_speed((char*)default_speeds_list, &dd->config_try_speeds, NULL);
  dd->connected_speed = 0;

  return 1;
}

//===========================================================================
/** \brief Config file 'options' keyword content parser
 *
 * \param device_index int - index of device data structure to add options to
 * \param opt char * - string from config
 * \return int - boolean success
*/
int maria301_parse_options(int device_index, char *opt)
{
struct t_device *dev;
struct t_driver_data *dd;
char *s, *sp;
int n;

  dev = &devices[device_index];
  dd = dev->driver_data;

  if (0 == strcasecmp(opt, "password")) // admin password for printer
  {
    s = config_parse_get_nextarg(NEXTARG_OPTIONAL);
    n = strtol(s, &sp, 10);

    if (n < 0 || n > 4294967296 || *sp != '\0')
      return 0;

    //*(unsigned short*)(dd->password) = (short)n;
    memcpy(dd->password, &n, 4);
  }
/*
  else if (0 == strcasecmp(opt, "speeds"))
  {
    process_config_options_speed(config_parse_get_remaining_arg(), &dd->config_try_speeds, default_speeds_list); // should die on errors
  }
*/
  else if (0 == strcasecmp(opt, "use_crc"))
  {
    n = config_parse_get_bool();

    if( n == -1 )
      return 0;

    dd->use_crc = n;
  }
  else
    return 0; //unrecognized

  return 1;
}

//===========================================================================
/** \brief Maria301 driver's internal. read printer's single raw block of data
 *
 * \param dev struct t_device * - ptr to the device struct
 * \param timeout int - timeout in millisecs
 * \return int - 0 on timeout, data size on success, -1 on error
 *
 * Read complete block of data from printer according to the manual from CMD_BEGIN marker to CMD_END marker
 * counting CRC and checking for basic errors.
*/
static int read_block(struct t_device *dev, int timeout)
{
  int n, i, got_bytes;
  struct t_driver_data *dd;
  struct timeval tv, time_end;


  dd = (struct t_driver_data *)(dev->driver_data);

  dev->buf_ptr = 0;
  memset(dev->buf, 255, dev->buf_size);

  if ( dev->tcpconn != NULL )
    ap_utils_timeval_set(dev->tcpconn->expire, AP_UTILS_TIMEVAL_ADD, timeout);

  ap_utils_timeval_set(&time_end, AP_UTILS_TIMEVAL_SET, timeout);

  timeout /= 10; // adjusting for average answer length in bytes

  got_bytes = 0;
  // we'll do simple scan for begin/end here. no mind blowing tricks. there is just short data packets
  do
  {
    gettimeofday(&tv, NULL);
    if ( timercmp(&tv, &time_end, >=) )
    {
      if ( debug_level )
        debuglog("? warning: maria301: read_block() dev %d/%s: timeout after %d bytes\n", dev->id, dev->tty, dev->buf_ptr);

      return 0;
    }

    if ( dev->buf_ptr == dev->buf_size )
    {
      dev->buf_ptr = 0;
      dev->state = NEED_RECONNECT;

      if ( debug_level )
        debuglog("? warning: maria301: read_block() dev %d/%s: buffer full of garbage\n", dev->id, dev->tty);

      return -1;
    }

    n = read_bytes(dev, 1, timeout);

    if ( n < 0 ) // error - we can do nothing here
    {
      if ( debug_level )
        debuglog("!ERROR: maria301: read_block() dev %d/%s: byte read return %d/%m\n", dev->id, dev->tty, n);

      dev->state = NEED_RECONNECT;

      return n;
    }
    else if ( n == 0 ) // we'll let global timeout to be a cause of return
    {
      continue;
    }

    ++got_bytes;

    // here we can try to skip garbage till we found some meaningful data
    if ( dev->buf[0] != CMD_BEGIN )
    {
      dev->buf_ptr = 0;

      if ( debug_level > 9 )
        debuglog("!ERROR: maria301: read_block() dev %d/%s: no CMD_BEGIN from start. read %d bytes\n", dev->id, dev->tty, got_bytes);

      continue;
    }
    else if ( dev->buf_ptr > 1 && dev->buf[dev->buf_ptr - 1] == CMD_BEGIN )
    {
      dev->buf[0] = CMD_BEGIN;
      dev->buf_ptr = 1;

      if ( debug_level > 4 )
        debuglog("!ERROR: maria301: read_block() dev %d/%s: another CMD_BEGIN after %d bytes\n", dev->id, dev->tty, dev->buf_ptr - 1);

      continue;
    }
  } while ( dev->buf[dev->buf_ptr - 1] != CMD_END );

  if ( debug_level > 9 )
  {
    debuglog("*debug: maria301: read_block() dev %d/%s:\n", dev->id, dev->tty);
    memdump(dev->buf, dev->buf_ptr);
  }

  // OK, got the sequence. now checking is data length correct?
  if ( dev->buf[dev->buf_ptr - 2] != dev->buf_ptr - 3 )
  {
    if ( debug_level )
      debuglog("!ERROR: maria301: read_block() dev %d/%s: seq length error: got %d bytes but should be %d\n", dev->id, dev->tty, dev->buf_ptr - 1, dev->buf[dev->buf_ptr - 2]);

    return -1;
  }

  if ( dd->use_crc )
  {
    n = read_bytes(dev, 2, timeout * 2);

    if ( n != 2 )
    {
      /* to hell with it now
      dev->state = NEED_RECONNECT;
      */

      return dev->buf_ptr;
    }

    if ( *((uint16_t *)(dd->buf + dev->buf_ptr - 2)) != count_crc16(dd->buf, dev->buf_ptr - 2) )
    {
      if ( debug_level )
        debuglog("!ERROR: maria301: read_block() dev %d/%s: seq CRC error\n", dev->id, dev->tty);

      return -1;
    }
  }

  return dev->buf_ptr;
}

//===========================================================================
/** \brief Maria301 driver's internal. read and analyze full printer's answer to command. DONE/READY, WAIT, WRK, PRN
 *
 * \param dev struct t_device * - ptr to the device struct
 * \param timeout int - timeout in millisecs
 * \return int - 0 on timeout, data size on success, -1 on error
 *
 * More intelligent procedure that should read all printer's babble
 * and construct something usable in the incoming buffer of device data structure
*/
static int read_answer(struct t_device *dev, int timeout)
{
  int i, n;
  int got_error;
  uint8_t tmpdata[256]; // temporary place for answer's actual data package
  int tmpdatalen;
  struct t_driver_data *dd;


  /*
    The culprit is in that printer can send us DONE/ERROR or DONE/READY blocks
    along with intermediate WRK,etc.
    So we should read all block by block until the timeout at the begin of another one.
    Which hopefully will be the end of whole answer sequence.
  */

  dd = (struct t_driver_data *)(dev->driver_data);
  dd->prnerrindex = -1;
  tmpdatalen = 0;

  for(;;) // begin of block sequence reading
  {
    n = read_block(dev, timeout);

    if ( n < 0 )
      return n;

    if ( n == 0 )
    {
      // !!! fill me?
    }

    if ( 0 == memcmp(dev->buf + 1, "WAIT", 4) || 0 == memcmp(dev->buf + 1, "WRK", 3) || 0 == memcmp(dev->buf + 1, "PRN", 3) )
    {
      if ( debug_level > 3 )
        dosyslog(LOG_ERR, "*debug: maria301(%d:%s): printer tells it's busy. we wait.\n", dev->id, dev->tty);

      usleep(1000000);

      continue;
    }
    else if ( 0 == memcmp(dev->buf + 1, "DONE", 4) )
    {
      if ( debug_level > 3 )
        dosyslog(LOG_ERR, "*debug: maria301(%d:%s): printer tells us 'DONE'.\n", dev->id, dev->tty);

      continue;
    }
    else if ( 0 == memcmp(dev->buf + 1, "READY", 5) )
    {
      if ( debug_level > 3 )
        dosyslog(LOG_ERR, "*debug: maria301(%d:%s): printer tells us 'READY'.\n", dev->id, dev->tty);

      if ( tmpdatalen > 0 ) // we've got some data previously
      {
        memcpy(dev->buf, tmpdata, tmpdatalen);
        dev->buf_ptr = tmpdatalen;
      }

      return dev->buf_ptr;
    }

    // some other data is here
    for ( i = 0; i < MARIA301_ERROR_MESSAGES_COUNT; ++i )
    {
      if ( 0 == memcmp(dev->buf + 1, maria301_error_messages[i][0], strlen(maria301_error_messages[i][0]) ) )
      {
        ++got_error;

        dd->prnerrindex = i;

        if ( debug_level )
          dosyslog(LOG_ERR, "*debug: maria301(%d:%s): printer error: %s (%s).\n", dev->id, dev->tty,
                   maria301_error_messages[i][0], maria301_error_messages[i][1]);

        continue;
      }
    }

    // not an std answer or error - maybe it's command's answer data block
    memcpy(tmpdata, dev->buf, dev->buf_ptr);
    tmpdatalen = dev->buf_ptr;
  } // for() - blocks reading
}

#include "maria301_init.c"

//===========================================================================
/** \brief Maria301 driver's internal. Sends command to printer
 *
 * \param dev struct t_device * - ptr to device structure data
 * \param buf char * - ptr to buffer to send
 * \param data_size size_t - size of data to send
 * \return int - boolean success
 *
 * Main, simple tool to send something to printer
*/
static int send_command(struct t_device *dev, char *buf, size_t data_size)
{
  struct t_driver_data *dd;
  struct timeval tv;
  size_t cmd_size;

  dd = dev->driver_data;

  dev->state = STATE_BUSY;

  memcpy(dev->buf + 1, dd->buf, data_size);
  dd->buf[0] = CMD_BEGIN;

  cmd_size = data_size + 1;

  dd->buf[cmd_size++] = data_size;
  dd->buf[cmd_size++] = CMD_END;

  if (dd->use_crc || 0 == strncmp(dd->buf + 1, "CSIN", 4) ) // CSIN = set CRC preference. we should add at least fake crc on this command
  {
    *((uint16_t *)(dd->buf + cmd_size)) = count_crc16(dd->buf, cmd_size);
    cmd_size += 2;
  }

  if (debug_level)
    debuglog("* debug: maria301: send_command() dev %d: %d bytes, '%4s'\n", dev->id, cmd_size, dev->buf + 1);

  if (debug_level > 9)
    memdump(dd->buf, cmd_size);

  if ( cmd_size != write_bytes(dev, dd->buf, cmd_size, "maria301: send_command() dev %d: write %d bytes: %m", dev->id, cmd_size) )
    return 1;

  tv.tv_sec = answer_timeouts[command_code] / 1000; // msec->sec
  tv.tv_usec = 999999; // +1 sec bonus

  if ( dev->tcpconn != NULL )
    timeradd(&(dev->tcpconn->expire), &tv, &(dev->tcpconn->expire)); // extending the timeout in tcp watcher

  n = read_bytes(dev, 1, answer_timeouts[command_code]);

  dev->state = STATE_CMDSENT;

  return cmd_size;
}

//===========================================================================
/** \brief Maria301 driver's internal. Just like send_command(), but with mighty printf abilities
 *
 * \param dev struct t_device * - ptr to device structure data
 * \param fmt char * - format string like for printf
 * \param ... - optional args for format
 * \return int - boolean success
 *
*/
int send_command_fmt(struct t_device *dev, char *fmt, ...)
{
  struct t_driver_data *dd;
  va_list vl;
  int len;
  char *tmpstr;

  dd = dev->driver_data;

  va_start(vl, fmt);
  len = vsnprintf((char*)(dd->buf), dd->buf_size - 3, fmt, vl);
  va_end(vl);

  tmpstr = getmem(len, "malloc on send_command_fmt");
  memcpy(tmpstr, dd->buf, len);
  len = send_command(dev, tmpstr, len);

  free(tmpstr);

  return len;
}

//===========================================================================
/** \brief Maria301 driver's external method for sending data to printer
 *
 * \param devid int - index of device data structure
 * \param data char * - ptr to buffer to send
 * \param data_size size_t - size of data to send
 * \return int - boolean success
 *
*/
int maria301_send_command(int devid, char *data, size_t data_size);
{
  struct t_device *dev;
  struct t_driver_data *dd;
  int n;

  dev = &devices[devid]; // making shortcut

  // send_command() must be used here
  //n = write_bytes(dev->fd, data, data_size, "maria301_send_command: '%4s' - %d bytes: %m", dev->buf + 1, size);

  return n;
}

//===========================================================================
/** \brief Maria301 driver's external method that is periodically called from alarm signal handler. checks for output from printer, it's status
 *
 * \param devid int - device id
 * \return int - boolean success
 *
 * long desc here
*/
int maria301_get_state(int devid);
{
  struct t_driver_data *dd;
  struct t_device *dev;
  int n;

  dev = &devices[devid];
  dd = dev->driver_data;
}
