/** \file shtrih_ltfrk.h* \brief Fiscal printers daemon's driver for Shtrih-FR-K printer - main header
 *
 * V1.200. Written by Andrej Pakhutin
****************************************************/
#ifndef SHTRIH_LTFRK_H
#define SHTRIH_LTFRK_H
// Shtrih-light-fr-k driver additional data
#include "../fprnconfig.h"
#include "../printers_common.h"
// main communication codes
// power on init
#define CODE_ENQ 0x05// begin of msg
#define CODE_STX 0x02// acknowledge
#define CODE_ACK 0x06// denial
#define CODE_NAK 0x15

typedef struct t_driver_data
{
  unsigned char *buf; // used to store commands to printer. printer's output always stored in device->buf
  int buf_size, buf_ptr;
  int state;
  int prnerrcode; // printer error code from last status command
  int mode, submode;
  unsigned short fr_flags;
  unsigned char fp_flags;
  //int fp_free; // fp free mem
  //int fp_fisc; // fiscalizations #
  unsigned char admin_password[4]; //printer admin password
  int *config_try_speeds; // array of config option <speed>. see printers_common.c/process_options_speed()
  int connected_speed; // currently connected speed
} t_driver_data;

// returns 0 if no error
extern int send_command(struct t_device *dev, char *data, size_t size);extern int send_command_fmt(struct t_device *dev, char *fmt, ...);
extern int shtrih_ltfrk_port_init(int devid);
extern int shtrih_ltfrk_get_state(int devid);
#endif