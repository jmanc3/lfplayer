
#include "main.h"
#include "application.h"
#include "config.h"
#include "dpi.h"
#include "drawer.h"
#include "album_tab.h"
#include "songs_tab.h"
#include "queue.h"
#include "stb_image.h"
#include "stb_image_resize2.h"
#include "components.h"
#include "player.h"
#include "edit_info.h"
#include "ThreadPool.h"
#include <thread>
#include <filesystem>
#include <fstream>
#include <stdlib.h>
#include <string.h>
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <wordexp.h>

#include <taglib/audioproperties.h>
#include <taglib/id3v2tag.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2frame.h>
#include <taglib/id3v2header.h>
#include <taglib/attachedpictureframe.h>

#include <taglib/flacpicture.h>
#include <taglib/oggfile.h>
#include <iostream>
#include <unordered_map>
#include <fontconfig/fontconfig.h>
#include "stb_image_write.h"


#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif


App *app;

bool restart = false;

ArgbColor white_bg = ArgbColor(.973, .973, .973, 1); 
ArgbColor underbar_bg = ArgbColor(.949, .949, .949, 1); 
ArgbColor top_bg = ArgbColor(.796, .796, .796, 1); 

int top_size = 60;
int top_underbar_size = 36;
int active_tab = 0;
int album_height = 400;
int album_width = 300;
int album_target_width = 170 * 1.6;
int song_width_in_album = 430;
int song_height_in_album = 30;
bool converting = false; // a track to a playable format using ffmpeg
long converting_start = 1;

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);

std::vector<CachedArt *> cached_art;
std::unordered_map<std::string, AlbumOption> album_songs;


static void load_image(const std::string& mp3Path, const std::string& imagePath) {
    // Open the MP3 file
    TagLib::MPEG::File file(mp3Path.c_str());
    if (!file.isValid()) {
        std::cerr << "Invalid MP3 file: " << mp3Path << std::endl;
        return;
    }

    // Read image into memory
    std::ifstream imageFile(imagePath, std::ios::binary);
    if (!imageFile) {
        std::cerr << "Failed to open image file: " << imagePath << std::endl;
        return;
    }
    std::vector<char> imageData((std::istreambuf_iterator<char>(imageFile)), std::istreambuf_iterator<char>());
    imageFile.close();

    // Access the ID3v2 tag (create if doesn't exist)
    TagLib::ID3v2::Tag* id3v2tag = file.ID3v2Tag(true);

    // Remove any existing APIC frames
    TagLib::ID3v2::FrameList frames = id3v2tag->frameListMap()["APIC"];
    for (auto* frame : frames) {
        id3v2tag->removeFrame(frame);
    }

    // Create and populate new APIC frame
    auto* frame = new TagLib::ID3v2::AttachedPictureFrame();
    frame->setMimeType("image/jpeg"); // Or "image/png" depending on image
    frame->setType(TagLib::ID3v2::AttachedPictureFrame::FrontCover);
    frame->setPicture(TagLib::ByteVector(imageData.data(), (uint)imageData.size()));
    frame->setDescription("Cover");

    // Add new frame to tag
    id3v2tag->addFrame(frame);

    // Save the tag to file
    if (!file.save()) {
        std::cerr << "Failed to save tag to MP3 file." << std::endl;
    } else {
        std::cout << "Album art successfully embedded." << std::endl;
    }
}

void load_in_fonts();

static int  get_text_width(AppClient *client, int size, std::string label) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto layout = get_cached_pango_font(client->cr, config->font, size, PANGO_WEIGHT_NORMAL);
    pango_layout_set_text(layout, label.data(), label.size());
    int width;
    int height;
    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);
    return (float) (logical.width / PANGO_SCALE);
}

void start_window_move(xcb_connection_t* conn, xcb_window_t window, int root_x, int root_y) {
    // Get the atoms
    xcb_intern_atom_cookie_t moveresize_cookie = xcb_intern_atom(conn, 0, strlen("_NET_WM_MOVERESIZE"), "_NET_WM_MOVERESIZE");
    xcb_intern_atom_reply_t* moveresize_reply = xcb_intern_atom_reply(conn, moveresize_cookie, NULL);
    if (!moveresize_reply) return;
    xcb_atom_t moveresize_atom = moveresize_reply->atom;
    free(moveresize_reply);

    // Create and send the ClientMessage event
    xcb_client_message_event_t ev;
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = window;
    ev.type = moveresize_atom;
    ev.data.data32[0] = root_x;      // root x
    ev.data.data32[1] = root_y;      // root y
    ev.data.data32[2] = 8;           // _NET_WM_MOVERESIZE_MOVE (the direction)
    ev.data.data32[3] = XCB_BUTTON_INDEX_1;  // button (usually 1)
    ev.data.data32[4] = 0;           // unused

    // Get the root window
    xcb_get_geometry_cookie_t geom_cookie = xcb_get_geometry(conn, window);
    xcb_get_geometry_reply_t* geom_reply = xcb_get_geometry_reply(conn, geom_cookie, NULL);
    if (!geom_reply) return;
    xcb_window_t root = geom_reply->root;
    free(geom_reply);

    xcb_send_event(conn, 0, root,
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
        (const char*)&ev);

    xcb_flush(conn);
}

static void paint_tab_label(AppClient *client, cairo_t *cr, Container *c) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
    auto label = (Label *) c->user_data;
    
    auto [f, w, h] = draw_text_begin(client, label->size, config->font, EXPAND(ArgbColor(0, 0, 0, 1)), label->text, true);
    f->end();
    
    auto circ_bounds = c->real_bounds;
    int pad = 4 * config->dpi;
    circ_bounds.y += circ_bounds.h / 2 - h / 2 - pad;
    circ_bounds.h = h + pad * 2;
    
    
    bool active = (label->text == "Songs" && active_tab == 0) 
        || (label->text == "Albums" && active_tab == 1) 
        || (label->text == "Artists" && active_tab == 2) 
        || (label->text == "Playlists" && active_tab == 3);
    
    if (c->state.mouse_hovering || active)
        draw_round_rect(client, ArgbColor(.878, .878, .878, 1), circ_bounds, circ_bounds.h / 2);
    
    auto thickness = std::round(1 * config->dpi);
    auto border_pos = circ_bounds;
    border_pos.x += .5;
    border_pos.y += .5;
    border_pos.w -= 1;
    border_pos.h -= 1;
    
    if (c->state.mouse_hovering || active) {
        auto dropshadow_bounds = c->real_bounds;
        dropshadow_bounds.y += 1;
        draw_text(client, label->size, config->font, EXPAND(ArgbColor(1, 1, 1, 1)), label->text, dropshadow_bounds, true);
    }
    draw_text(client, label->size, config->font, EXPAND(ArgbColor(0, 0, 0, 1)), label->text, c->real_bounds, true);
}

static void setup_tab(AppClient *client, Container *c, std::string text, int size) {
    c->when_paint = paint_tab_label;
    auto label = new Label(text);
    label->size = size;
    c->user_data = label;
}

bool endsWith(const std::string& str, const std::string& suffix) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
    if (suffix.size() > str.size()) return false;
    return std::equal(
        suffix.rbegin(), suffix.rend(), str.rbegin(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == 
                   std::tolower(static_cast<unsigned char>(b));
        }
    );
}

static void set_label(AppClient *client, Container *c, std::string text) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
  
    auto label = new Label(text);
    label->text = text;
    label->size = 26 * config->dpi;
    c->user_data = label;
    c->when_paint = [](AppClient *client, cairo_t *, Container *c) {
        auto data = (Label *) c->user_data;
        //draw_colored_rect(client, ArgbColor(1, 0, 1, 1), c->real_bounds);
        draw_text(client, data->size, config->icons, EXPAND(ArgbColor(0, 0, 0, 1)), data->text, c->real_bounds);
    };    
}

