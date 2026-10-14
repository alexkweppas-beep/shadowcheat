#include "security/xorstr.hpp"
#include "security.hpp"
#include "../security/vmp_markers.hpp"
#include "anti_analysis.hpp"
#include "advanced_tricks.hpp"
#include "honeypot.hpp"
#include "api_hash.hpp"
#include "vmp_markers.hpp"
#include "../../../../фикс/лоадер/src/ban_reporter.hpp"
#include <thread>
#include <chrono>
#include <atomic>
#include <string>

 
//  TLS Callback — вызывается ДО WinMain / DllMain
//  Критически важно: ставим здесь базовые проверки
 
static void NTAPI ShadowTlsCallback(PVOID, DWORD dwReason, PVOID) {
    SHADOW_PROTECT_MUT("TlsCallback");
    if (dwReason == DLL_PROCESS_ATTACH) {
        // Скрыть поток немедленно
        // API::HideThreadFromDebugger();

        // Базовые PEB проверки до main
        /*
        if (shadow::anti_debug::peb_being_debugged()
         || shadow::anti_debug::peb_nt_global_flag()
         || shadow::anti_debug::heap_flags()) {
            // Нет токена ещё — просто немедленный краш без репорта
            *(volatile int*)nullptr = 0;
        }
        */

        // VM check (без репорта — токена нет)
        /*
        if (shadow::anti_vm::cpuid_hypervisor_bit()
         && shadow::anti_vm::cpuid_hv_vendor()) {
            TerminateProcess(GetCurrentProcess(), 0xBAADF00D);
        }
        */

        // Инициализировать API кеш (только cached — без RWX VirtualAlloc)
        // API::direct::Init() вызывает MakeSyscallStub() → VirtualAlloc(PAGE_EXECUTE_READWRITE)
        // что на Windows с CET/Hardware Stack Protection убивает процесс через
        // STATUS_DYNAMIC_CODE_BLOCKED (kernel-level kill, не exception, VEH не поймает).
        // Переносим direct::Init() в security::Init() после полной инициализации.
        API::cached::Init();
        // API::direct::Init();  // ← УБРАНО из TLS callback, см. Init() ниже

        // Инициализировать honeypot систему
        shadow::honeypot::InitAll();
    }
    SHADOW_PROTECT_END();
}

// Регистрация TLS callback в PE-таблице
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma const_seg(".CRT$XLB")
extern "C" const PIMAGE_TLS_CALLBACK shadow_tls_cb = ShadowTlsCallback;
#pragma const_seg()

namespace shadow::security {

    // ─── Состояние ───
    static std::atomic<bool>  g_running{ false };
    static std::atomic<bool>  g_honeypot_pending{ false };
    static std::chrono::steady_clock::time_point g_honeypot_fire_time;

    static void SecLog(const char* msg) {
        // Logging disabled
        return;
    }

     
    //  Init — вызвать первым в WinMain, до всего
     
    void Init(const std::string& token, const std::string& hwid) {
        SHADOW_PROTECT_MUT("SecurityInit");
        SecLog("[2.1] SecInit: entering");

        // 2. Ban reporter инициализация
        SecLog("[2.2] SecInit: before ban::Init");
        shadow::ban::Init(token, hwid);
        SecLog("[2.3] SecInit: after ban::Init");

        // 3. Honeypot callback: немедленный тихий бан
        SecLog("[2.4] SecInit: setting honeypot callback");
        shadow::honeypot::SetCallback([](const char* trigger) {
            shadow::ban::TriggerSilent(shadow::ban::Reason::HONEYPOT_PATCHED, trigger);
        });
        SecLog("[2.5] SecInit: finished successfully");

        SHADOW_PROTECT_END();
    }

     
    //  ErasePEHeaders
     
