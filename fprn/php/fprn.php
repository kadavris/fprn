<?php
/** \file fprn.php
* \brief Fiscal printers daemon's web ui - main class
*
* V1.200. Written by Andrej Pakhutin
****************************************************/
$debugLevel = 0;
/*
  Initial code: Andrej Pakhutin.

  wrapper class for fiscal printers

  supported calls:
  getLastError() - _get_last_error() style
      error['type'] - code
      error['message'] - text
  printerStatus() - current printer TFPRN_ST_*
  requestPrinterStatus() - force status query from printer
  getPrinterType() - for the bold of heart. return TFPRN_DT_*
  setDriverParams($arr) - set Driver-specific parameters if any
  setParams($arr) - set some mystic or yet unknown parameter of this class if any

  // cheques:
  header($text) - header of cheque (may not print. depends on printer model)
  footer($text) - footer of cheque (may not print. depends on printer model)
  newCheque($type) - start new cheque of type TFPRN_CTYPE_*
  discardCheque() - as is. all cleared up.
  printCheque() - prints ready cheque
  addPosition($pos) - adds position to cheque. $pos - assotiative with possible members:
    'amount' - mandatory
    'price' - mandatory, but may be not used if printer likes it so
    'model' - model number. mandatory, but may be not used if printer likes it so
    'text' - text message/product name. mandatory, but may be not used if printer likes it so
    'tax[0-9]' - tax group/percent, etc.
    'department' - dept #

  beginShift($clerkPass) - registers the beginning of a new shift in printer if it supports it
  endShift($adminPass) - registers the ending of current shift in printer if it supports it

  continuePrint() - continue after jams, paper out, etc
  printLine($line) - print arbitrary line of text
  feedDocument($linesCount) - feed several lines of current doc

  CashIn() - adds initial cash to register or cash from client for payment if cheque is opened
  CashOut() - removes cash amount from register after shift or as a change if cheque is opened

  printReportNoClean()  - X-rep
  printReportWithClean() - Z-rep
*/

//global $DIR_FS_CATALOG;
require_once 'fprn_messages.php';
require_once 'fprn_settings.inc';

/*
  known printer types:
  uncomment the needed 'require'
*/
define('TFPRN_DT_MARIA301', 1);
define('TFPRN_DT_SHTRIH_LTFRK', 2);

if ( defined('USE_MARIA_301') )
  require_once 'fprn_maria.php';

if ( defined('USE_SHTRIH_LTFRK') )
  require_once 'fprn_shtrih.php';

// global statuses
define('TFPRN_ST_OK', 0);
define('TFPRN_ST_PAPEROUT', 1); // cheque paper out
define('TFPRN_ST_NOCONNECTION', 2); // no connection to printer/long timeout
define('TFPRN_ST_BUSY', 3);//busy on other op
define('TFPRN_ST_MEMFULL', 4); // internal memory full. halt
define('TFPRN_ST_OPENDOC', 5); // open document
define('TFPRN_ST_SOFTFAULT', 6); // printer soft fault/in need of administrative or service care
define('TFPRN_ST_HARDFAULT', 7); // printer hardware fault/case open/in need of administrative or service care

//checque types:
define('TFPRN_CTYPE_SELL', 0);
define('TFPRN_CTYPE_BUY', 1);
define('TFPRN_CTYPE_SELLRET', 2);
define('TFPRN_CTYPE_BUYRET', 3);

//************************************************
/** \brief 
 *
 * \param 
 * \return 
 *
 * long desc here
*/
class TGprinter
{
  // defaults
  private $error = array(); // as in _get_last_error(): 'type' = errcode, 'message' = sometext...
  private $host = FPRN_HOSTNAME;
  private $port = FPRN_PORT;

  private $sock = 0;
  private $pclass = null; // selected printer class
  private $opPassword = null; //operator's password. may be unset if driver knows what to do w/o it

  //===============================================
  function TGprinter($_devid, $_oppass = null, $newhost = null, $newport = null)
  {
    global $debugLevel;

    if ( $debugLevel > 9 )
      echo "** DBG: TGprinter.constructor: dev $_devid\n";

    if ( $newhost != null || $newport != null )
      $this->sethost($newhost, $newport);

    if ( isset($_oppass) )
      $this->opPassword = $_oppass;

    switch ( $this->getPrinterType($_devid) )
    {
      case TFPRN_DT_MARIA301:
        $this->pclass = new TGprn_maria($_devid, $_oppass, $this->host, $this->port);
        break;

      case TFPRN_DT_SHTRIH_LTFRK:
        $this->pclass = new TGprn_shtrih($_devid, $_oppass, $this->host, $this->port);
        break;

      default:
        if ( $debugLevel > 0 )
          echo "** DBG: TGprinter.constructor: getprintertype() fuckup or unknown type code\n";

        return;
    }
  }

