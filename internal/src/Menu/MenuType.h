#pragma once

struct ImFont;

// Typographic roles. Inter Medium carries body copy, SemiBold is reserved
// for titles and active labels, Inter Regular is meta, and IBM Plex Mono
// renders every number, key and hash so values line up while they change.
namespace MenuType
{
    ImFont* Micro() noexcept;    // 11.5 - dense meta
    ImFont* Body() noexcept;     // 13.5 - default UI text
    ImFont* Label() noexcept;    // 12   - uppercase section captions
    ImFont* Title() noexcept;    // 15   - panel and row titles
    ImFont* Display() noexcept;  // 20   - page header
    ImFont* Mono() noexcept;     // 13   - numerals, keys, hashes
    ImFont* Icon() noexcept;     // 18   - Font Awesome

    float MicroSize() noexcept;
    float BodySize() noexcept;
    float LabelSize() noexcept;
    float TitleSize() noexcept;
    float DisplaySize() noexcept;
    float MonoSize() noexcept;
}
