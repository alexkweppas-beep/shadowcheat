import os

# 1. Update main.cpp to remove the hardcoded cross and update fn_close logic
with open('src/main.cpp', 'r', encoding='utf-8') as f:
    main_content = f.read()

old_fn_close = 'ui.fn_close = [&]() { ui.want_close = true; };'
new_fn_close = '''ui.fn_close = [&]() {
    if (is_cheat_running()) {
      ShowWindow(hwnd, SW_HIDE);
      is_hidden = true;
    } else {
      ui.want_close = true;
    }
  };'''
main_content = main_content.replace(old_fn_close, new_fn_close)

start_marker = '// ── Close button (X)'
end_marker = '// ── Контент по экрану'
start_idx = main_content.find(start_marker)
end_idx = main_content.find(end_marker)
if start_idx != -1 and end_idx != -1:
    main_content = main_content[:start_idx] + main_content[end_idx:]

with open('src/main.cpp', 'w', encoding='utf-8') as f:
    f.write(main_content)

# 2. Update ui_imgui.hpp to fix the "Select product" position and button rendering
with open('src/ui_imgui.hpp', 'r', encoding='utf-8') as f:
    lines = f.readlines()

start_idx = -1
for i, line in enumerate(lines):
    if line.startswith('static void DrawTopBarTypewriter(LoaderUI& ui'):
        start_idx = i
        break

new_lines = lines[:start_idx]