static void clip_rounded_rect(cairo_t* cr, double x, double y, double width, double height, double radius) {
    double r = radius;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + width - r, y + r, r, -M_PI_2, 0);
    cairo_arc(cr, x + width - r, y + height - r, r, 0, M_PI_2);
    cairo_arc(cr, x + r, y + height - r, r, M_PI_2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
    cairo_clip(cr);
}

// TODO: this is not the funcitonality:
// the functionality is on click down, start at that spot, when drag, throttle to only update every 250
// If you let go, do nothing

static long last_time_set_position_set = 0;
static float position_set;

static void throttle_position(App *app, AppClient *client, Timeout *timeout, void *userdata) {
    timeout->keep_running = true;
    if (client->app->current - last_time_set_position_set > 240) {
        timeout->keep_running = false;
        player->playback_start();
        player->set_position(position_set);
    }
}

static void throttle_change_position(AppClient *client, float scalar) {
    // This should start a new timeout that's seeking for 300 ms of silence, and then execute
    if (std::abs(scalar - position_set) < .015) {
        return;
    }
    
    position_set = scalar;
    last_time_set_position_set = client->app->current;
    
    for (auto t: app->timeouts) {
        if (t->function == throttle_position) {
            return;
        }
    }
    player->playback_stop();
    app_timeout_create(app, client, 25, throttle_position, nullptr, "throttle_position_set");
}

struct CenterData : UserData {
    cairo_surface_t *surface = nullptr;
    cairo_surface_t *logo_surface = nullptr;
    std::string cached_path = "";
    Bounds bar;
    
    ~CenterData() {
        if (surface) {
            cairo_surface_destroy(surface);
        }
    }
};

void activate_coverlet(AppClient *client, std::string album_name) {
    Container *coverlet;
    Container *root;
    if (client->root->children[0]->name == "coverlet") {
        coverlet = client->root->children[0];
        root = client->root->children[1];
    } else {
        root = client->root->children[0];
        coverlet = client->root->children[1];
    }
    coverlet->interactable = true;
    root->interactable = false;
    coverlet->z_index = 2;
    root->z_index = 1;
    client->root->children.clear();
    client->root->children.push_back(coverlet);
    client->root->children.push_back(root);
    
    auto data = (SurfaceButton *) coverlet->user_data;
        
    if (data->surface) {
        cairo_surface_destroy(data->surface);
    }
    for (auto art : cached_art) {
        if (art->name == sanitize_file_name(album_name)) {
            data->surface = accelerated_surface(app, client, art->width * 2, art->height * 2);
            paint_surface_with_data(data->surface, art->large_data, art->width * 2, art->height * 2);
            //fade_out_edges_2(data->surface, 16 * config->dpi);
        }
    }
}

void deactivate_coverlet(AppClient *client) {
    Container *coverlet;
    Container *root;
    if (client->root->children[0]->name == "coverlet") {
        coverlet = client->root->children[0];
        root = client->root->children[1];
    } else {
        root = client->root->children[0];
        coverlet = client->root->children[1];
    }
    coverlet->interactable = false;
    root->interactable = true;
     coverlet->z_index = 1;
    root->z_index = 2;
    client->root->children.clear();
    client->root->children.push_back(root);
    client->root->children.push_back(coverlet);
    auto data = (SurfaceButton *) coverlet->user_data;
    if (data->surface) {
        cairo_surface_destroy(data->surface);
        data->surface = nullptr;
    }
}


