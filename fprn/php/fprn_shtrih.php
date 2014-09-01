<?php
/** \file fprn_shtrih.php
* \brief Fiscal printers daemon's web ui - Shtrih-FR-K specific extensions and class methods 'overrides'
*
* V1.200. Written by Andrej Pakhutin
****************************************************/
define('debugLevel', 0);
require_once 'fprn_settings.inc';
require_once 'fprn_shtrih.inc';
require_once 'fprn_shtrih_errors.inc';

// internal state bitmasks
define('SHTRIH_ST_NODOC',    0);
define('SHTRIH_ST_DOCOPEN',  1); // new doc opened. still empty
define('SHTRIH_ST_HAVEPOS',  2); // at least 1 position present
define('SHTRIH_ST_HAVECASH', 4); // cash from client
define('SHTRIH_ST_DOCREADY', SHTRIH_ST_HAVEPOS | SHTRIH_ST_HAVECASH); // ready to print
define('SHTRIH_ST_PRTHEAD',  5); // printing header
define('SHTRIH_ST_PRTPOS',   6); // printing positions
define('SHTRIH_ST_PRTFOOT',  7); // printing footer

class TGPrn_Shtrih
{
  public $error = array();
  public $status = 0;

  private $host;
  private $port;
  private $devid;
  private $sock = 0;

  private $state = 0;

  private $header = '';
  private $footer = '';
  private $positions = array();
  private $doctype = -1;
  private $cash = 0;
  private $division = 0;

  private $prnerrcode = 0;
  private $prnerrtext = '';
  private $mode = 0;
  private $mode_text = '';
  private $submode = 0;
  private $submode_text = '';
  private $fr_flags = 0;
  private $fr_flags_text = '';
  private $fp_flags = 0;
  private $fp_flags_text = '';
  private $devstate = ''; // full status as returned by daemon's DEVSTATE request

  //===============================================
  // constructor
  function TGPrn_shtrih($_devid, $oppass, $newhost, $newport)
  {
    $this->host = $newhost; $this->port = 0 + $newport;
    $this->devid = 0 + $_devid;
    $this->password = 0 + $oppass;
    $this->doctype = -1;
  }

  //===============================================================================
  // INTERNALs
  private function _setErr($status = null, $ecode = null, $emsg = null)
  {
    if ( isset($ecode) )
    {
      $this->error['type'] = $ecode;
      $this->error['message'] = $emsg;
    }
    else
      $this->error = error_get_last();

    if ( isset($status) )
      $this->status = $status;
  }

  //-------------------------------------------------------------------------------
  private function qct($text) // quick convert to printer's codepage
  {
    return iconv("UTF-8", CHEQUE_CODEPAGE, $text);
  }

  //-------------------------------------------------------------------------------
  private function _sopen() // open socket
  {
    $this->_setErr(0, 0);

    if ($this->sock > 0 )
      return $this->sock;

    $this->sock = fsockopen($this->host, $this->port);

    if ($this->sock === FALSE)
    {
      $this->_setErr(null, TFPRN_ST_NOCONNECTION);
      return FALSE;
    }

    return $this->sock;
  }

  //-------------------------------------------------------------------------------
  private function _sclose()// close socket
  {
    $this->_setErr(0);

    if ($this->sock <= 0)
      return TRUE;

    fclose($this->sock);
    $this->sock = 0;

    return TRUE;
  }

  //-------------------------------------------------------------------------------
  private function _swrite($data, $len) // write to socket with checks
  {
    for($i = 1; $i < 10; ++$i)
    {
      $res = fwrite($this->sock, $data, $len);
      if ( FALSE === $res )
      {
        if ( debugLevel > 3 )
          echo "*** DBG: fwrite: ", $this->error[message], "\n";

        return FALSE;
      }

      if ( $res == strlen($data) )
        return TRUE;

      if ( $res > 0 )
        $data = substr($data, $res, strlen($data));

      if ( debugLevel > 9 )
        echo "*** DBG: fwrite: loop $i\n";

      usleep(100000);
    }

    return FALSE;
  }

