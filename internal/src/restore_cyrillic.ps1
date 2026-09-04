$f = 'd:\Downloads\123\internal\src\Menu\MenuPageSettings.cpp'
$content = [System.IO.File]::ReadAllText($f, [System.Text.Encoding]::UTF8)

$replacements = @(
    @('T\("Until",[^)]+\)', 'T("Until", "До", "До")'),
    @('T\("Not activated",[^)]+\)', 'T("Not activated", "Не активирован", "Не активовано")'),
    @('T\("Keys",[^)]+\)', 'T("Keys", "Клавиши", "Клавіші")'),
    @('T\("Open menu",[^)]+\)', 'T("Open menu", "Открыть меню", "Відкрити меню")'),
    @('T\("Unload",[^)]+\)', 'T("Unload", "Выгрузить", "Вивантажити")'),
    @('T\("Capture",[^)]+\)', 'T("Capture", "Захват", "Захоплення")'),
    @('T\("OBS bypass",[^)]+\)', 'T("OBS bypass", "Обход OBS", "Обхід OBS")'),
    @('T\("Appearance",[^)]+\)', 'T("Appearance", "Внешний вид", "Зовнішній вигляд")'),
    @('T\("Size###flyout_scale",[^)]+\)', 'T("Size###flyout_scale", "Размер###flyout_scale", "Розмір###flyout_scale")'),
    @('T\("Accent###flyout_accent",[^)]+\)', 'T("Accent###flyout_accent", "Акцент###flyout_accent", "Акцент###flyout_accent")'),
    @('T\("Background###flyout_canvas",[^)]+\)', 'T("Background###flyout_canvas", "Фон###flyout_canvas", "Тло###flyout_canvas")'),
    @('T\("Dark",[^)]+\)', 'T("Dark", "Темная", "Темна")'),
    @('T\("Light",[^)]+\)', 'T("Light", "Светлая", "Світла")'),
    @('T\("Reset to default",[^)]+\)', 'T("Reset to default", "Сбросить по умолчанию", "Скинути за замовчуванням")'),
    @('T\("Language",[^)]+\)', 'T("Language", "Язык", "Мова")'),
    @('T\("Save",[^)]+\)', 'T("Save", "Сохранить", "Зберегти")'),
    @('T\("Load",[^)]+\)', 'T("Load", "Загрузить", "Завантажити")'),
    @('T\("CONFIG ACTIONS",[^)]+\)', 'T("CONFIG ACTIONS", "ДЕЙСТВИЯ", "ДІЇ")'),
    @('T\("Set as default",[^)]+\)', 'T("Set as default", "Сделать по умолчанию", "Зробити типовим")'),
    @('T\("Delete",[^)]+\)', 'T("Delete", "Удалить", "Видалити")'),
    @('T\("Cloud configs",[^)]+\)', 'T("Cloud configs", "Облачные конфиги", "Хмарні конфіги")'),
    @('T\("Search configs...",[^)]+\)', 'T("Search configs...", "Поиск конфигов...", "Пошук конфігів...")'),
    @('T\("New config",[^)]+\)', 'T("New config", "Новый конфиг", "Новий конфіг")'),
    @('T\("Created",[^)]+\)', 'T("Created", "Создан", "Створено")'),
    @('T\("Not signed in",[^)]+\)', 'T("Not signed in", "Нет авторизации", "Немає авторизації")'),
    @('T\("Launch through the loader to sync configs",[^)]+\)', 'T("Launch through the loader to sync configs", "Запусти через лоадер для синхронизации", "Запусти через лоадер для синхронізації")'),
    @('T\("Loaded",[^)]+\)', 'T("Loaded", "Загружен", "Завантажено")'),
    @('T\("Cloud",[^)]+\)', 'T("Cloud", "Облако", "Хмара")'),
    @('T\("Renamed",[^)]+\)', 'T("Renamed", "Переименован", "Перейменовано")'),
    @('T\("Saved",[^)]+\)', 'T("Saved", "Сохранён", "Збережено")'),
    @('T\("No configs",[^)]+\)', 'T("No configs", "Конфигов нет", "Конфігів немає")'),
    @('T\("Create a config or change the filter",[^)]+\)', 'T("Create a config or change the filter", "Создай конфиг или измени фильтр", "Створи конфіг або зміни фільтр")')
)

foreach ($r in $replacements) {
    $content = [System.Text.RegularExpressions.Regex]::Replace($content, $r[0], $r[1])
}

[System.IO.File]::WriteAllText($f, $content, [System.Text.Encoding]::UTF8)
Write-Host "Restored Cyrillic strings."
