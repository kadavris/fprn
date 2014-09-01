/** \file shtrih_ltfrk.c
 * \brief Fiscal printers daemon's driver for Shtrih-FR-K printer - main module
 *
 * V1.200. Written by Andrej Pakhutin
****************************************************/
#define SHTRIH_LTFRK_C
#include <fcntl.h>
#include <time.h>
#include "shtrih_ltfrk.h"
#include "shtrih_answer_timeouts.h"
#include "shtrih_flags.h"

// factory-def speed is 4800, but windows software prefer it to 19200, so these comes first.
const char *default_speeds_list = "19200,4800,9600,38400,57600,115200,2400";
const int each_speed_tries = 2;
const int standard_answer_timeout = 10000; //msec. win driver table std = 10000. the fucking printer is too slow even on 115000

#include "shtrih_ltfrk_get_state.c"
//===========================================================================
/** \brief Shtrih-FR-K driver. Method for sending comands to printer
 *
 * \param devid int - device id
 * \param data char* - buffer ptr
 * \param  size size_t - count of bytes to send
 * \return int - 0 - OK, errcode if not
 *
*/
int shtrih_ltfrk_send_command(int devid, char *data, size_t size)
{
  struct t_device *dev;
  int errcode;
  dev = get_dev_by_id(devid);
  errcode = send_command(dev, data, size);
  return errcode;
}

//===========================================================================
/** \brief Shtrih-FR-K driver internal. counting CRC according to the Manual
 *
 * \param mem void* - ptr to data
 * \param len unsigned int - length of data
 * \return unsigned char - CRC
 *
*/
unsigned char count_crc(void *mem, unsigned int len)
{
  unsigned char crc, *pch;

  pch = (unsigned char *)mem;
  crc = 0;

  while(len--) crc ^= *(pch++);

  return crc;
}

//===========================================================================
/** \brief Shtrih-FR-K driver. Method called within config reader on 'device' line.
 *
 * \param device_index int - index of device data struct in array
 * \return int - boolean success
 *
 * does initialization of data structure with buffers allocation and some other magic
*/
int shtrih_ltfrk_register_device(int device_index)
{
  struct t_device *dev;
  struct t_driver_data *dd;

  dev = &devices[device_index];
  dev->buf_size = 1024;
  dev->buf = getmem(dev->buf_size, "shtrih_ltfrk_register_device: malloc on devices.buf");

  dev->buf_ptr = 0;
  dev->psbuf_size = 10000;
  dev->psbuf = getmem(dev->psbuf_size, "shtrih_ltfrk_register_device: malloc on php state buf");
  dev->psbuf_ptr = 0;

  dev->driver_data = getmem(sizeof(struct t_driver_data), "shtrih_ltfrk_register_device: driver_data malloc");

  dd = dev->driver_data;

  dd->buf_size = 1024;
  dd->buf = getmem(dd->buf_size, "shtrih_ltfrk_register_device: driver_data buf malloc");
  dd->buf_ptr = 0;

  process_config_options_speed((char*)default_speeds_list, &dd->config_try_speeds, NULL);

  dd->connected_speed = 0;

  return 1;
}

//===========================================================================
/** \brief Shtrih-FR-K driver. Method for config file 'options' keyword parser
 *
 * \param device_index int - index of device data struct in array
 * \param opt char* - string with config line
 * \return int - boolean success
 *
*/
int shtrih_ltfrk_parse_options(int device_index, char *opt)
{
  struct t_device *dev;
  struct t_driver_data *dd;
  char *s, *sp;
  int n;

  dev = &devices[device_index];
  dd = dev->driver_data;

  if (0 == strcasecmp(opt, "password")) // admin password for printer
  {
    s = config_parse_get_next_token(1);
    n = strtol(s, &sp, 10);

    if (n < 0 || n > 4294967296 || *sp != '\0')
      return 0;

    //*(unsigned short*)(dd->password) = (short)n;
    memcpy(dd->admin_password, &n, 4);

  }
  else if (0 == strcasecmp(opt, "speeds"))
  {
    // should die on errors
    process_config_options_speed(config_parse_remaining_arg(), &dd->config_try_speeds, default_speeds_list);
  }
  else
    return 0; //unrecognized

  return 1;
}