  //===============================================================================
  //===============================================================================
  //===============================================================================
  // gets 2 lines: XXX code\nraw data
  // return FALSE on io errors
  // or received data on success ($this->prnerrcode set to printer's error code)
  private function _get_send_cmd_answer()
  {
    global $shtrih_error_codes;

    $this->_setErr(0, 0);
    $this->prnerrcode = 0;
    $this->prnerrtext = '';

    $data = stream_get_line($this->sock, 999, "\n");

    if ( debugLevel > 5 )
      echo "*** DBG: answer: $data \n";

    if (substr($data, 0, 3) != '200')
    {
      $this->_sclose();
      $this->_setErr(TFPRN_ST_SOFTFAULT, -1, $data);
      //$this->requestPrinterStatus();

      return FALSE;
    }

    $data = stream_get_contents($this->sock, 6); // get len (%06d) = data + LF

    if ( strlen($data) != 6 || intval($data) <= 0 )
    {
      $this->_sclose();
      //$this->_setErr(TFPRN_ST_SOFTFAULT, -1);
      $this->requestPrinterStatus();

      return FALSE;
    }

    $data = base64_decode(stream_get_contents($this->sock, 0 + $data));
    $this->prnerrcode = $data[1];
    $this->prnerrtext = $shtrih_error_codes[$this->prnerrcode];

    $this->_sclose();

    return $data;
  }

  //===============================================================================
  public function command($cmd)
  {
    $this->_setErr(0, 0);

    if ( FALSE === $this->_sopen() )
      return FALSE;

    $cmd = 'SEND ' . $this->devid . "\n" . base64_encode($cmd) . "\n";

    if ( debugLevel > 5 )
      echo "*** DBG: Sending: " , $cmd, "\n";

    if ( FALSE === $this->_swrite($cmd, strlen($cmd)))
      return FALSE;

    $data = $this->_get_send_cmd_answer();

    return $data;
  }

  //===============================================================================
  private function _savestate() //saves our state to driver
  {
    $data =  // keep the order of variables!
        base64_encode(serialize($header)) . "\n"
      . base64_encode(serialize($footer)) . "\n"
      . base64_encode(serialize($positions)) . "\n"
      . base64_encode(serialize($state)) . "\n"
      . base64_encode(serialize($doctype)) . "\n"
      . base64_encode(serialize($division)) . "\n";

    $lines = 7; // !!!!!!!! KEEP UP !!!!!!!!!!!!

    if ( $this->_sopen() === FALSE )
      return FALSE;

    $cmd = 'SAVESTATE ' . $this->devid . ' ' . $lines . "\n" . $data;

    if ( FALSE === $this->_swrite($cmd, strlen($cmd)))
      return FALSE;

    $data = stream_get_line($this->sock, 999, "\n");
    $this->_sclose();

    if (substr($data, 0, 3) != '200')
    {
      $this->_setErr(TFPRN_ST_NOCONNECTION, -1); // no connection to printer/long timeout
      return FALSE;
    }

    return TRUE;
  }

  private function _loadstate() //load our state from driver
  {
    if ($this->_sopen() === FALSE)
      return FALSE;

    $cmd = "LOADSTATE " . $this->devid . "\n";

    if ( FALSE === $this->_swrite($cmd, strlen($cmd)))
      return FALSE;

    $data = stream_get_line($this->sock, 999, "\n");

    if (substr($data, 0, 3) != '200')
    {
      $this->_setErr(TFPRN_ST_NOCONNECTION, -1); // no connection to printer/long timeout
      return FALSE;
    }

    $this->header = unserialize(base64_decode(stream_get_line($this->sock, 999, "\n")));
    $this->footer = unserialize(base64_decode(stream_get_line($this->sock, 999, "\n")));
    $this->positions = unserialize(base64_decode(stream_get_line($this->sock, 999999, "\n")));
    $this->state = unserialize(base64_decode(stream_get_line($this->sock, 999, "\n")));
    $this->doctype = unserialize(base64_decode(stream_get_line($this->sock, 999, "\n")));
    $this->division = unserialize(base64_decode(stream_get_line($this->sock, 999, "\n")));

    $this->_sclose();

    return TRUE;
  }