  //===============================================
  // INTERNALS:
  private function _setErr($ecode = null, $emsg = null)
  {
    if ( isset($ecode) )
    {
      $this->error['type'] = $ecode;
      $this->error['message'] = $emsg;
    }
    else $this->error = $this->pclass->error;
  }

  private function _sopen() // open socket
  {
    global $debugLevel;

    $this->_setErr(0);

    if ($this->sock > 0 )
      return TRUE;

    $this->sock = fsockopen($this->host, $this->port);

    if ($this->sock === FALSE)
    {
      $this->error = error_get_last();

      if ( $debugLevel > 9 )
        echo "*** DBG: fsockopen: ", $this->error[message], "\n";

      $this->status = TFPRN_ST_NOCONNECTION;
      $this->sock = 0;

      return FALSE;
    }

    return TRUE;
  }

  private function _sclose()// close socket
  {
    $this->_setErr(0);

    if ($this->sock <= 0)
      return TRUE;

    fclose($this->sock);
    $this->sock = 0;

    return TRUE;
  }

  private function _swrite($data, $len = null) // write to socket with checks
  {
    global $debugLevel;

    $this->_setErr(0);

    if ( ! isset($len) )
      $len = strlen($data);

    for($i = 1; $i < 10; ++$i)
    {
      $res = fwrite($this->sock, $data, $len);

      if ( FALSE === $res )
      {
        $this->error = error_get_last();
        if ( $debugLevel > 0 ) echo "*** DBG: fwrite: ", $this->error[message], "\n";

        return FALSE;
      }

      if ( $res == $len )
        return TRUE;

      if ( $res > 0 )
        $data = substr($data, $res, $len);

      if ( $debugLevel > 9 )
        echo "*** DBG: fwrite: loop $i\n";

      usleep(100000);
    }

    return FALSE;
  }

  //===============================================
  public function getPrinterType($devid)
  {
    global $debugLevel;

    $this->_setErr(0);

    if (FALSE === $this->_sopen())
      return FALSE;

    $cmd = 'DEVTYPE ' . $devid . "\n";

    if ( $debugLevel > 9 )
      echo "*** DBG: cmd: $cmd";

    $this->_swrite($cmd, strlen($cmd));
    $ans = stream_get_line($this->sock, 999, "\n");

    if (substr($ans, 0, 3) != '200')
    {
      $this->_setErr(-1, $ans);
      $this->_sclose();

      if ( $debugLevel > 9 )
        echo "*** DBG: getPrinterType ans: $ans\n";

      return FALSE;
    }

    $ans = stream_get_line($this->sock, 999, "\n");
    $this->_sclose();

    return intval($ans);
  }

  //===============================================================================
  public function getLastError()
  {
    if ( $this->pclass == null || $this->error[type] != 0 )
      return $this->error;
    else
      return $this->pclass->error;
  }

  //===============================================
  function msg($id)
  {
    if ( $this->pclass == null )
    {
      if ( $this->error[type] != 0 )
        return fprn_msg($id) . "\n" . $this->error[message];
    }
    else
    {
      if ( $this->pclass->error[type] != 0 )
        return fprn_msg($id) . "\n" . $this->pclass->error[message];
    }

    return fprn_msg($id);
  }

  //===============================================
  public function printerStatus()
  {
    if ($this->pclass == null)
    {
      $this->_setErr(-1, fprn_msg(10));
      return FALSE;
    }

    return $this->pclass->status;
  }

  //===============================================
  public function requestPrinterStatus()
  {
    if ($this->pclass == null)
    {
      $this->_setErr(-1, fprn_msg(10));
      return FALSE;
    }

    $this->pclass->requestPrinterStatus();

    return $this->pclass->status;
  }

  //===============================================
  public function verbosePrinterStatus() // this really is debug feature!
  {
    if ($this->pclass == null)
    {
      $this->_setErr(-1, fprn_msg(10));
      return FALSE;
    }

    return $this->pclass->verbosePrinterStatus();
  }

  //===============================================
  public function command($cmd)
  {
    $this->_setErr(0);
    $this->pclass->command($cmd);
  }

