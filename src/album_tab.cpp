
#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif


#include "album_tab.h"
#include "drawer.h"
#include "components.h"
#include "config.h"
#include "main.h"
#include "utility.h"
#include "player.h"
#include "edit_info.h"

struct AlbumSong : UserData {
    Option data;
    bool attempted = false;
    std::string time;
    double size = 10 * config->dpi;
};

struct AlbumsScrollRootData : UserData {
    std::vector<Container *> containers;
    
    Container *opening_container = nullptr;
    Container *opening = nullptr;
    Container *closing_container = nullptr;
    Container *closing = nullptr;
    
    bool first_frame = false;
    double openess = 0.0;
    double closingness = 0.0;
    int old_y = -1;
    int old_h = 0;
    cairo_surface_t *unknown_album_icon = nullptr;
    cairo_surface_t *volume_icon = nullptr;
    ArgbColor volume_color = ArgbColor(1, 0, 1, 0);
};

// {"anchors":[{"x":0,"y":0},{"x":0.30000000000000004,"y":0.025},{"x":0.55,"y":0.65},{"x":0.8,"y":0.025},{"x":1.175,"y":0},{"x":1.475,"y":0},{"x":1.6,"y":0}],"controls":[{"x":0.13902586932059735,"y":-0.6309350297566491},{"x":0.3976797154744436,"y":0.6358163698120345},{"x":0.7074444613334461,"y":0.5893040637251854},{"x":0.9851797154744433,"y":-0.36698467773927074},{"x":1.327487407782136,"y":0.05863743461861286},{"x":1.5375,"y":0}]}
std::vector<float> fls = { 1, 1.071, 1.1320000000000001, 1.183, 1.226, 1.259, 1.284, 1.3, 1.308, 1.308, 1.3, 1.285, 1.262, 1.231, 1.194, 1.149, 1.098, 1.04, 0.975, 0.877, 0.792, 0.716, 0.65, 0.593, 0.5429999999999999, 0.5, 0.46399999999999997, 0.43300000000000005, 0.40800000000000003, 0.388, 0.372, 0.361, 0.353, 0.35, 0.358, 0.369, 0.384, 0.402, 0.42400000000000004, 0.44999999999999996, 0.481, 0.518, 0.5589999999999999, 0.607, 0.663, 0.726, 0.798, 0.88, 0.975, 1.009, 1.039, 1.067, 1.091, 1.113, 1.131, 1.146, 1.158, 1.168, 1.174, 1.177, 1.177, 1.174, 1.168, 1.16, 1.148, 1.133, 1.116, 1.095, 1.071, 1.045, 1.016, 0.997, 0.991, 0.986, 0.982, 0.978, 0.975, 0.973, 0.972, 0.971, 0.971, 0.971, 0.973, 0.975, 0.978, 0.981, 0.986, 0.991, 0.997, 1, 1, 1, 1, 1, 1, 1, 1 };

void fade_out_edges_2(cairo_surface_t *surface, int pixels) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
    if (!surface || pixels <= 0)
        return;

    int width = cairo_image_surface_get_width(surface);
    int height = cairo_image_surface_get_height(surface);
    int stride = cairo_image_surface_get_stride(surface);
    unsigned char *data = cairo_image_surface_get_data(surface);

    if (pixels * 2 >= width || pixels * 2 >= height)
        return; // Avoid overfading if image too small

    cairo_surface_flush(surface);

    double max_dist = static_cast<double>(pixels * .5);
    
    auto ease = getEasingFunction(EaseOutCirc);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // Compute distance to the closest corner
            int dx = std::min(x, width - 1 - x);
            int dy = std::min(y, height - 1 - y);

            double edge_x = std::min(dx, pixels);
            double edge_y = std::min(dy, pixels);
            double dist = std::sqrt((pixels - edge_x) * (pixels - edge_x) +
                                    (pixels - edge_y) * (pixels - edge_y));

            // If we're inside the fade region, compute fade factor
            double factor = 1.0;
            if (edge_x < pixels || edge_y < pixels) {
                factor = std::clamp(1.0 - dist / pixels, 0.0, 1.0);
            }
            factor = ease(factor);

            // Access pixel
            uint32_t *pixel = reinterpret_cast<uint32_t *>(data + y * stride + x * 4);

            uint8_t a = (*pixel >> 24) & 0xFF;
            uint8_t r = (*pixel >> 16) & 0xFF;
            uint8_t g = (*pixel >> 8) & 0xFF;
            uint8_t b = (*pixel) & 0xFF;

            uint8_t new_a = static_cast<uint8_t>(a * factor);

            // Premultiplied alpha correction
            r = static_cast<uint8_t>(r * factor);
            g = static_cast<uint8_t>(g * factor);
            b = static_cast<uint8_t>(b * factor);

            *pixel = (new_a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    cairo_surface_mark_dirty(surface);
}

void paint_albums_root(AppClient *client, cairo_t *cr, Container *c) {
    draw_colored_rect(client, ArgbColor(.96, .96, .96, 1), c->real_bounds);
}

