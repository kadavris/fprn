<?php
// !!!!!!!!! write in UTF-8 here !!!!!!!!!!!!!!!!
// this is common settings for fprn driver php companion code

// ******** language used in cheques, etc
define('CHEQUE_LANG', 'RU'); //!!!!!!!!


// uncomment printers type that you have connected
//define('USE_MARIA301', 1);
define('USE_SHTRIH_LTFRK', 1);

// hostname/IP of running fprn and where is fiscal printer physically connected
define('FPRN_HOSTNAME', '192.168.1.101');
define('FPRN_PORT', 2011);
define('FPRN_DEVICE', 1); // device #. must match one in gprn.conf

// fiscal printer administrator's password. used for reports printing, etc.
// printer-depended of course
define('FPRN_ADMIN_PASSWORD', 30);
define('FPRN_OPERATOR_PASSWORD', 1);

// those may not print, but required
switch (CHEQUE_LANG)
{
  case 'EN':
    define('CHEQUE_HEADER', 'Your company name');
    define('CHEQUE_FOOTER', 'End of document');
    define('CHEQUE_REASON', 'Funding the local deposit');
    define('CHEQUE_CODEPAGE', 'ASCII'); // ????!!!!
    break;

  case 'RU':
    define('CHEQUE_HEADER', 'Название компании');
    define('CHEQUE_FOOTER', 'Конец чека');
    define('CHEQUE_REASON', 'Внесение денег на счёт');
    define('CHEQUE_CODEPAGE', 'Windows-1251');
    break;

  case 'UA':
    define('CHEQUE_HEADER', 'Назва компаніі');
    define('CHEQUE_FOOTER', 'Кінець чеку');
    define('CHEQUE_REASON', 'Внесення грошей');
    define('CHEQUE_CODEPAGE', 'Windows-1251');
    break;
}
?>