static void paint_center(AppClient *client, Container *c) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
    auto data = new CenterData;
    load_icon_full_path(app, client, &data->logo_surface, asset("penguin.png"), 44 * config->dpi);
    dye_surface(data->logo_surface, ArgbColor(.4, .4, .4, 1));
     
    c->user_data = data;
    c->when_paint = [](AppClient *client, cairo_t *, Container *c) {
        auto data = (CenterData *) c->user_data;
        auto b = c->real_bounds;
        b.shrink(5 * config->dpi);
        
        cairo_save(client->cr);
        cairo_pattern_t* pattern = cairo_pattern_create_linear(0, 0, 0, c->real_bounds.h);
        defer(cairo_pattern_destroy(pattern));

        clip_rounded_rect(client->cr, b.x, b.y, b.w, b.h, 6 * config->dpi);
        cairo_pattern_add_color_stop_rgb(pattern, 0.0, .635, .651, .678);
        cairo_pattern_add_color_stop_rgb(pattern, 0.6, .88, .88, .88);
        cairo_pattern_add_color_stop_rgb(pattern, 1.0, .937, .953, .961);
        set_rect(client->cr, b);
        cairo_set_source(client->cr, pattern);
        cairo_fill(client->cr);
        cairo_restore(client->cr);

        /*
        draw_round_rect(client, ArgbColor(.635, .651, .678, 1), b, 6 * config->dpi);
        draw_round_rect(client, ArgbColor(.937, .953, .961, 1), b, 6 * config->dpi);
        */
        b.shrink(.5);
        draw_round_rect(client, ArgbColor(.3, .3, .3, .2), b, 6 * config->dpi, 2);
        
        if (!player->data) {
            if (data->logo_surface) {
                int width = cairo_image_surface_get_width(data->logo_surface);
                int height = cairo_image_surface_get_height(data->logo_surface);
                cairo_set_source_surface(client->cr, data->logo_surface, c->real_bounds.x + c->real_bounds.w * .5 - width * .5, c->real_bounds.y + c->real_bounds.h * .5 - height * .5);
                cairo_paint(client->cr); 
            }
        } else if (player->data) {
            if (data->cached_path != player->path) {
                if (data->surface) {
                    cairo_surface_destroy(data->surface);
                    data->surface = nullptr;
                }
            }
            if (data->surface == nullptr && !player->path.empty()) {
                data->cached_path = player->path;
                load_icon_full_path(app, client, &data->surface, player->cover, c->real_bounds.h - 20 * config->dpi);
            }
            int font_size = 11 * config->dpi;
            int pad_size = 11 * config->dpi;
            int height;
            
            auto text_bounds = c->real_bounds;
            auto album_size = c->real_bounds.h - 20 * config->dpi;
            auto min_offset = c->real_bounds.x + album_size + 18 * config->dpi;
             
            text_bounds.w -= 70 * config->dpi;
            draw_clip_begin(client, text_bounds);
            {
                auto [f, w, h] = draw_text_begin(client, font_size, config->font, EXPAND(ArgbColor(0, 0, 0, 1)), player->title, true);
                auto title_offset = c->real_bounds.x + c->real_bounds.w / 2 - w / 2;
                if (title_offset < min_offset) {
                    title_offset = min_offset;
                }
                f->draw_text_end(title_offset, 
                                 c->real_bounds.y + 6 * config->dpi);
                height = h;
            }
            {
                if (!player->artist.empty() && !player->album.empty()) {
                    auto [f, w, h] = draw_text_begin(client, 9 * config->dpi, config->font, EXPAND(ArgbColor(0, 0, 0, .6)), player->artist + "  —  " + player->album, true);
                    auto artist_album_offset = c->real_bounds.x + c->real_bounds.w / 2 - w / 2;
                    if (artist_album_offset < min_offset) {
                        artist_album_offset = min_offset;
                    }
                    f->draw_text_end(artist_album_offset, 
                                         c->real_bounds.y + 8 * config->dpi + height * .9);
                }
            }
            draw_clip_end(client);
            
            {
                auto bg_bounds = c->real_bounds;
                int slider_size = 7 * config->dpi; 
                int squeeze_size = -130 * config->dpi;
                bg_bounds.y += bg_bounds.h - 12 * config->dpi - slider_size;
                bg_bounds.h = slider_size;
                
                bg_bounds.x -= squeeze_size;
                bg_bounds.w += squeeze_size * 2;
                
                auto raw_bg_bounds = bg_bounds;
                data->bar = raw_bg_bounds;
                bg_bounds.x -= 6 * config->dpi;
                bg_bounds.w += 12 * config->dpi;
                // Draw slider
                auto scalar = ((float) player->data->currentFrame / (float) player->data->end);
                draw_round_rect(client, ArgbColor(.608, .608, .608, .6), bg_bounds, bg_bounds.h / 2);
                draw_round_rect(client, ArgbColor(.4, .4, .4, .3), bg_bounds, bg_bounds.h / 2, 1 * config->dpi);
                
                Bounds progress_clip = Bounds(c->real_bounds);
                progress_clip.w = ((raw_bg_bounds.x + raw_bg_bounds.w * scalar - 2.5 * config->dpi)) - progress_clip.x;
                draw_clip_begin(client, progress_clip);
                draw_round_rect(client, ArgbColor(.3, .3, .3, .5), bg_bounds, bg_bounds.h / 2);
                draw_clip_end(client);
                
                draw_colored_rect(client, ArgbColor(.97, .97, .97, .8), 
                                      Bounds(bg_bounds.x + slider_size * .5, 
                                             bg_bounds.y + bg_bounds.h,
                                             bg_bounds.w - slider_size, 
                                             std::floor(1 * config->dpi)));
                
                
                auto scroll = raw_bg_bounds;
                scroll.y -= 2 * config->dpi;
                scroll.x += scroll.w * scalar - 2.5 * config->dpi;
                scroll.w = 4.5 * config->dpi;
                scroll.h += 4 * config->dpi;
                draw_round_rect(client, ArgbColor(1.0, 1.0, 1.0, 1.0), scroll, 2.25 * config->dpi);
                
                draw_round_rect(client, ArgbColor(0.6, 0.6, 0.6, 1.0), scroll, 2.25 * config->dpi, 1 * config->dpi);
                
                int width = 0;
                {
                    auto [f, w, h] = draw_text_begin(client, 8 * config->dpi, config->font, EXPAND(ArgbColor(0, 0, 0, 1)), player->length_in_seconds);
                    f->draw_text_end(raw_bg_bounds.x + raw_bg_bounds.w + 14 * config->dpi, raw_bg_bounds.y + raw_bg_bounds.h / 2 - h / 2);
                    height = h;
                    width = w;
                }

                {
                    auto current_frame = player->data->currentFrame;
                    if (current_frame <= 0)
                        current_frame = 1;
                    int time = 0;
                    if (player->data->decoder.outputSampleRate != 0) {
                        time = current_frame / player->data->decoder.outputSampleRate;
                    }
                    auto time_str = seconds_to_mmss(time);
                    auto [f, w, h] = draw_text_begin(client, 8 * config->dpi, config->font, EXPAND(ArgbColor(0, 0, 0, 1)), time_str);
                    if (width == 0)
                        width = w;
                    f->draw_text_end(raw_bg_bounds.x - 14 * config->dpi - width, raw_bg_bounds.y + raw_bg_bounds.h / 2 - h / 2);
                }
                
                if (data->surface) {
                    int width = cairo_image_surface_get_width(data->surface);
                    int height = cairo_image_surface_get_height(data->surface);
                    cairo_set_source_surface(client->cr, data->surface, c->real_bounds.x + 12 * config->dpi, c->real_bounds.y + c->real_bounds.h * .5 - height * .5);
                    cairo_paint(client->cr); 
                }
            }
        }
        
        if (converting) {
            auto bottom = b;
            float newh = 4 * config->dpi;
            bottom.y += bottom.h - newh;
            bottom.h = newh;
            draw_round_rect(client, ArgbColor(0.122, 0.514, 0.992, 1.0), bottom, 2 * config->dpi);
            
            {
                auto [f, w, h] = draw_text_begin(client, 8 * config->dpi, config->font, 0.0, 0.0, 0.0, 0.8,
                                                 "Auto-converting via ffmpeg...");
                f->draw_text_end(bottom.x + 4 * config->dpi, bottom.y - h - 2 * config->dpi);
            }
            
            auto highlight = bottom;
            float scalar = std::fmod((float) (client->app->current - converting_start), 1000.0f) / 1000.0f;
            float r = bottom.w * .37;
            highlight.x += scalar * (highlight.w + r * 4);
            highlight.w = 30 * config->dpi;
            
            draw_clip_begin(client, bottom);
            int x = highlight.x - r * 2;
            int y = highlight.y;
            cairo_pattern_t* radial = cairo_pattern_create_radial(x, y, 0, x, y, r);
            
            // Add color stops: white (fully opaque) at the center and transparent at the edge
            cairo_pattern_add_color_stop_rgba(radial, 0.0, 1.0, 1.0, 1.0, 0.5); // White
            cairo_pattern_add_color_stop_rgba(radial, 1.0, 1.0, 1.0, 1.0, 0.0); // Transparent
            
            // Set the pattern as the source
            cairo_set_source(client->cr, radial);
            
            // Draw the circle
            cairo_arc(client->cr, x, y, r, 0, 2 * M_PI);
            cairo_fill(client->cr);
            
            // Destroy the pattern to free memory
            cairo_pattern_destroy(radial);
            
            draw_clip_end(client);
            
            
            request_refresh(app, client); // aka keep pumping if converting
        }
    };    
    
    c->when_drag_end_is_click = false;
    c->when_mouse_down = [](AppClient *client, cairo_t *cr, Container *c) {
        auto data = (CenterData *) c->user_data;
        if (data->surface && client->mouse_current_x < c->real_bounds.x + c->real_bounds.h * 1.1) {
            auto data = (CenterData *) c->user_data;
            if (data->surface && client->mouse_current_x < c->real_bounds.x + c->real_bounds.h * 1.1) {
                auto path = player->path;
                std::string album_name;
                for (auto a : album_songs) {
                    for (auto option : a.second.songs) {
                        if (option.full == path) {
                            album_name = a.first;
                            goto out;
                        }
                    }
                }
                out:
 
                activate_coverlet(client, album_name);
                //app_timeout_create(app, client, 20, [](App *app, AppClient *client, Timeout *timeout, void *userdata) {
                //                     }, nullptr, "activate_coverlet");
            }
        } else {
            auto scalar = (client->mouse_current_x + 1 * config->dpi - data->bar.x) / data->bar.w;
            player->set_position(scalar);
        }
    };
    c->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {

    };
}

static void paint_textarea_border(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (!container->active) {
        auto data = (TextAreaData *) container->children[0]->user_data;
        if (!data->state->text.empty()) {
            data->state->text = "";
            data->state->cursor = 0;
            data->state->selection_x = -1;
            for (auto child: container->children[0]->children) {
                auto data = (ListOption *) child->user_data;
                //data->selected = false;
                child->exists = true;
            }    
        }
    }
    ArgbColor color;
    if (container->state.mouse_hovering || container->state.mouse_pressing || container->active) {
        if (container->state.mouse_pressing || container->active) {
            if (container->state.mouse_pressing && container->active) {
                color = lighten(config->color_pinned_icon_editor_field_pressed_border, 7);
            } else {
                color = config->color_pinned_icon_editor_field_pressed_border;
            }
        } else {
            color = config->color_pinned_icon_editor_field_hovered_border;
        }
    } else {
        color = config->color_pinned_icon_editor_field_default_border;
    }
    float size = 14 * config->dpi;
    //draw_colored_rect(client, ArgbColor(.984, .988, .992, 1), container->real_bounds);
    //draw_margins_rect(client, color, container->real_bounds, 2, 0);
    
    draw_round_rect(client, ArgbColor(.984, .988, .992, 1), container->real_bounds, container->real_bounds.h / 2, 0);
    draw_round_rect(client, color, container->real_bounds, container->real_bounds.h / 2, std::floor(1.0 * config->dpi));
}

static long last_time_set = 0;
static std::string track_set;

static void throttle(App *app, AppClient *client, Timeout *timeout, void *userdata) {
    timeout->keep_running = true;
    if (client->app->current - last_time_set > 400) {
        timeout->keep_running = false;
        player->play_track(track_set);
    }
}

