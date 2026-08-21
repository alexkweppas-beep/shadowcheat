#pragma once
#include <string>

namespace http {
    // Выполняет GET запрос с Bearer токеном и X-HWID
    // Возвращает тело ответа или пустую строку при ошибке.
    std::string get(const std::string& url, const std::string& token = "", const std::string& hwid = "");

    // Выполняет POST запрос с JSON payload, Bearer токеном и X-HWID
    // Возвращает тело ответа или пустую строку при ошибке.
    std::string post(const std::string& url, const std::string& json_data, const std::string& token = "", const std::string& hwid = "");
}
