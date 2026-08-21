#pragma once
#include "../../config/settings.hpp"
#include "../../core/globals.hpp"
#include "../../security/vmp_markers.hpp"
#include "security/xorstr.hpp"
#include <chrono>
#include <thread>
#include <windows.h>


namespace features {
inline void AntiAfkLoop() {
  while (g.Run) {
    if (cfg::anti_afk) {
      // Sleep for a long time but check g.Run and cfg::anti_afk periodically
      for (int i = 0; i < 10; i++) // 10 seconds
      {
        if (!g.Run || !cfg::anti_afk)
          break;
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }

      if (g.Run && cfg::anti_afk && g.GameHwnd == GetForegroundWindow()) {
        // Simulate pressing 'A' then 'D'
        keybd_event('A', MapVirtualKey('A', MAPVK_VK_TO_VSC), 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        keybd_event('A', MapVirtualKey('A', MAPVK_VK_TO_VSC), KEYEVENTF_KEYUP,
                    0);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        keybd_event('D', MapVirtualKey('D', MAPVK_VK_TO_VSC), 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        keybd_event('D', MapVirtualKey('D', MAPVK_VK_TO_VSC), KEYEVENTF_KEYUP,
                    0);
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
  }
}
} // namespace features