dashboard_code = '''static void DrawTopBarTypewriter(LoaderUI& ui, ImVec2 block_min, ImVec2 block_max, float rounding)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wsz = ImGui::GetWindowSize();
    
    dl->AddRectFilled(block_min, block_max, IM_COL32(22, 22, 24, 255), rounding);

    float top_w = block_max.x - block_min.x;
    float top_h = block_max.y - block_min.y;

    ImGui::SetCursorPos({block_min.x - ImGui::GetWindowPos().x, block_min.y - ImGui::GetWindowPos().y});
    ImGui::InvisibleButton("##drag", {top_w - 100.f, top_h});
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        ImGui::SetWindowPos(w, {w->Pos.x + d.x, w->Pos.y + d.y});
    }

    struct Phrase { const char* text; };
    static const Phrase kPhrases[] = {
        { "shadow cheats" },
        { "shadowcheats.cc" },
        { "t.me/cheats_gta" }
    };
    constexpr int kCount = 3;

    enum class TypeState { Typing, Hold, Erasing, Pause };
    static int       idx        = 0;
    static TypeState st         = TypeState::Typing;
    static float     state_t0   = (float)ImGui::GetTime() + 0.45f;
    static int       chars      = 0;
    static unsigned  seed       = 0xCAFEBABEu;

    const float per_type_char  = 0.07f;
    const float per_erase_char = 0.04f;
    const float hold_time      = 1.8f;
    const float pause_time     = 0.3f;

    const Phrase& cur = kPhrases[idx];
    const int     len = (int)strlen(cur.text);
    const float   now = (float)ImGui::GetTime();
    const float   dt  = now - state_t0;

    switch (st) {
    case TypeState::Typing: {
        if (dt < 0.f) { chars = 0; break; }
        chars = (int)(dt / per_type_char);
        if (chars >= len) { chars = len; st = TypeState::Hold; state_t0 = now; }
        break;
    }
    case TypeState::Hold:
        if (dt >= hold_time) { st = TypeState::Erasing; state_t0 = now; }
        break;
    case TypeState::Erasing: {
        int erased = (int)(dt / per_erase_char);
        chars = len - erased;
        if (chars <= 0) { chars = 0; st = TypeState::Pause; state_t0 = now; }
        break;
    }
    case TypeState::Pause:
        if (dt >= pause_time) {
            int next;
            do {
                seed = seed * 1664525u + 1013904223u;
                next = (int)(seed % (unsigned)kCount);
            } while (next == idx && kCount > 1);
            idx      = next;
            chars    = 0;
            st       = TypeState::Typing;
            state_t0 = now;
        }
        break;
    }

    std::string text_to_draw = std::string(cur.text).substr(0, chars);
    if (fmod(now, 1.0f) < 0.5f && st != TypeState::Erasing) {
        text_to_draw += "|";
    }

    if (ui.font_bold) ImGui::PushFont(ui.font_bold);
    ImVec2 ts = ImGui::CalcTextSize(text_to_draw.c_str());
    
    ImGui::SetCursorPos({ block_min.x - ImGui::GetWindowPos().x + (top_w - ts.x) * 0.5f, block_min.y - ImGui::GetWindowPos().y + (top_h - ts.y) * 0.5f });
    ImGui::TextColored(ImVec4{0.6f, 0.6f, 0.6f, 1.f}, "%s", text_to_draw.c_str());
    if (ui.font_bold) ImGui::PopFont();
    
    // Control Buttons on the right
    float btn_w = 24.f;
    float btn_h = 24.f;
    float btn_y = block_min.y - ImGui::GetWindowPos().y + (top_h - btn_h) * 0.5f;

    // Close button (X)
    ImVec2 close_pos = { block_max.x - ImGui::GetWindowPos().x - btn_w - 12.f, btn_y };
    ImGui::SetCursorPos(close_pos);
    if (ImGui::InvisibleButton("##close_btn", {btn_w, btn_h})) {
        if (ui.fn_close) ui.fn_close();
        else ui.want_close = true;
    }
    
    ImVec2 c_center = { ImGui::GetWindowPos().x + close_pos.x + btn_w * 0.5f, ImGui::GetWindowPos().y + close_pos.y + btn_h * 0.5f };
    float cross_sz = 5.f;
    ImU32 close_col = ImGui::IsItemHovered() ? IM_COL32(255, 95, 87, 255) : IM_COL32(180, 180, 180, 255);
    dl->AddLine({c_center.x - cross_sz, c_center.y - cross_sz}, {c_center.x + cross_sz, c_center.y + cross_sz}, close_col, 2.f);
    dl->AddLine({c_center.x + cross_sz, c_center.y - cross_sz}, {c_center.x - cross_sz, c_center.y + cross_sz}, close_col, 2.f);

    // Minimize button (_)
    ImVec2 min_pos = { block_max.x - ImGui::GetWindowPos().x - btn_w * 2.f - 12.f, btn_y };
    ImGui::SetCursorPos(min_pos);
    if (ImGui::InvisibleButton("##min_btn", {btn_w, btn_h})) {
        #ifdef _WIN32
        ShowWindow(GetForegroundWindow(), SW_MINIMIZE);
        #endif
    }
    
    ImVec2 m_center = { ImGui::GetWindowPos().x + min_pos.x + btn_w * 0.5f, ImGui::GetWindowPos().y + min_pos.y + btn_h * 0.5f };
    ImU32 min_col = ImGui::IsItemHovered() ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 180, 180, 255);
    dl->AddLine({m_center.x - cross_sz, m_center.y}, {m_center.x + cross_sz, m_center.y}, min_col, 2.f);
}

static void RenderDashboard(LoaderUI& ui)
{
    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImVec2      wpos = ImGui::GetWindowPos();
    ImVec2      wsz  = ImGui::GetWindowSize();

    float margin_x = 12.f; 
    float margin_y = 12.f; 
    float margin_bottom = 12.f; 
    float block_rounding = 10.f;
    
    float top_h = 44.f;
    ImVec2 tb_min = { wpos.x + margin_x, wpos.y + margin_y };
    ImVec2 tb_max = { wpos.x + wsz.x - margin_x, wpos.y + margin_y + top_h };
    
    DrawTopBarTypewriter(ui, tb_min, tb_max, block_rounding);

    float blocks_start_y = margin_y + top_h + 12.f;
    float block_h = wsz.y - blocks_start_y - margin_bottom;
    float left_w = 290.f; 
    float right_w = wsz.x - left_w - margin_x * 3.f; 

    // ── LEFT BLOCK ──
    ImVec2 lb_min = { wpos.x + margin_x, wpos.y + blocks_start_y };
    ImVec2 lb_max = { lb_min.x + left_w, lb_min.y + block_h };
    dl->AddRectFilled(lb_min, lb_max, IM_COL32(22, 22, 24, 255), block_rounding);

    ImGui::SetCursorPos({ margin_x, blocks_start_y });
    ImGui::BeginChild("##dashboard_left", { left_w, block_h }, false, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    {
        ImDrawList* cdl = ImGui::GetWindowDrawList();
        ImVec2 lp = ImGui::GetWindowPos();

        // Title: Select product (INSIDE the left block, where it belongs)
        ImGui::SetCursorPos({ 24.f, 20.f });
        if (ui.font_bold) ImGui::PushFont(ui.font_bold);
        ImGui::TextColored(ImVec4{1.f, 1.f, 1.f, 1.f}, "Select product");
        if (ui.font_bold) ImGui::PopFont();

        // Product list
        float list_y = 52.f;
        
        for (int i = 0; i < (int)ui.products.size(); i++) {
            auto& p = ui.products[i];
            if (!p.bought) continue;
            
            bool selected = (i == ui.selected_idx);
            ImVec2 item_pos { lp.x + 12.f, lp.y + list_y };
            ImVec2 item_size { left_w - 24.f, 70.f };
            float right_align_x = left_w - 24.f;

            if (selected) {
                cdl->AddRectFilled(item_pos, { item_pos.x + item_size.x, item_pos.y + item_size.y },
                                   IM_COL32(32, 32, 34, 255), block_rounding);
            }

            ImGui::SetCursorPos({ 24.f, list_y + 16.f });
            if (ui.font_bold) ImGui::PushFont(ui.font_bold);
            ImGui::TextColored(selected ? ImVec4{1.f, 1.f, 1.f, 1.f} : ImVec4{0.7f, 0.7f, 0.7f, 1.f}, "%s", p.name.c_str());
            if (ui.font_bold) ImGui::PopFont();
            
            ImVec2 st_size = ImGui::CalcTextSize(p.status.c_str());
            ImGui::SetCursorPos({ right_align_x - st_size.x, list_y + 16.f });
            ImGui::TextColored(p.ok ? ImVec4{0.29f, 0.87f, 0.5f, 1.f} : ImVec4{0.93f, 0.26f, 0.26f, 1.f}, "%s", p.status.c_str());

            const char* upd_date_str = "02.08.2026";
            ImVec2 ud_size = ImGui::CalcTextSize(upd_date_str);
            ImGui::SetCursorPos({ right_align_x - ud_size.x, list_y + 42.f });
            if (ui.font_small) ImGui::PushFont(ui.font_small);
            ImGui::TextColored(ImVec4{0.5f, 0.5f, 0.5f, 1.f}, "%s", upd_date_str);
            if (ui.font_small) ImGui::PopFont();

            ImGui::SetCursorPos({ 12.f, list_y });
            char sid[32]; snprintf(sid, sizeof(sid), "##prod_%d", i);
            if (ImGui::InvisibleButton(sid, item_size))
                ui.selected_idx = i;

            list_y += item_size.y + 4.f;
        }

        // ── USER PROFILE AT BOTTOM ──
        float footer_h = 70.f;
        float footer_y = block_h - footer_h;
        
        ImVec2 prof_pos { lp.x + 12.f, lp.y + footer_y };
        ImVec2 prof_size { left_w - 24.f, 58.f };
        bool prof_hov = ImGui::IsMouseHoveringRect(prof_pos, {prof_pos.x + prof_size.x, prof_pos.y + prof_size.y});
        if (prof_hov) {
            cdl->AddRectFilled(prof_pos, {prof_pos.x + prof_size.x, prof_pos.y + prof_size.y}, IM_COL32(255,255,255,10), block_rounding);
        }

        ImGui::SetCursorPos({ 24.f, footer_y + 20.f });
        ImGui::TextColored(ImVec4{0.9f, 0.9f, 0.9f, 1.f}, "%s", ui.username_display.empty() ? "fraud444" : ui.username_display.c_str());
        
        ImGui::SetCursorPos({ left_w - 44.f, footer_y + 19.f });
        ImGui::TextColored(ImVec4{0.6f, 0.6f, 0.6f, 1.f}, "<-]");
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsMouseClicked(0)) {
                ui.screen = Screen::Login;
                ui.logging_in = false;
            }
        }
    }
    ImGui::EndChild();

    // ── RIGHT BLOCK ──
    ImVec2 rb_min = { lb_max.x + margin_x, wpos.y + blocks_start_y };
    ImVec2 rb_max = { rb_min.x + right_w, rb_min.y + block_h };
    dl->AddRectFilled(rb_min, rb_max, IM_COL32(22, 22, 24, 255), block_rounding);

    ImGui::SetCursorPos({ lb_max.x + margin_x - wpos.x, blocks_start_y });
    ImGui::BeginChild("##dashboard_right", { right_w, block_h }, false, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    {
        if (ui.selected_idx >= 0 && ui.selected_idx < (int)ui.products.size()) {
            auto& prod = ui.products[ui.selected_idx];
            
            ImGui::SetCursorPos({ 32.f, 32.f });
            if (ui.font_bold) ImGui::PushFont(ui.font_bold);
            ImGui::TextColored(ImVec4{1.f, 1.f, 1.f, 1.f}, "%s", prod.name.c_str());
            if (ui.font_bold) ImGui::PopFont();

            ImVec2 st_size = ImGui::CalcTextSize(prod.status.c_str());
            ImGui::SetCursorPos({ right_w - 32.f - st_size.x, 32.f });
            ImGui::TextColored(prod.ok ? ImVec4{0.29f, 0.87f, 0.5f, 1.f} : ImVec4{0.93f, 0.26f, 0.26f, 1.f}, "%s", prod.status.c_str());

            float info_y = 76.f; 
            ImGui::SetCursorPos({ 32.f, info_y });
            ImGui::TextColored(ImVec4{0.6f, 0.6f, 0.6f, 1.f}, u8"Окончание подписки");
            char days_str[32]; snprintf(days_str, sizeof(days_str), "%d d", prod.days);
            ImVec2 ds_sz = ImGui::CalcTextSize(days_str);
            ImGui::SetCursorPos({ right_w - 32.f - ds_sz.x, info_y });
            ImGui::TextColored(ImVec4{1.f, 1.f, 1.f, 1.f}, days_str);
            
            float box_y = info_y + 40.f;
            float box_w = right_w - 64.f;
            float box_h = 80.f; 
            
            ImVec2 p0 = { ImGui::GetWindowPos().x + 32.f, ImGui::GetWindowPos().y + box_y };
            ImGui::GetWindowDrawList()->AddRectFilled(p0, {p0.x + box_w, p0.y + box_h}, IM_COL32(255,255,255,5), block_rounding);
            
            ImGui::SetCursorPos({ 48.f, box_y + 16.f });
            ImGui::PushTextWrapPos(right_w - 48.f);
            ImGui::TextColored(ImVec4{0.8f, 0.8f, 0.8f, 1.f}, "%s", prod.desc_ru.c_str());
            ImGui::PopTextWrapPos();

            float btn_y = block_h - 70.f;
            float btn_w = (box_w - 12.f) * 0.5f;
            
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, block_rounding);
            
            if (ui.phase == Phase::Launch) {
                ImGui::SetCursorPos({ 32.f, btn_y });
                ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(36, 36, 38, 255));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(46, 46, 48, 255));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(26, 26, 28, 255));
                ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32(230, 230, 230, 255));
                
                if (ui.busy_prepare) {
                    ImGui::Button("...", { btn_w, 46.f });
                } else {
                    if (ImGui::Button(u8"Инжект", { btn_w, 46.f })) {
                        if (ui.fn_prepare) {
                            ui.busy_prepare = true;
                            ui.last_prepare_result = "__pending__";
                        }
                    }
                }
                ImGui::PopStyleColor(4);
            } else {
                ImGui::SetCursorPos({ 32.f, btn_y });
                if (ui.busy_inject) {
                    ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(36, 36, 38, 255));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(36, 36, 38, 255));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(36, 36, 38, 255));
                    ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32(230, 230, 230, 255));
                    ImGui::Button("...", { btn_w, 46.f });
                    ImGui::PopStyleColor(4);
                } else {
                    float cancel_w = 46.f;
                    float main_w = btn_w - cancel_w - 8.f;

                    ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(36, 36, 38, 255));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(46, 46, 48, 255));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(26, 26, 28, 255));
                    ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32(230, 230, 230, 255));
                    
                    if (ImGui::Button(u8"Запустить", { main_w, 46.f })) {
                        if (ui.fn_inject) {
                            ui.busy_inject = true;
                            ui.last_inject_result = "__pending__";
                        }
                    }
                    ImGui::PopStyleColor(4);

                    ImGui::SameLine(0, 8.f);
                    ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(239, 68, 68, 38));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(239, 68, 68, 64));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(239, 68, 68, 80));
                    ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32(239, 68, 68, 255));
                    if (ImGui::Button("X", { cancel_w, 46.f })) {
                        ui.phase = Phase::Launch;
                    }
                    ImGui::PopStyleColor(4);
                }
            }
            
            ImGui::SetCursorPos({ 32.f + btn_w + 12.f, btn_y });
            ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(36, 36, 38, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(46, 46, 48, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(26, 26, 28, 255));
            ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32(230, 230, 230, 255));
            if (ImGui::Button(u8"Очистить", { btn_w, 46.f })) {
                if (ui.fn_cleanup) ui.fn_cleanup();
                ui.AddToast(u8"Очистка запущена", ToastType::Success);
            }
            ImGui::PopStyleColor(4);
            
            ImGui::PopStyleVar();
        }
    }
    ImGui::EndChild();
}
'''
new_lines.append(dashboard_code + '\n')

end_idx = -1
for i in range(start_idx, len(lines)):
    if lines[i].startswith('// ─────────────────────────────────────────────────────────') and 'Render — Toast notifications' in lines[i+1]:
        end_idx = i
        break

if end_idx != -1:
    new_lines.extend(lines[end_idx:])

with open('src/ui_imgui.hpp', 'w', encoding='utf-8') as f:
    f.writelines(new_lines)
print('Success!')
