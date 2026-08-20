#pragma once

#include <cstdio>

namespace shadow {

#ifdef SHADOW_DEV
inline constexpr bool kDevBuild = true;
#else
inline constexpr bool kDevBuild = false;
#endif

template <typename... Args>
inline void log(const char* fmt, Args... args) {
    if constexpr (kDevBuild) {
        std::printf(fmt, args...);
        std::printf("\n");
    }
}

}  // namespace shadow
