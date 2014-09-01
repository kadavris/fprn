FPrn: UNIX/Linux fiscal printers manager with networked remote control and PHP interface add-on.

This project features nearly full and tested support for russian Shtrih-FR-K fiscal printer.
Almost done driver for Ukrainian Maria 301.

The whole approach is to allow easy addition of new driver modules for other models.
Daemon feature simple and effective newtwork protocol to allow remote control
and, in first place, working interface with PHP that provides means to build intranet based systems
with thin clients or seamless integration into more sofisticated software.

The code provided AS IS, without support. I've done this project almost a year ago
and have no time or will to support it anymore.

Parts it evolved into much more robust and independent software,
so I give this software into public domain in hope that someone will use it or even make or clone it better. ;)

Original code by Andrej Pakhutin (pakhutin@gmail.com), 2012-2013
* Main repository at the github: git clone https://github.com/kadavris/fprn.git
* Secondary at sourceforge: git clone git://git.code.sf.net/p/fprn/code fprn
* Compiled libraries at sourceforge: http://sourceforge.net/projects/fprn/files/?source=navbar

-------------------------------------------------------
Russian:

FPrn: UNIX/Linux демон для управления фискальными принтерами.

Демон создавался с таким расчётом, чтобы можно было просто добавлять поддержку
новых моделей принтеров разных производителей.

Главная изюминка программы заключается в предоставлении возможности печати в сети
подавая команды на TCP порт. Поскольку основной целью была связка с Web GUI,
написанном на PHP, исходный код включает в себя довольно подробно разработанный
интерфейс к демону, написанный на PHP.

Этот проект включает в себя довольно полную поддержку российской модели Штрих-ФР-К.
Имеется почти готовый драйвер для украинского Мария 301.

В настоящее время проект мною не поддерживается, т.к. контракт с заказчиком закончен.

Оригинальный код: (с) Андрей Пахутин (pakhutin@gmail.com), 2012-2013
* Основной репозиторий на github: git clone https://github.com/kadavris/fprn.git
* Вторичный на sourceforge: git clone git://git.code.sf.net/p/fprn/code fprn
