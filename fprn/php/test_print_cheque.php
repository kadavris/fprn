#!/usr/bin/php
<?php
/** \file test_print_cheque.php
* \brief Fiscal printers daemon's web ui - working example of manual control of most cheque printing details
*
* V1.200. Written by Andrej Pakhutin
* If your printer is in fiscal mode use test_return_cheque.php to print and register money return cheque to void balance!
****************************************************/

require_once 'fprn.php';

$r = test1();

if ($r != '')
{
  print "ERROR: $r\n";
}

exit;

function test1()
{
  $c = new TGprinter(FPRN_DEVICE, FPRN_OPERATOR_PASSWORD);

  if ( $c == NULL )
    return fprn_msg(0);

  print "> discardCheque()\n";

  if ( FALSE === $c->discardCheque() )
    return fprn_msg(1);

  print "> header\n";

  if ( FALSE === $c->header("!TEST HEADER!"))
      return fprn_msg(2);

  print "> footer\n";

  if ( FALSE === $c->footer("!TEST_FOOTER!"))
      return fprn_msg(3);

  print "> newCheque()\n";

  if ( FALSE === $c->newCheque(TFPRN_CTYPE_SELL))
      return fprn_msg(4);

  print "> addPosition()\n";

  $sum = 3412;
  $pos = array();
  $pos['amount'] = 1;
  $pos['price'] = $sum;
  $pos['text'] = 'Test position';
  $pos['model'] = 'cin';
  $pos['division'] = 1;
  $pos['tax0'] = $pos['tax1'] = $pos['tax2'] = $pos['tax3'] = 0;

  $e = $c->addPosition($pos);

  if ( $e === FALSE )
      return fprn_msg(5);

  print "> cashIn()\n";

  if ( FALSE === $c->CashIn($sum))
      return fprn_msg(6);

  print "> printCheque()\n";

  if ( FALSE === $c->printCheque())
      return fprn_msg(7);

  print ">> Sale done!";

  return '';
}

?>

