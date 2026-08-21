#pragma once
#include <cstdint>

// =====================================================================
//  V8Offsets — V8 API и RAGE MP JS-loader адреса в multiplayer.dll.
//
//  Все адреса — ABSOLUTE (из live-дампа от 02.07.2026, imagebase
//  0x7FFE59840000). Для использования в cheat нужно вычислять через
//  GetModuleHandleA("multiplayer.dll") + RVA, где RVA = addr - 0x7FFE59840000.
//
//  Источник: IDA Pro анализ multiplayer_dump.dll (дамп из живой памяти,
//  .rdata расшифрован). V8 статически слинкован в multiplayer.dll.
//
//  Версия RAGE MP: "RAGE Multiplayer Legacy 1.1 ("
//  Версия V8: встроенная, Script::Compile и Script::Run INLINED
//             (не существуют как standalone функции).
// =====================================================================

namespace V8Offsets {
    constexpr uintptr_t LiveImagebase = 0x7FFE59840000; // multiplayer.dll base в live-процессе

    // ----- V8 API функции (RVA от multiplayer.dll) -----
    namespace Api {
        // v8::String::NewFromUtf8(Isolate* rcx, const char* rdx, NewStringType r8, int length r9)
        // -> MaybeLocal<String>  (50+ callers, частоиспользуемая)
        constexpr uintptr_t String_NewFromUtf8      = 0x7FFE59D99A90 - LiveImagebase;  // 0x159A90

        // v8::String::NewFromUtf8Literal(Isolate* rcx, const char* rdx, ...)
        constexpr uintptr_t String_NewFromUtf8Lit   = 0x7FFE59D99C10 - LiveImagebase;

        // v8::String::NewFromOneByte(Isolate* rcx, const uint8_t* rdx, ...)
        constexpr uintptr_t String_NewFromOneByte   = 0x7FFE59D99660 - LiveImagebase;

        // v8::String::NewFromTwoByte(Isolate* rcx, const uint16_t* rdx, ...)
        constexpr uintptr_t String_NewFromTwoByte   = 0x7FFE59D99810 - LiveImagebase;

        // v8::String::WriteUtf8(this rcx, ...) — запись V8 string в UTF-8 buffer
        constexpr uintptr_t String_WriteUtf8        = 0x7FFE59D8D1A0 - LiveImagebase;

        // v8::String::Write(this rcx, ...)
        constexpr uintptr_t String_Write            = 0x7FFE59DA0990 - LiveImagebase;

        // v8::Function::Call(this rcx, Context rdx, Value recv r8, int argc r9, Value* argv)
        // -> MaybeLocal<Value>
        constexpr uintptr_t Function_Call           = 0x7FFE59D8E290 - LiveImagebase;

        // v8::Function::New(Isolate* rcx, ...)
        constexpr uintptr_t Function_New            = 0x7FFE59D98790 - LiveImagebase;

        // v8::Function::NewInstance(this rcx, ...)
        constexpr uintptr_t Function_NewInstance    = 0x7FFE59D9A150 - LiveImagebase;

        // v8::Object::Get(this rcx, Context rdx, Value key r8, ...) -> MaybeLocal<Value>
        // NOTE: 0x552210 — это overload Get(context, uint32_t index).
        //       Он принимает численный индекс, а не Local<Value> key.
        constexpr uintptr_t Object_Get              = 0x7FFE59D92210 - LiveImagebase;

        // v8::Object::Get(this rcx, Context rdx, Local<Value> key r9, ...) -> MaybeLocal<Value>
        //这才是 правильный overload для lookup по string key ("eval", "mp", и т.д.).
        // ABI (подтверждён разбором call-sites в IDA):
        //   RCX = object (Local<Object>),
        //   RDX = out   (MaybeLocal<Value>*),
        //   R8  = context (Local<Context>),
        //   R9  = key    (Local<Value>)
        constexpr uintptr_t Object_GetByKey        = 0x7FFE59D92650 - LiveImagebase;

