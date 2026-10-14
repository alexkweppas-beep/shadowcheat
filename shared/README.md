# Общий код

`shadow_shared` — заголовочная библиотека CMake. Сейчас здесь находятся вспомогательные функции диагностики в [include/shadow/dev.hpp](include/shadow/dev.hpp).

Флаг `SHADOW_DEV_BUILD` добавляет определение `SHADOW_DEV`. В корневом каркасе библиотеку подключают internal и external.
