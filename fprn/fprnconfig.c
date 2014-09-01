/** \file fprnconfig.c
* \brief Fiscal printers daemon's configuration storage and processing module
*
* V1.200. Written by Andrej Pakhutin
****************************************************/
#define FPRNCONFIG_C
#include "fprnconfig.h"

const int DEFAULTADDR = INADDR_LOOPBACK; // TCP listener default addr
const int DEFAULTPORT = 2011;            // TCP listener default port
struct sockaddr_in bind_sock;
int bind_retries, bind_retry_sleep;

const char *DEFAULTCONFIGFILE = "/etc/fprn/fprn.conf";
char *CONFIGFILE = NULL;
const char *default_pid_file = "/var/run/fprn.pid";
char *pidfile = NULL;

int history_lock = 0; // simple varlock

// devices
int devices_count; // attached devices count
struct t_device devices[MAXDEVS];

int poll_freq = 100000; // sleep time between polls (us)

int daemonize = 0;

//#define max_io_speeds_index XXX - in fprnconfig.h
const int io_speeds[max_io_speeds_index + 1] =
{ 
  B1200, B1800, B2400, B4800, B9600, B19200, B38400, B57600, B115200,
  B230400, B460800, B500000, B576000, B921600, B1000000, B1152000, B1500000, B2000000, B2500000, B3000000, B3500000, B4000000
};

const int io_speeds_printable[max_io_speeds_index + 1] =   //for logging
{
   1200, 1800, 2400, 4800, 9600, 19200, 38400, 57600, 115200,
   230400,  460800,  500000,  576000,  921600,  1000000,  1152000,  1500000,  2000000,  2500000,  3000000,  3500000,  4000000
};

/*
  !!! For the following externs don't forget to add/change #defines with Id in .h!!!
*/

#ifdef DRIVER_MARIA301
extern int maria301_port_init(int devid);
extern int maria301_get_state(int devid);
extern int maria301_send_command(int devid, char *data, size_t size);
extern int maria301_register_device(int device_index);
extern int maria301_parse_options(int device_index, char *opt);
#endif
#ifdef DRIVER_SHTRIH_LTFRK
extern int shtrih_ltfrk_port_init(int devid);
extern int shtrih_ltfrk_get_state(int devid);
extern int shtrih_ltfrk_send_command(int devid, char *data, size_t size);
extern int shtrih_ltfrk_register_device(int device_index);
extern int shtrih_ltfrk_parse_options(int device_index, char *opt);
#endif
#ifdef DRIVER_INNOVA
extern int innova_port_init(int devid);
extern int innova_get_state(int devid);
extern int innova_send_command(int devid, char *data, size_t size);
extern int innova_register_device(int device_index);
extern int innova_parse_options(int device_index, char *opt);
#endif

const int device_types_count = 3; // !!! count of device definitions blocks. change if added or removed !!!

const struct t_device_type device_types[] =
{
  {
     DEVICE_TYPE_MARIA301, "maria301", "Maria 301MTM (firmware M301T7)",
#ifdef DRIVER_MARIA301
     maria301_port_init, maria301_get_state, maria301_send_command
#else
     NULL, NULL, NULL
#endif
  },

  {
     DEVICE_TYPE_SHTRIH_LTFRK, "shtrih_ltfrk", "Shtrih-Light-FR-K",
#ifdef DRIVER_SHTRIH_LTFRK
     shtrih_ltfrk_port_init, shtrih_ltfrk_get_state, shtrih_ltfrk_send_command
#else
     NULL, NULL, NULL
#endif
  },

  {
     DEVICE_TYPE_INNOVA, "innova", "Innova S.A. (PL) DF-1 FV",
#ifdef DRIVER_INNOVA
     innova_port_init, innova_get_state, innova_send_command
#else
     NULL, NULL, NULL
#endif
  }
};

//=============================================================================
/** \brief returns ptr to device data structure associated with given id
 *
 * \param id int - device id to find
 * \return struct t_device *
 *
*/
struct t_device *get_dev_by_id(int id)
{
int i;

  for (i = 0; i < devices_count; ++i)
    if ( devices[i].id == id )
      return &devices[i];

  return NULL;
}

/** \brief returns index of device data structure associated with given id
 *
 * \param id int - device id to find
 * \return int
 *
*/
int dev_idx_by_id(int id)
{
int i;

  for (i = 0; i < devices_count; ++i)
    if ( devices[i].id == id )
      return i;

  return -1;
}

//----------------------------------------------------------------------
/** \brief Outputs command line help to console
 *
 * \param void
 * \return void
*/
void help(void)
{
  printf("fprn daemon for fiscal printers. V1.200. Written by Andrej Pakhutin.\n%s\nusage:\n\
  fprn [options]\n\
  -h - this help\n\
  -d - daemonize\n\
  -f config_file - use this config (default: %s)\n\
  -v - debug mode. no daemonizing, stderr/debug channel(s) logging also\n\
  \nLasciate ogni speranza voi ch 'entrate\n",
  compiled_time, CONFIGFILE);
}