  //===============================================================================
  // return unified status TFPRN_ST_*
  public function requestPrinterStatus()
  {
    global $shtrih_error_codes;
    $this->_setErr(TFPRN_ST_OK, 0);

    if ( $this->_sopen() === FALSE )
      return FALSE;

    $cmd = 'DEVSTATE ' . $this->devid . "\n";

    if ( FALSE === $this->_swrite($cmd, strlen($cmd)))
      return FALSE;

    $this->devstate = '';

    while(1)
    {
      $data = stream_get_line($this->sock, 999, "\n");

      if ( $data === FALSE )
        break;

      $this->devstate .= $data . "\n";

      if ( substr($data, 0, 7) == "errcode" )
      {
        $this->prnerrcode = intval(substr($data, 8));
        $this->prnerrtext = $shtrih_error_codes[$this->prnerrcode];
        $this->devstate .= $this->prnerrtext . "\n";
      }
      else if ( substr($data, 0, 4) == "mode" )
      {
        $m = preg_split("/ /", $data);
        $this->mode = intval($m[1]);
        $this->submode = intval($m[2]);
      }
      else if ( substr($data, 0, 8) == "fr_flags" )
      {
        $this->fr_flags = intval(substr($data, 9));
      }
      else if ( substr($data, 0, 8) == "fp_flags" )
      {
        $this->fp_flags = intval(substr($data, 9));
      }
      else if ( substr($data, 0, 3) == "end" )
        break;
    }

    $this->_sclose();

    switch($this->mode)
    {
      case 0: $this->mode_text = "Принтер в рабочем режиме"; break;
      case sh_mode_output: $this->mode_text = "Выдача данных."; break;
      case sh_mode_oshshort: $this->mode_text = "Открытая смена, 24 часа не кончились."; break;
      case sh_mode_oshlong: $this->mode_text = "Открытая смена, 24 часа кончились. "; break;
      case sh_mode_closedshift: $this->mode_text = "Закрытая смена"; break;
      case sh_mode_fcktxman: $this->mode_text = "Блокировка по неправильному паролю налогового инспектора."; break;
      case sh_mode_dateack: $this->mode_text = "Ожидание подтверждения ввода даты."; break;
      case sh_mode_decpnt: $this->mode_text = "Разрешение изменения положения десятичной точки."; break;
      case sh_mode_od_sale: $this->mode_text = "Открытый документ: Продажа."; break;
      case sh_mode_od_buy: $this->mode_text = "Открытый документ: Покупка."; break;
      case sh_mode_od_sret: $this->mode_text = "Открытый документ: Возврат продажи."; break;
      case sh_mode_od_bret: $this->mode_text = "Открытый документ: Возврат покупки."; break;
      case sh_mode_zero: $this->mode_text = "Режим разрешения технологического обнуления. В этот режим ККМ переходит по включению питания, если некорректна информация в энергонезависимом ОЗУ ККМ."; break;
      case sh_mode_testrun: $this->mode_text = "Тестовый прогон."; break;
      case sh_mode_fullfisc: $this->mode_text = "Печать полного фис. отчета."; break;
      case sh_mode_erollprn: $this->mode_text = "Печать отчёта ЭКЛЗ. "; break;
      case sh_mode_taxroll_od_sale: $this->mode_text = "Работа с фискальным подкладным документом: Продажа (открыт)."; break;
      case sh_mode_taxroll_od_buy: $this->mode_text = "Работа с фискальным подкладным документом: Покупка (открыт)."; break;
      case sh_mode_taxroll_od_sret: $this->mode_text = "Работа с фискальным подкладным документом: Возврат продажи (открыт)."; break;
      case sh_mode_taxroll_od_bret: $this->mode_text = "Работа с фискальным подкладным документом: Возврат покупки (открыт)"; break;
      case sh_mode_copyroll_load_wait: $this->mode_text = "Печать подкладного документа. Ожидание загрузки."; break;
      case sh_mode_copyroll_loadnpos: $this->mode_text = "Печать подкладного документа. Загрузка и позиционирование."; break;
      case sh_mode_copyroll_pos: $this->mode_text = "Печать подкладного документа. Позиционирование."; break;
      case sh_mode_copyroll_print: $this->mode_text = "Печать подкладного документа. Печать."; break;
      case sh_mode_copyroll_printed: $this->mode_text = "Печать подкладного документа. Печать закончена."; break;
      case sh_mode_copyroll_ejecting: $this->mode_text = "Печать подкладного документа. Выброс документа."; break;
      case sh_mode_copyroll_eject_wait: $this->mode_text = "Печать подкладного документа. Ожидание извлечения."; break;
      case 15: $this->mode_text = "Фискальный подкладной документ сформирован."; break;
    }

    switch ( $this->submode )
    {
      case sh_submode_ready: $this->submode_text = "Бумага есть – ФР не в фазе печати операции – может принимать от хоста команды, связанные с печатью на том документе, датчик которого сообщает о наличии бумаги."; break;
      case sh_submode_papout: $this->submode_text = "Пассивное отсутствие бумаги – ФР не в фазе печати операции – не принимает от хоста команды, связанные с печатью на том документе, датчик которого сообщает об отсутствии бумаги."; break;
      case sh_submode_stalled: $this->submode_text = "Активное отсутствие бумаги – ФР в фазе печати операции – принимает только команды, не связанные с печатью. Переход из этого подрежима только в подрежим 3."; break;
      case sh_submode_wait: $this->submode_text = "После активного отсутствия бумаги – ФР ждет команду продолжения печати. Кроме этого принимает команды, не связанные с печатью"; break;
      case sh_submode_fprint: $this->submode_text = "Фаза печати операции полных фискальных отчетов – ФР не принимает от хоста команды, связанные с печатью, кроме команды прерывания печати"; break;
      case sh_submode_print: $this->submode_text = "Фаза печати операции – ФР не принимает от хоста команды, связанные с печатью"; break;
    }

    $this->fr_flags_text = "Флаги ФР: "
      .( "Рулон чековой ленты: " . ( ( $this->fr_flags & sh_frf_sliproll ) ? "есть" : "нет" ) )
      .( ", Положение десятичной точки: " . ( ( $this->fr_flags & sh_frf_decpnt ) ? "2 знака" : "0 знаков" ) )
      .( ", Оптический датчик чековой ленты: " . ( ( $this->fr_flags & sh_frf_slprollos ) ? "бумага есть" : "бумаги нет" ) )
      .( ", Рычаг термоголовки чековой ленты: " . ( ( $this->fr_flags & sh_frf_slprollvr ) ? "опущен" : "поднят" ) )
      .( ", Крышка корпуса ФР: " . ( ( $this->fr_flags & sh_frf_caseclosed ) ? "поднята" : "опущена" ) )
      .( ", Денежный ящик: " . ( ( $this->fr_flags & sh_frf_moneybox ) ? "окрыт" : "закрыт" ) )
      .( ( $this->fr_flags & sh_frf_eleroll ) ? ", ЭКЛЗ есть" : "" )
      .( ( $this->fr_flags & sh_frf_rtsesfail ) ? ", Отказ правого датчика принтера" : "" )
      .( ( $this->fr_flags & sh_frf_ltsesfail ) ? ", Отказ левого датчика принтера" : "" )
      .( ( $this->fr_flags & sh_frf_elerllful ) ? ", ЭКЛЗ почти заполнена" : "" )
      .( ( $this->fr_flags & sh_frf_bufferfilled ) ? ", Буфер принтера непуст" : "" )
      ;
      //.( "Рулон операционного журнала: " . ( ( $this->fr_flags & sh_frf_oproll ) ? "есть" : "нет" ) )
      //.( ", Верхний датчик подкладного документа: " . ( ( $this->fr_flags & sh_frf_upsens ) ? "да" : "нет" ) )
      //.( ", Нижний датчик подкладного документа: " . ( ( $this->fr_flags & sh_frf_dnsens ) ? "да" : "нет" ) )
      //.( ", Оптический датчик операционного журнала: " . ( ( $this->fr_flags & sh_frf_oprollos ) ? "бумага есть" : "бумаги нет" ) )
      //.( ", Рычаг термоголовки контрольной ленты: " . ( ( $this->fr_flags & sh_frf_ctrolllvr ) ? "опущен" : "поднят" ) )
      //.( ( $this->fr_flags & sh_frf_prspapinp ) ? ", Бумага на входе в презентер" : "" )
      //.( ( $this->fr_flags & sh_frf_prspapout ) ? ",  Бумага на выходе из презентера" : "" )
      //.( ", Увеличенная точность количества: " . ( ( $this->fr_flags & sh_frf_extprecis ) ? "" : "" ) )

    $this->fp_flags_text = "Флаги ФП: "
      .( "ФП 1 " . ( ( $this->fp_flags & sh_fpf_fr1 ) ? "есть" : "нет" ) )
      .( ", ФП 2 " . ( ( $this->fp_flags & sh_fpf_fr2 ) ? "есть" : "нет" ) )
      .( ", Лицензия: " . ( ( $this->fp_flags & sh_fpf_license ) ? "введена" : "не введена" ) )
      .( ", Батарея ФП: " . ( ( $this->fp_flags & sh_fpf_battery ) ? "<80%" : ">80%" ) )
      .( ", Последняя запись ФП: " . ( ( $this->fp_flags & sh_fpf_lastrec ) ? "корректна" : "испорчена" ) )
      .( ", Смена в ФП: " . ( ( $this->fp_flags & sh_fpf_shift ) ? "открыта" : "закрыта" ) )
      .( ", 24 часа в ФП: " . ( ( $this->fp_flags & sh_fpf_24h ) ? "кончились" : "не кончились" ) )
      .( ( $this->fp_flags & sh_fpf_overflow ) ? ", Переполнение ФП" : "" )
      ;

    if (!($this->fr_flags & sh_frf_sliproll)) $this->status = TFPRN_ST_HARDFAULT;
    if (!($this->fr_flags & sh_frf_slprollvr)) $this->status = TFPRN_ST_HARDFAULT;
    if (!($this->fr_flags & sh_frf_caseclosed))  $this->status = TFPRN_ST_HARDFAULT;

    switch ($this->mode)
    {
      case sh_mode_fcktxman:
      case sh_mode_dateack:
        $this->status = TFPRN_ST_SOFTFAULT;
        break;

      case sh_mode_od_sale:
      case sh_mode_od_buy:
      case sh_mode_od_sret:
      case sh_mode_od_bret:
        $this->status = TFPRN_ST_OPENDOC;
        break;

      case sh_mode_fullfisc:
      case sh_mode_erollprn:
        $this->status = TFPRN_ST_BUSY;
        break;
    }

    if ($this->fp_flags & sh_fpf_overflow) $this->status = TFPRN_ST_MEMFULL;
    //if ($this->fp_flags & sh_fpf_battery) $this->status = TFPRN_ST_HARDFAULT;

    if ($this->submode == sh_submode_papout || $this->submode == sh_submode_stalled
        || $this->submode == sh_submode_wait )
      $this->status = TFPRN_ST_PAPEROUT;

    $this->devstate .= "Описание режима: " . $this->mode_text . "\nОписание подрежима: " . $this->submode_text . "\n"
                       . "Расшифровка флагов ФР: " . $this->fr_flags_text . "\n"
                       . "Расшифровка флагов ФП: " . $this->fp_flags_text . "\n";

    return $this->status;
  }