  //===============================================
  public function sethost($newhost, $newport = null)
  {
    $this->_setErr(0);

    if ($newhost != '')
      $this->host = $newhost;

    if (isset($port))
      $this->port = $newport;
  }

  //===============================================
  public function header($text)
  {
    if ($this->pclass == null)
    {
      $this->_setErr(-1, fprn_msg(10));
      return FALSE;
    }

    $this->_setErr(0);

    return $this->pclass->header($text);
  }

  //===============================================
  public function footer($text)
  {
    if ($this->pclass == null)
    {
      $this->_setErr(-1, fprn_msg(10));
      return FALSE;
    }

    $this->_setErr(0);

    return $this->pclass->footer($text);
  }

  //===============================================
  public function newCheque($type)
  {
    if ($this->pclass == null)
    {
      $this->_setErr(-1, fprn_msg(10));
      return FALSE;
    }

    $this->_setErr(0);

    return $this->pclass->newCheque($type);
  }

  //===============================================
  public function discardCheque()
  {
    if ($this->pclass == null)
    {
      $this->_setErr(-1, fprn_msg(10));
      return FALSE;
    }

    $this->_setErr(0);

    return $this->pclass->discardCheque();
  }

  //===============================================
  public function printCheque()
  {
    if ($this->pclass == null)
    {
      $this->_setErr(-1, fprn_msg(10));
      return FALSE;
    }

    $this->_setErr(0);

    return $this->pclass->printCheque();
  }

  //===============================================
  public function addPosition($pos)
  {
    if ($this->pclass == null)
    {
      $this->_setErr(-1, fprn_msg(10));
      return FALSE;
    }

    $this->_setErr(0);

    return $this->pclass->addPosition($pos);
  }

  //===============================================
  // adds initial cash or cash from client
  public function CashIn($amount)
  {
    if ($this->pclass == null)
    {
      $this->_setErr(-1, fprn_msg(10));
      return FALSE;
    }

    $this->_setErr(0);

    return $this->pclass->CashIn($amount);
  }

  //===============================================
  public function beginShift($clerkPass)
  {
    if ($this->pclass == null)
    {
      $this->_setErr(-1, fprn_msg(10));
      return FALSE;
    }

    $this->_setErr(0);

    return $this->pclass->beginShift($clerkPass);
  }

  //===============================================
  public function endShift($adminPass)
  {
    if ($this->pclass == null)
    {
      $this->_setErr(-1, fprn_msg(10));
      return FALSE;
    }

    $this->_setErr(0);

    return $this->pclass->endShift($adminPass);
  }

  //===============================================
  public function printReportNoClear($adminPass)
  {
    if ($this->pclass == null)
    {
      $this->_setErr(-1, fprn_msg(10));
      return FALSE;
    }

    $this->_setErr(0);

    return $this->pclass->printReportNoClean($adminPass);
  }

  //===============================================
  public function printReportWithClear($adminPass)
  {
    if ($this->pclass == null)
    {
      $this->_setErr(-1, fprn_msg(10));
      return FALSE;
    }

    $this->_setErr(0);

    return $this->pclass->printReportWithClean($adminPass);
  }

  //===============================================
  public function continuePrint()
  {
    if ($this->pclass == null)
    {
      $this->_setErr(-1, fprn_msg(10));
      return FALSE;
    }

    $this->_setErr(0);

    return $this->pclass->continuePrint();
  }

  //===============================================
  public function printLine($line)
  {
    if ($this->pclass == null)
    {
      $this->_setErr(-1, fprn_msg(10));
      return FALSE;
    }

    $this->_setErr(0);

    return $this->pclass->printLine($line);
  }

  //===============================================
  public function setDriverParams($arr)
  {
    if ($this->pclass == null)
    {
      $this->_setErr(-1, fprn_msg(10));
      return FALSE;
    }

    $this->_setErr(0);

    return $this->pclass->setDriverParams($arr);
  }

  //===============================================
  public function setParams($arr)
  {
    if ($this->pclass == null)
    {
      $this->_setErr(-1, fprn_msg(10));
      return FALSE;
    }

    $this->_setErr(0);

    return $this->pclass->setParams($arr);
  }

  //===============================================
  public function feedDocument($linesCount)
  {
    if ($this->pclass == null)
    {
      $this->_setErr(-1, fprn_msg(10));
      return FALSE;
    }

    $this->_setErr(0);

    return $this->pclass->feedDocument($linesCount);
  }

}//class TGPrint

?>