//----------------------------------------------------------------------
// these are for internal use of getconfig and nextarg
static char cfg_buf[1024];
static char *cfg_buf_ptr;
static int line, errors;

//----------------------------------------------------------------------
/** \brief Helper for config file processing. Returns next token from current config line
 *
 * \param optional int - if true then do not report error if token is missing
 * \return static char *
 *
 * Returns the next space-delimited substring from line buffer. Space and htab characters are used in processing.
 * Used within config file processing and probably while parsing "options" keywords in specific printer's driver
*/
char *config_parse_get_next_token(int optional)
{
  char *s;

  s = strsep(&cfg_buf_ptr, " \t");

  if ( ! optional && s == NULL )
  {
    printf("! ERROR at line %d: missing arg: %s\n", line, cfg_buf);
    ++errors;
  }
  //if (debug_level) debuglog("Token: '%s'\n", s);

  return s;
}

//----------------------------------------------------------------------
/** \brief Helper for config file processing. Returns the rest of config line buffer.
 *
 * \param void
 * \return char *
 *
 * Wrapper for returning all remaining content of line buffer.
 * Used within config file processing and probably while parsing "options" keywords in specific printer's driver
*/
char *config_parse_remaining_arg(void)
{
  return cfg_buf_ptr;
}

//----------------------------------------------------------------------
/** \brief Helper for config file processing. Returns the boolean representation of current token
 *
 * \param void
 * \return int - 0/1 if valid boolean token found and -1 if not
 *
 * Calls config_parse_get_next_token() and tries to interpret the returned value as a boolean.
 * Recognises on/off, 1/0, yes/no, true/false pairs. Case insensitive.
 * Used within config file processing and probably while parsing "options" keywords in specific printer's driver
*/
int config_parse_get_bool(void)
{
#define GET_BOOL_KW_COUNT 8
  struct
  {
    char *s;
    int result;
  } keywords[GET_BOOL_KW_COUNT] = {
    { "on", 1 },
    { "off", 0 },
    { "1", 1 },
    { "0", 0 },
    { "yes", 1 },
    { "no", 0 },
    { "true", 1 },
    { "false", 0 }
  };
  char *s;
  int i;

  s = config_parse_get_next_token(NEXT_TOKEN_REQUIRED);

  if ( s == NULL )
    exit(1);

  for( i = 0; i < GET_BOOL_KW_COUNT; ++i )
    if ( 0 == strcasecmp(keywords[i].s, s) )
      return keywords[i].result;

  return -1;
}