    void ErasePEHeaders(void* base) {
        DWORD old;
        if (!base) return;
        VirtualProtect(base, 4096, PAGE_READWRITE, &old);
        SecureZeroMemory(base, 4096);
        VirtualProtect(base, 4096, old, &old);
    }

    void HideThread(HANDLE hThread) {
        // API::HideThreadFromDebugger(hThread);
    }

     
    //  ГЛАВНЫЙ SECURITY LOOP
     
    void SecurityLoop(const std::string& token, const std::string& hwid) {
        SHADOW_PROTECT_MUT("SecurityLoop");

        // Скрыть поток
        // API::HideThreadFromDebugger();
        shadow::ban::Init(token, hwid);
        g_running.store(true);

        uint32_t tick = 0;

        while (g_running.load()) {

            // OBF: junk block перед critical checks — выглядит как цикл
            //   подготовки state. Крякер который ставит BP на detection
            //   branch (DR_REGS), увидит здесь мусорную «инициализацию».
            OBF_JUNK_BLOCK(
                volatile uint32_t _obf_init = (uint32_t)GetTickCount() ^ 0xDEADBEEF;
                for (int _obf_i = 0; _obf_i < 3; _obf_i++)
                    _obf_init = (_obf_init << 3) ^ (uint32_t)_obf_i ^ 0x1337;
                (void)_obf_init;
            );


             
            //  КРИТИЧЕСКИЕ — немедленный бан
             

            // VMProtect utilizes hardware debug registers, modifies PEB debug flags, and triggers heap flags.
            //
            // DR регистры (аппаратные BP) — обёрнут в opaque-predicate
            //   для CFG diversification. Путь один и тот же, статика запутана.
            //if (OBF_ALWAYS_TRUE() && shadow::anti_debug::hardware_breakpoints()) {
            //    shadow::ban::Trigger(shadow::ban::Reason::HARDWARE_BREAKPOINT,
            //                         skCrypt("DR_REGS"));
            //}

            // Integrity .text
            //
            //if (OBF_ALWAYS_TRUE() && shadow::integrity::check()) {
            //    shadow::ban::Trigger(shadow::ban::Reason::INTEGRITY_FAIL,
            //                         skCrypt("TEXT_CRC_FAIL"));
            //}
            //

             
            //  DEBUGGER — каждый тик
            //   OBF: вместо 4 прямых OR (одна сигнатура в дампе),
            //   соберём результат в bitmask и проверим через mba_equals.
            //   Крякер приплывёт патчить всё по одной function — каждая
            //   ветка из Trigger'а должна быть отдельно вырезана.
             
            /*
            {
                uint32_t dbg_mask = 0;
                if (shadow::anti_debug::peb_being_debugged())     dbg_mask |= 1u;
                if (shadow::anti_debug::nt_debug_flags())        dbg_mask |= 2u;
                if (shadow::anti_debug::nt_debug_port())         dbg_mask |= 4u;
                if (shadow::anti_debug::nt_debug_object_handle()) dbg_mask |= 8u;
                if (shadow::obf::mba_equals((int)dbg_mask, 0) == false) {
                    shadow::ban::Trigger(shadow::ban::Reason::DEBUGGER_DETECTED,
                                         skCrypt("PEB_NT_DBG"));
                }
            }

            {
                uint32_t heap_mask = 0;
                if (shadow::anti_debug::peb_nt_global_flag())  heap_mask |= 1u;
                if (shadow::anti_debug::heap_flags())          heap_mask |= 2u;
                if (shadow::memory_checks::debug_heap_pattern())    heap_mask |= 4u;
                if (shadow::obf::mba_equals((int)heap_mask, 0) == false) {
                    shadow::ban::Trigger(shadow::ban::Reason::DEBUGGER_DETECTED,
                                         skCrypt("HEAP_DBG_FLAGS"));
                }
            }

            if (shadow::anti_debug::closeinvalid_handle_trick()) {
                shadow::ban::Trigger(shadow::ban::Reason::DEBUGGER_DETECTED,
                                     skCrypt("CLOSE_HANDLE_TRICK"));
            }

            if (shadow::anti_debug::se_debug_privilege()) {
                shadow::ban::Trigger(shadow::ban::Reason::DEBUGGER_DETECTED,
                                     skCrypt("SE_DEBUG_PRIV"));
            }
            */

             
            //  HOOKS — каждый тик
             

            //
            //if (shadow::anti_hook::ntdll_unhooked_check()
            // || shadow::anti_hook::check_iat_integrity()) {
            //    shadow::ban::Trigger(shadow::ban::Reason::HOOK_DETECTED,
            //                         skCrypt("NTDLL_HOOK"));
            //}
            //

            /*
            if (shadow::anti_hook::etw_patched()) {
                // Тихий — ETW могут патчить и AV, не баним сразу
                shadow::ban::TriggerSilent(shadow::ban::Reason::ETW_PATCHED,
                                            skCrypt("ETW_HOOK"));
            }

            if (shadow::anti_hook::amsi_patched()) {
                shadow::ban::TriggerSilent(shadow::ban::Reason::HOOK_DETECTED,
                                            skCrypt("AMSI_HOOK"));
            }
            */

             
            //  BAD PROCESSES / WINDOWS / MODULES
             

            /*
            if (shadow::process_scan::find_bad_process()) {
                shadow::ban::Trigger(shadow::ban::Reason::BAD_PROCESS,
                                     skCrypt("BAD_PROC"));
            }
            if (shadow::process_scan::find_bad_window()
             || shadow::process_scan::find_bad_window_class()) {
                shadow::ban::Trigger(shadow::ban::Reason::BAD_PROCESS,
                                     skCrypt("BAD_WND"));
            }
            if (shadow::process_scan::find_bad_module()) {
                shadow::ban::Trigger(shadow::ban::Reason::HOOK_DETECTED,
                                     skCrypt("BAD_MOD_INJECT"));
            }
            */

             
            //  ADVANCED TRICKS — каждый тик
             
            /*
            {
                auto adv = shadow::advanced::RunAdvancedChecks();
                if (adv.any_threat) {
                    // Отправить бан с причиной из advanced_tricks
                    shadow::ban::Trigger(shadow::ban::Reason::ADVANCED_THREAT,
                                         adv.first_hit ? adv.first_hit : skCrypt("ADVANCED_CHECK"));
                }
            }
            */

             
            //  HONEYPOT
             
            /*
            {
                auto hp = shadow::honeypot::CheckAll();
                if (hp.fn_patched || hp.canary_killed || hp.pipe_hit) {
                    if (!g_honeypot_pending.load()) {
                        g_honeypot_pending.store(true);
                        // Задержка 3-5 минут: крякер думает что всё ок
                        uint32_t delay_secs = 180 + (GetTickCount() % 120);
                        g_honeypot_fire_time = std::chrono::steady_clock::now()
                                             + std::chrono::seconds(delay_secs);
                        // Тихий репорт сейчас
                        shadow::ban::TriggerSilent(shadow::ban::Reason::HONEYPOT_PATCHED,
                                                    hp.first_trigger);
                    }
                }
                if (hp.dump_hit) {
                    // Дамп — немедленный бан
                    shadow::ban::Trigger(shadow::ban::Reason::HONEYPOT_PATCHED,
                                         skCrypt("DUMP_PAGE_HIT"));
                }
                // Отложенный бан
                if (g_honeypot_pending.load()
                 && std::chrono::steady_clock::now() >= g_honeypot_fire_time) {
                    shadow::ban::Trigger(shadow::ban::Reason::HONEYPOT_PATCHED,
                                         skCrypt("HONEYPOT_DELAYED_BAN"));
                }
            }
            */

             
            //  MEMORY (каждые 5 тиков)
             
            /*
            if (tick % 5 == 0) {
                if (shadow::memory_checks::suspicious_rwx_regions()) {
                    shadow::ban::TriggerSilent(shadow::ban::Reason::HOOK_DETECTED,
                                               skCrypt("SUSPICIOUS_RWX"));
                }
                if (shadow::memory_checks::debug_heap_pattern()) {
                    shadow::ban::Trigger(shadow::ban::Reason::DEBUGGER_DETECTED,
                                         skCrypt("DEBUG_HEAP_PAT"));
                }
            }
            */

            // VMProtect slows down execution and modifies timing.
            //
            //if (tick % 3 == 0) {
            //    // Нужны ОБА метода чтобы снизить ложные срабатывания
            //    if (shadow::timing::rdtsc_delta() && shadow::timing::qpc_timing()) {
            //        shadow::ban::Trigger(shadow::ban::Reason::RDTSC_TIMING,
            //                             skCrypt("DUAL_TIMING"));
            //    }
            //    if (shadow::timing::dual_timer_mismatch()) {
            //        shadow::ban::Trigger(shadow::ban::Reason::RDTSC_TIMING,
            //                             skCrypt("TIMER_MISMATCH"));
            //    }
            //}
            //

             
            //  VM / ENV (каждые 20 тиков — медленные проверки)
             
            // VMProtect creates a virtualized environment that triggers VM and heuristic checks.
            //
            //if (tick % 20 == 0) {
            //    bool is_vm = (shadow::anti_vm::cpuid_hypervisor_bit()
            //               && shadow::anti_vm::cpuid_hv_vendor())
            //              || shadow::anti_vm::vm_devices()
            //              || shadow::anti_vm::vm_registry()
            //              || shadow::anti_vm::vm_processes()
            //              || shadow::anti_vm::vm_mac_address();
            //
            //    if (is_vm) {
            //        // Не баним за VM (могут быть легитимные пользователи)
            //        // Но завершаем процесс
            //        shadow::ban::TriggerSilent(shadow::ban::Reason::VIRTUAL_MACHINE,
            //                                   skCrypt("VM_DETECTED"));
            //        TerminateProcess(GetCurrentProcess(), 0xDEAD1337);
            //    }
            //
            //    // Environment heuristics
            //    int score = 0;
            //    if (shadow::anti_vm::low_ram())              score++;
            //    if (shadow::anti_vm::low_cpu_cores())        score++;
            //    if (shadow::anti_vm::sparse_system32())      score++;
            //    if (shadow::anti_vm::low_uptime())           score++;
            //    if (shadow::anti_vm::small_disk())           score++;
            //    if (shadow::env_checks::no_display())        score++;
            //    if (shadow::env_checks::too_few_processes()) score++;
            //    if (shadow::env_checks::no_recent_files())   score++;
            //    if (shadow::anti_vm::suspicious_hostname())  score++;
            //    if (shadow::anti_vm::suspicious_username())  score++;
            //
            //    if (score >= 4) {
            //        shadow::ban::TriggerSilent(shadow::ban::Reason::ENV_HEURISTICS,
            //                                   skCrypt("ENV_SCORE"));
            //        TerminateProcess(GetCurrentProcess(), 0xDEAD1337);
            //    }
            //}
            //

             
            //  PARENT PROCESS (один раз при старте)
             
            /*
            if (tick == 0 && shadow::parent_check::bad_parent_process()) {
                shadow::ban::Trigger(shadow::ban::Reason::BAD_PARENT,
                                     skCrypt("BAD_PARENT"));
            }
            */

            tick++;
            std::this_thread::sleep_for(std::chrono::milliseconds(750));
        }

        SHADOW_PROTECT_END();
    }

    // Honeypot functions (публичные — чтобы были видны в IAT)
    bool FakeLicenseCheck()  { return shadow::honeypot::CheckLicense(); }
    bool FakeAuthBypass()    { return shadow::honeypot::AuthBypass(); }

} // namespace shadow::security





