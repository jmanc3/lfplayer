//
// Created by jmanc3 on 2/3/25.
//

#ifndef WINBAR_DRAWER_H
#define WINBAR_DRAWER_H

#include "container.h"
#include "utility.h"


struct ClientTexture {
    ImmediateTexture *texture = new ImmediateTexture;
    AppClient *client = nullptr;
    std::weak_ptr<bool> lifetime;
};

struct gl_surface {
    bool valid = false; // If not valid, texture needs to be copied from src_surface again
    
    std::vector<ClientTexture *> textures;
        
    gl_surface();
    
    AppClient *creation_client = nullptr; // Which client the gl_surface is valid for
};

class HoverableButton : public UserData {
public:
    bool hovered = false;
    double hover_amount = 0;
    ArgbColor color = ArgbColor(0, 0, 0, 0);
    int previous_state = -1;
    
    std::string text;
    std::string icon;
    
    int color_option = 0;
    ArgbColor actual_border_color = ArgbColor(0, 0, 0, 0);
    ArgbColor actual_gradient_color = ArgbColor(0, 0, 0, 0);
    ArgbColor actual_pane_color = ArgbColor(0, 0, 0, 0);
};

class IconButton : public HoverableButton {
public:
    cairo_surface_t *surface__ = nullptr;
    gl_surface *gsurf = nullptr;
    
    // These three things are so that opening menus with buttons toggles between opening and closing
    std::string invalidate_button_press_if_client_with_this_name_is_open;
    bool invalid_button_down = false;
    long timestamp = 0;
    
    IconButton() {
        gsurf = new gl_surface();
    }
    
    ~IconButton() {
        cairo_surface_destroy(surface__);
        delete gsurf;
    }
};


class ButtonData : public IconButton {
public:
    std::string text;
    std::string full_path;
    bool valid_button = true;
};


void draw_colored_rect(AppClient *client, const ArgbColor &color, const Bounds &bounds);

void draw_round_rect(AppClient *client, const ArgbColor &color, const Bounds &bounds, float round, float stroke_w = 0.0f);

void draw_margins_rect(AppClient *client, const ArgbColor &color, const Bounds &bounds, double width, double pad);

struct gl_surface;

void draw_gl_texture(AppClient *client, gl_surface *gl_surf, cairo_surface_t *surf, int x, int y, int w = 0, int h = 0);

FontReference *draw_get_font(AppClient *client, int size, std::string font, bool bold = false, bool italic = false);

struct FontText {
    FontReference *f;
    float w;
    float h;
};

FontText draw_text_begin(AppClient *client, int size, std::string font, float r, float g, float b, float a, std::string text, bool bold = false, bool italic = false);

void draw_text(AppClient *client, int size, std::string font, float r, float g, float b, float a, std::string text, Bounds bounds, int alignment = 5, int x_off = -1, int y_off = -1);

void draw_clip_begin(AppClient *client, const Bounds &b);

void draw_clip_end(AppClient *client);

void draw_operator(AppClient *client, int op);

void draw_push_temp(AppClient *client);

void draw_pop_temp(AppClient *client);

void
rounded_rect(AppClient *client, double corner_radius, double x, double y, double width, double height, ArgbColor color, float stroke_w = 0);


#endif //WINBAR_DRAWER_H
