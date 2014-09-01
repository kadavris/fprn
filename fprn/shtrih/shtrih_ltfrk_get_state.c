/** \file shtrih_ltfrk_get_state.c
* \brief Fiscal printers daemon's driver for Shtrih-FR-K printer - procedure for getting the detailed state of printer hardware
*
* V1.200. Written by Andrej Pakhutin
*
* !!! This file is being #include-d into shtrih_ltfrk.c just to avoid Makefile hell
****************************************************/
#define SHTRIH_LTFRK_GET_STATE_C
/*
#include <fcntl.h>
#include <time.h>
#include "shtrih_ltfrk.h"
#include "shtrih_answer_timeouts.h"
#include "shtrih_flags.h"
*/
//===========================================================================
/** \brief Shtrih-FR-K driver. Method that queries and returns detailed status of printer hardware
 *
 * \param devid int - device id
 * \return int - 0 - OK, errcode otherwise
 *
 * returns somewhat thorough decyphering of printer's state in the devices[devid]->buf as string
*/
int shtrih_ltfrk_get_state(int devid)
{
  struct t_device *dev;
  struct t_driver_data *dd;
  int errcode;
  int state_speed, state_timeout;
  struct timeval tv; // for timeout fixing
  char *tmpbuf;

  dev = get_dev_by_id(devid);
  dd = dev->driver_data;

  tv.tv_sec = 10; // 10 sec is good enough for slow connection
  tv.tv_usec = 0;

  if ( dev->tcpconn != NULL )
    timeradd(&(dev->tcpconn->expire), &tv, &(dev->tcpconn->expire)); // extending the timeout in tcp watcher

  /* get dev type */
  errcode = send_command(dev, "\xfc", 1);
  dd->prnerrcode = dev->buf[3];

  if ( errcode != 0 )
    return errcode;

  if ( dd->prnerrcode != 0 )
  {
    dosyslog(LOG_ERR, "PRINTER ERROR in shtrih_ltfrk_get_state dev id: %d. error code: %d\n", dev->id, dev->buf[3]);
  }
  else
  {
    // device type validation
    // todo: the checked numbers should be defines with clear meaning
    if ( dev->buf[4] != 0 || dev->buf[5] != 0)
      dosyslog(LOG_ERR, "shtrih_ltfrk_get_state dev id: %d. wrong type/subtype: %d/%d\n", dev->id, dev->buf[4], dev->buf[5]);

    if ( dev->buf[8] != 252 )
      dosyslog(LOG_ERR, "shtrih_ltfrk_get_state dev id: %d. wrong model code: %d. no warranty applied!\n", dev->id, dev->buf[8]);

    if ( dev->buf[6] != 1 || dev->buf[7] != 5)
      dosyslog(LOG_ERR, "shtrih_ltfrk_get_state dev id: %d. proto version differs: %d/%d\n", dev->id, dev->buf[6], dev->buf[7]);
  }

  // get comm params
  errcode = send_command_fmt(dev, "\x15%c%c%c%c%c", dd->admin_password[0], dd->admin_password[1],dd->admin_password[2], dd->admin_password[3], 0);
  dd->prnerrcode = dev->buf[3];

  if ( errcode != 0 )
    return errcode;

  if ( dd->prnerrcode != 0 )
  {
    dosyslog(LOG_ERR, "PRINTER ERROR in shtrih_ltfrk_get_state dev id: %d. error code: %d\n", dev->id, dev->buf[3]);
  }
  else
  {
    if ( dev->buf[5] <= 150 )
      state_timeout = dev->buf[5];
    else if ( dev->buf[5] <= 249 )
      state_timeout = dev->buf[5] * 150;
    else
      state_timeout = dev->buf[5] * 15000;

    state_speed = io_speeds_printable[2 + dev->buf[4]]; // io_speeds[2] == 2400

    dosyslog(LOG_NOTICE, "shtrih_ltfrk_get_state(#%d): comm params: speed: %d, timeout: %d ms \n", dev->id, state_speed, state_timeout);
  }

  /* get long status */
  errcode = send_command_fmt(dev, "\x11%c%c%c%c", dd->admin_password[0], dd->admin_password[1], dd->admin_password[2], dd->admin_password[3]);
  dd->prnerrcode = dev->buf[3];

  if ( errcode == 0 )
  {
    dd->fr_flags = *((uint16_t*)(dev->buf + 15));
    dd->fp_flags = dev->buf[33];
    dd->mode = dev->buf[17];
    dd->submode = dev->buf[18];

    if (debug_level > 0)
    {
      debuglog("* debug: got: err code: %#x\n\tFR V%u.%u, Build %u, date: %02u-%02u-%04u\n",
               dev->buf[3], dev->buf[5], dev->buf[6], *((uint16_t*)(dev->buf+7)),
               dev->buf[9], dev->buf[10], 2000 + dev->buf[11]);

      debuglog("\tmode: %u/%u\n\t\t> ", dd->mode, dd->submode);
      switch (dd->mode)
      {
        case sh_mode_output:   debuglog("Data output\n"); break;
        case sh_mode_oshshort: debuglog("Open shift < 24h\n"); break;
        case sh_mode_oshlong:  debuglog("Open shift > 24h\n"); break;
        case sh_mode_closhift: debuglog("Shift closed\n"); break;
        case sh_mode_fcktxman: debuglog("Wrong taxman password\n"); break;
        case sh_mode_dateack:  debuglog("Waiting for date input approval\n"); break;
        case sh_mode_decpnt:   debuglog("Allowed to change decimal point pos\n"); break;
        case sh_mode_od_sale:  debuglog("Open document: sale\n"); break;
        case sh_mode_od_buy:   debuglog("Open document: buy\n"); break;
        case sh_mode_od_sret:  debuglog("Open document: sale return\n"); break;
        case sh_mode_od_bret:  debuglog("Open document: buy return\n"); break;
        case sh_mode_zero:     debuglog("Technological zeroin enabled\n"); break;
        case sh_mode_testrun:  debuglog("Test run\n"); break;
        case sh_mode_fullfisc: debuglog("Full fiscal log print\n"); break;
        case sh_mode_erollprn: debuglog("EKLZ print\n"); break;
      }

      debuglog("\t\t> ");
      switch (dd->submode)
      {
        case sh_submode_ready:   debuglog("Ready\n"); break;
        case sh_submode_papout:  debuglog("Paper out, no jobs pending\n"); break;
        case sh_submode_stalled: debuglog("Paper out. job waiting. next possible mode 0x03\n"); break;
        case sh_submode_wait:    debuglog("After 0x02. waiting for 'continue print' command\n"); break;
        case sh_submode_fprint:  debuglog("Full fiscal log print. DND. (print stop cmd allowed)\n"); break;
        case sh_submode_print:   debuglog("Job output printing.\n"); break;
      }

      debuglog("\tFR flags: ");
      memdumpb(dev->buf + 15, 2);

      debuglog("\t\t> Рулон чековой ленты: %s\n", ( ( dd->fr_flags & sh_frf_sliproll ) ? "есть" : "нет (!)" ) );
      debuglog("\t\t> Положение десятичной точки: %s\n", ( ( dd->fr_flags & sh_frf_decpnt ) ? "2 знака" : "0 знаков" ) );
      debuglog("\t\t> Оптический датчик чековой ленты: %s\n", ( ( dd->fr_flags & sh_frf_slprollos ) ? "бумага есть" : "бумаги нет (!)" ) );
      debuglog("\t\t> Рычаг термоголовки чековой ленты: %s\n", ( ( dd->fr_flags & sh_frf_slprollvr ) ? "опущен" : "поднят (!)" ) );
      debuglog("\t\t> Крышка корпуса ФР: %s\n", ( ( dd->fr_flags & sh_frf_caseclosed ) ? "поднята (!)" : "опущена" ) );
      debuglog("\t\t> Денежный ящик: %s\n", ( ( dd->fr_flags & sh_frf_moneybox ) ? "открыт" : "закрыт" ) );
      if( dd->fr_flags & sh_frf_eleroll ) debuglog("\t\t> ЭКЛЗ есть\n");
      if( dd->fr_flags & sh_frf_rtsesfail ) debuglog("\t\t! Отказ правого датчика принтера\n");
      if( dd->fr_flags & sh_frf_ltsesfail ) debuglog("\t\t! Отказ левого датчика принтера\n");
      if( dd->fr_flags & sh_frf_elerllful ) debuglog("\t\t! ЭКЛЗ почти заполнена\n");
      if( dd->fr_flags & sh_frf_bufferfilled ) debuglog("\t\t! Буфер принтера непуст\n");

      //debuglog( "\t\t> Рулон операционного журнала: ", ( ( dd->fr_flags & sh_frf_oproll ) ? "есть" : "нет" ) );
      //debuglog( "\t\t> Верхний датчик подкладного документа: ", ( ( dd->fr_flags & sh_frf_upsens ) ? "да" : "нет" ) );
      //debuglog( "\t\t> Нижний датчик подкладного документа: ", ( ( dd->fr_flags & sh_frf_dnsens ) ? "да" : "нет" ) );
      //debuglog( "\t\t> Оптический датчик операционного журнала: ", ( ( dd->fr_flags & sh_frf_oprollos ) ? "бумага есть" : "бумаги нет" ) );
      //debuglog( "\t\t> Рычаг термоголовки контрольной ленты: ", ( ( dd->fr_flags & sh_frf_ctrolllvr ) ? "опущен" : "поднят" ) );
      //if( dd->fr_flags & sh_frf_prspapinp ) ? "\t\t! Бумага на входе в презентер" : "" );
      //if( dd->fr_flags & sh_frf_prspapout ) ? "\t\t! Бумага на выходе из презентера" : "" );
      //debuglog( ", Увеличенная точность количества: ", ( ( dd->fr_flags & sh_frf_extprecis ) ? "" : "" ) );

      debuglog("\tFMem V%u.%u, Build %u, Flags: ", dev->buf[20], dev->buf[21], *((uint16_t*)(dev->buf+22)));
      memdumpb(dev->buf + 33, 1);
      debuglog("\t\t> ФП 1 ", ( ( dd->fp_flags & sh_fpf_fr1 ) ? "есть" : "нет" ) );
      debuglog("\t\t> ФП 2 ", ( ( dd->fp_flags & sh_fpf_fr2 ) ? "есть" : "нет" ) );
      debuglog("\t\t> Лицензия: ", ( ( dd->fp_flags & sh_fpf_license ) ? "введена" : "не введена (!)" ) );
      debuglog("\t\t> Батарея ФП: ", ( ( dd->fp_flags & sh_fpf_battery ) ? "<80% (!)" : ">80%" ) );
      debuglog("\t\t> Последняя запись ФП: ", ( ( dd->fp_flags & sh_fpf_lastrec ) ? "корректна" : "испорчена" ) );
      debuglog("\t\t> Смена в ФП: ", ( ( dd->fp_flags & sh_fpf_shift ) ? "открыта" : "закрыта" ) );
      debuglog("\t\t> 24 часа в ФП: ", ( ( dd->fp_flags & sh_fpf_24h ) ? "кончились" : "не кончились" ) );
      if( dd->fp_flags & sh_fpf_overflow ) debuglog("\t\t! Переполнение ФП");

      debuglog("\tdate: %02u-%02u-%04u\n", dev->buf[24], dev->buf[25], 2000 + dev->buf[26]);
    }
  } // if (debug_level > 0)
  else
    return errcode;

  // return something for php
  dev->buf[0] = '\0';
  if ( NULL == ( tmpbuf = malloc(dev->buf_size)) )
    return 0;

  snprintf(tmpbuf, dev->buf_size - 1, "errcode %d\nspeed %d\ntimeout %d\nmode %u %u\nfr_flags %u\nfp_flags %u\n"
           "operator %d\nfw_ver %d.%d\nfw_build %d\nfw_date %02d-%02d-%04d\n"
           "depts_num %d\ndoc_no %d\nport %d\n"
           "eklz_ver %d.%d\neklz_build %d\neklz_date %02d-%02d-%04d\n"
           "date %02d-%02d-%04d\ntime %02d:%02d:%02d\n"
           "id %u\nlast_closed_shift_id %d\n"
           "eklz_free %d\nregs_count %d\nregs_left %d\nINN %llu\n"
           "end\n",
           dd->prnerrcode, state_speed, state_timeout, dd->mode, dd->submode, dd->fr_flags, dd->fp_flags,
           dev->buf[4], dev->buf[5], dev->buf[6], *((uint16_t*)(dev->buf+7)), dev->buf[9], dev->buf[10], 2000 + dev->buf[11],
           dev->buf[12], *((uint16_t*)(dev->buf+13)), dev->buf[19],
           dev->buf[20], dev->buf[21], *((uint16_t*)(dev->buf+22)), dev->buf[24], dev->buf[25], 2000 + dev->buf[26],
           dev->buf[27], dev->buf[28], 2000 + dev->buf[29], dev->buf[30], dev->buf[31], dev->buf[32],
           *((uint32_t*)(dev->buf+34)), *((uint16_t*)(dev->buf+38)),
           *((uint16_t*)(dev->buf+40)), dev->buf[42], dev->buf[43],
           *((uint64_t*)(dev->buf+44)) & 0x0000ffffffffffffl
  );

  strcpy((char *)(dev->buf), tmpbuf);
  free(tmpbuf);

  return errcode;
}
