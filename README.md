# Internal для RAGE Multiplayer

DLL с оверлеем на Direct3D 11 и меню Dear ImGui. Игровая часть, настройки и скриптовый мост находятся в `src/`.

## Сборка

Нужны MSVC, Windows SDK и CMake 3.21 или новее.

```sh
cmake -S . -B build -A x64
cmake --build build --config Release
```

Цели сборки — `shadow_internal.dll` и `shadow_injector.exe`. MinHook и остальные включённые зависимости находятся в репозитории.

CRT подключён статически, точка входа — `_DllMainCRTStartup`. В этой ветке нет папки `src/Dev`; включение `SHADOW_DEV` без неё остановит конфигурацию CMake.

## Где что лежит

- `src/Main/` — хуки и кадровый цикл.
- `src/Features/` — игровые функции и скриптовый мост.
- `src/Menu/` — страницы меню, тема и конфиги.
- `src/SDK/` — структуры и игровые данные.
- `injector/` — отдельный инжектор.
- `third_party/minhook/` и `src/imgui/` — сторонние библиотеки.

Настройки пользовательской сборки заданы в [src/SDK/RageBuildFlavor.h](src/SDK/RageBuildFlavor.h). Конфиги, скрипты и авторизация обрабатываются в `MenuConfig.cpp`, `ScriptBridge.cpp` и `CloudStorage.cpp`.
