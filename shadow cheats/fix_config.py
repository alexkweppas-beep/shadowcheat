import re

header_path = 'src/config/settings.hpp'
cpp_path = 'src/config/config.cpp'

with open(header_path, 'r', encoding='utf-8') as f:
    header = f.read()

vars = []
for line in header.split('\n'):
    line = line.strip()
    if line.startswith('inline '):
        match = re.search(r'inline\s+([A-Za-z0-9_:]+)\s+([A-Za-z0-9_]+)', line)
        if match:
            vtype, vname = match.groups()
            if vtype in ['bool', 'int', 'float', 'ImColor']:
                vars.append((vtype, vname))

# Also aimbot_configs[7]
aimbot_vars = ['aimbot', 'aimtype', 'fov', 'aim_distance', 'aim_chance', 'aimbone', 'draw_fov', 'show_fov_only_aiming', 'smooth', 'smooth_type', 'smooth_factor', 'ignore_friends']

serialize_str = 'std::string serialize() {\n    std::stringstream ss;\n'
for vtype, vname in vars:
    if vtype == 'bool':
        serialize_str += f'    ss << "{vname}=" << (cfg::{vname} ? 1 : 0) << ";\\n";\n'
    elif vtype in ['int', 'float']:
        serialize_str += f'    ss << "{vname}=" << cfg::{vname} << ";\\n";\n'
    elif vtype == 'ImColor':
        serialize_str += f'    ss << "{vname}=" << cfg::{vname}.Value.x << "," << cfg::{vname}.Value.y << "," << cfg::{vname}.Value.z << "," << cfg::{vname}.Value.w << ";\\n";\n'

for i in range(7):
    for av in aimbot_vars:
        if av in ['aimbot', 'draw_fov', 'show_fov_only_aiming', 'smooth', 'ignore_friends']:
            serialize_str += f'    ss << "aimbot_{i}_{av}=" << (cfg::aimbot_configs[{i}].{av} ? 1 : 0) << ";\\n";\n'
        elif av in ['smooth_factor']:
            serialize_str += f'    ss << "aimbot_{i}_{av}=" << cfg::aimbot_configs[{i}].{av} << ";\\n";\n'
        else:
            serialize_str += f'    ss << "aimbot_{i}_{av}=" << cfg::aimbot_configs[{i}].{av} << ";\\n";\n'

serialize_str += '    return ss.str();\n}\n'

deserialize_str = '''void deserialize(const std::string& data) {
    std::istringstream stream(data);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        size_t delim = line.find('=');
        if (delim == std::string::npos) continue;
        std::string key = line.substr(0, delim);
        std::string val = line.substr(delim + 1);
        if (!val.empty() && val.back() == ';') val.pop_back();

'''

for vtype, vname in vars:
    deserialize_str += f'        if (key == "{vname}") {{\n'
    if vtype == 'bool':
        deserialize_str += f'            cfg::{vname} = (std::stoi(val) != 0);\n'
    elif vtype == 'int':
        deserialize_str += f'            cfg::{vname} = std::stoi(val);\n'
    elif vtype == 'float':
        deserialize_str += f'            cfg::{vname} = std::stof(val);\n'
    elif vtype == 'ImColor':
        deserialize_str += f'''            size_t c1 = val.find(',');
            size_t c2 = val.find(',', c1 + 1);
            size_t c3 = val.find(',', c2 + 1);
            if (c1 != std::string::npos && c2 != std::string::npos && c3 != std::string::npos) {{
                float r = std::stof(val.substr(0, c1));
                float g = std::stof(val.substr(c1 + 1, c2 - c1 - 1));
                float b = std::stof(val.substr(c2 + 1, c3 - c2 - 1));
                float a = std::stof(val.substr(c3 + 1));
                cfg::{vname} = ImColor(r, g, b, a);
            }}
'''
    deserialize_str += '        }\n'

for i in range(7):
    for av in aimbot_vars:
        deserialize_str += f'        if (key == "aimbot_{i}_{av}") {{\n'
        if av in ['aimbot', 'draw_fov', 'show_fov_only_aiming', 'smooth', 'ignore_friends']:
            deserialize_str += f'            cfg::aimbot_configs[{i}].{av} = (std::stoi(val) != 0);\n'
        elif av in ['smooth_factor']:
            deserialize_str += f'            cfg::aimbot_configs[{i}].{av} = std::stof(val);\n'
        else:
            deserialize_str += f'            cfg::aimbot_configs[{i}].{av} = std::stoi(val);\n'
        deserialize_str += '        }\n'

deserialize_str += '    }\n}\n'

with open(cpp_path, 'r', encoding='utf-8') as f:
    cpp_lines = f.readlines()

new_cpp = []
in_ser = False
for line in cpp_lines:
    if line.startswith('std::string serialize()'):
        in_ser = True
        new_cpp.append(serialize_str)
        new_cpp.append('\\n')
        new_cpp.append(deserialize_str)
        new_cpp.append('\\n')
    elif line.startswith('namespace fs = std::filesystem;'):
        in_ser = False
        new_cpp.append(line)
    elif not in_ser:
        new_cpp.append(line)

with open(cpp_path, 'w', encoding='utf-8') as f:
    f.writelines(new_cpp)

