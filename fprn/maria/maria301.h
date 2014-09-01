/** \file maria301.h
* \brief Fiscal printers daemon's driver for maria301 main header
*
* V1.000a. Written by Andrej Pakhutin
****************************************************/
#ifndef MARIA301_H
#define MARIA301_H

#include "../gprnconfig.h"

#define CMD_BEGIN 253
#define CMD_END   254

#define STATE_INIT     0
#define STATE_SPEEDSET 1
#define STATE_READY    2
#define STATE_CMDSENT  3


typedef struct t_driver_data
{
  char *buf; // used for printer's output
  int buf_size, buf_ptr; // size and current position
  int state;
  int timeout; // timeout on wait for answer

  int prnerrindex; // index of printer error code from last cmd. -1 if none

  uint8_t admin_password[4]; //printer admin password
  int use_crc; // generate/check crc in commands
} t_driver_data;


#define MARIA301_ERROR_MESSAGES_COUNT 64

extern char *maria301_errors[MARIA301_ERROR_MESSAGES_COUNT][2];

// internal: sends command to printer
extern int send_command(struct t_device *dev, struct t_driver_data *dd, char *fmt, ...);

#endif
