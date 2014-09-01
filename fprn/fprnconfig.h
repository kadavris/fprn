#ifndef FPRNCONFIG_H
#define FPRNCONFIG_H

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include "../libs/ap_log.h"
#include "../libs/ap_str.h"
#include "../libs/ap_tcp.h"
#include "../libs/ap_utils.h"

// common stuff

// driver states
#define STATE_INIT     0 // initialization
#define STATE_SPEEDSET 1 // --"--
#define STATE_READY    2 // ready for the new task
#define STATE_CMDSENT  3 // command sent to printer, but answer not arrived yet
#define STATE_BUSY     4 // processing something
#define STATE_NEEDRECONNECT 5 // connection error happened. driver should attempt to reconnect
#define STATE_ERROR    6 // serious I/O error happened

// truly generous amount
#define MAXDEVS 2

// tcp conn statuses (mostly internal for TCPAnswer() func)
#define TC_ST_READY  0
#define TC_ST_BUSY   1
#define TC_ST_DATAIN 2
#define TC_ST_OUTPUT 3

//----------------------------------------------------------------------
// these defines should match the device_types[].type field in fprnconfig.c
#define DEVICE_TYPE_MARIA301      1
#define DEVICE_TYPE_SHTRIH_LTFRK  2
#define DEVICE_TYPE_INNOVA        3

typedef struct t_device_type
{
  int type; // internal type-id. see above
  char *configtype; // 'device' config line type string
  char *description; // human-readable description
  int (*func_port_init)(int devid); // ptr to device initializatin function
  int (*func_get_state)(int devid); // ptr to device state query function
  int (*func_send_command)(int devid, char *data, size_t size); // ptr to function that send enquiries to device
} t_device_type;

typedef struct t_device
{
  char *tty;
  int fd; // opened tty file descriptor or 0
  int id; // human-configured, numeric ID != 0
  struct t_device_type const *device_type;
  unsigned state; //current state. connected, fault etc. see STATE_*
  struct timeval next_attempt; // universal due time for the next attempt of anything ;). used mainly to prevent every other second re-init attemts in cases of errors
  /* IO buffer (actually this is _only_ for combed printer's output.
     The commands and other data being sent _to_ printer
     must be stored in driver's data block as it is internal matters only.
  */
  unsigned char *buf; // IO buffer. should be only used for output data from printer
  int buf_size, buf_ptr; // buf size/current index

  void *driver_data; // driver's data block. depends on printer model. set on port init procedure call
  struct ap_tcp_connection_t *tcpconn; // ptr to the tcp connection associated with this dev. NULL if none. used to extend timeouts on long jobs, etc

  unsigned char *psbuf; // saved PHP class data if not NULL
  int psbuf_size, psbuf_ptr; // PHP save
} t_device;

#define INITPORT_GENERALERROR -1;

#define max_io_speeds_index 21

extern char *compiled_time;

#define NEXT_TOKEN_OPTIONAL 1
#define NEXT_TOKEN_REQUIRED 0

//----------------------------------------------------------------------
#ifndef FPRNCONFIG_C

/* reads default or provided config file and returns error code */
extern void getconfig( int argc, char **argv );
extern char *config_parse_get_next_token(int optional);
extern char *config_parse_remaining_arg(void);
extern int config_parse_get_bool(void);

extern struct t_device *get_dev_by_id(int id);
extern int dev_idx_by_id(int id);

extern struct sockaddr_in bind_sock;
extern int bind_retries, bind_retry_sleep;

extern int devices_count;
extern struct t_device devices[MAXDEVS];
extern int poll_freq;

extern const int device_types_count;
extern struct t_device_type *device_types;
extern int daemonize;

extern char *pidfile;

extern const int io_speeds[max_io_speeds_index + 1];
extern const int io_speeds_printable[max_io_speeds_index + 1];

#endif

#endif
