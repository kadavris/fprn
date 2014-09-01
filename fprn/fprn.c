/** \file fprn.c
* \brief Fiscal printers daemon's main module
*
* V1.200. Written by Andrej Pakhutin
* WARNING! This all is old code that is unsupported by me
****************************************************/
#define FPRN_C
#include "fprnconfig.h"
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

//************ Prototypes ***************
void timer_event(int a); // sigalarm
void open_ports(void); // initializes the devices
extern void tcp_answer(int idx); // answering PHP side inquiries. idx is dev index
void tcp_close_connection(int idx, char *msg); // close tcp connection by index, msg !=NULL to post some answer before close

//=======================================================================
int main(int argc, char **argv)
{
  struct itimerval timer_val;
  int lsock; // listener socket
  int n;
  FILE *fpidf; // /var/run/PID
  struct sigaction sigact;

  openlog(NULL, LOG_PID, LOG_DAEMON);
  getconfig(argc, argv);

  if (debug_level)
    debuglog("Initializing ports\n");

  open_ports();

  if ( -1 == ( lsock = socket( AF_INET, SOCK_STREAM, 0 ) ) )
  {
    dosyslog(LOG_ERR, "socket(): %m");
    exit(1);
  }

  for(;;)
  {
    if ( -1 != bind( lsock, (struct sockaddr *)&bind_sock, sizeof(bind_sock) ) )
      break; // bind OK

    if ( --bind_retries == 0 )
    {
      dosyslog(LOG_ERR, "bind(): %m");
      exit(1);
    }

    dosyslog(LOG_ERR, "bind(): %m: retries left: %d, sleeping for %d sec(s)", bind_retries, bind_retry_sleep);

    sleep(bind_retry_sleep);
  }

  //*******************************************

  if ( daemonize && (debug_to_tty == 0) ) // daemonizing
  {
    //n = getpid();
    ioctl(fileno(stdin), TIOCNOTTY);

    if ( 0 != (n = fork()) )
    {
      if ( NULL == (fpidf = fopen(pidfile, "w")) )
      {
        dosyslog(LOG_ERR, "fopen(%s): %m", pidfile);
        exit(1);
      }

      fprintf(fpidf, "%d", n);
      fclose(fpidf);

      exit(0);
    }
  }

  signal(SIGALRM, timer_event);
  signal(SIGHUP, SIG_IGN); // maybe should re-read config on it

  sigact.sa_handler = ap_tcp_check_conns;
  sigemptyset(&sigact.sa_mask);
  sigact.sa_flags = 0;

  if ( -1 == sigaction(SIGPIPE, &sigact, NULL)) // stop terminate on broken tcp conn
  {
    dosyslog(LOG_ERR, "sigaction PIPE: %m");
    exit(1);
  }

  if (debug_level) debuglog("Entering main loop\n");

  // setting timer for looking at activity on ports
  timer_val.it_interval.tv_sec = 0;
  timer_val.it_interval.tv_usec = 0;
  timer_val.it_value.tv_sec = 0;
  timer_val.it_value.tv_usec = poll_freq;
  setitimer(ITIMER_REAL, &timer_val, NULL);

  //-------------------------------------------
  // tcp listener loop
  for(;;)
  {
    int tcpci; // conn index

    if ( listen( lsock, ap_tcp_max_connections ) )
    {
      dosyslog(LOG_ERR, "listen(): %m");
      exit(1);
    }

    for (tcpci = 0; tcpci < ap_tcp_max_connections; ++tcpci) // finding free slot
      if (ap_tcp_connections[tcpci].fd == 0)
        break;

    if (tcpci == ap_tcp_max_connections)
      continue; // no free slots - loop to wait

    if ( ! ap_tcp_accept_connection(lsock) )
      continue;

    ap_tcp_connections[tcpci].state = TC_ST_READY;
  } // for(;;) - listen...

  closelog();
  return 0;
}

