<?php
/** \file fprn_messages.php
* \brief Fiscal printers daemon's web ui - error messages that can be presented to user
*
* V1.200. Written by Andrej Pakhutin
****************************************************/
require_once 'fprn_settings.inc';

function fprn_msg($id)
{
  switch ( $id )
  {
    case 0:
      switch ( CHEQUE_LANG )
      {
        case 'RU': return 'Ошибка создания класса управления фискальным принтером';
      }
      break;

    case 1:
      switch ( CHEQUE_LANG )
      {
        case 'RU': return 'Ошибка аннулирования недопечатанного чека';
      }
      break;

    case 2:
      switch ( CHEQUE_LANG )
      {
        case 'RU': return 'Ошибка задания заголовка чека';
      }
      break;

    case 3:
      switch ( CHEQUE_LANG )
      {
        case 'RU': return 'Ошибка задания подписи чека';
      }
      break;

    case 4:
      switch ( CHEQUE_LANG )
      {
        case 'RU': return 'Ошибка старта чека';
      }
      break;

    case 5:
      switch ( CHEQUE_LANG )
      {
        case 'RU': return 'Ошибка добавления позиции в чек';
      }
      break;

    case 6:
      switch ( CHEQUE_LANG )
      {
        case 'RU': return 'Ошибка ввода суммы оплаты';
      }
      break;

    case 7:
      switch ( CHEQUE_LANG )
      {
        case 'RU': return 'Ошибка печати чека';
      }
      break;

    case 8:
      switch ( CHEQUE_LANG )
      {
        case 'RU': return 'Ошибка закрытия смены';
      }
      break;

    case 9:
      switch ( CHEQUE_LANG )
      {
        case 'RU': return 'Ошибка продолжения печати';
      }
      break;

    case 10:
      switch ( CHEQUE_LANG )
      {
        case 'RU': return 'Не инициализирован класс принтера';
      }
      break;

    default:
      switch ( CHEQUE_LANG )
      {
        case 'RU': return '';
      }
  }

  return '';
}

?>
