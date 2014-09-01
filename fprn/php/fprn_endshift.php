<?php
/** \file fprn_endshift.php
* \brief Fiscal printers daemon's web ui - command line utility to manually close the current shift on defult device
*
* V1.200. Written by Andrej Pakhutin
****************************************************/
require_once 'fprn.php';

  $c = new TGprinter(FPRN_DEVICE, FPRN_OPERATOR_PASSWORD);

  if ( $c == NULL )
    print fprn_msg(0);

  if ( FALSE === $c->continuePrint())
    print fprn_msg(9);

  if ( FALSE === $c->discardCheque())
    print fprn_msg(1);

  if ( FALSE === $c->endShift(FPRN_ADMIN_PASSWORD))
    print fprn_msg(8);

  print 'OK';
}
?>