        // v8::Object::Set(this rcx, Context rdx, Value key r8, Value value r9)
        constexpr uintptr_t Object_Set              = 0x7FFE59D9C3F0 - LiveImagebase;

        // v8::Object::Has(this rcx, ...)
        constexpr uintptr_t Object_Has              = 0x7FFE59D95A20 - LiveImagebase;

        // v8::Object::Delete(this rcx, ...)
        constexpr uintptr_t Object_Delete           = 0x7FFE59D90330 - LiveImagebase;

        // v8::Object::SetAccessor(this rcx, ...) — регистрация property getter/setter
        constexpr uintptr_t Object_SetAccessor      = 0x7FFE59D8B2A0 - LiveImagebase;

        // v8::Object::ToString(this rcx, ...)
        constexpr uintptr_t Object_ToString         = 0x7FFE59D9FF00 - LiveImagebase;

        // v8::Object::New(Isolate* rcx)
        constexpr uintptr_t Object_New              = 0x7FFE59D98C50 - LiveImagebase;

        // v8::ObjectTemplate::New(Isolate* rcx, ...)
        constexpr uintptr_t ObjectTemplate_New      = 0x7FFE59D9AA50 - LiveImagebase;

        // v8::ObjectTemplate::NewInstance(this rcx, ...)
        constexpr uintptr_t ObjectTemplate_NewInst  = 0x7FFE59D99D80 - LiveImagebase;

        // v8::FunctionTemplate::New(Isolate* rcx, ...)
        constexpr uintptr_t FunctionTemplate_New    = 0x7FFE59D90B10 - LiveImagebase;

        // v8::FunctionTemplate::SetCallHandler(this rcx, ...)
        constexpr uintptr_t FunctionTemplate_SetCallHandler = 0x7FFE59D9D310 - LiveImagebase;

        // v8::FunctionTemplate::GetFunction(this rcx, ...)
        constexpr uintptr_t FunctionTemplate_GetFunc = 0x7FFE59D93460 - LiveImagebase;

        // v8::Context::New(...) — создание V8 context
        constexpr uintptr_t Context_New             = 0x7FFE59D99360 - LiveImagebase;

        // v8::Context::Exit(this rcx)
        constexpr uintptr_t Context_Exit            = 0x7FFE59D917C0 - LiveImagebase;

        // v8::Isolate::Dispose(Isolate* rcx)
        constexpr uintptr_t Isolate_Dispose         = 0x7FFE59D8D720 - LiveImagebase;

        // v8::External::New(Isolate* rcx, void* value)
        constexpr uintptr_t External_New            = 0x7FFE59D986A0 - LiveImagebase;

        // v8::Array::New(Isolate* rcx, ...)
        constexpr uintptr_t Array_New               = 0x7FFE59D97F10 - LiveImagebase;

        // v8::JSON::Parse(Context rcx, Value str) -> MaybeLocal<Value>
        constexpr uintptr_t JSON_Parse              = 0x7FFE59D9AC10 - LiveImagebase;

        // v8::JSON::Stringify(Context rcx, Value obj) -> MaybeLocal<String>
        constexpr uintptr_t JSON_Stringify          = 0x7FFE59D9E000 - LiveImagebase;

        // v8::Module::InstantiateModule(this rcx, ...)
        constexpr uintptr_t Module_Instantiate      = 0x7FFE59D96710 - LiveImagebase;

        // v8::Module::Evaluate(this rcx, ...)
        constexpr uintptr_t Module_Evaluate         = 0x7FFE59D91240 - LiveImagebase;

        // v8::Error::New(...)
        constexpr uintptr_t Error_New               = 0x7FFE59D90FA0 - LiveImagebase;

        // v8::RangeError::New(...)
        constexpr uintptr_t RangeError_New          = 0x7FFE59D9B280 - LiveImagebase;