void right_click_album(AppClient *client, std::string full_path, std::string title) {    
    struct SongRightClickData : UserData {
        std::string full_path;
        std::string title;
        cairo_surface_t *surface;
        cairo_surface_t *volume_surface;
    };

    struct RightClickOption : UserData {
        std::string icon;
        std::string text;
        cairo_surface_t *surface = nullptr;
        ArgbColor icon_color = ArgbColor(1.000, 0.306, 0.420, 1.0);
    };
    int option_height = 32 * config->dpi;
    int options = 4;
    int option_width = 280 * config->dpi;

    Settings settings;
    settings.force_position = true;
    settings.override_redirect = true;
    settings.decorations = false;
    settings.x = client->bounds->x + client->mouse_initial_x;
    settings.y = client->bounds->y + client->mouse_initial_y;
    settings.w = option_width;
    settings.h = option_height * options + 8 * config->dpi;
    settings.dialog = true;
    PopupSettings popup_settings;
    popup_settings.name = "right_click_song";
    auto popup = client->create_popup(popup_settings, settings);
    auto data = new SongRightClickData;
    data->full_path = full_path;
    data->title = title;
    popup->user_data = data;
    popup->root->type = ::vbox;
    popup->root->when_paint = [](AppClient *client, cairo_t *, Container *c) {
        auto data = (SongRightClickData *) client->user_data;
        draw_colored_rect(client, ArgbColor(1, 1, 1, 1), c->real_bounds);
        draw_round_rect(client, ArgbColor(.7, .7, .7, 1), c->real_bounds, 0, std::floor(1 * config->dpi));
    };    
    
    auto add_option = [popup](std::string icon, std::string text) 
    { 
        auto data = new RightClickOption;
        data->icon = icon;
        data->text = text;
        auto option = popup->root->child(FILL_SPACE, FILL_SPACE);
        load_icon_full_path(app, popup, &data->surface, asset(icon), 24 * config->dpi);
        dye_surface(data->surface, ArgbColor(.8, 0, 1, 1));
        dye_surface(data->surface, data->icon_color);
        option->user_data = data;
        option->when_paint = [](AppClient *client, cairo_t *, Container *c) {
            auto data = (RightClickOption *) c->user_data;
            int width = 24 * config->dpi;
            if (data->surface) {
                width = cairo_image_surface_get_width(data->surface);
            }
            
            if (c->state.mouse_hovering || c->state.mouse_pressing) {
                if (c->state.mouse_pressing) {
                    draw_colored_rect(client, ArgbColor(.93, .93, .93, 1), c->real_bounds);
                } else {
                    draw_colored_rect(client, ArgbColor(.955, .955, .955, 1), c->real_bounds);
                }
            }
             
            draw_text(client, 10 * config->dpi, config->font, EXPAND(ArgbColor(0, 0, 0, 1)), data->text, c->real_bounds, 5, 8 * config->dpi * 2 + width);
            if (data->surface) {
                int height = cairo_image_surface_get_height(data->surface);
                cairo_set_source_surface(client->cr, data->surface, c->real_bounds.x + 8 * config->dpi, c->real_bounds.y + c->real_bounds.h * .5 - height * .5);         
                cairo_paint(client->cr);
            }
        };
        option->when_clicked = [](AppClient *client, cairo_t *, Container *c) {
            auto data = (RightClickOption *) c->user_data;
            auto client_data = (SongRightClickData *) client->user_data;
            if (data->text == "Play Next") {
                player->album_play_next(client_data->title);
            } else if (data->text == "Play After All Next") {
                player->album_play_after_all_next(client_data->title);
            } else if (data->text == "Add to Queue") {
                player->album_play_last(client_data->title);
            } else if (data->text == "Edit Info") {
                edit_info(EditType::ALBUM_TYPE, client_data->title);
            } else {
                //activate_coverlet(client);
            }
            client_close_threaded(app, client);
        };
    };
    
    auto pad = popup->root->child(FILL_SPACE, 4 * config->dpi);
    add_option("corner-up-right.svg", "Play Next");
    add_option("corner-down-right.svg", "Play After All Next");
    add_option("arrow-bar-to-down.svg", "Add to Queue");
    auto seperator = popup->root->child(FILL_SPACE, 8 * config->dpi);
    seperator->when_paint = [](AppClient *client, cairo_t *, Container *c) {
        draw_colored_rect(client, ArgbColor(.7, .7, .7, .4), 
                              Bounds(c->real_bounds.x + 20 * config->dpi,
                                     c->real_bounds.y + 4 * config->dpi,
                                     c->real_bounds.w - 40 * config->dpi, std::floor(1 * config->dpi)));
  
    };
    add_option("", "Edit Info");
    //add_option("", "View Art");
    pad = popup->root->child(FILL_SPACE, 4 * config->dpi);
    
    /*
    popup->root->when_clicked = [](AppClient *client, cairo_t *, Container *c) {
        auto data = (SongRightClickData *) client->user_data;
        player->play_track(data->full_path);
    };    
    */ 

    client_show(app, popup);
}

void close_album(AppClient *client, Container *c);

Container *row_target_container(AppClient *client, Container *c) {
    auto a_data = (AlbumsScrollRootData *) container_by_name("albums_root", client->root)->user_data;

    auto tmp = c;
    while (tmp->parent != nullptr) {
        if (a_data->opening_container == tmp) {
            return a_data->opening;
        }
        if (a_data->closing_container == tmp) {
            return a_data->closing;
        }
        tmp = tmp->parent;
    }
    
    return nullptr;
}

