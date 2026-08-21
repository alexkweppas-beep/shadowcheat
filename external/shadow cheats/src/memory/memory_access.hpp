#pragma once

#include "../driver/driver_io.hpp"
#include <cstdint>
#include <vector>

// ─── Одиночное чтение / запись (один DeviceIoControl) ────────────────────────

template <typename T>
inline T read(uint64_t address)
{
    T buffer{};
    if (!address) return buffer;
    driver::read_physical_memory(reinterpret_cast<PVOID>(address),
        &buffer, sizeof(T));
    return buffer;
}

template <typename T>
inline void write(uint64_t address, T value)
{
    if (!address) return;
    driver::write_physical_memory(reinterpret_cast<PVOID>(address),
        &value, sizeof(T));
}

inline bool read_string(uint64_t address, char* buffer, size_t maxLen)
{
    if (!buffer || !maxLen || !address) return false;
    driver::read_physical_memory(reinterpret_cast<PVOID>(address),
        buffer, static_cast<DWORD>(maxLen));
    return true;
}

// ─── Bulk read: читаем N элементов типа T подряд — ОДИН DeviceIoControl ──────
// Используй вместо цикла из N отдельных read<T>(base + i*stride)

template <typename T>
inline bool read_array(uint64_t base, T* out, size_t count)
{
    if (!base || !out || count == 0) return false;
    driver::read_physical_memory(
        reinterpret_cast<PVOID>(base),
        out,
        static_cast<DWORD>(sizeof(T) * count));
    return true;
}

// ─── Читаем массив указателей с шагом stride — ОДИН DeviceIoControl ──────────
// GTA5 хранит списки как: ptr @ base+i*stride (stride обычно 0x10).
// Функция читает блок целиком и возвращает только ненулевые указатели.
// count    — максимум элементов для чтения
// max_fill — максимум результатов в out (защита от переполнения)
inline int read_ptr_stride_array(
    uint64_t       list_base,
    size_t         count,
    size_t         stride,          // обычно 0x10
    uint64_t*      out,
    size_t         max_fill)
{
    if (!list_base || count == 0 || stride == 0 || !out || max_fill == 0)
        return 0;

    // Читаем весь блок за один вызов
    const size_t block_bytes = count * stride;
    std::vector<uint8_t> s_buf(block_bytes);

    driver::read_physical_memory(
        reinterpret_cast<PVOID>(list_base),
        s_buf.data(),
        static_cast<DWORD>(block_bytes));

    int filled = 0;
    for (size_t i = 0; i < count && (size_t)filled < max_fill; ++i)
    {
        uint64_t ptr = 0;
        memcpy(&ptr, s_buf.data() + i * stride, sizeof(uint64_t));
        if (ptr) out[filled++] = ptr;
    }
    return filled;
}