static void limited_play_track(AppClient *client, std::string track) {
    player->playback_stop();
    track_set = track;
    last_time_set = client->app->current;
    // Throttle
    //player->play_track(track);
    for (auto t: app->timeouts) {
        if (t->function == throttle) {
            return;
        }
    }
    app_timeout_create(app, client, 25, throttle, nullptr, "throttle_play_track");
}

static void paint_volume_slider(AppClient *client, cairo_t *cr, Container *c) {
    auto bg_bounds = c->real_bounds;
    int slider_size = 13 * config->dpi; 
    int squeeze_size = 10 * config->dpi;
    bg_bounds.y += bg_bounds.h / 2 - slider_size / 2;
    bg_bounds.h = slider_size;
    auto raw_bg_bounds = bg_bounds;
    bg_bounds.x -= squeeze_size;
    bg_bounds.w += squeeze_size * 2;
    draw_round_rect(client, ArgbColor(.608, .608, .608, .6), bg_bounds, bg_bounds.h / 2);
    
    
    // TODO: replace with drop shadow from 
    cairo_save(client->cr);
    
    cairo_pattern_t* pattern = cairo_pattern_create_linear(0, 0, 0, c->real_bounds.h);

    clip_rounded_rect(client->cr, bg_bounds.x, bg_bounds.y, bg_bounds.w, bg_bounds.h, 6 * config->dpi); // Use 6 as radius or any value
    cairo_pattern_add_color_stop_rgba(pattern, 0.0, .1, .1, .1, .6); // Red
    cairo_pattern_add_color_stop_rgba(pattern, 0.55, .608, .608, .608, .6); // Red
    cairo_pattern_add_color_stop_rgba(pattern, 1.0, .62, .62, .62, .6); // Green
    set_rect(client->cr, bg_bounds);
    cairo_set_source(client->cr, pattern);
    cairo_fill(client->cr);
    
    cairo_restore(client->cr);
    
    // TODO: replace with drop shadow from 
    cairo_save(client->cr);
    
    cairo_pattern_destroy(pattern);
    pattern = cairo_pattern_create_linear(0, 0, 0, c->real_bounds.h);
    
    clip_rounded_rect(client->cr, bg_bounds.x, bg_bounds.y, bg_bounds.w * player->volume_unthrottled, bg_bounds.h, 6 * config->dpi); // Use 6 as radius or any value
    cairo_pattern_add_color_stop_rgba(pattern, 0.0, .1, .1, .1, .38); // Red
    cairo_pattern_add_color_stop_rgba(pattern, 0.55, .3, .3, .3, .38); // Red
    cairo_pattern_add_color_stop_rgba(pattern, 1.0, .4, .4, .4, .38); // Green
    set_rect(client->cr, bg_bounds);
    cairo_set_source(client->cr, pattern);
    cairo_fill(client->cr);
    
    cairo_restore(client->cr);
    
    draw_round_rect(client, ArgbColor(.49, .49, .49, .6), bg_bounds, bg_bounds.h / 2, std::floor(1 * config->dpi));
    auto bline = bg_bounds;
    bline.y = bline.y + bline.h;
    bline.h = std::floor(1 * config->dpi);
    auto sq = bg_bounds.h * .35;
    bline.x += sq;
    bline.w -= sq * 2;
    draw_colored_rect(client, ArgbColor(.82, .82, .82, .6), bline);
    bline.y += 1;
    draw_colored_rect(client, ArgbColor(.922, .922, .922, .6), bline);
    
    auto circ_bounds = raw_bg_bounds;
    int nub_size = 22 * config->dpi; 
    circ_bounds.x += circ_bounds.w * player->volume_unthrottled;
    circ_bounds.w = nub_size;
    circ_bounds.x -= nub_size / 2;
    circ_bounds.y += circ_bounds.h / 2 - nub_size / 2;
    circ_bounds.h = nub_size;
    draw_round_rect(client, ArgbColor(.8, .8, .8, 1), circ_bounds, circ_bounds.h / 2);
    
    draw_round_rect(client, ArgbColor(.5, .5, .5, 1), circ_bounds, circ_bounds.h / 2, std::floor(1.0 * config->dpi));
    static cairo_surface_t* image = cairo_image_surface_create_from_png(asset("plate3.png").c_str());
    
    // Get image dimensions
    int width = cairo_image_surface_get_width(image);
    int height = cairo_image_surface_get_height(image);

    
    // Compute scale factor
    double scale = static_cast<double>(22.0 * config->dpi) / width;
    double scale_up = (double) width / static_cast<double>(22.0 * config->dpi);

    cairo_save(cr);
    // Scale the context
    cairo_scale(cr, scale, scale); // This scales everything drawn afterward

    // Draw the image surface onto the target surface
    cairo_set_source_surface(cr, image, circ_bounds.x * scale_up, circ_bounds.y * scale_up); // Position at (0, 0)
    cairo_paint_with_alpha(cr, .6); // Paint the image onto the surface
    cairo_restore(cr);

}

static void set_volume_throttled(AppClient *client, float scalar) {
    player->volume_unthrottled = scalar;
    if (player->volume_unthrottled > 1)
        player->volume_unthrottled = 1;
    if (player->volume_unthrottled <= 0)
        player->volume_unthrottled = 0;
    for (auto t: client->app->timeouts) {
        if (t->text == "throttle_volume") {
            float *data = (float *) t->user_data;
            *data = scalar;
            return;
        }
    }
    float *data = new float;
    *data = scalar;
    app_timeout_create(app, client, 50, [](App *app, AppClient *client, Timeout *timeout, void *userdata) {
                               timeout->keep_running = false;
                               float *data = (float *) userdata;
                               player->set_volume(*data);
                           }, data, "throttle_volume");
}

 static void active_next(AppClient *client) {
    if (auto songs_content = container_by_name("songs_content", client->root)) {
        for (int i = 0; i < songs_content->children.size(); i++) {
            auto child = songs_content->children[i];
            if (child->exists) {
                auto data = (ListOption *) child->user_data;
                if (data->selected) {
                    data->selected = false;
                    
                    bool set = false;
                    for (int s = i + 1; s < songs_content->children.size(); s++) {
                        child = songs_content->children[s];
                        if (child->exists) {
                            auto data = (ListOption *) child->user_data;
                            set = true;
                            data->selected = true;
                            //limited_play_track(client, data->label->text);
                            break;
                        }
                    }
                    if (!set) { // wrap around end to start
                        for (int i = 0; i < songs_content->children.size(); i++) {
                            auto child = songs_content->children[i];
                            if (child->exists) {
                                auto data = (ListOption *) child->user_data;
                                data->selected = true;
                                //limited_play_track(client, data->label->text);
                                break;
                            }
                        }
                    }
                    
                    break;
                }
            }
        }    
    }
    put_selected_on_screen(client);   
}

static void active_previous(AppClient *client) {
    if (auto songs_content = container_by_name("songs_content", client->root)) {
        for (int i = 0; i < songs_content->children.size(); i++) {
            auto child = songs_content->children[i];
            if (child->exists) {
                auto data = (ListOption *) child->user_data;
                if (data->selected) {
                    data->selected = false;
                    
                    bool set = false;
                    for (int s = i - 1; s >= 0; s--) {
                        child = songs_content->children[s];
                        if (child->exists) {
                            auto data = (ListOption *) child->user_data;
                            set = true;
                            data->selected = true;
                            //limited_play_track(client, data->label->text);
                            break;
                        }
                    }
                    if (!set) { // wrap around end to start
                        for (int i = songs_content->children.size() - 1; i >= 0; i--) {
                            auto child = songs_content->children[i];
                            if (child->exists) {
                                auto data = (ListOption *) child->user_data;
                                data->selected = true;
                                //limited_play_track(client, data->label->text);
                                break;
                            }
                        }
                    }
                    
                    break;
                }
            }
        }    
    }
    put_selected_on_screen(client);
}

