<?php
/** \file fprn_shtrih.inc
* \brief Fiscal printers daemon's web ui - printer hardware status bits
*
* V1.200. Written by Andrej Pakhutin
****************************************************/

// ---- FR flags (2 bytes)
define('sh_frf_oproll',   0x0001); // operations log roll found
define('sh_frf_sliproll', 0x0002); // slips roll found
define('sh_frf_upsens',   0x0004); // copy roll upper sensor
define('sh_frf_dnsens',   0x0008); // copy roll lower sensor
define('sh_frf_decpnt',   0x0010); // decimal point position
define('sh_frf_eleroll',  0x0020); // EKLZ found
define('sh_frf_oprollos', 0x0040); // oper log optosensor paper detect
define('sh_frf_slprollos',0x0080); // slip roll optosensor paper detect
define('sh_frf_ctrolllvr',0x0100); // control roll thermal head lever down
define('sh_frf_slprollvr',0x0200); // slip roll --"--
define('sh_frf_caseclosed', 0x0400);
define('sh_frf_moneybox',  0x0800); // money box open
define('sh_frf_rtsesfail',0x1000); // right sensor fail
define('sh_frf_prspapinp',0x1000); // presenter paper input jam
define('sh_frf_ltsesfail',0x2000); // left sensor fail
define('sh_frf_prspapout',0x2000); // presenter paper out jam
define('sh_frf_elerllful',0x4000); // EKLZ roll about filled
define('sh_frf_bufferfilled',0x8000);
define('sh_frf_extprecis',0x8000); // extended precision of amounts

// ---- FRam flags
define('sh_fpf_fr1',      0x01); // FRam 1 present
define('sh_fpf_fr2',      0x02); // Fram 2 present
define('sh_fpf_license',  0x04); // license found
define('sh_fpf_overflow', 0x08); // FRam overflow
define('sh_fpf_battery',  0x10); // battery state low
define('sh_fpf_lastrec',  0x20); // last record is good
define('sh_fpf_shift',    0x40); // last shif os open
define('sh_fpf_24h',      0x80); // 24 shift hours expired

// ---- modes
define('sh_mode_output',  0x01); // data output
define('sh_mode_oshshort',0x02); // open shift < 24h
define('sh_mode_oshlong', 0x03); // open shift > 24h
define('sh_mode_closedshift',0x04); // shift closed
define('sh_mode_fcktxman',0x05); // wrong taxman password
define('sh_mode_dateack', 0x06); // wait for date input approval
define('sh_mode_decpnt',  0x07); // allow to change decimal point pos
define('sh_mode_opendoc', 0x08); // Open document:
define('sh_mode_od_sale', 0x18); // -- sale
define('sh_mode_od_buy',  0x28); // -- buy
define('sh_mode_od_sret', 0x48); // -- sale return
define('sh_mode_od_bret', 0x88); // -- buy return
define('sh_mode_zero',    0x09); // technologicla zeroin ena
define('sh_mode_testrun', 0x0a);
define('sh_mode_fullfisc',0x0b); // full fiscal log print
define('sh_mode_erollprn',0x0c); // electronic roll print
define('sh_mode_taxroll_od_sale',0x0d); // tax roll: open sale
define('sh_mode_taxroll_od_buy', 0x1d);  // tax roll: open buy
define('sh_mode_taxroll_od_sret',0x2d); // tax roll: open sale return
define('sh_mode_taxroll_od_bret',0x3d); // tax roll: open buy return
define('sh_mode_copyroll_load_wait',0x0e); // copy roll print: load
define('sh_mode_copyroll_loadnpos', 0x1e); // copy roll print: load n positioning
define('sh_mode_copyroll_pos',      0x2e); // copy roll print: positioning
define('sh_mode_copyroll_print',    0x3e); // copy roll print: printing
define('sh_mode_copyroll_printed',  0x4e); // copy roll print: print done
define('sh_mode_copyroll_ejecting', 0x5e); // copy roll print: ejecting
define('sh_mode_copyroll_eject_wait',0x6e); // copy roll print: ejaculation waiting

define('sh_submode_ready',  0x00); // ready...
define('sh_submode_papout', 0x01); // paper out, no jobs pending
define('sh_submode_stalled',0x02); // paper out. job waiting. next possible mode 0x03
define('sh_submode_wait',   0x03); // after 0x02. waitng for "continue print" command
define('sh_submode_fprint', 0x04); // full fiscal log print. do not disturb. (print stop cmd allowed)
define('sh_submode_print',  0x05); // job output printing.

?>