//===========================================================================
/** \brief Shtrih-FR-K driver internal. reads and validates printer answers with timeout enforcing
 *
 * \param dev struct t_device * - ptr to device data struct
 * \param timeout int - timeout in millisec (at least 50 there will be)
 * \param confirm_char char - the answer code to printer
 * \return int - bytes read if any, 0 if timeout, -1 if error
 *
 *  overwrites dev->buf with new data:
 *  dev->buf filled with:
 *  00: STX
 *  01: data length
 *  02: data...*/
int read_answer3(struct t_device *dev, int timeout, char confirm_char)
{
  struct timeval tvstart, tvnow, tvdiff;
  struct t_driver_data *dd;
  int n, data_len;
  unsigned char crc;

  dd = dev->driver_data;
  timeout *= 1000; // to microseconds
  gettimeofday(&tvstart, NULL);

  for(;;)
  {
    gettimeofday(&tvnow, NULL);
    timersub(&tvnow, &tvstart, &tvdiff);

    if (tvdiff.tv_sec * 1000000 + tvdiff.tv_usec >= timeout)
    {
      if (debug_level > 10)
        debuglog("* debug: read_answer(): timeout from printer id: %d (%d bytes read so far)\n", dev->id, dev->buf_ptr);
      return 0;
    }

    dev->buf_ptr = 0;
    memset(dev->buf, 0, dev->buf_size);

    dev->state = STATE_BUSY;

    n = read_bytes(dev, 1, standard_answer_timeout); // should get STX + answer length

    if (n == 0)
      continue;  // timeout. looping

    if (n != 1 || dev->buf[0] != CODE_STX)
    {
      if(debug_level > 3) dosyslog(LOG_ERR, "!ERROR: read_answer: answer begins with %#x on dev %d", dev->buf[0], dev->id);
      continue; // garbage data or not the first byte of previous answer. re-trying whole process till timeout
    }

    n = read_bytes(dev, 1, standard_answer_timeout); // data length

    if (n == 0)
      continue; // timeout. looping

    data_len = dev->buf[1];
    n = read_bytes(dev, data_len + 1, standard_answer_timeout); // data + CRC byte. at least 8-10 msec on 2400 per byte, but you know...

    dev->state = STATE_READY;
    if (n < 0)
    {
      dd->buf[0] = CODE_NAK;
      write_bytes(dev, dd->buf, 1, NULL);
      dosyslog(LOG_ERR, "shtrih_ltfrk: read_answer: answer read error on dev %d", dev->id);
      return 0; // fatality
    }
    if (n != data_len + 1)
    {
      dd->buf[0] = CODE_NAK;
      write_bytes(dev, dd->buf, 1, NULL);
      dosyslog(LOG_ERR, "shtrih_ltfrk: read_answer: FR different length answer (timeout?) %d of %d bytes on dev %d", n, data_len + 1, dev->id);
      return 0; // brutality
    }

    crc = count_crc(dev->buf + 1, data_len + 1); // CRC is for length byte + data

    if (dev->buf[data_len + 2] != crc)
    {
      dd->buf[0] = CODE_NAK;
      write_bytes(dev, dd->buf, 1, NULL);
      dosyslog(LOG_ERR, "shtrih_ltfrk: read_answer: FR answer CRC error on dev %d: should be %#x, count: %#x", dev->id, (int)(dev->buf[data_len + 2]), crc);

      if (debug_level > 9 )
      {
        debuglog("read_answer: dev->buf_ptr=%d:%c", dev->buf_ptr, (dev->buf_ptr>10?'\n':' '));
        memdump(dev->buf, dev->buf_ptr);
      }

      return 0; // babality
    }

    // answering with ACK or something other by request
    dd->buf[0] = confirm_char;
    if ( 1 != write_bytes(dev, dd->buf, 1, NULL) )
       return 0; // fucality

    //tcflush(dev->fd, TCIFLUSH); // flushing input. just in case
    if (debug_level > 9) debuglog("read_answer: data packet received OK\n");

    break; // OK
  } //for(;;)

  return 1;
}

