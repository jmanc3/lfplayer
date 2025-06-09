#include "edit_info.h"
#include "main.h"
#include "drawer.h"

static void paint_root(AppClient *client, cairo_t *cr, Container *c) {
    draw_colored_rect(client, ArgbColor(.93, .93, .93, 1), c->real_bounds);
}

void edit_info(EditType type, std::string name) {
    Settings settings;
    auto client = client_new(app, settings, "settings");
    client->root->when_paint = paint_root;
    
    std::string title = "Edit Info: \"" + name + "\"";
    xcb_ewmh_set_wm_name(&app->ewmh, client->window, title.length(), title.c_str());
    
    
    client_show(app, client);
}
    