//----------------------------------------------------------------------
/** \brief Main procedure that reads configuration file and set initial values to globals
 *
 * \param argc int - passed from main()'s argc
 * \param argv char** - passed from main()'s argv
 * \return void
 *
 * Initializes globals, then reads config file and sets other values from it.
 * Terminates program execution on any fatal error encountered.
*/
void getconfig( int argc, char **argv )
{
int i, c, n; 
FILE *cfgh; // config file handle
char *s;
extern char *optarg;
extern int optind, optopt;

  // init
  errors = 0;
  memset (&bind_sock, 0, sizeof (bind_sock));

  bind_sock.sin_family = AF_INET;
  bind_sock.sin_port = htons(DEFAULTPORT);
  bind_sock.sin_addr.s_addr = htonl(DEFAULTADDR);

  makestr(&CONFIGFILE, (char*)DEFAULTCONFIGFILE);
  makestr(&pidfile, (char*)default_pid_file);

  devices_count = 0;
  for ( i = 0; i < MAXDEVS; ++i )
  {
    devices[i].tty = NULL;
    devices[i].id = -1;
    devices[i].state = 0;
    devices[i].fd = 0;
    gettimeofday(&devices[i].next_attempt, NULL);
  }

  max_tcp_conn_time.tv_sec = 2;
  max_tcp_conn_time.tv_usec = 0;

  // parsing command line args
  while( (c = getopt(argc, argv, ":dhvf:") ) != -1)
  {
    switch( c )
    {
      case 'd':
        daemonize = 1;
        break;

      case 'h':
        help();
        exit(0);

      case 'f': 
        makestr(&CONFIGFILE, optarg);
        break;

      case 'v':
        debug_to_tty = 1;
        break;

      case ':': /* -f without operand */
        fprintf(stderr, "Option -%c requires an operand\n", optopt);
        exit(1);

      case '?':
        fprintf(stderr, "Unrecognized option: -%c\n", optopt);
        exit(1);
    } //switch(c)
  } // while (c = getopt)

  // parsing config
  if ( ! ( cfgh = fopen(CONFIGFILE, "r") ) )
  {
    fprintf(stderr, "! Error opening config (%s): ", CONFIGFILE);
    perror(NULL);
    exit(1);
  }

  //-----------------------------------------------------
  // main loop
  line = 0;
  while( NULL != fgets(cfg_buf, 1023, cfgh) )
  {
    i = strcspn(cfg_buf, "\r\n");
    if (i > 0) cfg_buf[i] = '\0'; // do chomp ;)

    cfg_buf_ptr = cfg_buf;
    s = strsep(&cfg_buf_ptr, " \t\r\n");

    if(debug_level > 10)
      debuglog("> line %d parse start: '%s'\n", line, s);

    ++line;

    if ( s == NULL || *s == '\0' || *s == '#' )
      continue;

    //++++++++++++++++++++++++++++++++++++++++++++
    //++++++++++++++++++++++++++++++++++++++++++++
    //++++++++++++++++++++++++++++++++++++++++++++
    // IP/port to bind to:
    // bind address [number_of_retries [retry_sleep_time]]
    // retry sleep time is in ms. It's a pause between bind() tries in case of previous attempt error.
    // number_of_retries sets maximal count of attempt to do.
    // Useful on system statrtup where daemon runs before the interface initialization.
    if ( 0 == strcasecmp(s, "bind") )
    {
      s = config_parse_get_next_token(NEXT_TOKEN_REQUIRED);

      if ( ! inet_aton(s, (struct in_addr*)&bind_sock.sin_addr.s_addr) )
      {
        fprintf(stderr, "! ERROR at line %d: bad ip: %s\n", line, cfg_buf);
        ++errors;
      }

      if( NULL != (s = config_parse_get_next_token(NEXT_TOKEN_OPTIONAL)) ) // retries set
      {
        n = atoi(s);

        if ( n <= 0 )
        {
          fprintf(stderr, "! ERROR at line %d: bad re-try count: %s\n", line, cfg_buf);
          ++errors;
        }
        else
          bind_retries = n;

        if( NULL != (s = config_parse_get_next_token(NEXT_TOKEN_OPTIONAL)) ) // retries sleep time
        {
          n = atoi(s);

          if ( n <= 0 || n > 600 )
          {
            fprintf(stderr, "! ERROR at line %d: bad re-try sleep value: %s\n", line, cfg_buf);
            ++errors;
          }
          else
            bind_retry_sleep = n;
        }
      }
    }
    //++++++++++++++++++++++++++++++++++++++++++++
    // debuglevel <value>
    // sets output verboseness
    else if ( 0 == strcasecmp(s, "debuglevel") )
    {
      s = config_parse_get_next_token(NEXT_TOKEN_OPTIONAL);
      if (s == NULL) 
        debug_level = 1;
      else
        debug_level = atoi(s);

      if (debug_level)
        debuglog("debug level set to %d\n", debug_level);
    }
    //++++++++++++++++++++++++++++++++++++++++++++
    // device <id> <type> <tty>
    // id - human-selectable ID that should be same in the daemon and web configs
    // type - printer model string. any of device_types[].configtype
    // tty - path to serial port device
    // "device" keyword may be followed by on or more optional "options" keywords with driver specific settings
    else if ( 0 == strcasecmp(s, "device") )
    {
      s = config_parse_get_next_token(NEXT_TOKEN_REQUIRED); // id

      if ( 0 == ( n = atoi(s) ) )
      {
        fprintf(stderr, "! ERROR at line %d: bad ID value: %s\n", line, cfg_buf);
        ++errors;
      }

      s = config_parse_get_next_token(NEXT_TOKEN_REQUIRED); // type

      for (i = 0; i < device_types_count; ++i )
        if ( 0 == strcasecmp(device_types[i].configtype, s) )
          break;

      if ( i == device_types_count )
      {
        fprintf(stderr, "! ERROR at line %d: bad 'Type' value: %s\n", line, s);
        ++errors;
      }

      devices[devices_count].device_type = &device_types[i];

      s = config_parse_get_next_token(NEXT_TOKEN_REQUIRED); // tty
      devices[devices_count].tty = NULL;
      makestr(&devices[devices_count].tty, s);

      devices[devices_count].id = n;
      devices[devices_count].fd = 0;
      devices[devices_count].state = 0;
      devices[devices_count].tcpconn = NULL;
      devices[devices_count].buf_ptr = 0;
      devices[devices_count].buf_size = 0; // must be altered by driver's init func + buf getmem

      switch( devices[devices_count].device_type->type )
      {
#ifdef DRIVER_MARIA301
         case DEVICE_TYPE_MARIA301:
           m301_register_device(devices_count);
           break;
#endif

#ifdef DRIVER_SHTRIH_LTFRK
         case DEVICE_TYPE_SHTRIH_LTFRK:
           shtrih_ltfrk_register_device(devices_count);
           break;
#endif

#ifdef DRIVER_INNOVA
         case DEVICE_TYPE_INNOVA:
           innova_register_device(devices_count);
           break;
#endif
      }

      if (debug_level > 2)
        debuglog("Added device id: %d (%s)\n", devices[devices_count].id, devices[devices_count].tty);

      ++devices_count;
    }
    //++++++++++++++++++++++++++++++++++++++++++++
    // maxtcpsessions <number>
    // maximum simultaneous tcp connections allowed
    else if ( 0 == strcasecmp(s, "maxtcpsessions") )
    {
      s = config_parse_get_next_token(NEXT_TOKEN_REQUIRED);

      if ( 0 == ( n = atoi(s) ) || n < 1 )
      {
        fprintf(stderr, "! ERROR at line %d: bad number: %s\n", line, cfg_buf);
        ++errors;
      }

      ap_tcp_max_connections = n;
    }
    //++++++++++++++++++++++++++++++++++++++++++++
    // options <driver-specific data>
    // can appear next line after 'device' keyword to provide additional options specific to this printer model or instance
    // parsed within driver
    else if ( 0 == strcasecmp(s, "options") )
    {
      s = config_parse_get_next_token(NEXT_TOKEN_REQUIRED);

      if ( devices_count == 0 )
      {
        fprintf(stderr, "! ERROR at line %d: no devices defined before 'options'\n", line);
        ++errors;
      }
      else
      {
        switch( devices[devices_count - 1].device_type->type )
        {
#ifdef DRIVER_MARIA301
           case DEVICE_TYPE_MARIA301:
              m301_parse_options(devices_count - 1, s);
              break;
#endif

#ifdef DRIVER_SHTRIH_LTFRK
           case DEVICE_TYPE_SHTRIH_LTFRK:
             shtrih_ltfrk_parse_options(devices_count - 1, s);
             break;
#endif

#ifdef DRIVER_INNOVA
           case DEVICE_TYPE_INNOVA:
             innova_parse_options(devices_count - 1, s);
             break;
#endif
        }
      }
    }
    //++++++++++++++++++++++++++++++++++++++++++++
    // polltime <ms>
    // pause between device and tcp connections polls. Set in milliseconds
    else if ( 0 == strcasecmp(s, "polltime") )
    {
      s = config_parse_get_next_token(NEXT_TOKEN_REQUIRED);

      if ( 0 == ( n = atoi(s) ) || n < 10000 || n > 999999)
      {
        fprintf(stderr, "! ERROR at line %d: bad value: %s\n", line, cfg_buf);
        ++errors;
      }
    }
    //++++++++++++++++++++++++++++++++++++++++++++
    // port <number>
    // sets non-standard tcp port to bind
    else if ( 0 == strcasecmp(s, "port") )
    {
      s = config_parse_get_next_token(NEXT_TOKEN_REQUIRED);

      if ( 0 == ( n = atoi(s) ) )
      {
        fprintf(stderr, "! ERROR at line %d: bad port: %s\n", line, cfg_buf);
        ++errors;
      }

      bind_sock.sin_port = htons(n);
    }
    //++++++++++++++++++++++++++++++++++++++++++++
    // tcptimeout <ms>
    // expiration time for tcp session in milliseconds.
    // it is total time that session allowed to be.
    else if ( 0 == strcasecmp(s, "tcptimeout") )
    {
      s = config_parse_get_next_token(NEXT_TOKEN_REQUIRED);

      if ( 0 == ( n = atoi(s) ) || n < 1 || n > 60)
      {
        fprintf(stderr, "! ERROR at line %d: bad number: %s\n", line, cfg_buf);
        ++errors;
      }

      max_tcp_conn_time.tv_sec = n;
      max_tcp_conn_time.tv_usec = 0;
    }
    //++++++++++++++++++++++++++++++++++++++++++++
    // pidfile <path>
    // path to file with PID. default is /var/run/fprn.pid
    else if ( 0 == strcasecmp(s, "pidfile") )
    {
      makestr(&pidfile, (const char*)config_parse_get_next_token(NEXT_TOKEN_REQUIRED));
    }
    //++++++++++++++++++++++++++++++++++++++++++++
    else {
      fprintf(stderr, "Unknown option: %s\n", cfg_buf);
      ++errors;
    };
    //++++++++++++++++++++++++++++++++++++++++++++
    //++++++++++++++++++++++++++++++++++++++++++++
    //++++++++++++++++++++++++++++++++++++++++++++

  }; // while( NULL != fgets(cfg_buf, 1023, cfgh) ){

  fclose(cfgh);

  if (errors)
    exit(1);

  if (devices_count == 0)
  {
    fprintf(stderr, "no devices configured. exitting\n");
    exit(1);
  }

  // post-config init
  ap_tcp_connection_module_init();
}