  //===============================================================================
  // return string with verbose status
  public function verbosePrinterStatus()
  {
    $this->_setErr(TFPRN_ST_OK, 0);

    $this->requestPrinterStatus();

    return $this->devstate;
  }

  //===============================================================================
  public function header($text)
  {
    $this->_setErr(TFPRN_ST_OK, 0);
    $this->header = $this->qct($text);

    return TRUE;
  }

  //===============================================================================
  public function footer($text)
  {
    $this->_setErr(TFPRN_ST_OK, 0);
    $this->footer = $this->qct($text);

    return TRUE;
  }

  //===============================================================================
  public function newCheque($type)
  {
    if ( debugLevel > 5 )
      echo "*** DBG: newCheque begin\n";

    $this->_setErr(TFPRN_ST_OK, 0);

    if ($type != -1 && $this->doctype != -1)
    {
      if ( debugLevel > 0 )
        echo "*** DBG: newCheque problem: type: " , $type, " current: ", $this->doctype, "\n";

      $this->_setErr(TFPRN_ST_OPENDOC, 0);

      return FALSE;
    }

    $this->state = SHTRIH_ST_DOCOPEN;

    // set internal doctype
    switch ($type)
    {
      case TFPRN_CTYPE_SELL:    $this->doctype = 0; break;
      case TFPRN_CTYPE_BUY:     $this->doctype = 1; break;
      case TFPRN_CTYPE_SELLRET: $this->doctype = 2; break;
      case TFPRN_CTYPE_BUYRET:  $this->doctype = 3; break;
      case -1:
        $this->doctype = -1;
        $this->state = SHTRIH_ST_NODOC;
        break;
    }

    $this->positions = array();

    if ( $this->doctype == -1 )
    {
      if (FALSE === ($data = $this->command(pack('CV', 0x88, $this->password))))
        return FALSE;
    }
    else
    {
      if (FALSE === ($data = $this->command(pack('CVC', 0x8D, $this->password, $this->doctype))))
        return FALSE;
    }

    return TRUE;
  }

