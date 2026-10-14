# Internal — отдельная версия

В этой ветке основная разработка находится в [internal/](internal/README.md). Там лежат DLL, меню, инжектор и скрипты сборки.

`external/`, `shared/`, `launcher/` и `website/` сохранились от каркаса монорепозитория. Полные исходники остальных компонентов находятся в своих ветках — ссылки есть в [основном README](https://github.com/alexkweppas-beep/shadowcheat).

Эта версия отличается от `components/internal` настройками защиты и сборки. В `internal/CMakeLists.txt` пока прописан локальный путь к VMProtect, поэтому конфигурация требует проверки на другой машине.

[Другие компоненты проекта](https://github.com/alexkweppas-beep/shadowcheat).