        // v8::ArrayBuffer::New(...)
        constexpr uintptr_t ArrayBuffer_New         = 0x7FFE59D980D0 - LiveImagebase;

        // ----- Дополнительные адреса, подтверждённые анализом IDA -----

        // GetIsolate->GetCurrentContext-style getter:
        //   sub_7FFE59D92F40(Isolate* rcx, Local<Context>* rdx)
        //   Возвращает Local<Context> обёрнутый через handle_scope_data.
        //   Читает *(Isolate + 0x3348) -> Map -> native_context, оборачивает.
        //   Safe API: не делает harmful side-effects, не требует active
        //   HandleScope (использует handle_scope_data Isolate'а напрямую).
        constexpr uintptr_t Isolate_GetCurrentContext = 0x7FFE59D92F40 - LiveImagebase;

        // Isolate::Enter(Isolate*) — sub_7FFE59E58590
        //   Создаёт PerIsolateThreadData для текущего потока, инициализирует
        //   HandleScopeImplementer. Без этого HSI=null на render thread.
        //   ABI: RCX = Isolate
        constexpr uintptr_t Isolate_Enter           = 0x7FFE59E58590 - LiveImagebase;

        // Isolate::Exit() — sub_7FFE59E58700
        //   Decrement entry_count, cleanup PerIsolateThreadData if 0.
        //   ABI: RCX = Isolate (or reads from TLS)
        constexpr uintptr_t Isolate_Exit            = 0x7FFE59E58700 - LiveImagebase;
    }

    // ----- Внутренние offsets в V8 Isolate (подтверждены IDA + live-дампом) -----
    namespace IsolateOffsets {
        // *(Isolate + kContextOffset) = текущий Context как tagged heap pointer
        //   Подтверждено: v8::Context::Exit пишет сюда saved_context:
        //     mov [rdi-65A8h], rax  где rdi = Isolate + 0x98F0
        //     => Isolate + 0x98F0 - 0x65A8 = Isolate + 0x3348
        //   Также читается в sub_7FFE59D92F40 (context getter):
        //     mov rax, [rcx+3348h]
        constexpr uintptr_t kContextOffset           = 0x3348;

        // *(Isolate + kHandleScopeDataNext) = HandleScope::next (handle slot ptr)
        //   Используется каждым V8 API для выделения handle slots.
        //   ВАЖНО: 42792 decimal = 0xA728 (не 0xA748!).
        constexpr uintptr_t kHandleScopeDataNext    = 0xA728;
        constexpr uintptr_t kHandleScopeDataLimit   = 0xA730;
        // level (int, 4 bytes) — инкрементируется HandleScope ctor.
        //   check в sub_7FFE59DB5D60: if (level == sealed_level) FATAL.
        constexpr uintptr_t kHandleScopeDataLevel   = 0xA738;
        constexpr uintptr_t kHandleScopeDataSealed  = 0xA73C;

        // *(Isolate + kHandleScopeImplementer) = HandleScopeImplementer*
        //   Используется sub_7FFE59D92F40 и sub_7FFE59DB5F00 (handle allocator).
        //   HSI содержит entered_contexts_ / saved_contexts_ векторы.
        constexpr uintptr_t kHandleScopeImplementer = 0xA740;

        // entered_contexts_ / saved_contexts_ внутри HandleScopeImplementer
        //   Подтверждено разбором v8::Context::Exit:
        //     mov r10, [r8+30h]  ; entered_contexts_.size
        //     mov rcx, [r8+20h]  ; entered_contexts_.data
        //     dec qword [r8+48h] ; is_microtask_context_.size
        //     mov rcx, [r8+60h]  ; saved_contexts_.size
        //     mov rax, [r8+50h]  ; saved_contexts_.data
        constexpr uintptr_t kEnteredContextsData    = 0x20;
        constexpr uintptr_t kEnteredContextsSize    = 0x30;
        constexpr uintptr_t kMicrotaskContextsData  = 0x38;
        constexpr uintptr_t kMicrotaskContextsSize  = 0x48;
        constexpr uintptr_t kSavedContextsData      = 0x50;
        constexpr uintptr_t kSavedContextsSize      = 0x60;

