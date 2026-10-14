# Internal для RAGE Multiplayer

Версия DLL с меню Dear ImGui, оверлеем D3D11 и отдельными инструментами сборки. Исходники находятся в `src/`, проект задаётся в [CMakeLists.txt](CMakeLists.txt).

## Сборка

Нужны MSVC, Windows SDK, CMake 3.21 или новее и MASM. При включённом `SHADOW_ENABLE_VMP` требуется установленный VMProtect с SDK и консольной утилитой.

В CMake сейчас жёстко задан локальный путь к VMProtect: значение `SHADOW_VMP_ROOT` из командной строки затем перезаписывается. Этот путь нужно проверить перед сборкой на другой машине.

Настройки упаковки находятся в `cmake/RunVMProtect.cmake` и `protection/`. Для локальной сборки без протектора в существующей конфигурации предусмотрен `SHADOW_ENABLE_VMP=OFF`.

Сборка создаёт DLL и `shadow_injector.exe`. CRT статический, точка входа — `_DllMainCRTStartup`. Папки `src/Dev` в этой ветке нет.

## Папки

- `src/Main/`, `src/SDK/` — хуки, кадровый цикл и игровые данные.
- `src/Features/` — игровые функции и скриптовый мост.
- `src/Menu/` — интерфейс и настройки.
- `injector/`, `tools/` — отдельные инструменты.
- `scripts/` — проверки сборок и скрипты подготовки вариантов.
- `src/imgui/`, `third_party/minhook/` — сторонние библиотеки.

Параметры пользовательской сборки заданы в [src/SDK/RageBuildFlavor.h](src/SDK/RageBuildFlavor.h). Лицензии библиотек сохранены в их папках.
