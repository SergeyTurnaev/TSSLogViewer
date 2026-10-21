# TSSLogViewer

Просмотрщик логов заездов устройств TSS: графики телеметрии, описание заезда,
сравнение данных и статистика. Работает в Windows, Linux и macOS.
Интерфейс переведён на 11 языков.

Это самостоятельный проект на C++17 и Qt 6. Для сборки не нужны исходники
конфигуратора TSSLimiterX или прошивок устройств.

## Сборка

Требуются CMake, Ninja, компилятор C++17, OpenGL и Qt 6 с модулями Widgets,
PrintSupport, OpenGLWidgets и LinguistTools. В GitHub Actions используется Qt 6.10.1.

```sh
git clone https://github.com/SergeyTurnaev/TSSLogViewer.git
cd TSSLogViewer
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build
```

На Windows команды выполняются в окружении MSVC x64; `CMAKE_PREFIX_PATH` должен
указывать на комплект Qt для MSVC x64. На macOS используется соответствующий
комплект Qt. Для Linux дополнительно нужны системные development-пакеты OpenGL
и зависимостей Qt; их список приведён в `.github/workflows/build.yml`.

Версия задаётся в `project(... VERSION ...)` файла `CMakeLists.txt`.
Переводы обновляются командой `cmake --build build --target update_translations`.

## GitHub Actions

- Push в `main` и pull request проверяют Linux-сборку.
- Теги `v*` и ручной запуск `build` собирают Windows x64 ZIP, Linux AppImage
  и macOS universal `.dmg` с перетаскиванием приложения в «Программы».
- Ручной `release` собирает выбранный существующий тег и создаёт релиз,
  по умолчанию черновик.
- Сборка не требует доступа к приватным репозиториям или секретам проекта.
  macOS-пакет подписывается локальной ad-hoc подписью, без Developer ID
  и нотарификации Apple.

В бинарные пакеты включаются лицензии и архив исходников именно того коммита,
из которого собрана программа: `Source/TSSLogViewer-source.zip`.
В macOS они находятся внутри `.app/Contents/Resources`, в AppImage —
в `usr/share/TSSLogViewer` (доступно после распаковки AppImage).

## Совместная поставка с TSSLimiterX

В репозитории TSSLimiterX этот проект подключён как подмодуль `TSSLogViewer`.
Общая сборка использует закреплённый коммит подмодуля и собирает обе программы
одной версией Qt. Общий Windows-установщик выпускается из TSSLimiterX.
Версии приложений могут различаться.

Изменения просмотрщика сначала коммитятся и отправляются в этот репозиторий,
затем в TSSLimiterX отдельным коммитом обновляется указатель подмодуля.

## Лицензия

TSSLogViewer распространяется под GNU General Public License версии 3:
см. [LICENSE](LICENSE). Разрешены личное и коммерческое использование,
изучение, изменение и распространение на условиях GPLv3.

Сведения о сторонних компонентах находятся в
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Исходники и патчи сторонних библиотек хранятся в `thirdparty/`;
изменения QCustomPlot оформляются патчами в `patches/`.
