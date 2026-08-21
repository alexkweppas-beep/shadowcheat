#include "MenuType.h"

#include "MenuState.h"

namespace Menu
{
    namespace TypeRoles
    {
        ImFont* Micro() noexcept;
        ImFont* Body() noexcept;
        ImFont* Label() noexcept;
        ImFont* Title() noexcept;
        ImFont* Display() noexcept;
        ImFont* Mono() noexcept;
        ImFont* Icon() noexcept;
        float Scale() noexcept;
    }
}

namespace MenuType
{
    namespace
    {
        // Raster sizes must match the atlas built in Menu.cpp.
        constexpr float kMicro = 12.5f;
        constexpr float kLabel = 13.f;
        constexpr float kBody = 15.f;
        constexpr float kMono = 14.5f;
        constexpr float kTitle = 17.f;
        constexpr float kDisplay = 23.f;
    }

    ImFont* Micro() noexcept { return Menu::TypeRoles::Micro(); }
    ImFont* Body() noexcept { return Menu::TypeRoles::Body(); }
    ImFont* Label() noexcept { return Menu::TypeRoles::Label(); }
    ImFont* Title() noexcept { return Menu::TypeRoles::Title(); }
    ImFont* Display() noexcept { return Menu::TypeRoles::Display(); }
    ImFont* Mono() noexcept { return Menu::TypeRoles::Mono(); }
    ImFont* Icon() noexcept { return Menu::TypeRoles::Icon(); }

    float MicroSize() noexcept
    {
        return kMicro * Menu::TypeRoles::Scale();
    }

    float BodySize() noexcept
    {
        return kBody * Menu::TypeRoles::Scale();
    }

    float LabelSize() noexcept
    {
        return kLabel * Menu::TypeRoles::Scale();
    }

    float TitleSize() noexcept
    {
        return kTitle * Menu::TypeRoles::Scale();
    }

    float DisplaySize() noexcept
    {
        return kDisplay * Menu::TypeRoles::Scale();
    }

    float MonoSize() noexcept
    {
        return kMono * Menu::TypeRoles::Scale();
    }
}