        // TLS key для V8 g_current_isolate_ (DWORD по этому RVA в .data)
        constexpr uintptr_t kIsolateTlsKeyRva       = 0x15602C8;
    }

    // ----- RAGE MP JS-loader -----
    namespace RageMp {
        // ГЛАВНЫЙ JS-LOADER: sub_7FFE59A97AC0 (10018 bytes)
        //   Строки: "script", "module", "current_path()", "absolute(p1,p2)",
        //          "canonical(p1,p2)", "invalid char filename argument",
        //          "index.js", "index.mjs", "index.wasm"
        //   249 basic blocks, cyclomatic complexity 125
        //   Вызывает v8::Context::Exit, v8::Isolate::Dispose, v8::String::WriteUtf8, и др.
        //   Сигнатура: сложная (10KB функция, много аргументов)
        //   0 прямых callers — вызывается через function pointer
        constexpr uintptr_t JsLoader_Main           = 0x7FFE59A97AC0 - LiveImagebase;  // 0x257AC0

        // Функция рядом (4172 bytes), использует "eval", ":", "\n"
        //   4 callers: sub_7FFE59A9D370, sub_7FFE59A9D590, sub_7FFE59A9D830, sub_7FFE59B95BB0
        //   141 basic blocks, complexity 73
        //   Возможно — обработчик eval-выражений или JS expression evaluator
        constexpr uintptr_t EvalHandler             = 0x7FFE59A9C0C0 - LiveImagebase;

        // Крупный caller of NewFromUtf8 (7081 bytes)
        //   Строки: "RAGE Multiplayer", "\\..\\"
        //   Вызывает v8::Module::GetUnboundScript, v8::String::WriteUtf8
        //   149 basic blocks, complexity 59
        //   Возможно — JS module loader (ES modules)
        constexpr uintptr_t JsModuleLoader          = 0x7FFE59A9D9D0 - LiveImagebase;

        // Функция с "RAGE Multiplayer" строкой (2942 bytes)
        //   Также использует "480fps", "720fps", "960fps" — VIDEO/RENDERING, НЕ JS
        constexpr uintptr_t VideoFpsFunc            = 0x7FFE599AA520 - LiveImagebase;

        // ----- V8 helpers (найдены при анализе JS-loader) -----
        // V8 HandleScope constructor (78 bytes, 50+ callers)
        //   Инициализирует структуру V8 handle'ов: копирует поля из source
        //   sub_7FFE59D8D150(rcx=output, rdx=source) -> rcx
        constexpr uintptr_t V8_HandleScope_ctor     = 0x7FFE59D8D150 - LiveImagebase;

        // V8 flag parser wrapper (39 bytes)
        //   strlen + вызов tokenizer + вызов flag parser
        constexpr uintptr_t V8_FlagParser_wrapper   = 0x7FFE59D9DA70 - LiveImagebase;

        // V8 flag list parser (13700 bytes)
        //   Строки: "harmony", "jitless", "lite_mode", "future", "es_staging", и др.
        constexpr uintptr_t V8_FlagList_parser      = 0x7FFE59DA2C80 - LiveImagebase;

        // String tokenizer (559 bytes) — разделяет строку на токены
        constexpr uintptr_t String_tokenizer        = 0x7FFE59DA6F20 - LiveImagebase;

        // Isolate::Enter / Context::Enter (120 bytes)
        //   sub_7FFE59DA13A0(a1=scope_obj, a2=isolate)
        //   Входит в V8 isolate/context, обновляет thread-local state
        constexpr uintptr_t V8_Isolate_Enter        = 0x7FFE59DA13A0 - LiveImagebase;

