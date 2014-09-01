/** \file tcpanswer.c
* \brief Fiscal printers daemon's networking functions
*
* V1.200. Initial code by Andrej Pakhutin
****************************************************/
#include "fprnconfig.h"
#include "../libs/b64.h"

char *std_answers[] =
{
#define SA_OK 0
  "200 OK\r\n",
#define SA_BADIDX 1
  "400 no or bad index\r\n",
#define SA_UNKCMD 2
  "401 unknown command\r\n",
#define SA_PRINTERERROR 3
  "403 printer error\r\n",
#define SA_BADPARAM 4
  "404 command parameter error\r\n",
#define SA_DEVINUSE 5
  "405 device already in use\r\n",
  NULL
};

//=============================================================================
/** \brief checking if data available for reading from file descriptor
 *
 * \param fd int - file descriptor
 * \return int - num of fds ready (see select())
 *
 * copying from aptcp.c:
 * according to the mans what's down below is total bullshit.
 * you always have to monitor for EPIPE and EAGAIN errors and, possibly for 0 bytes read return as eof() indication specifically.
 * The read and write states on non-blocked socket are always ON.
*/
int check_for_data(int fd)
{
  struct timeval tv;
  int n;
  fd_set fds;

  tv.tv_sec = 0;
  tv.tv_usec = 0;
  FD_ZERO(&fds);
  FD_SET(fd, &fds);

  n = select(fd + 1, &fds, NULL, NULL, &tv);

  if (n < 0)
    dosyslog(LOG_ERR, "select(): %m");

  return n;
}

//=============================================================================
/** \brief Get next line of data from TCP connection
 *
 * \param tc struct ap_tcp_connection_t * - connection data structure to use
 * \return char* - ptr to read data
 *
 * returns ptr to the _next_complete_ line in buffer or NULL if none
 * Can enlarge buffer if needed.
 * Used to accumulate input from TCP peer in waiting for another command.
*/
char *tcp_get_line(struct ap_tcp_connection_t *tc)
{
  char *s;
  int i, n;
  const int upsize = 1024; //how much to add to buf in case of overflow?

  // nextline: init state = -1
  if ( tc->nextline != -1 ) // moving to beginning of the buffer
  {
    if ( tc->nextline != 0 ) // tc->nextline == 0 is a special case-something was read, but not the full line.
    {
      n = tc->bufptr - tc->nextline;
      memcpy(tc->buf, tc->buf + tc->nextline, n);
      tc->bufptr = n;

      if ( debug_level > 9 )
        debuglog("*debug: nextline-shifted %d bytes\n", n);

      tc->nextline = 0;
    }
  }
  else // no next line in buffer. resetting
  {
    tc->bufptr = 0;
  }

  if ( tc->bufptr / (tc->bufsize / 100) > 90 ) // 90% of buffer filled. resizing
  {
    if ( tc->buf == ( s = realloc(tc->buf, tc->bufsize + upsize) ) )
    {
      dosyslog(LOG_ERR, "realloc error from %d to %d bytes", tc->bufsize, tc->bufsize + upsize);
      exit(1);
    }

    tc->bufsize += upsize;
    tc->buf = s;
  }

  //if ( debug_level > 9 ) debuglog("before read: ptr: %d, nextl: %d, size: %d\n", tc->bufptr, tc->nextline, tc->bufsize);
  // reading all what we can. even if we read 0 bytes, there is possibility that buffer already contains full string(s)
  if ( check_for_data(tc->fd) > 0 )
    n = read(tc->fd, tc->buf + tc->bufptr, tc->bufsize - tc->bufptr);
  else
    n = 0;
  //if (debug_level) debuglog("read: %d\n", n);

  if (n < 0 && errno != EAGAIN)
  {
    dosyslog(LOG_ERR, "tcp_answer: read: %s\n", strerror(errno));
    return NULL;
  }

  if (debug_level)
    debuglog("tcp conn fd: %d read: %d bytes, ptr: %d, nextl: %d, size: %d\n", tc->fd, n, tc->bufptr, tc->nextline, tc->bufsize);

  s = tc->buf; // always scan from the begin in case prev read() already got the full line
  tc->bufptr += n;

  for (i = 0; i < tc->bufptr; ++i, ++s)
    if (*s == '\r' || *s == '\n' )
      break;

  if ( i == tc->bufptr ) // no eol - waiting for more characters
  {
    tc->nextline = 0;
    return NULL;
  }

  // marking end and skipping to the next line begin
  *(s++) = '\0';

  while ( ++i < tc->bufptr && (*s == '\r' || *s == '\n' ) )
    ++s;

  tc->nextline = (i == tc->bufptr ? -1 : i); // if there is no next line, then next call will just reset ptr

  return tc->buf;
}

//=============================================================================
#define CMDCODE_SEND     1
#define CMDCODE_DEVSTATE 2
#define CMDCODE_DEVTYPE  3
#define CMDCODE_LDPSTATE 4
#define CMDCODE_SVPSTATE 5
#define CMDCODE_MONITOR  6

