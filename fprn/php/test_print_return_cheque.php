#!/usr/bin/php
<?php
/** \file test_print_return_cheque.php
* \brief Fiscal printers daemon's web ui - Working example of how to use miscellaneous data fields while printing money return cheque
*
* V1.200. Written by Andrej Pakhutin
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
    return 'class creation error';

  print "> discardCheque()\n";

  if ( FALSE === $c->discardCheque() )
    return 'discard error';

  print "> header\n";

  if ( FALSE === $c->header("!TEST HEADER!"))
      return 'header error';

  print "> footer\n";

  if ( FALSE === $c->footer("!TEST_FOOTER!"))
      return 'footer error';

  print "> newCheque()\n";

  if ( FALSE === $c->newCheque(TFPRN_CTYPE_SELLRET))
      return 'newCheque() error';

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
      return 'cheque print class: addpos 1 error';

  print "> cashIn()\n";

  if ( FALSE === $c->CashIn($sum))
      return 'cheque print class: cashin err';

  print "> printCheque()\n";

  if ( FALSE === $c->printCheque())
      return 'cheque print class: printcheque err';

  print ">> Sale done!";

  return '';
}

?>

