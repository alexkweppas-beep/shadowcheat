import re

file_path = r'd:\Downloads\123\internal\src\Menu\MenuPageSettings.cpp'

with open(file_path, 'r', encoding='utf-8') as f:
    content = f.read()

replacements = [
    (r'T\("Expired",[^)]+\)', 'T("Expired", "Истёк", "Вичерпано")'),
    (r'T\("Ending",[^)]+\)', 'T("Ending", "Заканчивается", "Закінчується")'),
    (r'T\("Active",[^)]+\)', 'T("Active", "Активен", "Активний")'),
    (r'T\("No subscription",[^)]+\)', 'T("No subscription", "Нет подписки", "Немає підписки")'),
    (r'T\("left",[^)]+\)', 'T("left", "осталось", "залишилось")'),
    (r'\{"ru", "\?[^"]+"\}', '{"ru", "Русский"}'),
    (r'\{"ua", "\?[^"]+"\}', '{"ua", "Українська"}')
]

for pat, repl in replacements:
    content = re.sub(pat, repl, content)

with open(file_path, 'w', encoding='utf-8') as f:
    f.write(content)

print("Fixed the remaining Cyrillic strings!")
