# Сторонние компоненты

Этот файл содержит сведения о сторонних компонентах TSSLogViewer. Условия
каждого компонента сохраняют силу. Лицензия самого TSSLogViewer находится
в [LICENSE](LICENSE); приведённые ниже уведомления её не заменяют.

## Модуль обновлений TSSUpdater

Модуль обновлений `updater/` — самостоятельный компонент TSSRacing под лицензией
MIT: [текст лицензии](updater/LICENSE). Он может использоваться независимо от
просмотрщика, в том числе в TSSLimiterX. В пакетах текст находится в
`licenses/TSSUpdater/LICENSE`.

## QCustomPlot 2.1.1

- Исходники: `thirdparty/qcustomplot/qcustomplot.h` и `qcustomplot.cpp`.
- Copyright (C) 2011–2022 Emanuel Eichhammer.
- Лицензия: **GNU GPL version 3 or, at your option, any later version**
  (`GPL-3.0-or-later`), как указано в заголовках исходников.
- Текст GPLv3: [LICENSE](LICENSE).
- Проект: <https://www.qcustomplot.com/>.

В копии для TSSLogViewer есть адаптация к Qt 6 и исправление совместного
использования OpenGL-контекста. Патч последнего исправления сохранён в
`thirdparty/qcustomplot/patches/0001-shared-opengl-context.patch`.

## Zippy

- Исходники: `thirdparty/Zippy/`.
- Copyright (c) 2019 Kenneth Troldal Balslev.
- Лицензия: MIT; [полный текст](licenses/Zippy-MIT.txt).
- Исходное уведомление сохранено в `thirdparty/Zippy/LICENSE` и заголовках.
- Проект: <https://github.com/troldal/Zippy>.

## miniz, включённый в Zippy

- Исходник: `thirdparty/Zippy/Zippy/miniz/miniz.h`, обозначенный в заголовке
  как miniz 2.0.8.
- Rich Geldreich; Copyright 2013–2014 RAD Game Tools and Valve Software;
  Copyright 2010–2014 Rich Geldreich and Tenacious Software LLC.
- Копия содержит уведомление о передаче в общественное достояние
  ([Unlicense](licenses/miniz-Unlicense.txt)) и отдельное
  [уведомление MIT](licenses/miniz-MIT.txt). Оба сохранены.
- В исходнике также отмечено, что функция записи PNG предоставлена
  Alex Evans в 2011 году в общественное достояние.
- Проект: <https://github.com/richgel999/miniz>.

## JSON for Modern C++ 3.11.2

- Исходник: `thirdparty/nlohmann/json.hpp`.
- Copyright (c) 2013–2022 Niels Lohmann.
- Лицензия: MIT; [полный текст](licenses/nlohmann-json-MIT.txt).
- Проект и исходники этой версии:
  <https://github.com/nlohmann/json/tree/v3.11.2>.

Единый заголовок включает следующие дополнительные уведомления:

| Компонент | Авторское уведомление | Текст лицензии |
| --- | --- | --- |
| Hedley | Copyright (c) 2016–2021 Evan Nemerson | [MIT](licenses/Hedley-MIT.txt) |
| Декодер UTF-8 | Copyright (c) 2008–2009 Björn Hoehrmann | [MIT](licenses/Hoehrmann-MIT.txt) |
| Grisu2 | Copyright (c) 2009 Florian Loitsch | [MIT](licenses/Grisu2-MIT.txt) |
| Фрагмент Google Abseil | Copyright 2018 The Abseil Authors | [Apache License 2.0](licenses/Abseil-Apache-2.0.txt) |

Раздел совместимости C++ в `json.hpp` содержит явное уведомление Apache 2.0
и ссылку на [исходный фрагмент Abseil](https://github.com/abseil/abseil-cpp/blob/10cb35e459f5ecca5b2ff107635da0bfa41011b4/absl/utility/utility.h).
Это уведомление сохранено вместе с MIT-уведомлениями объединённого заголовка.

## Qt 6

TSSLogViewer использует Qt Core, Gui, Widgets, PrintSupport, OpenGL и
OpenGLWidgets. LinguistTools используется при сборке переводов.
Qt — Copyright The Qt Company Ltd. и другие участники проекта; отдельные
авторские уведомления находятся в исходниках соответствующей версии Qt.

Открытая поставка используемых библиотек Qt доступна, в частности, по условиям
GNU Lesser General Public License version 3 или GNU General Public License
version 3. Qt и его использование покрываются выбранной применимой лицензией:

- [LGPLv3](licenses/Qt-LGPL-3.0.txt), включая условия GPLv3, на которые она ссылается;
- [GPLv3](licenses/Qt-GPL-3.0.txt);
- [официальные сведения о лицензиях Qt](https://doc.qt.io/qt-6/licensing.html);
- [исходники опубликованных версий Qt](https://download.qt.io/archive/qt/);
- [сторонние компоненты Qt и их лицензии](https://doc.qt.io/qt-6/licenses-used-in-qt.html).

Qt не включён в исходники этого репозитория; готовые пакеты могут содержать
его динамические библиотеки, плагины и переводы. Для них также действуют
уведомления сторонних компонентов конкретной сборки Qt. При подготовке
пакета следует сохранять эти уведомления и предоставлять исходники
соответствующей версии согласно применимым условиям лицензий.

Происхождение файлов лицензий описано в [licenses/SOURCES.md](licenses/SOURCES.md).