static void paint_queue_button(AppClient *client, cairo_t *cr, Container *c) {
    auto data = (SurfaceButton *) c->user_data;
    if (!data->attempted) {
        data->attempted = true;
        load_icon_full_path(app, client, &data->surface, asset("queue_list.png"), 22 * config->dpi);
    }
    
    if (c->state.mouse_pressing) {
        draw_round_rect(client, ArgbColor(.6, .6, .6, 1), c->real_bounds, 6 * config->dpi);
    } else if (c->state.mouse_hovering) {
        draw_round_rect(client, ArgbColor(.8, .8, .8, 1), c->real_bounds, 6 * config->dpi);
    }

    if (data->surface) {
        int width = cairo_image_surface_get_width(data->surface);
        int height = cairo_image_surface_get_height(data->surface);

        cairo_set_source_surface(client->cr, data->surface, c->real_bounds.x + c->real_bounds.w * .5 - width * .5, c->real_bounds.y + c->real_bounds.h * .5 - height * .5);         
        cairo_paint(client->cr);
    }
}

static std::string get_selected_track(AppClient *client) {
    if (auto songs_content = container_by_name("songs_content", client->root)) {
        for (auto child: songs_content->children) {
            if (child->exists) {
                auto data = (ListOption *) child->user_data;
                if (data->selected) {
                    return data->label->text;
                }
            }
        }
    }
    return "";
}

int dist_of_col_at_position(SortOption col, int try_pos, std::vector<SortOption> &cols, int target) {
    for (int i = 0; i < cols.size(); i++) {
        if (cols[i].name == col.name) {
            cols.erase(cols.begin() + i);
            break;
        }
    }
    cols.insert(cols.begin() + try_pos, col);
    int offset = 0;
    for (int i = 0; i < cols.size(); i++) {
        auto tcol = &cols[i];
        if (tcol->name == col.name) {
            return (offset - target);
        }
        offset += tcol->size;
    }
    
    return 0;   
}

void right_click_song(AppClient *client, std::string full_path, std::string title) {    
    struct SongRightClickData : UserData {
        std::string full_path;
        std::string title;
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
    settings.w = option_width * 1.8;
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
    
    popup->root->type = ::hbox;
    auto left = popup->root->child(option_width, FILL_SPACE);
    auto right = popup->root->child(FILL_SPACE, FILL_SPACE);
    right->when_paint = [](AppClient *client, cairo_t *, Container *c) {
        draw_colored_rect(client, ArgbColor(.8, .8, .8, 1), c->real_bounds);
    };    
    
    auto add_option = [popup, left](std::string icon, std::string text) 
    { 
        auto data = new RightClickOption;
        data->icon = icon;
        data->text = text;
        auto option = left->child(FILL_SPACE, FILL_SPACE);
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
                player->play_next(client_data->full_path);
            } else if (data->text == "Play After All Next") {
                player->play_after_all_next(client_data->full_path);
            } else if (data->text == "Add to Queue") {
                player->play_last(client_data->full_path);
            } else if (data->text == "Edit Info") {
                edit_info(EditType::SONG_TYPE, client_data->title);
            } else {
                //activate_coverlet(client);
            }
            client_close_threaded(app, client);
        };
    };
    
    auto pad = left->child(FILL_SPACE, 4 * config->dpi);
    add_option("corner-up-right.svg", "Play Next");
    add_option("corner-down-right.svg", "Play After All Next");
    add_option("arrow-bar-to-down.svg", "Add to Queue");
    auto seperator = left->child(FILL_SPACE, 8 * config->dpi);
    seperator->when_paint = [](AppClient *client, cairo_t *, Container *c) {
        draw_colored_rect(client, ArgbColor(.7, .7, .7, .4), 
                              Bounds(c->real_bounds.x + 20 * config->dpi,
                                     c->real_bounds.y + 4 * config->dpi,
                                     c->real_bounds.w - 40 * config->dpi, std::floor(1 * config->dpi)));
  
    };
    add_option("", "Edit Info");
    //add_option("", "View Art");
    pad = left->child(FILL_SPACE, 4 * config->dpi);
    
    /*
    popup->root->when_clicked = [](AppClient *client, cairo_t *, Container *c) {
        auto data = (SongRightClickData *) client->user_data;
        player->play_track(data->full_path);
    };    
    */ 
       

    client_show(app, popup);
 
}

