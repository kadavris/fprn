/*
  aptcp.c: sockets manipulation and alike functions. written by Andrej Pakhutin for his own use primarily.
*/
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define AP_TCP_C
#include "ap_tcp.h"
#include "ap_str.h"
#include "ap_log.h"

int ap_tcp_max_connections = 3;   // default max tcp connections to maintain
struct timeval max_tcp_conn_time;   // max tcp connection stall time. forced close after that)
ap_tcp_connection_t *ap_tcp_connections = NULL; // malloc'd on config read when 'ap_tcp_max_connections' is known
int ap_tcp_conn_count = 0; // current connections count

struct ap_tcp_stat_t ap_tcp_stat; // statistics companion

//=================================================================
void ap_tcp_connection_module_init(void)
{
  int i;


  ap_tcp_connections = getmem(ap_tcp_max_connections * sizeof(struct ap_tcp_connection_t), "malloc on ap_tcp_connections");

  for (i = 0; i < ap_tcp_max_connections; ++i)
  {
    ap_tcp_connections[i].fd = 0;
    ap_tcp_connections[i].bufptr = 0;
    ap_tcp_connections[i].bufsize = 1024;
    ap_tcp_connections[i].buf = getmem(ap_tcp_connections[i].bufsize, "malloc on ap_tcp_connections.buf");
  }

  ap_tcp_stat.conn_count = 0;
  ap_tcp_stat.active_conn_count = 0;
  ap_tcp_stat.timedout = 0;
  ap_tcp_stat.queue_full_count = 0;
  ap_tcp_stat.total_time.tv_sec = 0;
  ap_tcp_stat.total_time.tv_usec = 0;
}

//=================================================================
static int tcprecv(int sh, void *buf, int size)
{
  if ( -1 != fcntl(sh, F_GETFL) )
    return recv(sh, buf, size, MSG_DONTWAIT);

  if (debug_level > 9)
    debuglog("! tcprecv: sock %d recv error: %m\n", sh);

  return -1;
}

//=================================================================
static int tcpsend(int sh, void *buf, int size)
{
  if ( -1 != fcntl(sh, F_GETFL) )
    return send(sh, buf, size, MSG_DONTWAIT);

  if (debug_level > 9)
    debuglog("! tcpsend: sock %d send error: %m\n", sh);

  return -1;
}

//=================================================================
int ap_tcp_conn_recv(int conn_idx, void *buf, int size)
{
  int n;

  errno = 0;

  if ( 0 == ap_tcp_connections[conn_idx].fd )
  {
    dosyslog(LOG_ERR, "! connrecv() on closed fd");
    return -1;
  }

  n = tcprecv(ap_tcp_connections[conn_idx].fd, buf, size);

  if ( (n == -1 && errno == EPIPE) || n == 0)
  {
    if (debug_level)
      debuglog("? TCP Connection [%d] is dead prematurely\n", conn_idx);

    ap_tcp_close_connection(conn_idx, NULL);
  }

  return n;
}

//=================================================================
int ap_tcp_conn_send(int conn_idx, void *buf, int size)
{
  int n;

  if ( 0 == ap_tcp_connections[conn_idx].fd )
  {
    dosyslog(LOG_ERR, "! connrecv() on closed fd");
    return 0;
  }

  n = tcpsend(ap_tcp_connections[conn_idx].fd, buf, size);

  if ( (n == -1 && errno == EPIPE) || n == 0)
  {
    if (debug_level)
      debuglog("? TCP Connection #%d is dead prematurely: %m\n", conn_idx);

    ap_tcp_close_connection(conn_idx, NULL);
  }
  return n;
}

//=======================================================================
void ap_tcp_close_connection(int conn_idx, char *msg) // close tcp connection by index, msg !=NULL to post some answer before close
{
  struct timeval tv;
  int dh;

  if ( 0 == ap_tcp_connections[conn_idx].fd )
    return;

  dh = is_debug_handle(ap_tcp_connections[conn_idx].fd);
  remove_debug_handle(ap_tcp_connections[conn_idx].fd);

  if ( msg != NULL )
    ap_tcp_conn_send(conn_idx, msg, strlen(msg));

  close(ap_tcp_connections[conn_idx].fd);

  ap_tcp_connections[conn_idx].fd = 0;
  --ap_tcp_conn_count;

  if ( ! dh ) // debug connections will not count for execution time
  {
    gettimeofday(&tv, NULL);
    timersub(&tv, &ap_tcp_connections[conn_idx].created_time, &tv);
    timeradd(&ap_tcp_stat.total_time, &tv, &ap_tcp_stat.total_time);
  }

  if ( debug_level > 0 )
    debuglog("* TCP conn [%d] closed\n", conn_idx);
}

