#pragma once
#include "security/xorstr.hpp"

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <sstream>
#include <windows.h>
#include "memory_access.hpp"
#include "../core/globals.hpp"

namespace memory
{
    class signature
    {
        uintptr_t m_base;
        size_t m_size;
        std::unique_ptr<unsigned char[]> m_data;
        uintptr_t m_temp;

    public:
        signature(uintptr_t base_address)
        {
            m_base = base_address;
            m_size = 0;
            
            // Read PE headers to get the size of the image
            IMAGE_DOS_HEADER dos_header{};
            driver::read_physical_memory(reinterpret_cast<PVOID>(m_base), &dos_header, sizeof(IMAGE_DOS_HEADER));
            if (dos_header.e_magic != IMAGE_DOS_SIGNATURE) return;
            
            IMAGE_NT_HEADERS64 nt_headers{};
            driver::read_physical_memory(reinterpret_cast<PVOID>(m_base + dos_header.e_lfanew), &nt_headers, sizeof(IMAGE_NT_HEADERS64));
            if (nt_headers.Signature != IMAGE_NT_SIGNATURE) return;
            
            m_size = nt_headers.OptionalHeader.SizeOfImage;
            
            if (m_size > 0 && m_size < 1024 * 1024 * 500) // Sanity check, max 500MB
            {
                m_data = std::make_unique<unsigned char[]>(m_size);
                
                // Read the entire image into memory in page-sized chunks
                // This prevents the entire read from failing if a single page is unmapped/PAGE_NOACCESS
                for (size_t offset = 0; offset < m_size; offset += 4096)
                {
                    size_t to_read = 4096;
                    if (offset + to_read > m_size) to_read = m_size - offset;
                    
                    driver::read_physical_memory(reinterpret_cast<PVOID>(m_base + offset), m_data.get() + offset, static_cast<DWORD>(to_read));
                }
            }
            
            m_temp = m_base;
        }

        signature& scan(const std::string& pattern)
        {
            m_temp = 0;
            if (!m_data || m_size == 0) return *this;

            std::stringstream ss(pattern);
            std::vector<std::string> substrings;
            std::string substring;
            while (std::getline(ss, substring, ' ')) substrings.push_back(substring);
            
            std::vector<std::optional<unsigned char>> pattern_2;
            for (auto& str : substrings)
            {
                if (str == xorstr_("?") || str == xorstr_("??"))
                    pattern_2.push_back(std::nullopt);
                else
                    pattern_2.push_back(static_cast<unsigned char>(strtol(str.c_str(), nullptr, 16)));
            }

            for (size_t i = 0; i < m_size - pattern_2.size() + 1; i++)
            {
                size_t j;
                for (j = 0; j < pattern_2.size(); j++)
                {
                    if (!pattern_2[j].has_value())
                        continue;
                    if (pattern_2[j].value() != m_data[i + j])
                        break;
                }
                if (j == pattern_2.size())
                {
                    m_temp = m_base + i;
                    return *this;
                }     
            }
            return *this;
        }

        signature& add(int value) { if (m_temp) m_temp += value; return *this; }
        signature& sub(int value) { if (m_temp) m_temp -= value; return *this; }

        signature& rip() 
        { 
            if (m_temp == 0) return *this;
            int value = 0;
            driver::read_physical_memory(reinterpret_cast<PVOID>(m_temp), &value, sizeof(value));
            m_temp += value + 4;
            return *this;
        }

        template<typename T> T as() { return (T)m_temp; }

        operator uintptr_t() const { return m_temp; }
    };
}