static void make_row(AppClient *client, Container *row, Container *line, Container *albums_root) {
    // We need to set the height of the row, and also all the elements inside it
    auto album_data = (AlbumData *) line->user_data;
    auto a_data = (AlbumsScrollRootData *) container_by_name("albums_root", client->root)->user_data;

    
    // TODO: autoscroll We need to autoscroll so that the top of the album that was clicked is the same distance from the top of the screen as before, so that when we click, we end up at the same place
    static int top_height = 94 * config->dpi;
    row->pre_layout = [](AppClient *client, Container *c, const Bounds &bounds) {     
        auto album_data = (AlbumData *) row_target_container(client, c)->user_data;
        AlbumOption *al_option = &album_songs[album_data->option.album];
        int wanted_h = 0;
        wanted_h += top_height + top_height * .6; // Top and bottom 'empty' pads
        
        if (!al_option->songs.empty()) {
            int needed_h = 30 * config->dpi * al_option->songs.size();
            
            // TODO: the album_target_width * 2 needs to be reduced by the pad above and below the track list
            float count = al_option->songs.size();
            int max_per_column = 10000;
            if ((int) count > 7) {
                int colums_allowed = std::floor((client->bounds->w - 80 * config->dpi - (album_target_width * 2)) / (song_width_in_album * config->dpi));
                
                if (colums_allowed < 1)
                    colums_allowed = 1;
                max_per_column = std::round(count / (float) colums_allowed);
                needed_h = 30 * config->dpi * max_per_column;
            }
            
            int total_needed = wanted_h + needed_h;
            wanted_h = total_needed;
            
            if (wanted_h < (album_target_width * 2)) {
                wanted_h = (album_target_width * 2);
            }
        }
        c->wanted_bounds.h = wanted_h;
    };
    
    auto left = row->child(::absolute, 80 * config->dpi, FILL_SPACE);
    left->pre_layout = [](AppClient *, Container *c, const Bounds &bounds) {
        auto close_button = c->children[0];
        close_button->real_bounds.x = bounds.x + 8 * config->dpi;
        close_button->real_bounds.y = bounds.y + 12 * config->dpi;
        close_button->real_bounds.w = close_button->wanted_bounds.w;
        close_button->real_bounds.h = close_button->wanted_bounds.h;
    };
    auto close_button = left->child(32 * config->dpi, 32 * config->dpi);
    close_button->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        auto target = row_target_container(client, c);
        close_album(client, target);
    };
    auto close_data = new SurfaceButton;
    close_data->name = "close.png";
    close_button->user_data = close_data;
    
    close_button->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        auto data = (SurfaceButton *) c->user_data;
        if (!data->surface) {
            load_icon_full_path(app, client, &data->surface, asset(data->name), 24 * config->dpi);
            auto album_data = (AlbumData *) row_target_container(client, c)->user_data;
            bool use_dark = is_light_theme(album_data->avg_color);
            ArgbColor color = ArgbColor(.9, .9, .9, 1);
            if (use_dark)
                color = ArgbColor(.1, .1, .1, 1);
            
            dye_surface(data->surface, color);
            c->user_data = data;
        }
        //draw_colored_rect(client, ArgbColor(1, 0, 1, 1), c->real_bounds);
        int width = cairo_image_surface_get_height(data->surface);
        int height = cairo_image_surface_get_height(data->surface);
        cairo_set_source_surface(cr, data->surface, 
                                 c->real_bounds.x + c->real_bounds.w * .5 - width * .5,
                                 c->real_bounds.y + c->real_bounds.h * .5 - height * .5);
        cairo_paint(cr); 
    };
    
    auto right = row->child(FILL_SPACE, FILL_SPACE);
    auto image_area = row->child(album_target_width * 2, FILL_SPACE);
    image_area->when_clicked = [](AppClient *client, cairo_t *cr, Container * c) {
        auto a_data = (AlbumsScrollRootData *) container_by_name("albums_root", client->root)->user_data;
        if (a_data->opening) {
            if (a_data->opening->user_data) {
                auto data = (AlbumData *) a_data->opening->user_data;      
                activate_coverlet(client, data->option.album);
            }
        }
    };
    auto top = right->child(::absolute, FILL_SPACE, top_height);
    top->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        auto album_data = (AlbumData *) row_target_container(client, c)->user_data;
        int offset = c->real_bounds.h * .4;
        { // Album name
            bool use_dark = is_light_theme(album_data->avg_color);
            ArgbColor color = ArgbColor(1, 1, 1, 1);
            if (use_dark)
                color = ArgbColor(0, 0, 0, 1);
            
            
            int size = c->children[0]->wanted_bounds.w * 3 + 4 * config->dpi * 6;
            draw_clip_begin(client, Bounds(c->real_bounds.x,
                                           c->real_bounds.y,
                                           c->real_bounds.w - size,
                                           c->real_bounds.h));
            
            auto [f, w, h]  = draw_text_begin(client, 14 * config->dpi, config->font, EXPAND(color), album_data->option.album, true);
            f->draw_text_end(c->real_bounds.x, c->real_bounds.y + offset - 4 * config->dpi);
            offset += h;
            draw_clip_end(client);
        }
        { // Artist year
            std::string artist_year = album_data->option.artist;
            if (!album_data->option.year.empty() && album_data->option.year != "0") {
                artist_year += " (" + album_data->option.year + ")";
            }
            auto [f, w, h]  = draw_text_begin(client, 12 * config->dpi, config->font, EXPAND(album_data->second_color), artist_year);
            f->draw_text_end(c->real_bounds.x, c->real_bounds.y + offset);
            offset += h;
        }
    };
    auto play_button = top->child(20 * config->dpi, 20 * config->dpi);
    auto randomize_button = top->child(20 * config->dpi, 20 * config->dpi);
    auto queue_button = top->child(20 * config->dpi, 20 * config->dpi);
    top->pre_layout = [](AppClient *client, Container *c, const Bounds &bounds) {
        auto album_data = (AlbumData *) row_target_container(client, c)->user_data;
        auto [f, w, h]  = draw_text_begin(client, 14 * config->dpi, config->font, 1, 1, 1, 1, album_data->option.album, true);
        f->end();
        
        int init_off = w;
        int size = c->children[0]->wanted_bounds.w * 3 + 4 * config->dpi * 6;
        if (init_off > bounds.w - size) {
            init_off = bounds.w - size;
        }
        int w_off = 6 * config->dpi;
        for (int i = 0; i < c->children.size(); i++) {
            auto child = c->children[i];
            child->real_bounds.x = bounds.x + w_off + init_off;
            child->real_bounds.y = bounds.y + bounds.h * .4 - 2 * config->dpi;
            child->real_bounds.w = child->wanted_bounds.w;
            child->real_bounds.h = child->wanted_bounds.h;
            w_off += child->real_bounds.w + 6 * config->dpi;
        } 
    };
    play_button->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        auto data = (SurfaceButton *) c->user_data;
        if (!data->surface) {
            load_icon_full_path(app, client, &data->surface, asset(data->name), 16 * config->dpi);
            auto album_data = (AlbumData *) row_target_container(client, c)->user_data;
            bool use_dark = is_light_theme(album_data->avg_color);
            ArgbColor color = ArgbColor(.9, .9, .9, 1);
            if (use_dark)
                color = ArgbColor(.1, .1, .1, 1);
            
            dye_surface(data->surface, color);
            c->user_data = data;
        }
        //draw_colored_rect(client, ArgbColor(1, 0, 1, 1), c->real_bounds);
        int width = cairo_image_surface_get_height(data->surface);
        int height = cairo_image_surface_get_height(data->surface);
        cairo_set_source_surface(cr, data->surface, 
                                 c->real_bounds.x + c->real_bounds.w * .5 - width * .5,
                                 c->real_bounds.y + c->real_bounds.h * .5 - height * .5);
        cairo_paint(cr); 
    };
    play_button->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        auto target = row_target_container(client, c);
        auto line_data = (AlbumData *) target->user_data;
        player->album_play_next(line_data->text);
        player->pop_queue();
        //close_album(client, target);
    };
    
    
    auto play_data = new SurfaceButton;
    play_data->name = "play.png";
    play_button->user_data = play_data;
    
    randomize_button->when_paint = play_button->when_paint;
    auto randomize_data = new SurfaceButton;
    randomize_data->name = "shuffle.png";
    randomize_button->user_data = randomize_data;
    
    queue_button->when_paint = play_button->when_paint;
    auto queue_data = new SurfaceButton;
    queue_data->name = "queue.png";
    queue_button->user_data = queue_data;
    
    auto songs_parent = right->child(::absolute, FILL_SPACE, FILL_SPACE);
    songs_parent->pre_layout = [](AppClient *, Container *c, const Bounds &bounds) {
        int colums_allowed = std::floor(bounds.w / (song_width_in_album * config->dpi));
        if (colums_allowed < 1)
            colums_allowed = 1;
        
        float count = c->children.size();
        
        int max_per_column = 10000;
        if ((int) count > 7) {
            max_per_column = std::round(count / (float) colums_allowed);
        }
        
        int y = 0;
        int x = 0;
        float w = bounds.w / colums_allowed;
        for (int i = 0; i < count; i++) {
            auto child = c->children[i];
            if (y > max_per_column) {
                x++;
                y = 0;
            }
            
            child->real_bounds.x = bounds.x + x * w;
            child->real_bounds.y = bounds.y + y * song_height_in_album * config->dpi;
            child->real_bounds.w = w - 30 * config->dpi;
            //child->real_bounds.w = song_width_in_album * config->dpi;
            child->real_bounds.h = song_height_in_album * config->dpi;
            y++;
        }
        
    };
    
    AlbumOption *al_option = &album_songs[album_data->option.album];
    for (Option track : al_option->songs) {
        auto t = songs_parent->child(FILL_SPACE, song_height_in_album * config->dpi);
        auto a = new AlbumSong;
        a->data = track;
        t->user_data = a;
        t->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
            auto album_data = (AlbumData *) row_target_container(client, c)->user_data;
            auto a = (AlbumSong *) c->user_data;
            
            int x_off = 16 * config->dpi;
            
            bool playing = a->data.full == player->path;
            bool bold = c->state.mouse_hovering || playing;
            bool hovered = false;
            if (client->previous_x != -1 && client->mouse_current_x > 0) {
                Bounds mouse_bounds(0, 0, 1, 1);
                if (client->previous_x < client->motion_event_x) {
                    mouse_bounds.x = client->previous_x;
                    mouse_bounds.w = client->motion_event_x - client->previous_x;
                } else {
                    mouse_bounds.x = client->motion_event_x;
                    mouse_bounds.w = client->previous_x - client->motion_event_x;
                }
                if (client->previous_y < client->motion_event_y) {
                    mouse_bounds.y = client->previous_y;
                    mouse_bounds.h = client->motion_event_y - client->previous_y;
                } else {
                    mouse_bounds.y = client->motion_event_y;
                    mouse_bounds.h = client->previous_y - client->motion_event_y;
                }
                hovered = overlaps(c->real_bounds, mouse_bounds);
                if (hovered && container_by_name("root", client->root)->interactable) {
                    bold = true;
                }
            }
            
            bool italic = false;
            int size = 10 * config->dpi;
            if (c->state.mouse_pressing)
                size = 12 * config->dpi;
            
            bool use_dark = is_light_theme(album_data->avg_color);
            ArgbColor color = ArgbColor(1, 1, 1, 1);
            if (use_dark)
                color = ArgbColor(0, 0, 0, 1);
            
            
            
            /*
            if (!already_began(client, &a->size, size)) {
                client_create_animation(app, client, &a->size, c->lifetime, 0, 100, nullptr, size);
            }*/ 
            
            int double_char_width = 0;
            
            {
                auto [f, w, h]  = draw_text_begin(client, size, config->font, EXPAND(album_data->second_color), "WW", bold, italic);
                f->end();
                double_char_width = w;
            }
            
            
            { // Track Number
                int ww = 0;
                if (a->data.track != "0") {
                    auto [f, w, h]  = draw_text_begin(client, size, config->font, EXPAND(album_data->second_color), a->data.track, bold, italic);
                    f->draw_text_end(c->real_bounds.x + x_off - w, c->real_bounds.y + c->real_bounds.h * .5 - h * .5);
                    x_off += w + 16 * config->dpi;
                    ww = w;
                }
                
                // Draw playing volume
                if (playing) {
                    auto a_data = (AlbumsScrollRootData *) container_by_name("albums_root", client->root)->user_data;
                    if (a_data) {
                        if (a_data->volume_icon) {
                            if (a_data->volume_color != color) {
                                a_data->volume_color = color;
                                dye_surface(a_data->volume_icon, color);
                            }
                            int height = cairo_image_surface_get_height(a_data->volume_icon);
                            cairo_set_source_surface(cr, a_data->volume_icon, 
                                                     c->real_bounds.x - ww - 8 * config->dpi, 
                                                     c->real_bounds.y + c->real_bounds.h * .5 - height * .5);
                            
                            cairo_paint(cr);
                        }
                    }
                }
            }
            
            
            int w_of_time = 0;
            { // Time
                if (!a->attempted) {
                    a->attempted = true;
                    try {
                        a->time = seconds_to_mmss(std::atoi(a->data.length.c_str()));
                    } catch (...) {
                        
                    }
                }
                if (a->attempted) {
                    auto [f, w, h]  = draw_text_begin(client, size, config->font, EXPAND(album_data->second_color), a->time, bold, italic);
                    f->draw_text_end(c->real_bounds.x + c->real_bounds.w - w - 8 * config->dpi, c->real_bounds.y + c->real_bounds.h * .5 - h * .5);
                    w_of_time = w;
                }
            }
            
            
            { // Track Name
                draw_clip_begin(client, Bounds(c->real_bounds.x,
                                               c->real_bounds.y, 
                                               c->real_bounds.w - double_char_width - 14 * config->dpi,
                                               c->real_bounds.h));
                auto [f, w, h]  = draw_text_begin(client, size, config->font, EXPAND(color), a->data.name, bold, italic);
                f->draw_text_end(c->real_bounds.x + double_char_width + 14 * config->dpi, c->real_bounds.y + c->real_bounds.h * .5 - h * .5);
                x_off += w + 16 * config->dpi;
                draw_clip_end(client);
            }
            
            
            //draw_text(client, 10 * config->dpi, config->font, EXPAND(album_data->accent_color), a->data.name, c->real_bounds, 5, 8 * config->dpi * 2);
        };
        t->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
            auto a = (AlbumSong *) c->user_data;
            if (c->state.mouse_button_pressed == 3) {
                right_click_song(client, a->data.full, a->data.name);
                return;
            }
            int from_index = 0;
            for (int i = 0; i < c->parent->children.size(); i++) {
                if (c->parent->children[i] == c) {
                    from_index = i;
                    break;
                }
            }
            player->album_play_next(a->data.album, from_index);
            player->pop_queue();
        };
    }
    
    auto empty = right->child(FILL_SPACE, top_height * .6);
    
    row->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        auto a_data = (AlbumsScrollRootData *) container_by_name("albums_root", client->root)->user_data;
        float amount = a_data->openess;
        if (c == a_data->closing_container)
            amount = a_data->closingness;
        
        if (a_data->closing_container && a_data->opening_container) {
            if (std::abs(a_data->opening_container->real_bounds.y - a_data->closing_container->real_bounds.y) < 10) {
                // TODO: tricky, to get the correct amount it will have to be a tricky value
                // the smaller of the two will not move from 1
                float open_h = a_data->opening_container->wanted_bounds.h;
                float close_h = a_data->closing_container->wanted_bounds.h;
                
                bool open_taller = open_h > close_h;
                bool close_taller = !open_taller;
                
                float diff_h = std::abs(open_h - close_h);
                amount = 1;
            }
        }
        
        cairo_save(cr);
        cairo_push_group(cr);  // Starts drawing to a temporary surface
        
        if (row_target_container) {
            {
                auto data = (AlbumData *) row_target_container(client, c)->user_data;
                
                if (!data->calculated_avg_color && data->surface) {
                    data->calculated_avg_color = true;
                    
                    auto rawMap = mainColorsInImage(data->surface);
                    data->map = rawMap;
                    // Convert map to vector for sorting
                    std::vector<std::pair<ArgbColor, float>> sortedColors(rawMap.begin(), rawMap.end());
                    
                    // Sort by percentage descending (most common to least)
                    std::sort(sortedColors.begin(), sortedColors.end(), [](const auto& a, const auto& b) {
                                  return b.second < a.second;
                              });
                    
                    // Optionally store back into data->map or use sortedColors as-is
                    data->map.clear();
                    for (const auto& [color, percentage] : sortedColors) {
                        data->map[color] = percentage;
                    }
                    
                    int i = 0;
                    for (const auto& [color, perc] : sortedColors) {
                        if (i == 0) {
                            data->avg_color = color;
                            data->accent_color = color; // just in case there is no 1 and 2 index
                            data->second_color = color;
                        } else if (i == 1) {
                            data->second_color = color;
                        } else if (i == 2) {
                            data->accent_color = color;
                        }
                        i++;
                    }
                    
                    sortedColors.erase(sortedColors.begin());
                    
                    std::sort(sortedColors.begin(), sortedColors.end(), [data](const auto& a, const auto& b) {
                                  float a_dist = a.first.distance_to(data->avg_color);
                                  float b_dist = b.first.distance_to(data->avg_color);
                                  return a_dist > b_dist;
                              });
                    i = 0;
                    for (const auto& [color, perc] : sortedColors) {
                        if (i == 0) {
                            data->accent_color = color;
                            data->second_color = color;
                        }
                        i++;
                    }
                    
                    
                    
                    /*
                    data->calculated_avg_color = true;
                    DominantColors result = extract_main_and_accent_colors(data->surface);
                    data->avg_color = result.main_color;
                    data->accent_color = result.accent_colors[0];
                    if (result.accent_colors.size() > 1) {
                        data->second_color = result.accent_colors[1];
                    } else {
                        data->second_color = result.accent_colors[0];
                    }
                    */
                    
                    //data->avg_color = average_edge_pixels(data->surface);
                    //data->avg_color = average_edge_pixels_filtered(data->surface, data->avg_color, .8);
                }
            }
            
            auto album_data = (AlbumData *) row_target_container(client, c)->user_data;
            /*
            int y_off = 0;
            for (const auto& [color, perc] : album_data->map) {
                draw_colored_rect(client, color, Bounds(c->real_bounds.x, c->real_bounds.y + y_off, c->real_bounds.w, c->real_bounds.h * perc));
                y_off += c->real_bounds.h * perc;
            }
            */
            
            draw_colored_rect(client, album_data->avg_color, c->real_bounds);
            //draw_round_rect(client, ArgbColor(.8, .8, .8, 1), c->real_bounds, 0, std::floor(1 * config->dpi)); 
            
            cairo_set_line_width(cr, std::floor(1 * config->dpi));
            
            auto rtc = row_target_container(client, c);
            int w = 50 * config->dpi;
            int x = rtc->real_bounds.x + rtc->real_bounds.w * .5;
            x -= w * .5;
            int y = c->real_bounds.y + 1;
            
            auto a_data = (AlbumsScrollRootData *) container_by_name("albums_root", client->root)->user_data;

            if (a_data->closing_container == c) {
                y += 30 * config->dpi * (1.0 - a_data->closingness);
            }
            set_argb(cr, album_data->avg_color);
            cairo_line_to(cr, x, y);
            cairo_line_to(cr, x + w * .5, y - 30 * config->dpi);
            cairo_line_to(cr, x + w, y);
            cairo_fill(cr);
            
            if (a_data->opening_container == c) {
                set_argb(cr, ArgbColor(.8, .8, .8, .8));
                cairo_move_to(cr, x, y);
                cairo_line_to(cr, x + w * .5, y - 30 * config->dpi);
                cairo_line_to(cr, x + w, y);
                cairo_stroke(cr);
                
                cairo_move_to(cr, c->real_bounds.x, c->real_bounds.y);
                cairo_line_to(cr, c->real_bounds.x + x, c->real_bounds.y);
                cairo_stroke(cr);
                
                cairo_move_to(cr, c->real_bounds.x + x + w, c->real_bounds.y);
                cairo_line_to(cr, c->real_bounds.x + c->real_bounds.w, c->real_bounds.y);
                cairo_stroke(cr);
            }
                        if (album_data->surface) {
                //cairo_save(cr);
                int width = cairo_image_surface_get_height(album_data->large_album_art);
                
                cairo_save(client->cr);
                float scale = 1.9 / 2.0;
                cairo_scale(client->cr, scale, scale);
                float shrink = 1.0 / scale;
                cairo_set_source_surface(client->cr, album_data->large_album_art,
                                         (c->real_bounds.x + c->real_bounds.w - width + ((width - width * scale) * .5)) * shrink,
                                         (c->real_bounds.y + ((width - width * scale) * .5)) * shrink);         
                //cairo_paint(client->cr);
                
                //cairo_scale(cr, 2, 2);
                //cairo_set_source_surface(cr, album_data->large_album_art, (c->real_bounds.x + c->real_bounds.w - width), (c->real_bounds.y)); 
                cairo_paint(cr);
                cairo_restore(client->cr);
                
                //cairo_paint_with_alpha(cr, 1.0);
                //cairo_restore(cr);
            }
        }
        
        for (auto th: c->children) {
            paint_container(app, client, th);
        }
        
        cairo_pop_group_to_source(cr);  // Use the result as the current source
        draw_clip_begin(client, Bounds(c->real_bounds.x, c->real_bounds.y - album_height, c->real_bounds.w, c->real_bounds.h * amount + album_height));
        bool done = false;
        if (a_data->closing_container && a_data->opening_container) {
            if (std::abs(a_data->opening_container->real_bounds.y - a_data->closing_container->real_bounds.y) < 10) {
                done = true;
                if (a_data->closing_container == c) {
                    cairo_paint_with_alpha(cr, a_data->closingness);
                } else {
                    cairo_paint_with_alpha(cr, a_data->openess);
                }
            }
        }
        if (!done) {
            if (c == a_data->closing_container) {
                cairo_paint_with_alpha(cr, amount);                // Paint it back to the original context
            } else {
                cairo_paint(cr);                // Paint it back to the original context
            }
        }
        draw_clip_end(client);
        cairo_restore(cr);
    };
}