  //===============================================================================
  public function discardCheque()
  {
    return $this->newCheque(-1);
  }

  //===============================================================================
  public function printCheque()
  {
    $this->_setErr(TFPRN_ST_OK, 0);

    if ($this->state & SHTRIH_ST_DOCREADY != SHTRIH_ST_DOCREADY)
      return FALSE;

    $this->state = 0;

    /*
    if (strlen($this->header) > 0)
      if( ! ($retcode = $this->command(pack('CLCA40', 0x17, $this->password, 1, $this->header))))
        return 0;

    // open cheque
    if( ! ($retcode = $this->command(pack('CLC', 0x8D, $this->password, $this->doctype))))
      return 0;

    foreach ($positions as $pos)
    {
      if( ! ($retcode = $this->command(pack('CLC', 0x8D, $this->password, $this->doctype))))
        return 0;
    }

    if (strlen($this->footer) > 0)
      if( ! ($retcode = $this->command(pack('CLCA40', 0x17, $this->password, 1, $this->footer))))
        return 0;
    */

    // send close document
    if( FALSE === $this->command(pack("CVVa22A40", 0x85, $this->password, $this->cash, '', $this->footer)))
      return FALSE;

    return TRUE;
  }

  //===============================================================================
  // we need: amount, price, division, tax0, tax1, tax2, tax3, text
  public function addPosition($pos)
  {
    $this->_setErr(TFPRN_ST_OK, 0);

    if (! is_array($pos))
      return 0;

    $v = $pos['amount']; if (! isset($v) || $v < 0) return -1;
    $v = $pos['price'];  if (! isset($v) || $v < 0) return -2;
    $v = $pos['division']; if (! isset($v) || $v < 0 || $v > 16) return -3;
    $v = $pos['text']; if (! isset($v) || strlen($v) == 0) return -4;
    $tax0 = $pos['tax0']; if (! isset($tax0) || $tax0 < 0 || $tax0 > 3) return -5;
    $tax1 = $pos['tax1']; if (! isset($tax1) || $tax1 < 0 || $tax1 > 3) return -6;
    $tax2 = $pos['tax2']; if (! isset($tax2) || $tax2 < 0 || $tax2 > 3) return -7;
    $tax3 = $pos['tax3']; if (! isset($tax3) || $tax3 < 0 || $tax3 > 3) return -8;

    $this->division = $pos['division'];
    $pos['amount'] = 1000 * $pos['amount']; // amount is in 1/1000's
    $pos['price'] = 100 * $pos['price'];  // price is in kopieks

    $this->state |= SHTRIH_ST_HAVEPOS;
    $this->positions[] = array($pos['amount'], $pos['price'], substr($this->qct($pos['text']), 0, 40), $tax0, $tax1, $tax2, $tax3);

    if ( FALSE === ($data = $this->command(
                 pack("CVVxVxCCCCCA40", 0x80, $this->password, 0 + $pos['amount'], 0 + $pos['price'],
                      0 + $pos['division'], $tax0, $tax1, $tax2, $tax3, $this->qct($pos['text']))
             ))
       )
      return -9;

    return 1;
  }

