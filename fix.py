with open('src/ui_imgui.hpp', 'r', encoding='utf-8') as f:
    lines = f.readlines()

new_lines = lines[:290]

login_end = """        ImGui::InputTextWithHint("##pass", u8"Пароль", ui.inp_pass, sizeof(ui.inp_pass), ImGuiInputTextFlags_Password);
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);

        cy += 50.f;

        // Forgot
        ImGui::SetCursorPos({ cx, cy });
        const char* forgot_text = u8"Забыли пароль?";
        ImVec2 text_size = ImGui::CalcTextSize(forgot_text);
        ImVec2 cur_pos = ImGui::GetCursorScreenPos();
        bool hovered = ImGui::IsMouseHoveringRect(cur_pos, {cur_pos.x + text_size.x, cur_pos.y + text_size.y});
        ImGui::TextColored(hovered ? ImVec4{1.f,1.f,1.f,1.f} : Colors::TEXT_MUTED, forgot_text);
        if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsMouseClicked(0)) {
                ShellExecuteA(NULL, "open", "https://shadowcheats.cc", NULL, NULL, 1 /*SW_SHOWNORMAL*/);
            }
        }

        cy += 30.f;

        // ── Login button ──
        ImGui::SetCursorPos({ cx, cy });
        bool enter_pressed = ImGui::IsKeyPressed(ImGuiKey_Enter);

        if (ui.busy_prepare || ui.logging_in) {
            // Dots animation
            ImVec2 btn_min { fpos.x + cx, fpos.y + cy };
            ImVec2 btn_max { btn_min.x + cw, btn_min.y + 44.f };
            ImGui::GetWindowDrawList()->AddRectFilled(btn_min, btn_max,
                IM_COL32(28, 32, 35, 255), 10.f);
            DrawLoadingDots(ImGui::GetWindowDrawList(),
                { btn_min.x + cw * 0.5f, btn_min.y + 22.f },
                (float)ImGui::GetTime());
            // Placeholder чтобы курсор двигался
            ImGui::Dummy({ cw, 44.f });
        } else {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.f);
            ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(255,255,255,255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(230,230,230,255));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(200,200,200,255));
            ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32(0,0,0,255));
            bool clicked = ImGui::Button(u8"Войти##login", { cw, 44.f }) || enter_pressed;
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar();

            if (clicked) {
                std::string u = ui.inp_user;
                std::string p = ui.inp_pass;
                if (!u.empty() && !p.empty()) {
                    ui.logging_in = true;
                    on_login(u, p);
                } else {
                    ui.AddToast(u8"Введите логин и пароль", ToastType::Error);
                }
            }
        }
    }
    ImGui::EndChild();
}

// ─────────────────────────────────────────────────────────
//  Render — Dashboard
// ─────────────────────────────────────────────────────────
static void RenderDashboard(LoaderUI& ui)
{
    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImVec2      wpos = ImGui::GetWindowPos();
    ImVec2      wsz  = ImGui::GetWindowSize();

    // ── BLOCKS LAYOUT (Binware style) ──
    float margin_x = 12.f; 
    float margin_y = 12.f;
    float margin_bottom = 12.f; 
    float block_h = wsz.y - margin_y - margin_bottom;
    float left_w = 330.f; // Левый блок, как у Binware
    float right_w = wsz.x - left_w - margin_x * 3.f; 

    // ── LEFT BLOCK ──
    ImVec2 lb_min = { wpos.x + margin_x, wpos.y + margin_y };
    ImVec2 lb_max = { lb_min.x + left_w, lb_min.y + block_h };
    dl->AddRectFilled(lb_min, lb_max, IM_COL32(22, 22, 24, 255), 20.f);

    ImGui::SetCursorPos({ margin_x, margin_y });
    ImGui::BeginChild("##dashboard_left", { left_w, block_h }, false, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    {
        ImDrawList* cdl = ImGui::GetWindowDrawList();
        ImVec2 lp = ImGui::GetWindowPos();

        // Product list
        float list_y = 20.f;
        
        for (int i = 0; i < (int)ui.products.size(); i++) {
            auto& p = ui.products[i];
            if (!p.bought) continue;
            
            bool selected = (i == ui.selected_idx);
            ImVec2 item_pos { lp.x + 12.f, lp.y + list_y };
            ImVec2 item_size { left_w - 24.f, 70.f };

            // Background for selected item
            if (selected) {
                cdl->AddRectFilled(item_pos, { item_pos.x + item_size.x, item_pos.y + item_size.y },
                                   IM_COL32(32, 32, 34, 255), 12.f);
            }

            // Product icon (filled circle indicator)
            ImVec2 icon_p { item_pos.x + 16.f, item_pos.y + item_size.y * 0.5f };
            ImU32 icon_col = p.ok ? IM_COL32(255,255,255,255) : IM_COL32(239,68,68,255);
            cdl->AddCircleFilled(icon_p, 4.f, icon_col);

            // Text: Product name
            ImGui::SetCursorPos({ 32.f, list_y + 16.f });
            if (ui.font_bold) ImGui::PushFont(ui.font_bold);
            ImGui::TextColored(selected ? ImVec4{1.f, 1.f, 1.f, 1.f} : ImVec4{0.7f, 0.7f, 0.7f, 1.f}, "%s", p.name.c_str());
            if (ui.font_bold) ImGui::PopFont();
            
            // Text: Status (Right aligned)
            ImVec2 st_size = ImGui::CalcTextSize(p.status.c_str());
            ImGui::SetCursorPos({ left_w - 24.f - st_size.x, list_y + 16.f });
            ImGui::TextColored(p.ok ? ImVec4{0.29f, 0.87f, 0.5f, 1.f} : ImVec4{0.93f, 0.26f, 0.26f, 1.f}, "%s", p.status.c_str());

            // Text: Update date (Mocked for now)
            ImGui::SetCursorPos({ 32.f, list_y + 42.f });
            if (ui.font_small) ImGui::PushFont(ui.font_small);
            ImGui::TextColored(ImVec4{0.5f, 0.5f, 0.5f, 1.f}, u8"Последнее обновление: 18.07.2026");
            if (ui.font_small) ImGui::PopFont();

            // Invisible button
            ImGui::SetCursorPos({ 12.f, list_y });
            char sid[32]; snprintf(sid, sizeof(sid), "##prod_%d", i);
            if (ImGui::InvisibleButton(sid, item_size))
                ui.selected_idx = i;

            list_y += item_size.y + 4.f;
        }

        // ── USER PROFILE AT BOTTOM ──
        float footer_h = 70.f;
        float footer_y = block_h - footer_h;
        
        // Profile background (hoverable maybe?)
        ImVec2 prof_pos { lp.x + 12.f, lp.y + footer_y };
        ImVec2 prof_size { left_w - 24.f, 58.f };
        bool prof_hov = ImGui::IsMouseHoveringRect(prof_pos, {prof_pos.x + prof_size.x, prof_pos.y + prof_size.y});
        if (prof_hov) {
            cdl->AddRectFilled(prof_pos, {prof_pos.x + prof_size.x, prof_pos.y + prof_size.y}, IM_COL32(255,255,255,10), 12.f);
        }

        // Username
        ImGui::SetCursorPos({ 24.f, footer_y + 20.f });
        if (ui.font_bold) ImGui::PushFont(ui.font_bold);
        ImGui::TextColored(ImVec4{1.f, 1.f, 1.f, 1.f}, "%s", ui.username_display.empty() ? "fraud444" : ui.username_display.c_str());
        if (ui.font_bold) ImGui::PopFont();
        
        // Logout icon (Exit) on the right
        ImGui::SetCursorPos({ left_w - 44.f, footer_y + 19.f });
        if (ui.font_bold) ImGui::PushFont(ui.font_bold);
        ImGui::TextColored(ImVec4{0.6f, 0.6f, 0.6f, 1.f}, "<-]");
        if (ui.font_bold) ImGui::PopFont();
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
    ImVec2 rb_min = { lb_max.x + margin_x, wpos.y + margin_y };
    ImVec2 rb_max = { rb_min.x + right_w, rb_min.y + block_h };
    dl->AddRectFilled(rb_min, rb_max, IM_COL32(22, 22, 24, 255), 20.f);

    ImGui::SetCursorPos({ lb_max.x + margin_x - wpos.x, margin_y });
    ImGui::BeginChild("##dashboard_right", { right_w, block_h }, false, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    {
        if (ui.selected_idx >= 0 && ui.selected_idx < (int)ui.products.size()) {
            auto& prod = ui.products[ui.selected_idx];
            
            // Product Title
            ImGui::SetCursorPos({ 32.f, 32.f });
            if (ui.font_huge) ImGui::PushFont(ui.font_huge);
            ImGui::TextColored(ImVec4{1.f, 1.f, 1.f, 1.f}, "%s", prod.name.c_str());
            if (ui.font_huge) ImGui::PopFont();

            // Status (Top Right)
            ImVec2 st_size = ImGui::CalcTextSize(prod.status.c_str());
            ImGui::SetCursorPos({ right_w - 32.f - st_size.x, 42.f });
            if (ui.font_bold) ImGui::PushFont(ui.font_bold);
            ImGui::TextColored(prod.ok ? ImVec4{0.29f, 0.87f, 0.5f, 1.f} : ImVec4{0.93f, 0.26f, 0.26f, 1.f}, "%s", prod.status.c_str());
            if (ui.font_bold) ImGui::PopFont();

            // Subscription Info lines
            float info_y = 90.f;
            ImGui::SetCursorPos({ 32.f, info_y });
            ImGui::TextColored(ImVec4{0.6f, 0.6f, 0.6f, 1.f}, u8"Окончание подписки");
            char days_str[32]; snprintf(days_str, sizeof(days_str), "%d d", prod.days);
            ImVec2 ds_sz = ImGui::CalcTextSize(days_str);
            ImGui::SetCursorPos({ right_w - 32.f - ds_sz.x, info_y });
            ImGui::TextColored(ImVec4{1.f, 1.f, 1.f, 1.f}, days_str);

            ImGui::SetCursorPos({ 32.f, info_y + 26.f });
            ImGui::TextColored(ImVec4{0.6f, 0.6f, 0.6f, 1.f}, u8"Последнее обновление");
            const char* upd_str = "18.07.2026";
            ImVec2 us_sz = ImGui::CalcTextSize(upd_str);
            ImGui::SetCursorPos({ right_w - 32.f - us_sz.x, info_y + 26.f });
            ImGui::TextColored(ImVec4{1.f, 1.f, 1.f, 1.f}, upd_str);
            
            // Description box
            float box_y = info_y + 70.f;
            float box_w = right_w - 64.f;
            float box_h = block_h - box_y - 90.f;
            
            ImVec2 p0 = { ImGui::GetWindowPos().x + 32.f, ImGui::GetWindowPos().y + box_y };
            ImGui::GetWindowDrawList()->AddRectFilled(p0, {p0.x + box_w, p0.y + box_h}, IM_COL32(255,255,255,5), 12.f);
            
            ImGui::SetCursorPos({ 48.f, box_y + 16.f });
            ImGui::PushTextWrapPos(right_w - 48.f);
            ImGui::TextColored(ImVec4{0.8f, 0.8f, 0.8f, 1.f}, "%s", prod.desc_ru.c_str());
            ImGui::PopTextWrapPos();

            // Bottom Buttons
            float btn_y = block_h - 70.f;
            float btn_w = (box_w - 12.f) * 0.5f;
            
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.f);
            
            if (ui.phase == Phase::Launch) {
                // Inject button
                ImGui::SetCursorPos({ 32.f, btn_y });
                ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(36, 36, 38, 255));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(46, 46, 48, 255));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(26, 26, 28, 255));
                ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32(230, 230, 230, 255));
                
                if (ui.busy_prepare) {
                    ImGui::Button("...", { btn_w, 46.f }); // Dummy while busy
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
                // Launch Game / Inject phase
                ImGui::SetCursorPos({ 32.f, btn_y });
                if (ui.busy_inject) {
                    ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(36, 36, 38, 255));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(36, 36, 38, 255));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(36, 36, 38, 255));
                    ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32(230, 230, 230, 255));
                    ImGui::Button("...", { btn_w, 46.f }); // Dummy while busy
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

                    // Cancel button
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
            
            // Clean button
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
            
            ImGui::PopStyleVar(); // FrameRounding
        }
    }
    ImGui::EndChild();
}
"""
new_lines.append(login_end + '\n')
new_lines.extend(lines[550:])

with open('src/ui_imgui.hpp', 'w', encoding='utf-8') as f:
    f.writelines(new_lines)