void close_album(AppClient *client, Container *c) {
    auto album_root = (ScrollContainer *) container_by_name("albums_root", client->root);
    auto a_data = (AlbumsScrollRootData *) album_root->user_data;
    
    if (a_data->closing_container) {
        for (int i = 0; i < album_root->content->children.size(); i++) {
            if (album_root->content->children[i] == a_data->closing_container) {
                album_root->content->children.erase(album_root->content->children.begin() + i);
            }
        }
        
        delete a_data->closing_container;
        a_data->closing_container = nullptr;
        a_data->closing = nullptr;
        a_data->closingness = 1.0;
    }
    
    if (c->user_data) {
        auto close_data = (AlbumData *) c->user_data;
        close_data->selected = false;
        close_data->time_when_selected = client->app->current;
    }
    a_data->closing_container = a_data->opening_container;
    a_data->closing = a_data->opening;
    a_data->closingness = 1.0;
    client_create_animation(client->app, client, &a_data->closingness, album_root->lifetime, 1.0,
                                600.0,
                                getEasingFunction(EaseCustom),
                                0.0, true);
    a_data->opening_container = nullptr;
    a_data->opening = nullptr;
   
    client_layout(app, client);
    request_refresh(app, client);
}

void open_album(AppClient *client, Container *c) {
    auto album_root = (ScrollContainer *) container_by_name("albums_root", client->root);
    auto a_data = (AlbumsScrollRootData *) album_root->user_data;
    
    { // remove stale closing_container or there would be a gui bug
        if (a_data->closing_container && a_data->closingness == 0.0) {
            auto close_data = (AlbumData *) a_data->closing_container->user_data;

            for (int i = 0; i < album_root->content->children.size(); i++) {
                if (album_root->content->children[i] == a_data->closing_container) {
                    album_root->content->children.erase(album_root->content->children.begin() + i);
                }
            }
            
            delete a_data->closing_container;
            a_data->closing_container = nullptr;
            a_data->closing = nullptr;
            a_data->closingness = 1.0;
        }
    }
    
    bool was = ((AlbumData *) c->user_data)->selected;
    if (was)
        return;
    auto data = (AlbumData *) c->user_data;
    for (auto child : a_data->containers) {
        auto tmp_data = (AlbumData *) child->user_data;
        tmp_data->selected = false;
        tmp_data->time_when_selected = client->app->current;
    }
    data->selected = true;
    data->time_when_selected = client->app->current;
    
    if (a_data->opening_container) {
        close_album(client, a_data->opening_container);
    }
    
    auto row = album_root->content->child(::hbox, FILL_SPACE, album_height * 2);
    //auto row = new Container(::hbox, FILL_SPACE, album_height * 2);
    row->parent = album_root->content;
    row->automatically_paint_children = false;
    row->z_index = 2;
    a_data->opening_container = row;
    a_data->opening = c;
    a_data->first_frame = true;
    a_data->openess = 0.0;
    client_create_animation(client->app, client, &a_data->openess, album_root->lifetime, 0.0,
                            600.0,
                            getEasingFunction(EaseCustom),
                            1.0, true);
    
    make_row(client, row, c, album_root);
    
    client_layout(app, client);
    request_refresh(app, client);
}