  //===============================================================================
  public function CashIn($amount)
  {
    $this->_setErr(TFPRN_ST_OK, 0);

    if ($this->state != SHTRIH_ST_NODOC) // cash from client
    {
      $this->cash = 100 * $amount;
      $this->state |= SHTRIH_ST_HAVECASH;
    }
    else // cash to moneybox
    {
      if( ! $this->command(pack("CVVx", 0x50, $this->password, 0 + $amount)))
        return FALSE;
    }

    return TRUE;
  }

  //===============================================================================
  public function CashOut($amount)
  {
    $this->_setErr(TFPRN_ST_OK, 0);

    if ($this->state == SHTRIH_ST_NODOC) // initial cash in counter
    {
    }
    else // cash from client
    {
      $this->cash = $amount;
    }

    return $this->command(pack("CVVx", 0x51,$this->password, 0 + $amount));
  }

  //===============================================
  public function beginShift()
  {
    $this->_setErr(TFPRN_ST_OK, 0);

    if ( $this->mode != sh_mode_closedshift )
    {
      $this->_setErr(null, -1, 'Последняя смена не закрыта');
      return FALSE;
    }

    // ! Shtrih light is not using this command according to manual
    //if ( ! $this->command(pack("CV", 0xE1, 1)))
    //  return 0;

    //E0 ?

    return TRUE;
  }