//=======================================================================
int ap_tcp_accept_connection(int list_sock) // accepts new connection and adds it to the list
{
  int tcpci, n, new_sock;


  for (tcpci = 0; tcpci < ap_tcp_max_connections; ++tcpci) // finding free slot
    if (ap_tcp_connections[tcpci].fd == 0)
      break;

  if (tcpci == ap_tcp_max_connections)
  {
     ++ap_tcp_stat.queue_full_count;

     if (debug_level)
       debuglog("? Conn list is full. dumping new incoming\n");

     return 0;
  }

  n = sizeof(ap_tcp_connections[tcpci].addr);

  if ( -1 == (new_sock = accept(list_sock, (struct sockaddr *)&ap_tcp_connections[tcpci].addr, (socklen_t*)&n)) )
  {
    dosyslog(LOG_ERR, "! accept: %m");
    return 0;
  }

  /* setting non-blocking connection.
     data exchange will be performed in the timer_event() called by alarm handler
  */
  fcntl(new_sock, F_SETFL, fcntl(new_sock, F_GETFL) | O_NONBLOCK);

  ap_tcp_connections[tcpci].fd = new_sock;
  ap_tcp_connections[tcpci].idx = tcpci;

  gettimeofday(&ap_tcp_connections[tcpci].created_time, NULL);
  timeradd(&ap_tcp_connections[tcpci].created_time, &max_tcp_conn_time, &ap_tcp_connections[tcpci].expire);

  ap_tcp_connections[tcpci].bufptr = 0;
  ap_tcp_connections[tcpci].state = TC_ST_READY;
  ++ap_tcp_conn_count;

  ++ap_tcp_stat.conn_count;
  ap_tcp_stat.active_conn_count += ap_tcp_conn_count;

  if (debug_level) debuglog("* Got connected ([%d])\n", tcpci);

  return 1;
}

//=======================================================================
int ap_tcp_check_state(int conn_idx)
{
  struct timeval tv;
  fd_set fdr, fdw, fde;
  int n;


  if(conn_idx < 0 || conn_idx >= ap_tcp_max_connections)
    return -1;

  int fd = ap_tcp_connections[conn_idx].fd;

  if( fd == 0 )
    return -1;

  n = send(fd, &n, 0, 0);

  if ( n == -1 && errno == EPIPE )
  {
    if (debug_level > 10)
      debuglog("- ap_tcp_check_state(%d): send(): %d/%m\n", fd, n);

    return 4;
  }

  /*
  upd: maybe we should use epoll here?
       or ivykis - library for asynchronous I/O readiness notification

  according to the mans what's down below is total bullshit.
  you always have to monitor for EPIPE and EAGAIN errors
  and, possibly for 0 bytes read return as eof() indication specifically.
  The read and write states on non-blocked socket are always ON.
  */
  FD_ZERO(&fdr); FD_ZERO(&fdw); FD_ZERO(&fde);
  FD_SET(fd, &fdr); FD_SET(fd, &fdw); FD_SET(fd, &fde);

  n = fd + 1;
  tv.tv_sec = tv.tv_usec = 0;

  n = select(n, &fdr, &fdw, &fde, &tv);

  if ( n <= 0 ) return n;

  if ( FD_ISSET(fd, &fdr) ) n = 1;
  if ( FD_ISSET(fd, &fdw) ) n |= 2;
  if ( FD_ISSET(fd, &fde) ) n |= 4;
  //if (debug_level > 10 && debug_to_tty) fprintf(stderr, "- ap_tcp_check_state(%d): %d\n", fd, n);

  return n;
}

//=======================================================================
int ap_tcp_connection_is_alive(int conn_idx)
{
  int state = ap_tcp_check_state(conn_idx);

  if(state == -1 || 0 != (state & 4))
    return 0;

  return 1;
}

//=======================================================================
// used as sigaction() EPIPE handler to prevent dumping when connection dropped unexpectedly
void ap_tcp_check_conns(int a)
{
  int i,n;

  for (i = 0; i < ap_tcp_max_connections; ++i)
  {
    if ( ap_tcp_connections[i].fd == 0 )
      continue;

    n = ap_tcp_check_state(i);

    if ( n != -1 && ((n & 4) == 0) )
      continue;

    ap_tcp_close_connection(i, NULL);

    if (debug_level)
      debuglog("\n? [%d] Session dropped/error\n", i);
  }
}

//=======================================================================
void ap_tcp_print_stat(void)
{
  long n;

  n = ap_tcp_stat.active_conn_count * 100u / ap_tcp_stat.conn_count;

  debuglog("\n# aptcp: total conns: %u, avg: %u.%02u, t/o count: %u, queue full: %u times\n",
    ap_tcp_stat.conn_count, n/100u, n%100u, ap_tcp_stat.timedout, ap_tcp_stat.queue_full_count);

  n = ap_tcp_stat.total_time.tv_sec * 1000000000u + ap_tcp_stat.total_time.tv_usec;

  debuglog("# aptcp: total time: %ld sec, avg per conn: %ld.%03ld\n", ap_tcp_stat.total_time.tv_sec,
    n / 1000000000u, n % 1000000000u);
}