void fill_album_tab(AppClient *client, Container *albums_root, const std::vector<Option> &options) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
    albums_root->when_paint = paint_albums_root;
    albums_root->user_data = new SurfaceButton;
    //return;
    
    ScrollPaneSettings scroll_settings(config->dpi);
    scroll_settings.right_inline_track = true;
    auto albums_scroll_root = make_newscrollpane_as_child(albums_root, scroll_settings);
    albums_scroll_root->name = "albums_root";
    albums_scroll_root->content->name = "albums_content";
    albums_scroll_root->after_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        auto data = (AlbumsScrollRootData *) c->user_data;
        if (data->opening_container || data->closing_container) {
            cairo_save(cr);
            cairo_push_group(cr);  // Starts drawing to a temporary surface
            if (data->closing_container) {
                paint_container(app, client, data->closing_container);
            }
            if (data->opening_container) {
                paint_container(app, client, data->opening_container);
            }
            cairo_pop_group_to_source(cr);  // Use the result as the current source
            draw_clip_begin(client, c->real_bounds);
            cairo_paint(cr);                // Paint it back to the original context
            draw_clip_end(client);
            cairo_restore(cr);
        }
    };
    
    // Paint the unknown album icon
    albums_scroll_root->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        auto data = (AlbumsScrollRootData *) c->user_data;
        if (!data->unknown_album_icon) {
            load_icon_full_path(app, client, &data->unknown_album_icon, asset("clef.svg"), 100 * config->dpi);
            dye_surface(data->unknown_album_icon, ArgbColor(.5, .5, .5, 1));
        }
        if (!data->volume_icon) {
            load_icon_full_path(app, client, &data->volume_icon, asset("volume.png"), 16 * config->dpi);
            dye_surface(data->volume_icon, ArgbColor(.5, .5, .5, 1));
        }
                
        bool have_time = true;
        for (auto c: data->containers) {
            auto data = (AlbumData *) c->user_data;
            if (!data->surface && !data->attempted) {
                for (auto art : cached_art) {
                    if (art->name == sanitize_file_name(data->text)) {
                        if (have_time) {
                            data->surface = accelerated_surface(app, client, art->width, art->height);
                            paint_surface_with_data(data->surface, art->data, art->width, art->height);
                            
                            data->large_album_art = accelerated_surface(app, client, art->width * 2, art->height * 2);
                            paint_surface_with_data(data->large_album_art, art->large_data, art->width * 2, art->height * 2);
                            fade_out_edges_2(data->large_album_art, 16 * config->dpi);
                            //data->surface = blur_surface_edges_only(data->surface, 8.0 * config->dpi);
                            break;
                        }
                    }
                }
            }
            have_time = (get_current_time_in_ms() - client->app->current) < 5;
            have_time = true;
            if (!have_time) {
                break;
            }
        }    
    };
    albums_scroll_root->user_data = new AlbumsScrollRootData;
    
    std::vector<std::string> albums_added;
    for (auto o: options) {
        if (o.album.empty()) {
            AlbumOption *al = &album_songs["Unknown"];
            o.album = "Unknown";
            al->songs.push_back(o);
        } else {
            AlbumOption *al = &album_songs[o.album];
            al->songs.push_back(o);
        }
        bool already_added = false;
        for (auto a: albums_added) {
            if (o.album == a) {
                already_added = true;
                break;
            }
        }
        if (already_added)
            continue;
        albums_added.push_back(o.album);
        
        struct PlayButton : SurfaceButton {
            double scalar = 0.0;
            double hovering = 0.0;
        };
        
        auto add_album = [o, client, albums_scroll_root](std::string album) {
            auto line = albums_scroll_root->content->child(::absolute, 100 * config->dpi, 100 * config->dpi);
            //auto line = new Container(::absolute, FILL_SPACE, FILL_SPACE);
            auto play = line->child(56 * config->dpi, 56 * config->dpi);
            play->name = "play";
            auto play_data = new PlayButton;
            load_icon_full_path(app, client, &play_data->surface, asset("play.png"), 48 * config->dpi);
            if (play_data->surface) {
                dye_surface(play_data->surface, ArgbColor(1, 1, 1, .9));
            }
            play->user_data = play_data;
            static int top_offset = 24 * config->dpi;
            play->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
                auto album_data = (AlbumData *) c->parent->user_data;
                Bounds picture_bounds = c->parent->real_bounds;
                picture_bounds.w = album_target_width;
                picture_bounds.h = album_target_width;
                picture_bounds.x += (c->parent->real_bounds.w - picture_bounds.w) * .5;
                picture_bounds.y += top_offset;
                
                auto data = (PlayButton *) c->user_data;
                
                bool animating_bounce = data->scalar != 1.0;
                if (animating_bounce) {
                    animating_bounce = already_began(client, &data->scalar, 1.0);  
                }
                
                bool other = data->surface && bounds_contains(picture_bounds, client->mouse_current_x, client->mouse_current_y) && c->parent->state.mouse_hovering || animating_bounce;
                if (other || data->hovering != 0.0) {
                    if (other) {
                        if (!already_began(client, &data->hovering, 1.0)) {
                            client_create_animation(client->app, client, &data->hovering, client->lifetime, 0, 100, nullptr, 1.0);
                        } 
                    } else {
                        if (!already_began(client, &data->hovering, 0.0)) {
                            client_create_animation(client->app, client, &data->hovering, client->lifetime, 0, 250, nullptr, 0.0);
                        }
                    }
                    int width = cairo_image_surface_get_height(data->surface);
                    int height = cairo_image_surface_get_height(data->surface);
               
                    cairo_save(cr);
                    //double bg_fade = fls[data->hover_scalar * (fls.size() - 1)];
                    float add = (0.2 * album_data->hover_scalar);
                    float scale = fls[data->scalar * (fls.size() - 1)] + add;
                    if (bounds_contains(c->real_bounds, client->mouse_current_x, client->mouse_current_y)) {
                        //scale *= 1.2;
                    }
                    width *= scale;
                    height *= scale;
                    cairo_scale(cr, scale, scale);
                    float shrink = 1.0 / scale;

                    cairo_set_source_surface(cr, data->surface, 
                                                 (c->real_bounds.x + c->real_bounds.w * .5 - width * .4) * shrink,
                                                 (c->real_bounds.y + c->real_bounds.h * .5 - height * .5) * shrink);
                    cairo_paint_with_alpha(cr, data->hovering); 
                    cairo_restore(cr);
                }
            };
            play->when_clicked = [](AppClient *client, cairo_t *, Container *c) {
                //c->parent->when_clicked(client, client->cr, c->parent);
                //c->parent->when_clicked(client, client->cr, c->parent);
                auto line_data = (AlbumData *) c->parent->user_data;
                player->album_play_next(line_data->text);
                player->pop_queue();
                
                open_album(client, c->parent);
                
                auto data = (PlayButton *) c->user_data;
                data->scalar = 0.0;
                client_create_animation(client->app, client, &data->scalar, client->lifetime, 0, (double) fls.size() * 12.6 , nullptr, 1.0);
            };
            line->pre_layout = [](AppClient *client, Container *c, const Bounds &b) {                
                Bounds picture_bounds = c->real_bounds;
                picture_bounds.w = album_target_width;
                picture_bounds.h = album_target_width;
                picture_bounds.x += (c->real_bounds.w - picture_bounds.w) * .5;
                picture_bounds.y += top_offset;
 
                auto play = c->children[0];
                play->real_bounds.x = picture_bounds.x + picture_bounds.w * .5 - play->wanted_bounds.w * .5;
                play->real_bounds.y = picture_bounds.y + picture_bounds.h * .5 - play->wanted_bounds.h * .5;
                play->real_bounds.w = play->wanted_bounds.w;
                play->real_bounds.h = play->wanted_bounds.h;
            };
            
            auto albums = (AlbumsScrollRootData *) albums_scroll_root->user_data;
            albums->containers.push_back(line);
            
            auto data = new AlbumData;
            data->text = album;
            data->option = o;
            line->user_data = data;

            line->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
                auto data = (AlbumData *) c->user_data;
                if (data->selected) {
                    if (!already_began(client, &data->hover_scalar, 1.0)) {
                        client_create_animation(client->app, client, &data->hover_scalar, client->lifetime, 0, 300, getEasingFunction(EaseInSine), 1.0);
                    } 
                } else {
                    if (!already_began(client, &data->hover_scalar, 0.0)) {
                        client_create_animation(client->app, client, &data->hover_scalar, client->lifetime, 0, 300, nullptr, 0.0);
                    } 
                }

                Bounds picture_bounds = c->real_bounds;
                picture_bounds.w = album_target_width;
                picture_bounds.h = album_target_width;
                picture_bounds.x += (c->real_bounds.w - picture_bounds.w) * .5;
                picture_bounds.y += top_offset;
 
                draw_colored_rect(client, ArgbColor(.96, .96, .96, 1), c->real_bounds);
                int width = album_target_width;
                if (data->surface) {
                    cairo_save(cr);
                    int width = cairo_image_surface_get_height(data->surface);
                    int height = cairo_image_surface_get_height(data->surface);
                    float scale_amount = 0.05 * data->hover_scalar;
                    float scale = 1.0 + scale_amount;
                    cairo_scale(client->cr, scale, scale);
                    float shrink = 1.0 / scale;
                    
                    cairo_set_source_surface(cr, data->surface, 
                                                 (c->real_bounds.x + c->real_bounds.w * .5 - width * .5 - ((width * scale - width) * .5)) * shrink,
                                                 (c->real_bounds.y + top_offset - ((width * scale - width)) * .5) * shrink);
                    cairo_paint(cr); 
                    cairo_restore(cr);
                } else {
                    draw_round_rect(client, ArgbColor(0.878, 0.898, 0.914, 1.0), picture_bounds, 3 * config->dpi);
                    draw_round_rect(client, ArgbColor(.6, .6, .6, .8), picture_bounds, 3 * config->dpi, std::floor(1 * config->dpi));
                    
                    auto a_data = (AlbumsScrollRootData *) container_by_name("albums_root", client->root)->user_data;
                    
                    if (a_data->unknown_album_icon) {
                        int width = cairo_image_surface_get_height(a_data->unknown_album_icon);
                        int height = cairo_image_surface_get_height(a_data->unknown_album_icon);
                        cairo_set_source_surface(cr, a_data->unknown_album_icon,
                                                 c->real_bounds.x + c->real_bounds.w * .5 - width * .5,
                                //c->real_bounds.y + c->real_bounds.h * .5 - height * .5);
                                                 c->real_bounds.y + top_offset + album_target_width * .5 - height * .5);
                        cairo_paint(cr);
                    }
                }
                int over = c->real_bounds.w - width;
                int offset = 0;
                
                if (data->surface) {
                    int width = cairo_image_surface_get_height(data->surface);
                    float scale_amount = 0.05 * data->hover_scalar;
                    float scale = 1.0 + scale_amount;
                    double off =  ((width * scale - width)) * .5;
                    draw_clip_begin(client, Bounds(c->real_bounds.x + over * .5 - off,
                                                                       c->real_bounds.y, album_target_width * scale, c->real_bounds.h));
 
                } else {
                    draw_clip_begin(client, Bounds(c->real_bounds.x + over * .5,
                                                                       c->real_bounds.y, album_target_width, c->real_bounds.h));
                }
                
                {
                    double off = 0;
                    float scale_amount = 0.05 * data->hover_scalar;
                    float scale = 1.0 + scale_amount;
                    if (data->surface) {
                        int width = cairo_image_surface_get_height(data->surface);
                        off =  ((width * scale - width)) * .5;
                    }

                    
                    ArgbColor color = ArgbColor(0, 0, 0, 1);
                    auto [f, w, h] = draw_text_begin(client, 11 * config->dpi, config->font, EXPAND(color), data->option.album, true);
                    f->draw_text_end((c->real_bounds.x + over * .5 - off), (c->real_bounds.y + top_offset + 8 * config->dpi + album_target_width + off));
                    offset += h;
                }
                //if (!data->selected) {
                {
                    double off = 0;
                    if (data->surface) {
                        int width = cairo_image_surface_get_height(data->surface);
                        float scale_amount = 0.05 * data->hover_scalar;
                        float scale = 1.0 + scale_amount;
                        off =  ((width * scale - width)) * .5;
                    }


                    auto [f, w, h] = draw_text_begin(client, 10 * config->dpi, config->font, EXPAND(ArgbColor(.3, .3, .3, 1.0 - data->hover_scalar)), data->option.artist);
                    f->draw_text_end(c->real_bounds.x + over * .5 - off, c->real_bounds.y + top_offset + 8 * config->dpi + album_target_width + h + off);
                    offset += h;
                }
                //}
                draw_clip_end(client);
            };
            
            line->when_clicked = [](AppClient *client, cairo_t *, Container *c) {
                if (auto play = container_by_name("play", c)) {
                    if (play->state.mouse_hovering) {
                        return;
                    }
                }
                if (c->state.mouse_button_pressed == 3) {
                    auto data = (AlbumData *) c->user_data;
                    right_click_album(client, data->option.album, data->option.album);
                    return;   
                }
                
                auto album_root = (ScrollContainer *) container_by_name("albums_root", client->root);
                auto a_data = (AlbumsScrollRootData *) album_root->user_data;
                bool was = ((AlbumData *) c->user_data)->selected;
                
                if (!was) {
                    open_album(client, c);
                } else {
                    close_album(client, c);
                }
               
                return;
            };
        };
        
        add_album(o.album);
    }
    
    albums_scroll_root->content->type = ::absolute;
    albums_scroll_root->content->pre_layout = [](AppClient *client, Container *c, const Bounds &b) {
        // Set d->exists based on if it matches against non empty search string
        /*
        if (auto filter_textarea = container_by_name("filter_textarea", client->root)) {
            auto data = (TextAreaData *) filter_textarea->user_data;
            if (!data->state->text.empty()) {
                for (auto d: c->children) {
                    auto al = (AlbumData *) d->user_data;
                    if (al->option.album.find(data->state->text) != std::string::npos) {
                        d->exists = true;
                    } else {
                        d->exists = false;
                    }
                }
            }
        }
        */

        static double scroll_to = 0;
        static double target_y = 0;
        static double start_y = 0;
        
        auto scroll = ((ScrollContainer *) c->parent);
        auto a_data = (AlbumsScrollRootData *) scroll->user_data;
         if (a_data->closingness >= 1.0 && a_data->closing_container) {
            for (int i = 0; i < scroll->content->children.size(); i++) {
                if (scroll->content->children[i] == a_data->closing_container) {
                    scroll->content->children.erase(scroll->content->children.begin() + i);
                }
            }
        }
       

        auto content = scroll->content;
        auto album_per_line = std::floor(b.w / album_width);
        if (album_per_line <= 0)
            album_per_line = 1;
        
        if (a_data->opening_container) {
            if (a_data->opening_container->pre_layout) {
                a_data->opening_container->pre_layout(client, a_data->opening_container, Bounds(0, 0, b.w, 0)); 
            }
        }
        if (a_data->closing_container) {
            if (a_data->closing_container->pre_layout) {
                a_data->closing_container->pre_layout(client, a_data->closing_container, Bounds(0, 0, b.w, 0)); 
            }
        }
                 
        int xoff = 0;
        int yoff = 0;
        int flyer_y = 0;
        int open_flyer_y = 0;
        bool found_selected = false;
        bool had_opening = false;
        bool had_closing = false;
        for (auto d: c->children) {
            if (!d->exists) {
                continue;
            }
            if (d == a_data->opening_container || d == a_data->closing_container) {
                continue;
            }
            // TODO: check if d is selected, and then wait till end of line, and then find our selected container and do the layout calc
            d->wanted_bounds.w = b.w / album_per_line;
            d->wanted_bounds.h = album_height;
            d->real_bounds.x = b.x + xoff;
            d->real_bounds.y = b.y + yoff; 
            d->real_bounds.w = d->wanted_bounds.w;
            d->real_bounds.h = d->wanted_bounds.h;
            xoff += d->real_bounds.w;
            auto al = (AlbumData *) d->user_data;
            if (d == a_data->opening) {
                had_opening = true;
            }
            if (d == a_data->closing) {
                had_closing = true;
            }
             if (al->selected) {
                //found_selected = true;
            }
            
            if (xoff + d->real_bounds.w > b.w) {
                xoff = 0;
                yoff += d->real_bounds.h;
                float flyeroff = 0;
                if (had_opening) {
                    if (a_data->opening_container->pre_layout) {
                        a_data->opening_container->pre_layout(client, a_data->opening_container, b);
                    }
                    a_data->opening_container->real_bounds.x = b.x + xoff;
                    a_data->opening_container->real_bounds.y = b.y + yoff;
                    open_flyer_y = yoff;
                    a_data->opening_container->real_bounds.w = std::floor(b.w);
                    a_data->opening_container->real_bounds.h = a_data->opening_container->wanted_bounds.h * a_data->openess;
                    flyeroff = a_data->opening_container->wanted_bounds.h * a_data->openess;   
                }
                if (had_closing) {
                    if (a_data->closing_container->pre_layout) {
                        a_data->closing_container->pre_layout(client, a_data->closing_container, b);
                    }
                    a_data->closing_container->real_bounds.x = b.x + xoff;
                    if (had_opening) {
                        a_data->closing_container->real_bounds.y = a_data->opening_container->real_bounds.y;
                    } else {
                        a_data->closing_container->real_bounds.y = b.y + yoff;
                    }
                    flyer_y = yoff;
                    a_data->closing_container->real_bounds.w = std::floor(b.w);
                    if (had_opening) {
                        a_data->closing_container->real_bounds.h = a_data->closing_container->wanted_bounds.h;

                        flyeroff = a_data->opening_container->wanted_bounds.h;
                        float opening_h = a_data->opening_container->wanted_bounds.h;
                        float closing_h = a_data->closing_container->wanted_bounds.h;
                        if (closing_h > opening_h) {
                            flyeroff += (closing_h - opening_h) * a_data->closingness;
                        } else {
                            // if opening_h > closing_h
                            flyeroff = (opening_h - closing_h) * a_data->openess + a_data->closing_container->wanted_bounds.h;
                        }
                    } else {
                        a_data->closing_container->real_bounds.h = a_data->closing_container->wanted_bounds.h * a_data->closingness;
                        flyeroff = a_data->closing_container->real_bounds.h;   
                    }
                }
                yoff += flyeroff;
                had_closing = false;
                had_opening = false;
            }
        }
        float flyeroff = 0;
        if (had_opening || had_closing) {
            xoff = 0;
            yoff += album_height;
        }
        if (had_opening) {
            if (a_data->opening_container->pre_layout) {
                a_data->opening_container->pre_layout(client, a_data->opening_container, b);
            }
            a_data->opening_container->real_bounds.x = b.x + xoff;
            a_data->opening_container->real_bounds.y = b.y + yoff;
            open_flyer_y = yoff;
            a_data->opening_container->real_bounds.w = std::floor(b.w);
            a_data->opening_container->real_bounds.h = a_data->opening_container->wanted_bounds.h * a_data->openess;
            flyeroff = a_data->opening_container->wanted_bounds.h * a_data->openess;   
        }
        if (had_closing) {
            if (a_data->closing_container->pre_layout) {
                a_data->closing_container->pre_layout(client, a_data->closing_container, b);
            }
            a_data->closing_container->real_bounds.x = b.x + xoff;
            if (had_opening) {
                a_data->closing_container->real_bounds.y = a_data->opening_container->real_bounds.y;
            } else {
                a_data->closing_container->real_bounds.y = b.y + yoff;
            }
            flyer_y = yoff;
            a_data->closing_container->real_bounds.w = std::floor(b.w);
            if (had_opening) {
                float us = a_data->closing_container->real_bounds.h * a_data->closingness;
                float them = a_data->opening_container->real_bounds.h;
                if (us > them) {
                    flyeroff = std::abs(us - them);
                }
            } else {
                a_data->closing_container->real_bounds.h = a_data->closing_container->wanted_bounds.h * a_data->closingness;
                flyeroff = a_data->closing_container->real_bounds.h;   
            }
        }
        yoff += flyeroff;
        
        if (a_data->opening_container) {
            layout(client, client->cr, a_data->opening_container, a_data->opening_container->real_bounds);
        }
        if (a_data->closing_container) {
            layout(client, client->cr, a_data->closing_container, a_data->closing_container->real_bounds);
        }
        
        if (a_data->opening_container) {
            if (a_data->first_frame) {
                a_data->first_frame = false;
                scroll_to = 0;
                
                start_y = scroll->scroll_v_visual;
                client_create_animation(client->app, client, &scroll_to, c->lifetime, 0.0,
                                                    600.0,
                                                    getEasingFunction(EaseSmoothIn),
                                                    1.0, true);
            }
        }
        
        bool began = false;
        for (auto a: client->animations)
            if (a.value == &scroll_to)
                began = true;
     
        if (began && client->app->current - scroll->last_time_scrolled > 600 && a_data->opening_container) {
            //target_y = -(a_data->opening_container->real_bounds.y - b.y) + album_height * 1.27;
            target_y = -(a_data->opening_container->real_bounds.y - b.y) + album_height;
            scroll->scroll_v_visual = (((target_y - start_y) * scroll_to) + start_y);
            if (scroll->scroll_v_visual > 0) {
                scroll->scroll_v_visual = 0;
            }
            scroll->scroll_v_real = scroll->scroll_v_visual;
            //scroll->scroll_v_visual = target_y;
            //scroll->scroll_v_real = target_y;
        }
                          
        content->wanted_bounds.w = b.w;
        content->wanted_bounds.h = yoff;
    };
 
    return;
}

/*
*/

