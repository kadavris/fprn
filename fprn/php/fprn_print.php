<?php
/** \file fprn_print.php
* \brief Fiscal printers daemon's web ui - Working example of simple usage pattern
*
* V1.200. Written by Andrej Pakhutin
* prints cheque only with variable sum using predefined header, footer and oher parameters
****************************************************/
require_once 'fprn.php';

function fprn_print_check($sum)
{
  $c = new TGprinter(FPRN_DEVICE, FPRN_OPERATOR_PASSWORD);

  if ( $c == NULL )
    return fprn_msg(0);

  if ( FALSE === $c->discardCheque() )
    return fprn_msg(1);

  if ( FALSE === $c->header(CHEQUE_HEADER))
    return fprn_msg(2);

  if ( FALSE === $c->footer(CHEQUE_FOOTER))
    return fprn_msg(3);

  if ( FALSE === $c->newCheque(TFPRN_CTYPE_SELL))
    return fprn_msg(4);

  $pos = array();
  $pos['amount'] = 1;
  $pos['price'] =  $sum;
  $pos['text'] = CHEQUE_REASON;
  $pos['model'] = 'cin';
  $pos['division'] = 1;
  $pos['tax0'] = $pos['tax1'] = $pos['tax2'] = $pos['tax3'] = 0;

  $e = $c->addPosition($pos);

  if ( $e === FALSE )
    return fprn_msg(5);

  if ( FALSE === $c->CashIn($sum))
    return fprn_msg(6);

  if ( FALSE === $c->printCheque())
    return fprn_msg(7);

  return TRUE;
}

?>
