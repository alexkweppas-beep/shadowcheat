#pragma once
#include <string>
#include <vector>

namespace config_system {
    extern std::string g_hwid;
    extern std::string g_token;
    extern std::string g_username;

    void set_creds(const std::string& token, const std::string& hwid, const std::string& username = "");
    void init();
    std::vector<std::string> get_configs();
    void save(const std::string& name);
    void load(const std::string& name);
    void remove(const std::string& name);
}