static void fill_root(AppClient *client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    auto r = client->root;
    r->type = ::stack;
    
    auto root = client->root->child(FILL_SPACE, FILL_SPACE);
    root->name = "root";
    auto coverlet = r->child(FILL_SPACE, FILL_SPACE);
    coverlet->user_data = new SurfaceButton;
    coverlet->name = "coverlet";
    deactivate_coverlet(client);
    coverlet->when_clicked = [](AppClient *client, cairo_t *, Container *c) {
        deactivate_coverlet(client);
    };
    coverlet->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        draw_colored_rect(client, ArgbColor(0, 0, 0, .8), c->real_bounds);
        auto data = (SurfaceButton *) c->user_data;
        if (data->surface) {
            int width = cairo_image_surface_get_width(data->surface);

            cairo_save(client->cr);
            float scale = 1.7;
            cairo_scale(client->cr, scale, scale);
            float shrink = 1.0 / scale;
            cairo_set_source_surface(client->cr, data->surface,
                                         (c->real_bounds.x + c->real_bounds.w * .5 - (width * scale) * .5) * shrink,
                                         (c->real_bounds.y + c->real_bounds.h * .5 - (width * scale) * .5) * shrink);         
            cairo_paint(client->cr);
            cairo_restore(client->cr);
        }
    };
    
    root->type = ::vbox;
    root->when_paint = [](AppClient *client, cairo_t *, Container *c) {
        draw_colored_rect(client, white_bg, c->real_bounds);
    };    
  
    root->when_key_event = [](AppClient *client, cairo_t *cr, Container *self, bool is_string,
                                  xkb_keysym_t keysym, char string[64], uint16_t mods,
                                  xkb_key_direction direction) {
        if (keysym == XK_period || (keysym == XK_f && mods & XCB_MOD_MASK_CONTROL) || keysym == XK_slash || (keysym == XK_l && mods & XCB_MOD_MASK_CONTROL)) {
            if (auto filter_textarea = container_by_name("filter_textarea", client->root)) {
                filter_textarea->parent->active = true;
                blink_on(app, client, filter_textarea);
                //set_active(client, filter_textarea->parent, true);
            }
        }
        if (direction == XKB_KEY_DOWN && keysym == XK_Down) {
            active_next(client);
        }
        
        if (direction == XKB_KEY_DOWN && keysym == XK_Up) {
            active_previous(client);
        }
        
        if (auto filter_textarea = container_by_name("filter_textarea", client->root)) {
            if (filter_textarea->parent->active) {
                return;
            }
        }
        if (keysym == XK_Tab) {
            if (direction == XKB_KEY_DOWN) {
                if (active_tab == 0) {
                    active_tab = 1;
                    config->starting_tab_index = active_tab;
                    client_layout(app, client);
                } else {
                    active_tab = 0;
                    config->starting_tab_index = active_tab;
                    client_layout(app, client);
                }
            }
            return;
        }

        if (direction == XKB_KEY_DOWN && keysym == XK_Return) {
            player->play_track(get_selected_track(client));
        }
        
        if (direction == XKB_KEY_DOWN && keysym == XK_Left) {
            player->back_10();
        }
       
        if (direction == XKB_KEY_DOWN && keysym == XK_Right) {
            player->forward_10();
        }
        
        if (direction == XKB_KEY_DOWN && keysym == XK_a) {
            auto track = get_selected_track(client);
            if (!track.empty())
                player->play_next(track);
        }

        if (direction == XKB_KEY_DOWN && keysym == XK_s) {
            auto track = get_selected_track(client);
            if (!track.empty())
                player->play_after_all_next(track);
        }

        if (direction == XKB_KEY_DOWN && keysym == XK_d) {
            auto track = get_selected_track(client);
            if (!track.empty())
                player->play_last(track);
        }

        if (direction == XKB_KEY_DOWN && keysym == XK_f) {
            player->clear_queue();
        }

        if (direction == XKB_KEY_DOWN && keysym == XK_n) {
            active_next(client);
        }
         
        if (direction == XKB_KEY_DOWN && keysym == XK_p) {
            active_previous(client);
        }
        if (direction == XKB_KEY_DOWN && (keysym == XK_space)) {
            player->toggle();
        }
     };
    
    auto top = root->child(::hbox, FILL_SPACE, top_size * config->dpi);
    top->when_mouse_down = [](AppClient *client, cairo_t *cr, Container *c) {
        start_window_move(client->app->connection, client->window, client->mouse_current_x, client->mouse_current_y);  
    };
    top->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        // Create a linear gradient from top (0) to bottom (height)
        cairo_pattern_t* pattern = cairo_pattern_create_linear(0, 0, 0, c->real_bounds.h);
        defer(cairo_pattern_destroy(pattern));
    
        // Add red at the top
        cairo_pattern_add_color_stop_rgb(pattern, 0.0, .937, .937, .937); // Red
    
        // Add green at the bottom
        cairo_pattern_add_color_stop_rgb(pattern, 1.0, .773, .773, .773); // Green
    
        // Apply the gradient to a rectangle
        set_rect(cr, c->real_bounds);
        cairo_set_source(cr, pattern);
        cairo_fill(cr);
        
        auto bounds = c->real_bounds;
        bounds.y += bounds.h;
        
        auto thickness = std::round(1 * config->dpi);
        bounds.h = thickness;
        bounds.y -= thickness;
        draw_colored_rect(client, ArgbColor(.682, .675, .678, 1.0), bounds);
        bounds.y -= thickness;
        draw_colored_rect(client, ArgbColor(.824, .82, .82, 1.0), bounds);
        bounds.y -= thickness;
        draw_colored_rect(client, ArgbColor(.765, .765, .765, 1.0), bounds);
        
        static cairo_surface_t* image = cairo_image_surface_create_from_png(asset("silver.png").c_str());
        
        draw_clip_begin(client, c->real_bounds);
        cairo_save(cr);
        int width = cairo_image_surface_get_width(image);
        int times = std::ceil(c->real_bounds.w / width);
        for (int i = 0; i < times; i++) {
            cairo_set_source_surface(cr, image, c->real_bounds.x + width * i, c->real_bounds.y); // Position at (0, 0)
            cairo_paint_with_alpha(cr, .2); // Paint the image onto the surface
        }
        cairo_restore(cr);
        draw_clip_end(client);
    };    
    
    auto left_section = top->child(::hbox, FILL_SPACE, FILL_SPACE);
    left_section->child(30 * config->dpi, FILL_SPACE);
    
    auto left = left_section->child(55 * config->dpi, FILL_SPACE);
    //set_label(client, left, "\uF8AD");
    left->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        static cairo_surface_t *surface;
        static bool loaded = false;
        if (!loaded) {
            loaded = true;
            load_icon_full_path(app, client, &surface, asset("back.svg"), 34 * config->dpi);
            dye_surface(surface, ArgbColor(.5, .5, .5, 1));
        }
        
        cairo_set_source_surface(cr, surface, c->real_bounds.x + c->real_bounds.w / 2 - 17 * config->dpi, c->real_bounds.y + c->real_bounds.h / 2 - 17 * config->dpi); // Position at (0, 0)
        cairo_paint(cr); 
    };
    left->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        player->set_position(0);
        //active_previous(client);
        //player->play_track(get_selected_track(client));
    };
    
    auto play = left_section->child(55 * config->dpi, FILL_SPACE);
    play->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        static cairo_surface_t *play;
        static cairo_surface_t *pause;
        static bool loaded = false;
        if (!loaded) {
            loaded = true;
            load_icon_full_path(app, client, &play, asset("play.svg"), 30 * config->dpi);
            load_icon_full_path(app, client, &pause, asset("pause.svg"), 42 * config->dpi);
            dye_surface(play, ArgbColor(.5, .5, .5, 1));
            dye_surface(pause, ArgbColor(.5, .5, .5, 1));
        }
        
        // TODO: this needs to not use the actual paused but use the read only paused;
        if (player->data) {
            if (!player->data->paused) {
                cairo_set_source_surface(cr, pause, c->real_bounds.x + c->real_bounds.w / 2 - 21 * config->dpi, c->real_bounds.y + c->real_bounds.h / 2 - 21 * config->dpi); // Position at (0, 0)
                cairo_paint(cr);
            } else {
                cairo_set_source_surface(cr, play, 4 * config->dpi + c->real_bounds.x + c->real_bounds.w / 2 - 15 * config->dpi, c->real_bounds.y + c->real_bounds.h / 2 - 15 * config->dpi); // Position at (0, 0)
                cairo_paint(cr);
            }
        } else {
            cairo_set_source_surface(cr, play, 4 * config->dpi + c->real_bounds.x + c->real_bounds.w / 2 - 15 * config->dpi, c->real_bounds.y + c->real_bounds.h / 2 - 15 * config->dpi); // Position at (0, 0)
            cairo_paint(cr);
        }
    };
    play->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        player->toggle();
    };
    
    auto right = left_section->child(55 * config->dpi, FILL_SPACE);
    //set_label(client, right, "\uF8AC");
    //set_label(client, right, "\uEB9D");
    right->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        static cairo_surface_t *surface;
        static bool loaded = false;
        if (!loaded) {
            loaded = true;
            load_icon_full_path(app, client, &surface, asset("forward.svg"), 34 * config->dpi);
            dye_surface(surface, ArgbColor(.5, .5, .5, 1));
        }
        cairo_set_source_surface(cr, surface, c->real_bounds.x + c->real_bounds.w / 2 - 17 * config->dpi, c->real_bounds.y + c->real_bounds.h / 2 - 17 * config->dpi); // Position at (0, 0)
        cairo_paint(cr);
    };
    right->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        player->pop_queue();
        //active_next(client);
        //player->play_track(get_selected_track(client));
    };
    
    left_section->child(36 * config->dpi, FILL_SPACE);
    auto volume = left_section->child(120 * config->dpi, FILL_SPACE);
    left_section->child(16 * config->dpi, FILL_SPACE);
   
    volume->name = "volume";
    volume->when_paint = paint_volume_slider;
    volume->when_drag_start = [](AppClient *client, cairo_t *cr, Container *c) {
        auto scalar = ((float) client->mouse_current_x - c->real_bounds.x) / c->real_bounds.w;
        set_volume_throttled(client, scalar);
    };
    volume->when_drag = volume->when_drag_start;
    volume->when_drag_end = volume->when_drag_start;
    volume->when_mouse_down = volume->when_drag_start;
    volume->when_mouse_up = volume->when_drag_start;
    volume->when_fine_scrolled = [](AppClient *client,
                                        cairo_t *cr,
                                        Container *container,
                                        int scroll_x,
                                        int scroll_y,
                                        bool came_from_touchpad) {
        if (came_from_touchpad) {
            auto scalar = player->volume_unthrottled;
            scalar += scroll_y / 2000.0;
            set_volume_throttled(client, scalar);
        } else {
            int full = std::round(player->volume_unthrottled * 100);
            int last_digit;
            do {
                if (scroll_y > 0) {
                    full++;
                } else if (scroll_y < 0) {
                    full--;
                }
                last_digit = full % 10;
            } while (!(last_digit == 0 || last_digit == 3 || last_digit == 5 || last_digit == 7));
            
            set_volume_throttled(client, ((double) full) / 100.0);
        }
    };

    auto center = top->child(::absolute, FILL_SPACE, FILL_SPACE);
    center->pre_layout = [](AppClient *client, Container *c, const Bounds &bounds) {
        auto queue_button = c->children[0];
        queue_button->real_bounds.x = bounds.x + bounds.w - queue_button->wanted_bounds.w - 32 * config->dpi;
        c->wanted_bounds.w = bounds.w * .4;
        queue_button->real_bounds.y = bounds.y + bounds.h * .5 - queue_button->wanted_bounds.h * .5;
        queue_button->real_bounds.w = queue_button->wanted_bounds.w;
        queue_button->real_bounds.h = queue_button->wanted_bounds.h;
    };
    paint_center(client, center);
    auto queue_button = center->child(32 * config->dpi, 32 * config->dpi);
    queue_button->user_data = new SurfaceButton;
    queue_button->when_paint = paint_queue_button;
    queue_button->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        toggle_queue_window(client);
    };
    queue_button->name = "queue_button";
        
    auto sa = top->child(FILL_SPACE, FILL_SPACE);
    
    auto search_area = sa->child(FILL_SPACE, FILL_SPACE);
    search_area->alignment = ALIGN_CENTER;
    search_area->wanted_pad = Bounds(90 * config->dpi, 0, 65 * config->dpi, 0);
    
    auto vol = search_area->child(FILL_SPACE, 28 * config->dpi);
    
    TextAreaSettings textarea_settings = TextAreaSettings(config->dpi);
    textarea_settings.single_line = true;
    textarea_settings.bottom_show_amount = 2;
    textarea_settings.right_show_amount = 2;
    textarea_settings.font_size__ = 11 * config->dpi;
    textarea_settings.prompt = "Search Library";
    textarea_settings.color = config->color_pinned_icon_editor_field_default_text;
    auto prompt_color = textarea_settings.color;
    prompt_color.a *= .4;
    textarea_settings.color_prompt = prompt_color;
    textarea_settings.color_cursor = config->color_pinned_icon_editor_cursor;
    textarea_settings.pad = Bounds(40 * config->dpi, 5 * config->dpi, 0, 0);
    

    auto textarea = make_textarea(app, client, vol, textarea_settings);
    textarea->parent->after_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        static cairo_surface_t *surface = nullptr;
        static bool attempted = false;
        if (!attempted) {
            attempted = true;
            load_icon_full_path(app, client, &surface, asset("search.png"), 16 * config->dpi);
            dye_surface(surface, ArgbColor(.3, .3, .3, 1.0));
        }
        if (surface) {
            int h = cairo_image_surface_get_height(surface);
            cairo_set_source_surface(cr, surface, c->real_bounds.x + 14 * config->dpi, c->real_bounds.y + c->real_bounds.h * .5 - ((float) (h)) * .5);
            cairo_paint_with_alpha(cr, .6);
        }
    };
    textarea->when_key_event = [](AppClient *client, cairo_t *cr, Container *self, bool is_string,
                                  xkb_keysym_t keysym, char string[64], uint16_t mods,
                                  xkb_key_direction direction) {
        if (!self->active && !self->parent->active)
            return;
        
        if (keysym == XK_Tab) {
            if (direction == XKB_KEY_DOWN) {
                if (active_tab == 0) {
                    active_tab = 1;
                    config->starting_tab_index = active_tab;
                    client_layout(app, client);
                } else {
                    active_tab = 0;
                    config->starting_tab_index = active_tab;
                    client_layout(app, client);
                }
            }
            return;
        }
        if (direction == XKB_KEY_DOWN) {
            if (keysym == XK_Escape) {
                if (auto filter_textarea = container_by_name("filter_textarea", client->root)) {
                    auto data = (TextAreaData *) filter_textarea->user_data;
                    filter_textarea->parent->active = false;
                    data->state->text = "";
                    data->state->cursor = 0;
                    data->state->selection_x = -1;
                    client_layout(app, client);
                    request_refresh(app, client);
                    put_selected_on_screen(client);
                    return;
                }
            }
    
            if (keysym == XK_Return) {
                if (active_tab == 0) { // On songs page
                    player->play_track(get_selected_track(client));
                } else if (active_tab == 1) { // On albums page
                    if (auto c = (ScrollContainer *) container_by_name("albums_root", client->root)) {
                        for (auto child  : c->content->children) {
                            if (child->exists) {
                                auto al = (AlbumData *) child->user_data;
                                if (al) {
                                    player->album_play_next(al->option.album);
                                    player->pop_queue();
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        textarea_handle_keypress(client, self, is_string, keysym, string, mods, direction);
    };
 
    textarea->parent->when_paint = paint_textarea_border;
    textarea->name = "filter_textarea";
    //set_label(client, vol, "\uE767");
    
    
        
    auto underbar = root->child(::hbox, FILL_SPACE, top_underbar_size * config->dpi);
    underbar->spacing = 16 * config->dpi;
    underbar->alignment = ALIGN_CENTER_HORIZONTALLY;
    
    underbar->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        draw_colored_rect(client, ArgbColor(.93, .93, .93, 1.0), c->real_bounds);
                
        auto thickness = std::floor(1 * config->dpi);
        auto b = c->real_bounds;
        b.y += b.h;
        b.h = thickness;
        b.y -= thickness;
         draw_colored_rect(client, ArgbColor(.812, .812, .812, 0.4), b);
       b.y -= thickness;
        draw_colored_rect(client, ArgbColor(.729, .729, .729, 0.4), b);
    };
    int total_pad = 24 * config->dpi;
    int size = 10 * config->dpi;
    auto songs = underbar->child(get_text_width(client, size, "Songs") + total_pad, FILL_SPACE);
    setup_tab(client, songs, "Songs", size);
    songs->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        active_tab = 0;
        config->starting_tab_index = active_tab;
        client_layout(app, client);
    };
    auto albums = underbar->child(get_text_width(client, size, "Albums") + total_pad, FILL_SPACE);
    albums->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        active_tab = 1;
        config->starting_tab_index = active_tab;
        client_layout(app, client);
    };
    setup_tab(client, albums, "Albums", size);
    auto artists = underbar->child(get_text_width(client, size, "Artists") + total_pad, FILL_SPACE);
    artists->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        active_tab = 2;
        config->starting_tab_index = active_tab;
        client_layout(app, client);
   };
    setup_tab(client, artists, "Artists", size);
    auto playlists = underbar->child(get_text_width(client, size, "Playlists") + total_pad, FILL_SPACE);
    playlists->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        active_tab = 3;
        config->starting_tab_index = active_tab;
        client_layout(app, client);
    };
    setup_tab(client, playlists, "Playlists", size);
    
    // Will probably be stack? which will change or something
    auto content = root->child(FILL_SPACE, FILL_SPACE);
    
    auto songs_root = content->child(FILL_SPACE, FILL_SPACE);
    std::vector<Option> options;
    fill_songs_tab(client, songs_root, options);
    
    auto albums_root = content->child(FILL_SPACE, FILL_SPACE);
    fill_album_tab(client, albums_root, options);
    
    auto artists_root = content->child(FILL_SPACE, FILL_SPACE);
    artists_root->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        draw_colored_rect(client, ArgbColor(1, 0, 1, 1), c->real_bounds);
    };

    auto playlists_root = content->child(FILL_SPACE, FILL_SPACE);
    playlists_root->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        draw_colored_rect(client, ArgbColor(1, 0, 0, 1), c->real_bounds);
    };

    content->pre_layout = [](AppClient *, Container *c, const Bounds &) {
        for (auto c: c->children)
            c->exists = false;
        // TODO: index based on actual active tab
        c->children[active_tab]->exists = true;
    };
    active_tab = config->starting_tab_index;
}