//===========================================================================
/** \brief Shtrih-FR-K driver internal. default kind of read printer's answer, confirming with ACK. wrapper around read_answer3()
 *
 * \param dev struct t_device * - ptr to device data struct
 * \param timeout int - timeout in millisec (at least 50 there will be)
 * \return int - bytes read if any, 0 if timeout, -1 if error
*/
int read_answer(struct t_device *dev, int timeout)
{
  return read_answer3(dev, timeout, CODE_ACK);
}

//===========================================================================
/** \brief Shtrih-FR-K driver internal. sends command to printer
 *
 * \param dev struct t_device * - ptr to device data struct
 * \param data char* - data to send
 * \param data_size size_t - data size
 * \return int - 0 = no error, 1 - error, -1 = garbled communication
 *
 * format:
 * byte 0: STX(0x02) - begin of msg
 * byte 1: msg length (N). Length excluding bytes 0, LRC(CRC) and this
 * byte 2: command/answer code
 * bytes 3 to (N + 1): variable parms
 * byte N + 2 = LRC/CRC XOR-ing all bytes except byte 0
 *
 * special codes for messaging:
 * ENQ  5 = power on init
 * STX  2 = begin of msg
 * ACK  6 = acknowledge
 * NAK 15 = denial
*/
int send_command(struct t_device *dev, char *data, size_t data_size)
{
  struct t_driver_data *dd;
  struct timeval tv;
  int n, command_code, answer_try;

  dd = dev->driver_data;
  //if ( dd->state != STATE_READY ) return 1;
  tcflush(dev->fd, TCIFLUSH);

  dev->buf_ptr = 0;
  read_bytes(dev, dev->buf_size, 0); // skip garbage

  // don't remember why to trigger DTR, honestly...
  ioctl(dev->fd, TIOCMGET, &n);
  n &= ~(TIOCM_RTS| TIOCM_DTR);
  ioctl(dev->fd, TIOCMSET, &n);
  sleep(1);
  n |= TIOCM_DTR;
  ioctl(dev->fd, TIOCMSET, &n);

  // ENQ ------------------------------------------
  for (answer_try = 0; ; ++answer_try)
  {
    if (answer_try == 10)
    {
      dosyslog(LOG_ERR, "shtrih_ltfrk send_command: try #%d timeout on ENQ - aborting", answer_try);
      dd->state = STATE_NEEDRECONNECT;
      return 1;
    }

    dd->buf[0] = CODE_ENQ;
    if (1 != write_bytes(dev, dd->buf, 1, "shtrih_ltfrk send_command: write(ENQ) to %s: %m", dev->tty))
    {
      dd->state = STATE_NEEDRECONNECT;
      return 1;
    }

    dev->buf_ptr = 0;
    n = read_bytes(dev, 1, standard_answer_timeout);

    //if (debug_level > 9 && n > 0) debuglog("send_command: ENQ answer: %#x\n", *(dev->buf));

    if (n < 0)
    {
      dosyslog(LOG_ERR, "shtrih_ltfrk send_command: read on ENQ: %m");
      return n;
    }

    if (n == 0)
      continue; // timeout - silent next try

    if( *(dev->buf) == CODE_NAK ) // now we can send command
    {
      dd->state = STATE_READY;
      break;
    }
    else if ( *(dev->buf) == CODE_ACK )
    {
      if (debug_level > 5) debuglog("send_command: ENQ answer is ACK. devouring the previous command output first\n");

      read_answer(dev, standard_answer_timeout);

      continue; // anyway we'll try again
    }
    else
    {
      dosyslog(LOG_ERR, "!ERROR: shtrih_ltfrk send_command: printer returned invalid response code: %#x, dev: %d", *(dev->buf), dev->id);
      continue; // next try
    }
  }

  dev->state = STATE_BUSY;
  dd->buf[0] = CODE_STX;
  dd->buf[1] = data_size;
  memcpy(dd->buf + 2, data, data_size);
  dd->buf[data_size + 2] = count_crc(dd->buf + 1, data_size + 1); // crc for len + data bytes  n = data_size + 3;
  command_code = dd->buf[2];

  if (debug_level) debuglog("* debug: send_command dev %d: code %#x, %d bytes:%c", dev->id, command_code, n, (dev->buf_ptr>10?'\n':' ') );
  if (debug_level > 9) memdump(dd->buf, n);

  if ( n != write_bytes(dev, dd->buf, n, "shtrih_ltfrk: send_command dev %d: write %d bytes: %m", dev->id, n) )
    return 1;

  dev->state = STATE_CMDSENT;

  //--------------------------------------
  dev->buf_ptr = 0;
  tv.tv_sec = answer_timeouts[command_code] / 1000; // msec->sec
  tv.tv_usec = 999999; // +1 sec bonus

  if ( dev->tcpconn != NULL )
    timeradd(&(dev->tcpconn->expire), &tv, &(dev->tcpconn->expire)); // extending the timeout in tcp watcher

  n = read_bytes(dev, 1, answer_timeouts[command_code]); //read ACK/NAK

  dev->state = STATE_BUSY;

  //if (debug_level > 9) debuglog("send_command: command acknowledge answer: %#x\n", *(dev-buf));

  if (n != 1)
  {
    dosyslog(LOG_ERR, "shtrih_ltfrk: send_command: ACK/NAK timeout on dev %d", dev->id);
    return 1;
  }

  switch(*(dev->buf))
  {
    case CODE_ACK: // OK
      break;

    case CODE_NAK: // some job still in progress
      return 1;

    default:      dosyslog(LOG_ERR, "shtrih_ltfrk: send_command: dev: %d answer is not an ACK/NAK: %#x", dev->id, *(dev->buf));
      return -1;
  } //switch dev->buf

  // read data --------------------------------------
  n = read_answer(dev, standard_answer_timeout);

  if (n <= 0)
  {
    dosyslog(LOG_ERR, "!ERROR: shtrih_ltfrk: send_command: answer error. begins with %#x on dev %d, err (%m)", *(dev->buf), dev->id);
    dd->buf[0] = CODE_NAK;
    write_bytes(dev, dd->buf, 1, NULL);

    return 1;
  }

  tcflush(dev->fd, TCIFLUSH); // flushing input. just in case

  dev->state = STATE_READY;

  return 0; // no error
}

//===========================================================================
/** \brief Shtrih-FR-K driver internal. sends command to printer. just like send_command(), but with mighty printf abilities
 *
 * \param dev struct t_device * - ptr to device data struct
 * \param fmt char* - format string, like printf
 * \param ... - optional args for format
 * \return int - 0 = no error, 1 - error, -1 = garbled communication
 *
 * see send_command()
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

#include "shtrih_ltfrk_init.c"

//===========================================================================
/** \brief Shtrih-FR-K driver. Method periodically called from alarm signal handler.
 *
 * \param devid int - device id
 * \return int - boolean success
 *
 * checks for output from printer, it's status and makes reconnect/re-init if needed
*/int shtrih_ltfrk_check_state(int devid)
{
/*  struct t_driver_data *dd;
  struct t_device *dev;
  int n;
  dev = get_dev_by_id(devid);
  dd = dev->driver_data;
*/
  return 1;
}