#include "security/xorstr.hpp"
#include "../headers/config.h"

void c_config::init_config()
{
    static bool init = false;

    if (!init)
    {
        // Aimbot
        add_option<checkbox_t>(xorstr_("Enable Weapon Configs"));
        add_option<dropdown_t>(xorstr_("Weapon Category"), 0, std::vector<std::string>{ xorstr_("Global"), xorstr_("Pistol"), xorstr_("SMG"), xorstr_("Rifle"), xorstr_("Shotgun"), xorstr_("Sniper"), xorstr_("Heavy") });
        add_option<checkbox_t>(xorstr_("Enable Aimbot"), false, true);
        add_option<dropdown_t>(xorstr_("Aim Type"), 0, std::vector<std::string>{ xorstr_("Vector"), xorstr_("Silent") });
        add_option<slider_t>(xorstr_("FOV"), 180.f, 0.f, 360.f, xorstr_("%.f\xC2\xB0"));
        add_option<slider_t>(xorstr_("Distance"), 500.f, 0.f, 500.f, xorstr_("%.fm"));
        add_option<slider_t>(xorstr_("Hit Chance"), 100.f, 0.f, 100.f, xorstr_("%.f%%"));
        add_option<dropdown_t>(xorstr_("Aim Bone"), 0, std::vector<std::string>{ xorstr_("Head"), xorstr_("Neck"), xorstr_("Chest"), xorstr_("Body"), xorstr_("Nearest") });
        add_option<checkbox_t>(xorstr_("Draw FOV"));
        add_option<colorpicker_t>(xorstr_("FOV Color"), ImVec4{ 1.f, 1.f, 1.f, 1.f });
        add_option<checkbox_t>(xorstr_("Only When Aiming"), true, false);
        add_option<checkbox_t>(xorstr_("Smooth"));
        add_option<dropdown_t>(xorstr_("Smooth Type"), 0, std::vector<std::string>{ xorstr_("Dynamic"), xorstr_("Constant") });
        add_option<slider_t>(xorstr_("Smooth Factor"), 1.f, 1.f, 100.f, xorstr_("%.1f"));
        
        // Trigger
        add_option<checkbox_t>(xorstr_("Enable Triggerbot"), false, true);
        add_option<slider_t>(xorstr_("Trigger Delay"), 0.f, 0.f, 100.f, xorstr_("%.f ms"));
        add_option<checkbox_t>(xorstr_("Ignore Friends"), true, false);

        // Weapon
        add_option<checkbox_t>(xorstr_("No Recoil"));
        add_option<checkbox_t>(xorstr_("No Spread"));

        // Visuals
        add_option<checkbox_t>(xorstr_("2D Box"));
        add_option<colorpicker_t>(xorstr_("Box Color"), ImVec4{ 1.f, 1.f, 1.f, 1.f });
        add_option<dropdown_t>(xorstr_("Box Type"), 2, std::vector<std::string>{ xorstr_("Border"), xorstr_("Corners"), xorstr_("Filled"), xorstr_("Filled & Corners") });
        add_option<slider_t>(xorstr_("Box Thickness"), 1.5f, 1.f, 5.f, xorstr_("%.1f"));
        add_option<slider_t>(xorstr_("Box Rounding"), 0.f, 0.f, 15.f, xorstr_("%.1f"));
        add_option<checkbox_t>(xorstr_("Health Bar"));
        add_option<dropdown_t>(xorstr_("Health Bar Pos"), 1, std::vector<std::string>{ xorstr_("Top"), xorstr_("Bottom"), xorstr_("Left"), xorstr_("Right") });
        add_option<dropdown_t>(xorstr_("Armor Bar Pos"), 1, std::vector<std::string>{ xorstr_("Top"), xorstr_("Bottom"), xorstr_("Left"), xorstr_("Right") });
        add_option<checkbox_t>(xorstr_("Custom Health Colors"));
        add_option<colorpicker_t>(xorstr_("Health Color"), ImVec4{ 0.f, 1.f, 0.f, 1.f });
        add_option<colorpicker_t>(xorstr_("Armor Color"), ImVec4{ 0.2f, 0.6f, 1.f, 1.f });
        add_option<checkbox_t>(xorstr_("Draw Distance"));
        add_option<colorpicker_t>(xorstr_("Distance Color"), ImVec4{ 1.f, 1.f, 1.f, 1.f });
        add_option<checkbox_t>(xorstr_("Draw Weapon"));
        add_option<colorpicker_t>(xorstr_("Weapon Color"), ImVec4{ 1.f, 1.f, 1.f, 1.f });
        add_option<slider_t>(xorstr_("Max Distance"), 1000.f, 50.f, 1000.f, xorstr_("%.fm"));
        add_option<checkbox_t>(xorstr_("Skeleton Lines"));
        add_option<colorpicker_t>(xorstr_("Skeleton Color"), ImVec4{ 1.f, 1.f, 1.f, 1.f });
        add_option<slider_t>(xorstr_("Skeleton Thickness"), 1.f, 1.f, 5.f, xorstr_("%.1f"));
        add_option<checkbox_t>(xorstr_("Snap Line"));
        add_option<colorpicker_t>(xorstr_("Snap Line Color"), ImVec4{ 1.f, 1.f, 1.f, 1.f });
        add_option<checkbox_t>(xorstr_("Aim Line"));
        add_option<colorpicker_t>(xorstr_("Aim Line Color"), ImVec4{ 1.f, 1.f, 1.f, 1.f });
        add_option<checkbox_t>(xorstr_("Players Only"));

        // Friends
        add_option<checkbox_t>(xorstr_("Friends 2D Box"));
        add_option<colorpicker_t>(xorstr_("Friends Box Color"), ImVec4{ 1.f, 1.f, 1.f, 1.f });
        add_option<dropdown_t>(xorstr_("Friends Box Type"), 2, std::vector<std::string>{ xorstr_("Border"), xorstr_("Corners"), xorstr_("Filled"), xorstr_("Filled & Corners") });
        add_option<slider_t>(xorstr_("Friends Box Thickness"), 1.5f, 1.f, 5.f, xorstr_("%.1f"));
        add_option<slider_t>(xorstr_("Friends Box Rounding"), 0.f, 0.f, 15.f, xorstr_("%.1f"));
        add_option<checkbox_t>(xorstr_("Friends Health Bar"));
        add_option<dropdown_t>(xorstr_("Friends Health Bar Pos"), 1, std::vector<std::string>{ xorstr_("Top"), xorstr_("Bottom"), xorstr_("Left"), xorstr_("Right") });
        add_option<dropdown_t>(xorstr_("Friends Armor Bar Pos"), 1, std::vector<std::string>{ xorstr_("Top"), xorstr_("Bottom"), xorstr_("Left"), xorstr_("Right") });
        add_option<checkbox_t>(xorstr_("Friends Custom Health Colors"));
        add_option<colorpicker_t>(xorstr_("Friends Health Color"), ImVec4{ 0.f, 1.f, 0.f, 1.f });
        add_option<colorpicker_t>(xorstr_("Friends Armor Color"), ImVec4{ 0.2f, 0.6f, 1.f, 1.f });
        add_option<checkbox_t>(xorstr_("Friends Draw Distance"));
        add_option<colorpicker_t>(xorstr_("Friends Distance Color"), ImVec4{ 1.f, 1.f, 1.f, 1.f });
        add_option<checkbox_t>(xorstr_("Friends Draw Weapon"));
        add_option<colorpicker_t>(xorstr_("Friends Weapon Color"), ImVec4{ 1.f, 1.f, 1.f, 1.f });
        add_option<slider_t>(xorstr_("Friends Max Distance"), 1000.f, 50.f, 1000.f, xorstr_("%.fm"));
        add_option<checkbox_t>(xorstr_("Friends Skeleton Lines"));
        add_option<colorpicker_t>(xorstr_("Friends Skeleton Color"), ImVec4{ 1.f, 1.f, 1.f, 1.f });
        add_option<slider_t>(xorstr_("Friends Skeleton Thickness"), 1.f, 1.f, 5.f, xorstr_("%.1f"));
        add_option<checkbox_t>(xorstr_("Friends Snap Line"));
        add_option<colorpicker_t>(xorstr_("Friends Snap Line Color"), ImVec4{ 1.f, 1.f, 1.f, 1.f });
        add_option<checkbox_t>(xorstr_("Friends Aim Line"));
        add_option<colorpicker_t>(xorstr_("Friends Aim Line Color"), ImVec4{ 1.f, 1.f, 1.f, 1.f });

        // Admins
        add_option<checkbox_t>(xorstr_("Admins 2D Box"));
        add_option<colorpicker_t>(xorstr_("Admins Box Color"), ImVec4{ 1.f, 1.f, 1.f, 1.f });
        add_option<dropdown_t>(xorstr_("Admins Box Type"), 2, std::vector<std::string>{ xorstr_("Border"), xorstr_("Corners"), xorstr_("Filled"), xorstr_("Filled & Corners") });
        add_option<slider_t>(xorstr_("Admins Box Thickness"), 1.5f, 1.f, 5.f, xorstr_("%.1f"));
        add_option<slider_t>(xorstr_("Admins Box Rounding"), 0.f, 0.f, 15.f, xorstr_("%.1f"));
        add_option<checkbox_t>(xorstr_("Admins Health Bar"));
        add_option<dropdown_t>(xorstr_("Admins Health Bar Pos"), 1, std::vector<std::string>{ xorstr_("Top"), xorstr_("Bottom"), xorstr_("Left"), xorstr_("Right") });
        add_option<dropdown_t>(xorstr_("Admins Armor Bar Pos"), 1, std::vector<std::string>{ xorstr_("Top"), xorstr_("Bottom"), xorstr_("Left"), xorstr_("Right") });
        add_option<checkbox_t>(xorstr_("Admins Custom Health Colors"));
        add_option<colorpicker_t>(xorstr_("Admins Health Color"), ImVec4{ 0.f, 1.f, 0.f, 1.f });
        add_option<colorpicker_t>(xorstr_("Admins Armor Color"), ImVec4{ 0.2f, 0.6f, 1.f, 1.f });
        add_option<checkbox_t>(xorstr_("Admins Draw Distance"));
        add_option<colorpicker_t>(xorstr_("Admins Distance Color"), ImVec4{ 1.f, 1.f, 1.f, 1.f });
        add_option<checkbox_t>(xorstr_("Admins Draw Weapon"));
        add_option<colorpicker_t>(xorstr_("Admins Weapon Color"), ImVec4{ 1.f, 1.f, 1.f, 1.f });
        add_option<slider_t>(xorstr_("Admins Max Distance"), 1000.f, 50.f, 1000.f, xorstr_("%.fm"));
        add_option<checkbox_t>(xorstr_("Admins Skeleton Lines"));
        add_option<colorpicker_t>(xorstr_("Admins Skeleton Color"), ImVec4{ 1.f, 1.f, 1.f, 1.f });
        add_option<slider_t>(xorstr_("Admins Skeleton Thickness"), 1.f, 1.f, 5.f, xorstr_("%.1f"));
        add_option<checkbox_t>(xorstr_("Admins Snap Line"));
        add_option<colorpicker_t>(xorstr_("Admins Snap Line Color"), ImVec4{ 1.f, 1.f, 1.f, 1.f });
        add_option<checkbox_t>(xorstr_("Admins Aim Line"));
        add_option<colorpicker_t>(xorstr_("Admins Aim Line Color"), ImVec4{ 1.f, 1.f, 1.f, 1.f });

        // Objects
        add_option<checkbox_t>(xorstr_("Draw Objects"));
        add_option<colorpicker_t>(xorstr_("Objects Color"), ImVec4{ 1.f, 1.f, 1.f, 1.f });
        
        // Vehicles
        add_option<checkbox_t>(xorstr_("Draw Vehicles"));
        add_option<checkbox_t>(xorstr_("Vehicle Health"));

        // Misc
        add_option<slider_t>(xorstr_("Entity Update Delay"), 45.f, 0.f, 100.f, xorstr_("%.f ms"));
        add_option<slider_t>(xorstr_("Max Entities"), 256.f, 16.f, 1024.f, xorstr_("%.f"));
        add_option<checkbox_t>(xorstr_("Hide On Record"));

        // Friends
        add_option<slider_t>(xorstr_("Radius (m)"), 5.f, 1.f, 100.f, xorstr_("%.f"));
        add_option<slider_t>(xorstr_("Max dist (m)"), 30.f, 1.f, 1000.f, xorstr_("%.f"));

        // Other / Settings
        add_option<dropdown_t>(xorstr_("Theme"), 0, std::vector<std::string>{ xorstr_("Dark mode"), xorstr_("Light mode"), xorstr_("System theme") });
        add_option<dropdown_t>(xorstr_("Sidebar position"), 0, std::vector<std::string>{ xorstr_("Top left"), xorstr_("Top right"), xorstr_("Bottom left"), xorstr_("Bottom right") });
        add_option<checkbox_t>(xorstr_("Show / Hide icons"));
        add_option<slider_t>(xorstr_("Interface size"), 100.f, 100.f, 200.f, xorstr_("%.f%%"));
        add_option<slider_t>(xorstr_("Interface opacity"), 50.f, 0.f, 100.f, xorstr_("%.f%%"));
        add_option<slider_t>(xorstr_("Font size adjustment"), 100.f, 100.f, 200.f, xorstr_("%.f%%"));
        add_option<checkbox_t>(xorstr_("Always on top"));

        init = true;
    }
}
