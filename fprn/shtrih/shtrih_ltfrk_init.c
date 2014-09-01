/** \file shtrih_ltfrk_init.c
 * \brief Fiscal printers daemon's driver for Shtrih-FR-K printer - procedure for initializing the printer hardware
 *
 * V1.200. Written by Andrej Pakhutin
 *
 * ! This file is being #include-d into shtrih_ltfrk.c just to avoid Makefile hell
****************************************************/
#define SHTRIH_LTFRK_INIT_C
/*#include <fcntl.h>
#include <time.h>
#include "shtrih_ltfrk.h"
#include "shtrih_answer_timeouts.h"
#include "shtrih_flags.h"
*/

//===========================================================================
/** \brief Shtrih-FR-K driver. Method that is called to initialize that given hardware
 *
 * \param devid int - device id
 * \return int - 0 - OK, errcode otherwise
 *
 * according to manual the default speed is 4800 * but, looking at RMK soft it is clear that scan for actual speed is preferred idea,
 * but(!), at last RMK sets speed to 19200, so we assume that a preference #1
 *
 * So, we start at 19200 then 4800 and then 2400 up to 115200 sending ENQ
 * each_speed_trys tries at each speed with timeouts of try_timeout sec
 *
 * on power on device awaits for ENQ code (0x05) and answers with:
 * ACK(0x06) if busy with other command processing
 * NAK(0x0f) if ready to read new command
*/
int shtrih_ltfrk_port_init(int devid)
{
  struct termios tiop, testtio;
  struct t_device *dev;
  struct t_driver_data *dd;
  int n, speed_try, io_speed, splist_idx, answer_try;
  int ack_count;
  int errcode;

  dev = get_dev_by_id(devid);
  dd = dev->driver_data;
  dd->state = STATE_INIT;

  //------------------------------------------
  for (splist_idx = 0; ; ++splist_idx)
  {
    if ( 0 == dd->config_try_speeds[splist_idx] ) // end of list?
    {
      dosyslog(LOG_ERR, "!ERROR shtrih_ltfrk_port_init: out of tries (speed setting/ready check) printer id: %d, tty: %s", dev->id, dev->tty);

      process_config_options_speed((char*)default_speeds_list, &dd->config_try_speeds, NULL);  // re-setting to default speeds list

      dev->state = STATE_NEEDRECONNECT;

      return INITPORT_GENERALERROR;
    }

    io_speed = dd->config_try_speeds[splist_idx];

    if (debug_level) debuglog("\n\n########################################################\n* debug: init speed %d for printer id: %d, tty: %s\n", io_speeds_printable[io_speed], dev->id, dev->tty);

    if ( 0 != dev->fd )
      close(dev->fd);

    if ( ( -1 == (dev->fd = open(dev->tty, O_RDWR | O_NOCTTY | O_NONBLOCK)) ) )
    {
      dosyslog(LOG_ERR, "shtrih_ltfrk_port_init: open(%s): %m", dev->tty);

      dev->state = STATE_NEEDRECONNECT;

      return INITPORT_GENERALERROR;
    }

    //tcflush(dev->fd, TCIFLUSH);
    ioctl(dev->fd, TIOCMGET, &n);
    //n &= ~(TIOCM_RTS | TIOCM_DTR); ioctl(dev->fd, TIOCMSET, &n); sleep(1);
    n |= TIOCM_DTR; ioctl(dev->fd, TIOCMSET, &n);

    if ( -1 == tcgetattr(dev->fd, &tiop) )
    {
      dosyslog(LOG_ERR, "shtrih_ltfrk_port_init: tcgetattr %s: %m", dev->tty);
      dev->state = STATE_NEEDRECONNECT;
      return INITPORT_GENERALERROR;
    }

    memset(&tiop, 0, sizeof(tiop));

    cfmakeraw(&tiop);
    tiop.c_iflag &= ~(IXON | IXOFF | IXANY | INPCK); //| BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IEXTEN);
    //tiop.c_iflag |= (IGNBRK | IGNPAR);
    tiop.c_oflag &= ~OPOST;
    tiop.c_cflag &= ~(CSIZE | PARENB | CSTOPB);
    tiop.c_cflag |= (CS8 | CLOCAL | CREAD);
    tiop.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); //| CRTSCTS
    //tiop.c_cc[VTIME] = 10; //1 sec timeouts on non-canonical
    //tiop.c_cc[VMIN] = 0; //0 char min read on non-canonical
    cfsetispeed(&tiop, io_speeds[io_speed]);
    cfsetospeed(&tiop, io_speeds[io_speed]);
    /*
    options.c_cflag |= CLOCAL; // dont ignore control lines
    options.c_cflag |= CREAD;  // enable receiver
    options.c_cflag &= ~CSIZE; // 8 data bits
    options.c_cflag |= CS8;
    options.c_cflag &= ~PARENB;	// no parity
    options.c_cflag &= ~CSTOPB; // 1 stop-bit
    options.c_cflag |= HUPCL; // Lower modem control lines after last process closes the device (hang up).
    options.c_iflag &= ~INPCK; // no input parity check
    options.c_iflag &= ~( IXON | IXOFF | IXANY );
    //options.c_iflag = IGNBRK | IGNPAR; //Ignore BREAK condition on input and parity errors.
    options.c_lflag &= ~( ICANON | ECHO | ECHOE | ISIG ); // raw mode
    options.c_oflag &= ~OPOST;
    cfsetispeed( &options, this->baudrate ); // speed
    cfsetospeed( &options, this->baudrate );
    tcsetattr( port, , &options );
    */

    if ( -1 == tcsetattr(dev->fd, TCSAFLUSH /*TCSADRAIN*/, &tiop) )
    {
      dosyslog(LOG_ERR, "shtrih_ltfrk_port_init: tcsetattr %s: %m", dev->tty);

      dev->state = STATE_NEEDRECONNECT;

      return INITPORT_GENERALERROR;

    }

    if ( -1 == tcgetattr(dev->fd, &testtio) )
    {
      dosyslog(LOG_ERR, "shtrih_ltfrk_port_init: tcgetattr %s: %m", dev->tty);

      dev->state = STATE_NEEDRECONNECT;

      return INITPORT_GENERALERROR;
    }

    if ( testtio.c_iflag != tiop.c_iflag || testtio.c_oflag != tiop.c_oflag ||
         testtio.c_cflag != tiop.c_cflag || testtio.c_lflag != tiop.c_lflag )
    {
      dosyslog(LOG_ERR, "shtrih_ltfrk_port_init: c_Xflag(s) set wrong for %s", dev->tty);

      dev->state = STATE_NEEDRECONNECT;

      return INITPORT_GENERALERROR;
    }

    //------------------------------------------
    dd->state = STATE_SPEEDSET;
    ack_count = 0;

    for (speed_try = 0; speed_try < each_speed_tries; ++speed_try)
    {
      if (debug_level > 5) debuglog("\n\n-------------------------------------------------\n* debug: speed try %d\n", speed_try + 1);

      tcflush(dev->fd, TCIFLUSH);

      // sending ENQ. let printer acknowledge link
      dd->buf[0] = CODE_ENQ;

      if (1 != write_bytes(dev, dd->buf, 1, "shtrih_ltfrk %s: try %d at %d - write ENQ: %m", dev->tty, speed_try, io_speeds_printable[io_speed]))
        break;

      if (debug_level > 5) debuglog("* debug: wait for ACK/NAK\n");

      //------------------------------------------
      for (answer_try = 0; answer_try < 5; ++answer_try)
      {
        dev->buf_ptr = 0;
        n = read_bytes(dev, 1, standard_answer_timeout);

        if (n < 0)
        {
          dosyslog(LOG_ERR, "shtrih_ltfrk_port_init: read(): %m");
          break; // it's an error - try next speed then
        }

        if (n == 0)
          break; // timeout - silent next try

        if ( *(dev->buf) == CODE_ACK )
        {
          dd->state = STATE_BUSY; // init OK, but still busy on other task

          dosyslog(LOG_NOTICE, "shtrih_ltfrk_port_init: printer %d is busy on previous command-skipping data", dev->id);

          if ( ++ack_count == 1 ) // kinda it's more busy on itself. trying to break the peace nicely
          {
            read_answer(dev, standard_answer_timeout); // try to process old output
          }
          else // trying more powerful spells
          {
            dosyslog(LOG_ERR, "!ERROR shtrih_ltfrk_port_init: printer %d cycling on previous answer", dev->id);

            if ( ack_count % 2 == 1 ) // on odd tries we act by the specs
            {
              read_answer(dev, standard_answer_timeout);
            }
            else // on even tries we gourge on packet. but answering with NAK to loop againg
            {
              read_answer3(dev, standard_answer_timeout, CODE_NAK);
              //dd->buf[0] = CODE_ACK; write_bytes(dev, dd->buf, 1, NULL);
            }

            usleep(500000);
            dd->buf[0] = CODE_ENQ;

            write_bytes(dev, dd->buf, 1, NULL);
          }// if (ack_count)

          if (debug_level > 5) debuglog("\n!-!-!-!-!-!-!-!-! loop !-!-!-!-!-!-!-!-!-!-!\n");

          continue;
        }
        else if( *(dev->buf) == CODE_NAK )
        {
          dd->state = STATE_READY;
        }
        else
        {
          dosyslog(LOG_ERR, "!ERROR shtrih_ltfrk_port_init : printer returned invalid response code: %#x, dev: %d", *(dev->buf), dev->id);

          continue; // next try
        }

        if (debug_level > 9) memdump(dev->buf, n);
        if (debug_level > 5) debuglog("* debug: port init done, getting printer status\n");

        dev->state = STATE_READY;
        dd->connected_speed = io_speed;

        errcode = shtrih_ltfrk_get_state(dev->id);

        if ( ! errcode )
          send_command_fmt(dev, "\x13%c%c%c%c", dd->admin_password[0], dd->admin_password[1],dd->admin_password[2], dd->admin_password[3]); // beep!!!

        return errcode;
      } // for(answer_try

      dosyslog(LOG_ERR, "shtrih_ltfrk_port_init: Out of tries on printer answer read id: %d, tty: %s, speed: %d", dev->id, dev->tty, io_speeds_printable[io_speed]);
    } // for(speed_try)
  } //for(splist_idx)

  dev->state = STATE_NEEDRECONNECT;

  return INITPORT_GENERALERROR; // really we shouldn't be here ever
}
