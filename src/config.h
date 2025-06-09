#ifndef CONFIG_HEADER
#define CONFIG_HEADER

#include "utility.h"
#include <string>
#include <vector>

struct Config {
    bool found_config = false;
    
    int config_version = 1;
    
    bool dpi_auto = true;
    
    float dpi = 1;
    
    std::string font = "Segoe UI Variable Mod";
    
    std::string icons = "Segoe MDL2 Assets Mod";
    
    int starting_tab_index = 1;
    float volume = 1.0f;
    
    ArgbColor color_apps_scrollbar_gutter = ArgbColor("#ff353535");
    ArgbColor color_apps_scrollbar_default_thumb = ArgbColor("#ff5d5d5d");
    ArgbColor color_apps_scrollbar_hovered_thumb = ArgbColor("#ff868686");
    ArgbColor color_apps_scrollbar_pressed_thumb = ArgbColor("#ffaeaeae");
    
    ArgbColor color_apps_scrollbar_pressed_button = ArgbColor("#ffaeaeae");
    ArgbColor color_apps_scrollbar_hovered_button = ArgbColor("#ff494949");
    ArgbColor color_apps_scrollbar_default_button = ArgbColor("#ff353535");
    
    ArgbColor color_apps_scrollbar_pressed_button_icon = ArgbColor("#ff545454");
    ArgbColor color_apps_scrollbar_hovered_button_icon = ArgbColor("#ffffffff");
    ArgbColor color_apps_scrollbar_default_button_icon = ArgbColor("#ffffffff");
    
    ArgbColor color_pinned_icon_editor_field_default_text = ArgbColor("#ff000000");
    ArgbColor color_pinned_icon_editor_field_hovered_text = ArgbColor("#ff2d2d2d");
    ArgbColor color_pinned_icon_editor_field_pressed_text = ArgbColor("#ff020202");    
    
    ArgbColor color_pinned_icon_editor_field_default_border = ArgbColor("#ffb4b4b4");
    ArgbColor color_pinned_icon_editor_field_hovered_border = ArgbColor("#ff646464");
    ArgbColor color_pinned_icon_editor_field_pressed_border = ArgbColor("#ff0078d7");
    
    ArgbColor color_pinned_icon_editor_background = ArgbColor("#ffffffff");
    ArgbColor color_search_accent = ArgbColor("#ff0078d7");
    ArgbColor color_wifi_icons = ArgbColor("#ffffffff");
    
    ArgbColor color_wifi_default_button = ArgbColor("#00ffffff");
    ArgbColor color_wifi_hovered_button = ArgbColor("#22ffffff");
    ArgbColor color_wifi_pressed_button = ArgbColor("#44ffffff");
    
    ArgbColor color_pinned_icon_editor_cursor = ArgbColor("#ff000000");
};

extern Config *config;

void config_load();

void config_save();

#endif