  //===============================================
  public function endShift($adminPass)
  {
    $this->_setErr(TFPRN_ST_OK, 0);

    return $this->printReportWithClear($adminPass);
  }

  //===============================================
  public function printReportNoClear($adminPass)
  {
    $this->_setErr(TFPRN_ST_OK, 0);

    return $this->command(pack("CV", 0x40, $adminPass));
  }

  //===============================================
  public function printReportWithClear($adminPass)
  {
    $this->_setErr(TFPRN_ST_OK, 0);

    return $this->command(pack("CV", 0x41, $adminPass));
  }

  //===============================================
  public function continuePrint()
  {
    $this->_setErr(TFPRN_ST_OK, 0);

    //if ($this->submode != sh_submode_papout && $this->submode != sh_submode_stalled)
    //  return 0;

    return $this->command(pack('CV', 0xB0, $this->password));
  }

  //===============================================
  public function printLine($text)
  {
    $this->_setErr(TFPRN_ST_OK, 0);

    return $this->command(pack("CVCA40", 0x17, $this->password, 1, $this->qct($text)));
  }

  //===============================================
  public function setDriverParams($arr)
  {
    $this->_setErr(TFPRN_ST_OK, 0);

    return TRUE;
  }

  //===============================================
  public function setParams($arr)
  {
    $this->_setErr(TFPRN_ST_OK, 0);

    return TRUE;
  }

  //===============================================
  public function feedDocument($linesCount)
  {
    $this->_setErr(TFPRN_ST_OK, 0);

    return TRUE;
  }

}

?>
