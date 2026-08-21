#include "security/xorstr.hpp"
#include "logger.hpp"
#include <iostream>

namespace slog
{
    void info (std::string_view m) { std::cout << xorstr_("[INFO] ") << m << '\n'; }
    void warn (std::string_view m) { std::cout << xorstr_("[WARN] ") << m << '\n'; }
    void error(std::string_view m) { std::cerr << xorstr_("[ERR ] ") << m << '\n'; }
}