//=======================================================================
/** \brief Initial attempt to connect to devices
 *
 * \param void
 * \return void
 *
 * Calls initialization for all configured devices
*/
void open_ports (void)
{
  int i, errcode;

  for ( i = 0; i < devices_count; ++i )
  {
    errcode = devices[i].device_type->func_port_init(devices[i].id);
    // if there is error init printer then we'll try to reconnect in the background

    if (errcode == 0)
    {
      if (debug_level) debuglog("port %s initialized\n", devices[i].tty);
    }
    else devices[i].state = STATE_NEEDRECONNECT;
  }
}

//=======================================================================
//=======================================================================
//=======================================================================
/** \brief Called periodically to process data on serial ports
 *
 * \param a int - dummy for sigalarm compatibility
 * \return void
 *
 * Queries configured serial ports for new data coming from printers.
 * Resets printer's state in case of comm errors.
 * After checking the printers, looks at opened TCP connections
 * processing expiration and errors.
*/
void timer_event(int a)
{
static int rotator = 0;
int i, ii, n, errcode;
fd_set fds;
struct timeval tv;
struct itimerval timer_val;

  // checking devices state
  for ( i = 0; i < devices_count; ++i )
  {
    for (n = 0; n < ap_tcp_max_connections; ++n) // do not try to re-init stale printer if there is connections active
      if ( ap_tcp_connections[n].fd != 0 )
        break;

    gettimeofday(&tv, NULL);

    if (devices[i].state == STATE_NEEDRECONNECT && timercmp(&tv, &devices[i].next_attempt, >= ))
    {
      dosyslog(LOG_NOTICE, "fprn timer_event: re-init of dev %d (%s) requested", devices[i].id, devices[i].tty);

      errcode = devices[i].device_type->func_port_init(devices[i].id);

      if (errcode == 0)
      {
        if (debug_level) debuglog("port %s re-initialized\n", devices[i].tty);
      }
      else
      {
        devices[i].state = STATE_NEEDRECONNECT;
      }

      gettimeofday(&devices[i].next_attempt, NULL);

      devices[i].next_attempt.tv_sec += 9;
    }
  } // for ( i = 0; i < devices_count; ++i )

  /*++++++++++++++++++++++++++++++++++++++++++++++++++
    checking TCP connections for activity and timeouts
  */
  FD_ZERO(&fds);
  n = 0;

  for (i = 0; i < ap_tcp_max_connections; ++i)
  {
    if ( ap_tcp_connections[i].fd == 0 )
      continue;

    gettimeofday(&tv, NULL);

    if ( timercmp(&tv, &ap_tcp_connections[i].expire, >=) )//session expired. closing
    {
      ap_tcp_close_connection(i, NULL/*"\n401 Session Expired\n"*/);

      for(ii = 0; ii < devices_count; ++ii)
      {
        if (devices[ii].tcpconn == &ap_tcp_connections[i] )
        {
          devices[ii].tcpconn = NULL;
          break;
        }
      }

      if (debug_level)
        debuglog("\n%d Session Expired\n", i);

      continue;
    }

    FD_SET(ap_tcp_connections[i].fd, &fds);

    if ( n < ap_tcp_connections[i].fd ) n = ap_tcp_connections[i].fd + 1;
  }

  tv.tv_sec = 0;
  tv.tv_usec = 0;

  n = select(n, &fds, NULL, NULL, &tv);

  if (n < 0)
    dosyslog(LOG_ERR, "select(): %m");

  if (n <= 0)
    goto end; // error or no data available yet

  // running answering machine
  for (i = 0; i < ap_tcp_max_connections; ++i)
  {
    if ( ap_tcp_connections[i].fd != 0 && FD_ISSET(ap_tcp_connections[i].fd, &fds) )
      tcp_answer(i);
  }

end:
  if (debug_to_tty && debug_level)
  {
    fprintf(stderr,"%c\r", "/-\\|"[rotator]);
    if (++rotator == 4) rotator = 0;
  }

  // ...and in the end... plan next call
  //signal(SIGALRM, timer_event);
  timer_val.it_interval.tv_sec = 0;
  timer_val.it_interval.tv_usec = 0;
  timer_val.it_value.tv_sec = 0;
  timer_val.it_value.tv_usec = poll_freq;

  if ( -1 == setitimer(ITIMER_REAL, &timer_val, NULL))
  {
    dosyslog(LOG_ERR, "setitimer(): %m");
    exit(1);
  }
}