bool customCompare(const std::string &a, const std::string &b) {
    // Handle empty strings
    if (a.empty()) return true;
    if (b.empty()) return false;
    
    char a0 = a[0];
    char b0 = b[0];
    
    bool aIsAlpha = std::isalpha(static_cast<unsigned char>(a0));
    bool bIsAlpha = std::isalpha(static_cast<unsigned char>(b0));
    
    // Symbols go before letters
    if (aIsAlpha != bIsAlpha)
        return !aIsAlpha; // true if a is symbol and b is letter
    
    // Otherwise, do normal lexicographic comparison
    return a < b;
}


void cache_art() {
    std::thread t([]() {
        //std::this_thread::sleep_for(std::chrono::milliseconds(200));
        char *home = getenv("HOME");
        std::string lfp_album_art(home);
        lfp_album_art += "/.cache";
        lfp_album_art += "/lfp_album_art";
        namespace fs = std::filesystem;
        
        std::vector<std::string> albums;
        try {
            if (fs::exists(lfp_album_art) && fs::is_directory(lfp_album_art)) {
                for (const auto &entry: fs::recursive_directory_iterator(lfp_album_art)) {
                    std::string art_name = entry.path().stem().string();
                    bool already_cached = false;
                    for (auto art_option: cached_art) {
                        if (art_option->name == art_name) {
                            already_cached = true;
                        }
                    }
                    if (!already_cached) {
                        albums.push_back(art_name);
                    }
                    continue;
                }
            }
        } catch (...) {
        
        }
        std::sort(albums.begin(), albums.end(), customCompare);
        
        unsigned int threads = std::thread::hardware_concurrency();
        if (threads == 0)
            threads = 8;
        ThreadPool pool(threads);
        std::vector<std::future<void> > results;
        
        for (auto a: albums) {
            results.emplace_back(
                    pool.enqueue([a, lfp_album_art] {
                        int width, height, channels;
                        auto small_path = lfp_album_art + "/" + a + "_small.jpg";
                        auto large_path = lfp_album_art + "/" + a + "_large.jpg";
                        unsigned char *large_data = stbi_load(large_path.c_str(), &width, &height, &channels,
                                                              4); // force RGBA
                        unsigned char *small_data = stbi_load(small_path.c_str(), &width, &height, &channels,
                                                              4); // force RGBA
                        if (!small_data || !large_data)
                            return;
                        
                        int target_width = width;
                        int target_height = height;
                        
                        auto art = new CachedArt;
                        art->name = a;
                        art->data = small_data;
                        art->large_data = large_data;
                        art->width = target_width;
                        art->height = target_height;
                        cached_art.push_back(art);
                    })
            );
        }
    });
   
    t.detach();
}

