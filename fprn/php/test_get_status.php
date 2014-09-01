#!/usr/bin/php
<?php
/** \file test_get_status.php
* \brief Fiscal printers daemon's web ui - command line utility returning detailed printer hardware state
*
* V1.200. Written by Andrej Pakhutin
****************************************************/

require_once 'fprn.php';

  $c = new TGprinter(FPRN_DEVICE, FPRN_OPERATOR_PASSWORD);

  if ( $c == NULL )
    return fprn_msg(0);

  $st = $c->verbosePrinterStatus();

  if ( $st == '')
      print 'error';
  else
      print $st;

?>

