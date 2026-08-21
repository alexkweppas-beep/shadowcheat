import os

with open('src/ui_imgui.hpp', 'r', encoding='utf-8') as f:
    content = f.read()

# Make buttons show text "Loading..." when busy
old_busy_prepare = '''                if (ui.busy_prepare) {
                    ImGui::Button("...", { btn_w, 46.f });'''
new_busy_prepare = '''                if (ui.busy_prepare) {
                    ImGui::Button("Loading...", { btn_w, 46.f });'''
content = content.replace(old_busy_prepare, new_busy_prepare)

old_busy_inject = '''                    ImGui::Button("...", { btn_w, 46.f });
                    ImGui::PopStyleColor(4);'''
new_busy_inject = '''                    ImGui::Button("Loading...", { btn_w, 46.f });
                    ImGui::PopStyleColor(4);'''
content = content.replace(old_busy_inject, new_busy_inject)

with open('src/ui_imgui.hpp', 'w', encoding='utf-8') as f:
    f.write(content)
