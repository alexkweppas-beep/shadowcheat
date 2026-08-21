#pragma once
#include <string_view>

namespace slog
{
    void info (std::string_view msg);
    void warn (std::string_view msg);
    void error(std::string_view msg);
}
