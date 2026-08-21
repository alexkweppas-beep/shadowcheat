#pragma once
#include "../headers/flags.h"
#include "imgui.h"
#include "security/xorstr.hpp"
#include <d3d11.h>
#include <string>
#include <vector>
#include <windows.h>


struct subtab_t {
  std::string name{};
  std::vector<std::string> weapons_list{};
};

struct tab_t {
  std::string icon{};
  std::string name{};
  std::vector<subtab_t> subtabs_list{};
};

class c_variables {
public:
  struct {
    float dpi = 1.0f;
    int stored_dpi = 100;
    bool dpi_changed = true;
  } gui;

  struct {
    std::vector<tab_t> list = {
        tab_t{xorstr_("I"), xorstr_("Aimbot"),
              std::vector<subtab_t>{subtab_t{xorstr_("Aimbot")},
                                    subtab_t{xorstr_("Trigger")},
                                    subtab_t{xorstr_("Weapon")}}},
        tab_t{xorstr_("U"), xorstr_("Visuals"),
              std::vector<subtab_t>{
                  subtab_t{xorstr_("Players")}, subtab_t{xorstr_("Friends")},
                  subtab_t{xorstr_("Admins")}, subtab_t{xorstr_("Objects")},
                  subtab_t{xorstr_("Other")}}},
        tab_t{xorstr_("R"), xorstr_("Misc")},
        tab_t{xorstr_("H"), xorstr_("Settings"), std::vector<subtab_t>{}},
        tab_t{xorstr_("F"), xorstr_("Friends")},
        tab_t{xorstr_("P"), xorstr_("Configs")}};

    int tab_current = 0;
    int tab_stored = 0;

    int subtab_current = 0;
    int subtab_stored = 0;

    int item_current = 0;
    int item_stored = 0;

    float alpha = 0.f;

    ImDrawList *draw_list;

  } tab;

  struct {
    ImDrawList *draw_list;
  } widget;

  struct {
    float alpha;
    bool active;
  } blackout;

  struct {
    ImTextureID background;
    ImTextureID logo;
  } img;

  struct {
    ID3D11Device *device;
    ID3D11DeviceContext *ctx;
    IDXGISwapChain *swap_chain;
  } dx;

  struct {
    HWND hwnd;
    RECT rc;
  } winapi;

  gui_style style;
};

inline std::unique_ptr<c_variables> var = std::make_unique<c_variables>();
