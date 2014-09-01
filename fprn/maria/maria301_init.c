/** \file maria301.c
* \brief Fiscal printers daemon's driver for maria301 printer state query module
*
* V1.000a. Written by Andrej Pakhutin
*
* ! This file is being #include-d into maria301.c
* just to avoid Makefile hell
****************************************************/
#define MARIA301_PORT_INIT_C

//===========================================================================
/** \brief Maria301 driver's external method for initializing hardware
 *
 * \param devid int - device id
 * \return int - boolean success
 *
*/
int maria301_port_init(int devid);
{
  struct termios tiop;
  struct t_device *dev;
  struct t_driver_data *dd;
  int init_try, i, n;
  int errcode = 0;

  dev = &devices[devid]; // making shortcut

  dd->state = STATE_INIT;

  //------------------------------------------
  memset(&tiop, 0, sizeof(tiop));

  cfmakeraw(&tiop);
  cfsetispeed(B115200, &tiop);
  cfsetospeed(B115200, &tiop);

  tiop.c_iflag |= (IGNBRK | IGNPAR | ISTRIP);
  //tiop.c_oflag =
  tiop.c_cflag &= ~CSIZE; // clear size
  tiop.c_cflag |= (CLOCAL | CREAD | CS8 | CSTOPB | PARENB | CRTSCTS); // 8E2 by manual
  //tiop.c_lflag =

  if ( ( -1 == tcsetattr(dev->fd, TCSANOW, &tiop) ) )
  {
    dosyslog(LOG_ERR, dev->tty); perror(": tcsetattr");

    return(INITPORT_GENERALERROR);
  }

  //------------------------------------------
  dd->state = STATE_SPEED_SET;

  for (init_try = 0; ; ++init_try)
  {
    errcode = 0;

    if (init_try == 5) // 5 trys to init
    {
      dosyslog(LOG_ERR, "!ERROR: maria301(%d:%s): out of tries (speed setting/ready check)\n", dev->id, dev->tty);

      return(INITPORT_GENERALERROR);
    }

    if (debugLevel)
      debuglog("* debug: maria301(%d:%s): init try %d\n", dev->id, dev->tty, init_try);

    // setting DTR to on
    ioctl(dev->fd, TIOCMGET, &st);
    st |= TIOCM_DTR;
    ioctl(dev->fd, TIOCMSET, &st);
    usleep(1000000);

    // checking for CTS/DSR signal raise = printer ready
    // quote from manual: "init can be delayed by 10 sec if there is too many Z-reports records stored in device"
    // so sleep for 500 msec between checks * 30 trys = 15 sec total
    for ( i = 1; 0 == (st & (TIOCM_CTS | TIOCM_DSR)); ++i )
    {
      if ( i == 30 )
      {
        dosyslog(LOG_ERR, "!ERROR: maria301(%d:%s): timeout waiting for ready signal from printer\n", dev->id, dev->tty);

        errcode = INITPORT_GENERALERROR;
        break;
      }

      usleep(500000);

      ioctl(dev->fd, TIOCMGET, &st);
    }

    if (errcode > 0) // re-init
    {
      // DTR drop - disabled state
      st &= ~TIOCM_DTR;
      ioctl(dev->fd, TIOCMSET, &st);

      usleep(3500000); //3.5 sec (3 sec minimum by manual)

      continue;
    }

    // skipping leftover garbage
    dev->buf_ptr = 0;
    read_bytes(dev, dev->buf_size, 1000);

    if (debugLevel)
      debuglog("* debug: maria301: speed setup\n");

    //------------------------------------------
    // sending two 'U's to let printer acknowledge data speed
    for (i = 1; i <= 2; ++i)
    {
      if (1 != write(dev->fd, "U", 1) )
      {
        dosyslog(LOG_ERR, "maria301(%d:%s): write (speed set %d pass): %m", dev->id, dev->tty, i);

        errcode = INITPORT_GENERALERROR;
        break;
      }

      usleep(20000); // 20 msec (1 msec minimum wait)
    }

    if (errcode > 0)
      continue; // re-init

    errcode = 0;

    for (i = 0; ; ++i)
    {
      if (i == 30)
      {
        dosyslog(LOG_ERR, "!ERROR: maria301(%d:%s): timeout for self-init of printer\n", dev->id, dev->tty);
        errcode = INITPORT_GENERALERROR;
        break;
      }

      ioctl(dev->fd, TIOCMGET, &st);

      if ( st & (TIOCM_CTS | TIOCM_DSR) ) // printer ready to finish init?
        break;

      usleep(500000); // =500 msec
    }

    if (errcode > 0)
      continue; // re-init

    if (debugLevel)
      debuglog("* debug: maria301(%d:%s): wait for READY answer\n", dev->id, dev->tty);

    //------------------------------------------
    // checking for "READY" answer = printer ready (really! ;)
    // timeout = 3000 msec
    errcode = 0;
    dev->buf_ptr = 0;

    n = read_std_answer(dev, 3000);

    if (n == 0)
    {
      dosyslog(LOG_ERR, "!ERROR: maria301(%d:%s): timeout for final 'READY' answer from printer\n", dev->id, dev->tty);
      errcode = INITPORT_GENERALERROR;
      continue;
    }
    else if ( n < 0 )
    {
      dosyslog(LOG_ERR, "!ERROR: maria301(%d:%s): read error (%m) on 'READY' answer of printer\n", dev->id, dev->tty);
      errcode = INITPORT_GENERALERROR;
      continue;
    }

    // check if busy on other task.
    if ( 0 == memcmp(dev->buf + 1, "WAIT", 4) || 0 == memcmp(dev->buf + 1, "WRK", 3) || 0 == memcmp(dev->buf + 1, "PRN", 3) )
    {
      dosyslog(LOG_ERR, "!WARNING: maria301(%d:%s): printer tells it's busy. retrying.\n", dev->id, dev->tty);
      // we've got some meaningful answer. So printer is initialized. just waiting here a little to let it do pending task
      usleep(3000000);
    }
    else if ( 0 != memcmp(dev->buf + 1, "READY", 5) )
    {
      // still not 'READY' = error
      dev->buf[6] = '\0';
      dosyslog(LOG_ERR, "!ERROR: maria301(%d:%s): not 'READY' answer (got: '%s') on printer init\n", dev->buf, dev->id, dev->tty);
      errcode = INITPORT_GENERALERROR;
      continue;
    }

    dd->state = STATE_READY;

    send_command(dev, "CSIN%d", dd->use_crc); // set CRC generation/processing

    break; // init done: all seems OK

  } // speed set loop

  if (debugLevel)
    debuglog("* debug: maria301(%d:%s): init done\n", dev->id, dev->tty);

  if (errcode > 0)
    return errcode;

  return maria301_get_state(devid);
}
