
#include "queue.h"
#include "application.h"
#include "main.h"
#include "config.h"
#include "drawer.h"
#include "components.h"
#include "player.h"

struct QueueOption : UserData {
    std::string top;    // Song name   / Album name
    std::string middle; // Album name  / Artist Name
    std::string bottom; // Artist Name
    cairo_surface_t *surface = nullptr;
};

static void paint_root(AppClient *client, cairo_t *cr, Container *c) {
    // bg
    draw_colored_rect(client, ArgbColor(.973, .973, .973, 1), c->real_bounds);
    // border
    draw_round_rect(client, ArgbColor(.6, .6, .6, 1), c->real_bounds, 0, std::floor(1 * config->dpi));
}

static void paint_option(AppClient *client, cairo_t *cr, Container *c) {
    auto data = (QueueOption *) c->user_data;
    // bg
    draw_colored_rect(client, ArgbColor(.973, .973, .973, 1), c->real_bounds);
    // border
    draw_round_rect(client, ArgbColor(.6, .6, .6, .7), c->real_bounds, 6 * config->dpi, std::floor(1 * config->dpi));
    
    float ih = 0;
    {
        auto [f, w, h] = draw_text_begin(client, 10 * config->dpi, config->font, EXPAND(ArgbColor(0, 0, 0, 1)), "\uF746");
        ih = h;
        if (data->bottom.empty()) {
            f->draw_text_end(c->real_bounds.x + c->real_bounds.w - w * 2, c->real_bounds.y + c->real_bounds.h / 2 - h * .5);
        } else {
            ih /= 3;
        }
    }
    
    std::string text = data->top + "\n" + data->middle;
    if (!data->bottom.empty())
        text += "\n" + data->bottom;
    {
        auto [f, w, h] = draw_text_begin(client, 10 * config->dpi, config->font, EXPAND(ArgbColor(0, 0, 0, 1)), text);
        draw_clip_begin(client, Bounds(c->real_bounds.x, c->real_bounds.y, c->real_bounds.w - ih * 3, c->real_bounds.h));
        f->draw_text_end(c->real_bounds.x + c->real_bounds.h, c->real_bounds.y + c->real_bounds.h * .5 - h * .5);
        draw_clip_end(client);
    }
    
    if (data->surface) {
        int width = cairo_image_surface_get_height(data->surface);
        int height = cairo_image_surface_get_height(data->surface);
        
        float pad = 42 * config->dpi;
        cairo_save(cr);
        float scale = (c->real_bounds.h / (height + pad));
        cairo_scale(client->cr, scale, scale);
        float half = (c->real_bounds.h - (((float) height) * scale)) * .5;
        float shrink = 1.0 / scale;
        
        cairo_set_source_surface(cr, data->surface, 
                                     ((c->real_bounds.x + half) * shrink),
                                     ((c->real_bounds.y + half) * shrink));
        cairo_paint(cr); 
        cairo_restore(cr);
    }
}

void add_queue_items(AppClient *client, ScrollContainer *queue_scroll, const std::vector<QueueItem> &items) {
    for (auto item: items) {
        auto c = queue_scroll->content->child(FILL_SPACE, 64 * config->dpi);
        auto data = new QueueOption;
        
        std::string album_name;
        if (item.type == QueueType::ALBUM) {
            for (auto tuple : album_songs) {
                if (tuple.first == item.path) {
                    for (auto song_data : tuple.second.songs) {
                        data->top = item.path;
                        data->middle = song_data.artist;
                        album_name = data->top;
                        goto out;
                    }
                }
            }
         } else if (item.type == QueueType::SONG) {
            // item.path for song is the full path
            for (auto tuple : album_songs) {
                for (auto song_data : tuple.second.songs) {
                    if (song_data.full == item.path) {
                        data->top = song_data.name;
                        data->middle = song_data.album;
                        data->bottom = song_data.artist;
                        album_name = song_data.album;
                        goto out;
                    }
                }
            }
        }  
        out:
        
        if (!album_name.empty()) { 
            for (auto art : cached_art) {
                if (art->name == sanitize_file_name(album_name)) {
                    data->surface = accelerated_surface(app, client, art->width, art->height);
                    paint_surface_with_data(data->surface, art->data, art->width, art->height);
                }
            }
        }
        
        c->user_data = data;
        c->when_paint = paint_option;        
    }
}

void fill_root(AppClient *client) {
    auto r = client->root;
    r->when_paint = paint_root;
    r->type = ::vbox;
    double pad = 8 * config->dpi;
    
    ScrollPaneSettings scroll_settings(config->dpi);
    scroll_settings.right_inline_track = true;
    auto queue_scroll = make_newscrollpane_as_child(r, scroll_settings);
    queue_scroll->content->spacing = pad;
    queue_scroll->content->wanted_pad = Bounds(pad);
    
    add_queue_items(client, queue_scroll, player->next_items);
    add_queue_items(client, queue_scroll, player->queued_items);
}

void toggle_queue_window(AppClient *client) {
    auto queue_button = container_by_name("queue_button", client->root);
    
    if (auto c = client_by_name(app, "lfp_queue")) {
        client_close(app, c);
        return;
    }
    int option_height = 64 * config->dpi;
    int options = player->next_items.size() + player->queued_items.size();
    if (options < 3)
        options = 3;
    if (options > 9)
        options = 9;
    int option_width = 360 * config->dpi;

    Settings settings;
    settings.force_position = true;
    settings.override_redirect = true;
    settings.decorations = false;
    settings.x = client->bounds->x + queue_button->real_bounds.x + queue_button->real_bounds.w * .5 - option_width * .5;
    settings.y = client->bounds->y + queue_button->real_bounds.y + queue_button->real_bounds.h * 1.2;
    settings.w = option_width;
    settings.h = option_height * options + (8 * config->dpi * (options + 1));
    settings.dialog = true;
    PopupSettings popup_settings;
    popup_settings.name = "lfp_queue";
    auto popup = client->create_popup(popup_settings, settings);
    
    fill_root(popup);
    
    client_show(app, popup);
}
    