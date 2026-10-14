# Shadow

Исходники shadowcheat.cc: internal и external для GTA V / RAGE Multiplayer, Windows-лаунчер и сайт.

В `main` находятся описание проекта и каркас CMake. Реализации лежат в отдельных ветках:

| Ветка | Содержимое |
| --- | --- |
| [components/internal](https://github.com/alexkweppas-beep/shadowcheat-portfolio/tree/components/internal) | DLL, оверлей на D3D11, меню Dear ImGui |
| [components/internal-next](https://github.com/alexkweppas-beep/shadowcheat-portfolio/tree/components/internal-next) | Другая версия internal в папке `internal/` |
| [components/external](https://github.com/alexkweppas-beep/shadowcheat-portfolio/tree/components/external) | Внешний клиент на C++ |
| [components/launcher](https://github.com/alexkweppas-beep/shadowcheat-portfolio/tree/components/launcher) | Лаунчер под Windows |
| [components/website](https://github.com/alexkweppas-beep/shadowcheat-portfolio/tree/components/website) | React-сайт и API на Express / SQLite |

У компонентов разные системы сборки. Требования и пути к исходникам описаны в README соответствующих веток.

`archive/dev-skeleton` содержит прежний каркас интеграционной ветки. Тег `archive/monorepo-presplit` оставлен для просмотра старого общего дерева.

Сторонние библиотеки лежат рядом с исходниками; их лицензии сохранены. В истории сайта есть `.env` и базы данных, поэтому перед открытой публикацией нужно проверить их содержимое.
