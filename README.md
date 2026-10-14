# External

Внешний клиент на C++ с отдельным оверлеем и меню Dear ImGui.

Основной проект Visual Studio — `shadow cheats/shadow cheats.vcxproj`, решение — `shadow cheats.slnx`. Корневой `CMakeLists.txt` остался от первоначального каркаса и не описывает весь клиент.

## Исходники

- `shadow cheats/src/core/` — запуск и список сущностей.
- `shadow cheats/src/features/` — игровые функции.
- `shadow cheats/src/render/` — оверлей и отрисовка.
- `shadow cheats/src/config/` — настройки.
- `shadow cheats/src/driver/` — интерфейс доступа к памяти.
- `shadow cheats/external/` — ImGui и FreeType.

Для сборки нужен C++-инструментарий Visual Studio и Windows SDK. Пути к библиотекам и параметры конфигураций находятся в `.vcxproj`.
