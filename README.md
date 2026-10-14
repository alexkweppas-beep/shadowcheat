# Shadow

Shadow: internal и external для GTA V / RAGE Multiplayer, Windows-лаунчер и сайт.

Компоненты проекта находятся в отдельных ветках. `main` содержит общую документацию и каркас CMake:

| Ветка | Содержимое |
| --- | --- |
| [components/internal](https://github.com/alexkweppas-beep/shadowcheat/tree/components/internal) | DLL, оверлей на D3D11, меню Dear ImGui |
| [components/internal-next](https://github.com/alexkweppas-beep/shadowcheat/tree/components/internal-next) | Другая версия internal в папке `internal/` |
| [components/external](https://github.com/alexkweppas-beep/shadowcheat/tree/components/external) | Внешний клиент на C++ |
| [components/launcher](https://github.com/alexkweppas-beep/shadowcheat/tree/components/launcher) | Лаунчер под Windows |
| [components/website](https://github.com/alexkweppas-beep/shadowcheat/tree/components/website) | React-сайт и API на Express / SQLite |

У компонентов разные системы сборки. Требования и пути к исходникам описаны в README соответствующих веток.

## Работа с проектом

Для изменений используйте ветку нужного компонента. От неё можно создать короткую ветку под задачу и открыть PR обратно в компонент. Перед слиянием нужно проверить его сборку; общей сборки всех компонентов пока нет.

Ошибки и задачи можно заводить через Issues. В описании ошибки укажите компонент, конфигурацию сборки и шаги воспроизведения.

`archive/dev-skeleton` содержит прежний каркас интеграционной ветки. Тег `archive/monorepo-presplit` оставлен для просмотра старого общего дерева.

Сторонние библиотеки лежат рядом с исходниками; их лицензии сохранены. В истории сайта есть `.env` и базы данных, поэтому перед открытой публикацией нужно проверить их содержимое.