/** \brief Checks if command is ready in the incoming buffer of selected TCP connection and executes it
 *
 * \param tcp_conn_idx int - index of connection to check
 * \return void
 *
 * Used to process commands sent from web ui or any other external counterpart that issues and/or controls printing jobs.
 * currently processed comands are:
 * SEND <dev_id> <b64string>
 *      send fully prepared command to the device specified. the most commands is usually comdes in binary form, so it should be base64 encoded.
 *      printer's answer if any is sent back also b64 encoded
 * DEVSTATE <dev_id>
 *          Queries the state of printer's driver. Returned is integer contining bitmask of internal driver's flags. See STATE_* in fprnconfig.h
 * DEVTYPE <dev_id>
 *         Returns type of device assigned to given id. Used to control correctness of inter-config data mostly.
 * SAVEPHPSTATE <lines_count>
 * LOADPHPSTATE
 *              As it is derived from the names of commands these were used to save and extract arbitrary data
 *              that can be used as a kind of web cookie in cases where there no external database is available to store such data
 *              <lines_count> is a number of lines of arbitrary data that follows SAVEPHPSTATE command
 * MON[ITOR][ new_debug_level]
 *    Marks this connection as another channel for debug info output, whilst optionally setting the new debug level or verbosity.
 *    This connection cannot be force-closed on standard timeout and will persists until client disconnect.
*/
void tcp_answer(int tcp_conn_idx) // answering web side inquiries
{
int dev_index, n, exec_status, answer_len;
char *token, *s, answer[1024], *answer_ptr;
char *nexttokenptr;
struct ap_tcp_connection_t *tc;

  tc = &ap_tcp_connections[tcp_conn_idx];

  if ( NULL == tcp_get_line(tc) )
    return; // no data/incomplete line

  if (debug_level)
    debuglog("tcp conn %d data: %s\n", tcp_conn_idx, tc->buf);

  answer_ptr = answer;

  for (;;)
  {
    if ( tc->state == TC_ST_READY ) // new command
    {
      tc->cmdcode = 0;
      tc->state = TC_ST_BUSY;
      tc->needlines = 0;
      exec_status = SA_OK;
      answer[0] = '\0';
      answer_len = 0;

      nexttokenptr = tc->buf;
      token = strsep(&nexttokenptr, " \t"); // get command name

      //------------------------------------------------------
      // send next line to printer.
      if ( 0 == strcasecmp(token, "SEND") )
      {
        tc->needlines = 1;
        tc->cmdcode = CMDCODE_SEND;
      }
      else if ( 0 == strcasecmp(token, "DEVSTATE"))
      {
        tc->needlines = 0;
        tc->cmdcode = CMDCODE_DEVSTATE;
      }
      else if ( 0 == strcasecmp(token, "DEVTYPE"))
      {
        tc->needlines = 0;
        tc->cmdcode = CMDCODE_DEVTYPE;
      }
      // saves arbitrary data from peer. You may think of it as web cookie
      // param is the count of lines of data to follow
      else if ( 0 == strcasecmp(token, "SAVEPHPSTATE"))
      {
        s = strsep(&nexttokenptr, " \t");
        if ( s == NULL || 0 >= (n = atoi(s)) )
        {
          exec_status = SA_BADPARAM;
          answer_len = 0;
          break;
        }
        else
        {
          tc->needlines = n;
          tc->cmdcode = CMDCODE_SVPSTATE;
        }
      }
      // sends peer saved data back to it
      else if ( 0 == strcasecmp(token, "LOADPHPSTATE"))
      {
        tc->cmdcode = CMDCODE_LDPSTATE;
      }
      // request debug monitoring. optional arg is a new debug level
      else if ( 0 == strncasecmp(token, "MON", 3) )
      {
        tc->cmdcode = CMDCODE_MONITOR;
      }
      else
      {
        s = "help: SEND/DEVSTATE/DEVTYPE/SAVEPHPSTATE/LOADPHPSTATE devid\nMON[ITOR][ new_debug_level]\n";
        ap_tcp_conn_send(tcp_conn_idx, s, strlen(s));
        exec_status = SA_UNKCMD;
      }
    } // new command pre-init


    /************************************
     command execution division
     ************************************/
    // check for valid device.
    if ( tc->cmdcode != CMDCODE_MONITOR )
    {
      s = strsep(&nexttokenptr, " \t");
      if ( s == NULL || 0 == (dev_index = atoi(s)) || -1 == (dev_index = dev_idx_by_id(dev_index)) )
      {
        dosyslog(LOG_ERR, "TCP Conn %d: bad dev id: %s", tcp_conn_idx, s);
        exec_status = SA_BADIDX;
        break;
      }

/* this check is makes sense for multithread mode.
      if ( devices[dev_index].tcpconn != NULL ) // check if busy in another conn.
      {
        if ( 0 < ap_tcp_connection_is_alive(devices[dev_index].tcpconn->idx) )
        { // busy
          dosyslog(LOG_ERR, "TCP Conn %d: dev id %s is in use by other connection", tcp_conn_idx, s);
          exec_status = SA_DEVINUSE;
          break;
        }
      }
*/

      devices[dev_index].tcpconn = tc;
    }

    if ( tc->cmdcode == CMDCODE_SEND )
    {
      if ( NULL == tcp_get_line(tc) )
        return; // no data/incomplete line

      s = base64_decode(tc->buf, strlen(tc->buf), (size_t*)&n);

      // sending to printer
      if (0 != devices[dev_index].device_type->func_send_command(devices[dev_index].id, s, n)) // error?
      {
        exec_status = SA_PRINTERERROR;
        answer_len = sprintf(answer, "%d\n", devices[dev_index].state);
      }
      else
      {
        // sent OK. we answer with %06d length, encoded data, LF
        s = base64_encode((char*)(devices[dev_index].buf), devices[dev_index].buf_ptr, (size_t*)&answer_len);
        sprintf(tc->buf, "%06d", answer_len + 1);
        tc->bufptr = 6;

        if ( ! check_buf_size(&tc->buf, &tc->bufsize, &tc->bufptr, answer_len + 1) )
        {
          dosyslog(LOG_ERR, "realloc for +%d: %m", answer_len + 1);
          exit(1);
        }

        memcpy(tc->buf + 6, s, answer_len);
        tc->buf[answer_len + 6] = '\n';
        answer_len += 7;
        answer_ptr = tc->buf;
        free(s);
      }
    }
    //++++++++++++++++++++++++++++++++++++++++++++
    else if ( tc->cmdcode == CMDCODE_DEVSTATE )
    {
      if (0 != devices[dev_index].device_type->func_get_state(devices[dev_index].id))
      {
        exec_status = SA_PRINTERERROR;
        answer_len = sprintf(answer, "device state: %d\n", devices[dev_index].state);
      }
      else
      {
        answer_len = sprintf(answer, "%d\n%s\n", devices[dev_index].state, devices[dev_index].buf);
      }
    }
    //++++++++++++++++++++++++++++++++++++++++++++
    // returns device type code
    else if ( tc->cmdcode == CMDCODE_DEVTYPE)
    {
      answer_len = sprintf(answer, "%d\n", devices[dev_index].device_type->type);
    }
    //++++++++++++++++++++++++++++++++++++++++++++
    // saves php class state in case of errors with page reload needed
    else if ( tc->cmdcode == CMDCODE_SVPSTATE )
    {
      tc->state = TC_ST_DATAIN;

      while ( tc->needlines ) // lines count that peer requested to send
      {
        if ( NULL == tcp_get_line(tc) )
          return; // no data/incomplete line

        tc->needlines--;

        // check for small buffer. reserve space for LF
        if ( devices[dev_index].psbuf_size - devices[dev_index].psbuf_ptr < tc->bufptr + 1)
        {
          if ( NULL == realloc(devices[dev_index].psbuf, tc->bufptr + 1) )
          {
            dosyslog(LOG_ERR, "gtcpanswer: realloc fail on %d + %d", devices[dev_index].psbuf_size, tc->bufptr + 1);
            exit(1);
          }
        } // small buf

        memcpy(devices[dev_index].psbuf + devices[dev_index].psbuf_ptr, tc->buf, tc->bufptr);
        devices[dev_index].psbuf_ptr += tc->bufptr;

        memcpy(devices[dev_index].psbuf + devices[dev_index].psbuf_ptr, "\n", 1);
        devices[dev_index].psbuf_ptr += 1;
      }// while(needlines)

      answer_len = 0;
      break;
    }
    //++++++++++++++++++++++++++++++++++++++++++++
    // send back to peer it's saved state
    else if ( tc->cmdcode == CMDCODE_LDPSTATE )
    {
      answer_len = devices[dev_index].psbuf_ptr;
      answer_ptr = (char*)devices[dev_index].psbuf;
    }
    //++++++++++++++++++++++++++++++++++++++++++++
    else if ( tc->cmdcode == CMDCODE_MONITOR )
    {
      add_debug_handle(ap_tcp_connections[tcp_conn_idx].fd);
      s = strsep(&nexttokenptr, " \t");

      if ( s != NULL )
        debug_level = atoi(s);
    }


    break;
  } //for(;;)

  tc->state = TC_ST_OUTPUT;

  ap_tcp_conn_send(tcp_conn_idx, std_answers[exec_status], strlen(std_answers[exec_status]));

  if (debug_level)
    debuglog("* debug: standard answer: %s\n", std_answers[exec_status]);

  if (exec_status == SA_OK && answer_len > 0)
  {
    ap_tcp_conn_send(tcp_conn_idx, answer_ptr, answer_len);
    if(debug_level > 9)
    {
      debuglog("* debug: answer2:");
      memdump(answer_ptr, answer_len); debuglog("\n");
    }
  }

  fsync(tc->fd);

  if ( ! is_debug_handle(ap_tcp_connections[tcp_conn_idx].fd) )
    ap_tcp_close_connection(tcp_conn_idx, NULL);

  devices[dev_index].tcpconn = NULL;

  tc->state = TC_ST_READY;
}