        // Isolate::Exit cleanup (177 bytes)
        //   sub_7FFE59DA10F0(a1=scope_obj) — выходит из isolate, cleanup
        constexpr uintptr_t V8_Isolate_Exit         = 0x7FFE59DA10F0 - LiveImagebase;

        // HandleScope destructor (349 bytes, 32+ callers)
        constexpr uintptr_t V8_HandleScope_dtor     = 0x7FFE59D8D870 - LiveImagebase;

        // Compile+Run thunk (4078 bytes, только ~150 bytes кода)
        //   Вызывается из JS-loader, делает 3 calls в .text0 (V8 internal)
        //   Сигнатура: (rcx=isolate/ctx, rdx=bool flag, r8, r9, + 5 stack args)
        constexpr uintptr_t CompileRun_thunk        = 0x7FFE59A9F580 - LiveImagebase;

        // V8 internal functions в .text0 (не проанализированы IDA)
        //   Это предположительно Script::Compile и Script::Run (inlined)
        constexpr uintptr_t V8_internal_1           = 0x7FFE5BEED6AE - LiveImagebase;  // 0x256D6AE
        constexpr uintptr_t V8_internal_2           = 0x7FFE5B3A02DA - LiveImagebase;  // 0x21C02DA
    }

    // ----- V8 API строки в .rdata (для xref-анализа) -----
    namespace Strings {
        constexpr uintptr_t V8_Script_Run           = 0x7FFE5A8AD260 - LiveImagebase;
        constexpr uintptr_t V8_ScriptCompiler_Compile = 0x7FFE5A8AD458 - LiveImagebase;
        constexpr uintptr_t V8_String_NewFromUtf8   = 0x7FFE5A8ADAF8 - LiveImagebase;
        constexpr uintptr_t V8_Isolate_Dispose      = 0x7FFE5A8AE040 - LiveImagebase;
        constexpr uintptr_t RAGE_Multiplayer        = 0x7FFE5A871938 - LiveImagebase;
        constexpr uintptr_t RAGE_Multiplayer_Legacy = 0x7FFE5A8698C8 - LiveImagebase;
        constexpr uintptr_t Index_js                = 0x7FFE5A87BC40 - LiveImagebase;
        constexpr uintptr_t Index_mjs               = 0x7FFE5A87BBF0 - LiveImagebase;
        constexpr uintptr_t JS_Startup_Error_1      = 0x7FFE5A87B948 - LiveImagebase;
        constexpr uintptr_t Could_not_locate_file   = 0x7FFE5A87B9C0 - LiveImagebase;
        constexpr uintptr_t Eval_str                = 0x7FFE5A87B7E8 - LiveImagebase;
    }
}

// =====================================================================
//  ИТОГИ АНАЛИЗА:
//
//  1. V8 статически слинкован в multiplayer.dll (нет отдельного v8.dll)
//  2. .rdata в статическом multiplayer.dll зашифрован (все 0xFF),
//     расшифровывается в рантайме. IDA-анализ работает только на live-дампе.
//  3. V8 API функции найдены через xrefs к CHECK-failed строкам
//     ("v8::String::NewFromUtf8" и т.д. используются в assertion-макросах)
//  4. v8::Script::Compile и v8::Script::Run INLINED — не существуют как
//     standalone функции. Нельзя вызвать напрямую.
//  5. RAGE MP JS-loader найден: sub_7FFE59A97AC0 (10KB, использует
//     "script"/"module"/filesystem-строки). Сигнатура сложная.
//
//  РЕКОМЕНДАЦИЯ ДЛЯ V8 EXECUTOR:
//    Option A: 0.3.7 hijack trick — HWBP на NewFromUtf8, подмена input string
//    Option B: Найти C++ биндинги entity.getVariable / player.name напрямую
//    Option C: Через v8::Function::Call вызвать eval() (нужно Isolate+Context)
// =====================================================================