int main(int argc, char* argv[]) {    
    struct stat assets_stat{};
    if (stat("/usr/share/lfp/icons", &assets_stat) != 0) { // exists
        player->finished = true;
        player->wake();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        fprintf(stderr, "Directory: '/usr/share/lfp/icons' not found. Make sure to run install.sh which places required assets where they're needed.");
        return -1;
    }
    
    config_load();
    player->volume_unthrottled = config->volume;
    player->volume = config->volume;
    
    static std::string full_path;
    if (argc >= 2) {
        char resolvedPath[PATH_MAX];
        if (realpath(argv[1], resolvedPath) == nullptr) {
            std::cerr << "Error resolving path: " << strerror(errno) << std::endl;
            exit(EXIT_FAILURE);
        } else {
            full_path = std::string(resolvedPath);
            player->play_track(full_path);
        }
    }
    
    cache_art();
    {
#ifdef TRACY_ENABLE
        ZoneScopedN("Create app");
#endif

        app = app_new();
    }
   
    if (app == nullptr) {
        printf("Couldn't start application\n");
        return -1;
    }
    
    {
#ifdef TRACY_ENABLE
        ZoneScopedN("DPI setup");
#endif
        dpi_setup(app);
        int amount_of_times = 4;
        int reattempt_time = 250;
        if (config->dpi_auto) {
            for (int oj = 0; oj < amount_of_times; oj++) {
                for (auto &i: screens) {
                    auto *screen = (ScreenInformation *) i;
                    if (screen->is_primary) {
                        config->dpi = screen->height_in_pixels / 1080.0;
                        config->dpi = std::round(config->dpi * 2) / 2;
                        if (config->dpi < 1)
                            config->dpi = 1;
                        goto out;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(reattempt_time));
                update_information_of_all_screens(app);
            }
        }
        out:
        printf("");
    }
    
    // Open our windows
    Settings settings;
    // TODO: Should restore (w and h) from save file
    settings.w = 1600 * .85 * config->dpi;
    settings.h = 1000 * .85 * config->dpi;
    settings.skip_taskbar = false;
    
    config->font = "SF Pro";
    //config->font = "SF Pro";
    config->icons = "Ubuntu";
    
    AppClient *client;
    {
#ifdef TRACY_ENABLE
        ZoneScopedN("create client");
#endif
        client = client_new(app, settings, "lfplayer");
        client->when_closed = [](AppClient *client) {
            if (player->data) {
                player->playback_stop();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                ma_device_uninit(player->data->device);
                ma_decoder_uninit(&player->data->decoder);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            config_save();
        };
    }
    
    std::string title = "Local First Music Player";
    xcb_ewmh_set_wm_name(&app->ewmh, client->window, title.length(), title.c_str());
    
    std::string icon = "minitunes";
    xcb_ewmh_set_wm_icon_name(&app->ewmh, client->window, icon.length(), icon.c_str());
    
    fill_root(client);
    
    client_show(app, client);
    xcb_set_input_focus(app->connection, XCB_INPUT_FOCUS_PARENT, client->window, XCB_CURRENT_TIME);
    
    client_layout(app, client);
    client_paint(app, client);
    
    client_register_animation(app, client);
    app_timeout_create(app, client, 5000, [](App *app, AppClient *client, Timeout *t, void *) {
                               t->kill = true;
                               client_unregister_animation(app, client);
                           }, nullptr, "");
    if (!full_path.empty()) {
        player->play_track(full_path);
    }
    
    std::thread art_thread([] {
        char *home = getenv("HOME");
        std::string lfp_album_art(home);
        lfp_album_art += "/.cache";
        lfp_album_art += "/lfp_album_art";
        
        for (auto q: album_songs) {
            std::string art = lfp_album_art + "/" + sanitize_file_name(q.first);
            if (!std::filesystem::exists(art + ".jpg")) {
                if (!q.second.songs.empty()) {
                    extract_album_art(q.second.songs[0].full, art);
                }
            }
            if (std::filesystem::exists(art + ".jpg")) {
                std::string small = art + "_small.jpg";
                std::string large = art + "_large.jpg";
                auto path = art + ".jpg";
                int width, height, channels;
                unsigned char *data = stbi_load(path.c_str(), &width, &height, &channels, 4); // force RGBA
                if (!data)
                    return;
                defer(free(data));
                int target_width = album_target_width;
                int target_height = target_width;
                if (!std::filesystem::exists(small)) {
                    auto *resized_image = stbir_resize_uint8_linear(data, width, height, 0, NULL, target_width,
                                                                    target_height, 0, STBIR_RGBA);
                    // Save resized image (as PNG for example)
                    if (!stbi_write_png(small.c_str(), target_width, target_height, 4, resized_image,
                                        0)) {
                        printf("Failed to write image.\n");
                    }
                }
                
                if (!std::filesystem::exists(large)) {
                    auto *large_image = stbir_resize_uint8_linear(data, width, height, 0, NULL, target_width * 2,
                                                                  target_height * 2, 0, STBIR_RGBA);
                    // Save resized image (as PNG for example)
                    if (!stbi_write_png(large.c_str(), target_width * 2, target_height * 2, 4, large_image,
                                        0)) {
                        printf("Failed to write image.\n");
                    }
                }
            }
            
        }
        
        cache_art();
    });
    art_thread.detach();
    
    // Start our listening loop until the end of the program
    app_main(app);
    
    if (player->data) {
        player->data->finished = false;
    }
    
    cleanup_cached_fonts();
    
    // Clean up
    app_clean(app);
    
   
    if (restart) {
        restart = false;
        main(argc, argv);
    }
    return 0;
}


void load_in_fonts() {
    char *home = getenv("HOME");
    std::string font_directory(home);
    font_directory += "/.config/winbar/fonts";
    
    FcInit();
    FcConfig *now = FcConfigGetCurrent();
    const FcChar8 *file = (const FcChar8 *) font_directory.c_str();
    FcBool fontAddStatus = FcConfigAppFontAddDir(now, file);
    FcConfigBuildFonts(now);
}

