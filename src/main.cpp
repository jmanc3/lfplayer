

#include "main.h"
#include "application.h"
#include "config.h"
#include "dpi.h"
#include "drawer.h"
#include "miniaudio.cc"
#include "components.h"
#include <thread>
#include <filesystem>
#include <atomic>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <taglib/fileref.h>
#include <taglib/tag.h>

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

typedef struct {
    ma_decoder decoder;
    ma_uint64 currentFrame;
    std::atomic<bool> paused = false;
    std::atomic<bool> finished = false;
    ma_uint64 start = 0;
    ma_uint64 end = 0;
    std::mutex mutex;
    ma_device *device;
    
    bool reached_end_of_song = false;  
} AudioData;

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    auto userData = (AudioData *) pDevice->pUserData;
    if (!userData) return;
    std::lock_guard<std::mutex> guard(userData->mutex);

    bool paused = userData->paused;

    if (paused) {
        memset(pOutput, 0, frameCount * ma_get_bytes_per_frame(pDevice->playback.format, pDevice->playback.channels));
        return;
    }

    ma_decoder_read_pcm_frames(&userData->decoder, pOutput, frameCount, NULL);
    userData->currentFrame += frameCount;
    
    // TODO: might need to be changed (due to 'cue' files, and gapless playback)
    if (userData->currentFrame >= userData->end) {
        userData->finished = true;
        userData->reached_end_of_song = true;
    }
}

struct Player {
    // TODO: has to be atomic possibly
    AudioData *data = nullptr;
    bool read_only_paused = false;
    float volume = 1.0;
    float volume_unthrottled = 1.0;
    
    void toggle() { 
        if (!data)
            return;   
        std::lock_guard<std::mutex> guard(data->mutex);
        
        data->paused = !data->paused;
    }
    
    void playback_start() {
        if (!data)
            return;   
        std::lock_guard<std::mutex> guard(data->mutex);
     
        data->paused = false;
    }
    
    void playback_stop() {
        if (!data)
            return;   
        std::lock_guard<std::mutex> guard(data->mutex);
     
        data->paused = true;
    }
    
    // TODO: needs mutex
    void forward_10() {
        if (!data)
            return;   
        std::lock_guard<std::mutex> guard(data->mutex);
        //ma_device_set_master_volume(data->device, 1);
        
        ma_uint64 current = data->currentFrame;
        ma_uint64 framesToSeek = data->decoder.outputSampleRate * 10;
        ma_uint64 newFrame = current + framesToSeek;
        ma_decoder_seek_to_pcm_frame(&data->decoder, newFrame);
        data->currentFrame = newFrame;
    }
    
    // TODO: needs mutex
    void back_10() {
        if (!data)
            return;   
         std::lock_guard<std::mutex> guard(data->mutex);
        //ma_device_set_master_volume(data->device, .5);
        
        ma_uint64 current = data->currentFrame;
        ma_uint64 framesToSeek = data->decoder.outputSampleRate * 10;
        ma_uint64 newFrame = (current > framesToSeek) ? current - framesToSeek : 0;
        ma_decoder_seek_to_pcm_frame(&data->decoder, newFrame);
        data->currentFrame = newFrame;
    }
    
    void set_position(float scalar) {
        if (!data)
            return;   
        if (scalar > 1)
            scalar = 1;
        if (scalar < 0)
            scalar = 0;
        
        std::lock_guard<std::mutex> guard(data->mutex);
        //ma_device_set_master_volume(data->device, .5);
        
        ma_uint64 newFrame = data->end * scalar;
        ma_decoder_seek_to_pcm_frame(&data->decoder, newFrame);
        data->currentFrame = newFrame;
    }
    
    bool animating = false;
    
    void play_track(std::string filePath) {
        int attempts = 0;
        int max_attempts = 10;
        if (data != nullptr)
            data->finished = true;
        while (data != nullptr) {
            if (attempts > max_attempts)
                return;
            attempts++;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!animating) {
            client_register_animation(app, client_by_name(app, "lfplayer"));
        }

        std::thread t([this, filePath]() {
                              AudioData userData = {0};
                              data = &userData;
                              read_only_paused = false;
                              
                              if (ma_decoder_init_file(filePath.c_str(), NULL, &userData.decoder) != MA_SUCCESS) {
                                  printf("Failed to initialize decoder.\n");
                                  return;
                              }
                              
                              // TODO: for cue files, this needs to be something else than 0
                              userData.start = 0;
                              if (ma_decoder_get_length_in_pcm_frames(&userData.decoder, &userData.end) != MA_SUCCESS) {
                                  printf("Failed to get total frame count.\n");
                                  ma_decoder_uninit(&userData.decoder);
                                  return;
                              }
                              
                              ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
                              deviceConfig.playback.format   = userData.decoder.outputFormat;
                              deviceConfig.playback.channels = userData.decoder.outputChannels;
                              deviceConfig.sampleRate        = userData.decoder.outputSampleRate;
                              deviceConfig.dataCallback      = data_callback;
                              deviceConfig.pUserData         = &userData;
                              
                              ma_device device;
                              if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
                                  printf("Failed to initialize playback device.\n");
                                  ma_decoder_uninit(&userData.decoder);
                                  return;
                              }
                              
                              userData.device = &device;
                              set_volume(volume);
                              if (ma_device_start(&device) != MA_SUCCESS) {
                                  printf("Failed to start playback.\n");
                                  ma_device_uninit(&device);
                                  ma_decoder_uninit(&userData.decoder);
                                  return;
                              }
                              
                              while (!userData.finished) {
                                  ma_sleep(100);  // sleep for 100ms
                              }
                              data = nullptr;
                              
                              ma_device_uninit(&device);
                              ma_decoder_uninit(&userData.decoder);
                              client_unregister_animation(app, client_by_name(app, "lfplayer"));
                              if (userData.reached_end_of_song) {
                                  
                              }
        });
        t.detach();                            
    }
    
    void set_volume(float new_volume) {
        if (new_volume < 0)
            new_volume = 0;
        if (new_volume > 1)
            new_volume = 1;
        volume = new_volume;
        if (!data)
            return;   
        std::lock_guard<std::mutex> guard(data->mutex);
        ma_device_set_master_volume(data->device, volume);
    }
    
};

static Player *player = new Player;

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

struct ListOption : UserData {
    Label *label = nullptr;
    std::string title;
    std::string title_all_lower;
    long last_time_clicked = 0;
    bool selected = false;
};


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

static void set_label_vis(AppClient *client, Container *c, std::string text) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
    auto label = new Label(text);
    
    label->text = text;
    label->size = 26 * config->dpi;
    c->user_data = label;
    c->when_paint = [](AppClient *client, cairo_t *, Container *c) {
        auto data = (Label *) c->user_data;
        auto b = c->real_bounds;
        b.shrink(5 * config->dpi);
        
        cairo_save(client->cr);
        
        cairo_pattern_t* pattern = cairo_pattern_create_linear(0, 0, 0, c->real_bounds.h);
        
        clip_rounded_rect(client->cr, b.x, b.y, b.w, b.h, 6 * config->dpi); // Use 6 as radius or any value
    
        // Add red at the top
        cairo_pattern_add_color_stop_rgb(pattern, 0.0, .635, .651, .678); // Red
        cairo_pattern_add_color_stop_rgb(pattern, 0.6, .88, .88, .88); // Red
    
        // Add green at the bottom
        cairo_pattern_add_color_stop_rgb(pattern, 1.0, .937, .953, .961); // Green
    
        // Apply the gradient to a rectangle
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
        
        if (player->data) {
            auto clip_b = b;
            // TODO: account for starting frame not being 0
            clip_b.w = ((float) player->data->currentFrame / (float) player->data->end) * b.w;
            draw_clip_begin(client, clip_b);
            draw_round_rect(client, ArgbColor(.2, .7, .4, .4), b, 6 * config->dpi);
            draw_clip_end(client);
        }
        
        
        //draw_text(client, data->size, config->icons, EXPAND(ArgbColor(0, 0, 0, 1)), data->text, c->real_bounds);
    };    
    
    c->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        auto scalar = ((float) client->mouse_current_x - c->real_bounds.x) / c->real_bounds.w;
        player->set_position(scalar);
    };
}

static void paint_textarea_border(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
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
    
    draw_round_rect(client, ArgbColor(.984, .988, .992, 1), container->real_bounds, 5 * config->dpi, 0);
    draw_round_rect(client, color, container->real_bounds, 5 * config->dpi, std::floor(1.0 * config->dpi));
}

std::string toLower(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return lower;
}

static void put_selected_on_screen(AppClient *client) {
    if (auto songs_content = container_by_name("songs_content", client->root)) {
        for (int i = 0; i < songs_content->children.size(); i++) {
            auto child = songs_content->children[i];
            if (child->exists) {
                auto data = (ListOption *) child->user_data;
                if (data->selected) {
                    // Check if it's off screen
                    auto smaller = songs_content->parent->real_bounds;
                    smaller.shrink(90 * config->dpi);
                    
                    auto barely_offscreen = !overlaps(child->real_bounds, smaller) && overlaps(child->real_bounds, songs_content->parent->real_bounds);
                    
                    if (barely_offscreen) {
                        if (songs_content->parent->when_fine_scrolled) {
                            if (child->real_bounds.y < smaller.y + smaller.y / 2) {
                                songs_content->parent->when_fine_scrolled(client, client->cr, songs_content->parent,
                                                                  0, 90 * config->dpi, false);
                            } else {
                                songs_content->parent->when_fine_scrolled(client, client->cr, songs_content->parent,
                                                                  0, -90 * config->dpi, false);
                            }
                        }
                    } else if (!overlaps(child->real_bounds, smaller)) {
                        // TODO: actually implement
                        int offset = -get_offset(child, (ScrollContainer *) songs_content->parent);
                        songs_content->parent->scroll_v_real = offset;
                        songs_content->parent->scroll_v_visual = offset;
                        client_layout(app, client);
                        return;
                    }
                }
            }
        }
    }
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
    draw_round_rect(client, ArgbColor(.2, .2, .2, .6), bg_bounds, bg_bounds.h / 2);
    
    auto circ_bounds = raw_bg_bounds;
    int nub_size = 22 * config->dpi; 
    circ_bounds.x += circ_bounds.w * player->volume_unthrottled;
    circ_bounds.w = nub_size;
    circ_bounds.x -= nub_size / 2;
    circ_bounds.y += circ_bounds.h / 2 - nub_size / 2;
    circ_bounds.h = nub_size;
    draw_round_rect(client, ArgbColor(.8, .8, .8, 1), circ_bounds, circ_bounds.h / 2);
    
    draw_round_rect(client, ArgbColor(.5, .5, .5, 1), circ_bounds, circ_bounds.h / 2, std::floor(1.0 * config->dpi));
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

static void fill_root(AppClient *client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
    auto root = client->root;
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
        if (direction == XKB_KEY_DOWN && (keysym == XK_n || keysym == XK_Down)) {
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
                                    limited_play_track(client, data->label->text);
                                    break;
                                }
                            }
                            if (!set) { // wrap around end to start
                                for (int i = 0; i < songs_content->children.size(); i++) {
                                    auto child = songs_content->children[i];
                                    if (child->exists) {
                                        auto data = (ListOption *) child->user_data;
                                        data->selected = true;
                                        limited_play_track(client, data->label->text);
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
        
        if (auto filter_textarea = container_by_name("filter_textarea", client->root)) {
            if (filter_textarea->parent->active) {
                return;
            }
        }
 
        if (direction == XKB_KEY_DOWN && (keysym == XK_p || keysym == XK_Up)) {
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
                                    limited_play_track(client, data->label->text);
                                    break;
                                }
                            }
                            if (!set) { // wrap around end to start
                                for (int i = songs_content->children.size() - 1; i >= 0; i--) {
                                    auto child = songs_content->children[i];
                                    if (child->exists) {
                                        auto data = (ListOption *) child->user_data;
                                        data->selected = true;
                                        limited_play_track(client, data->label->text);
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
        if (direction == XKB_KEY_DOWN && (keysym == XK_space)) {
            player->toggle();
        }
     };
    
    auto top = root->child(::hbox, FILL_SPACE, top_size * config->dpi);
    top->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        // Create a linear gradient from top (0) to bottom (height)
        cairo_pattern_t* pattern = cairo_pattern_create_linear(0, 0, 0, c->real_bounds.h);
    
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
        draw_colored_rect(client, ArgbColor(.482, .475, .478, 1.0), bounds);
        bounds.y -= thickness;
        draw_colored_rect(client, ArgbColor(.824, .82, .82, 1.0), bounds);
        bounds.y -= thickness;
        draw_colored_rect(client, ArgbColor(.765, .765, .765, 1.0), bounds);
    };    
    
    auto left_section = top->child(::hbox, FILL_SPACE, FILL_SPACE);
    left_section->child(38 * config->dpi, FILL_SPACE);
    
    auto left = left_section->child(55 * config->dpi, FILL_SPACE);
    //set_label(client, left, "\uF8AD");
    set_label(client, left, "\uEB9E");
    left->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        player->back_10();
    };
     
    auto play = left_section->child(55 * config->dpi, FILL_SPACE);
    set_label(client, play, "\uEDB5");
    play->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        player->toggle();
    };
    
    auto right = left_section->child(55 * config->dpi, FILL_SPACE);
    //set_label(client, right, "\uF8AC");
    set_label(client, right, "\uEB9D");
    right->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        player->forward_10();
    };
    
    left_section->child(16 * config->dpi, FILL_SPACE);
    auto volume = left_section->child(120 * config->dpi, FILL_SPACE);
    left_section->child(16 * config->dpi, FILL_SPACE);
   
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

    auto toggle = top->child(FILL_SPACE, FILL_SPACE);
    toggle->pre_layout = [](AppClient *client, Container *c, const Bounds &bounds) {
        c->wanted_bounds.w = bounds.w * .4;
    };
    set_label_vis(client, toggle, "\uF8AE");

        
    auto sa = top->child(FILL_SPACE, FILL_SPACE);
    
    auto search_area = sa->child(FILL_SPACE, FILL_SPACE);
    search_area->alignment = ALIGN_CENTER;
    search_area->wanted_pad = Bounds(90 * config->dpi, 0, 65 * config->dpi, 0);
    
    auto vol = search_area->child(FILL_SPACE, 28 * config->dpi);
    
    TextAreaSettings textarea_settings = TextAreaSettings(config->dpi);
    textarea_settings.single_line = true;
    textarea_settings.bottom_show_amount = 2;
    textarea_settings.right_show_amount = 2;
    textarea_settings.font_size__ = 13 * config->dpi;
    textarea_settings.color = config->color_pinned_icon_editor_field_default_text;
    textarea_settings.color_cursor = config->color_pinned_icon_editor_cursor;
    textarea_settings.pad = Bounds(4 * config->dpi, 3 * config->dpi, 8 * config->dpi, 0);
    

    auto textarea = make_textarea(app, client, vol, textarea_settings);
    textarea->when_key_event = [](AppClient *client, cairo_t *cr, Container *self, bool is_string,
                                  xkb_keysym_t keysym, char string[64], uint16_t mods,
                                  xkb_key_direction direction) {
        if (!self->active && !self->parent->active)
            return;
        
        if (direction == XKB_KEY_DOWN) {
            if (keysym == XK_Escape) {
                if (auto filter_textarea = container_by_name("filter_textarea", client->root)) {
                    auto data = (TextAreaData *) filter_textarea->user_data;
                    if (data->state->text.empty()) {
                        filter_textarea->parent->active = false;
                        client_layout(app, client);
                        request_refresh(app, client);
                        return;
                    }
                    // TODO: do this in such a fashion that it generates an undo
                    data->state->text = "";
                    data->state->cursor = 0;
                    client_layout(app, client);
                    request_refresh(app, client);
                }
            }
    
            if (keysym == XK_Return) {
                // 
                if (auto songs_content = container_by_name("songs_content", client->root)) {
                    for (auto child: songs_content->children) {
                        if (child->exists) {
                            auto data = (ListOption *) child->user_data;
                            player->play_track(data->label->text);
                            break;
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
    
    
    
    underbar->when_paint = [](AppClient *client, cairo_t *, Container *c) {
        draw_colored_rect(client, underbar_bg, c->real_bounds);
                
        auto thickness = std::floor(1 * config->dpi);
        auto b = c->real_bounds;
        b.y += b.h;
        b.h = thickness;
        b.y -= thickness;
        draw_colored_rect(client, ArgbColor(.812, .812, .812, 1.0), b);
        b.y -= thickness;
        draw_colored_rect(client, ArgbColor(.729, .729, .729, 1.0), b);
        b.y -= thickness;
        draw_colored_rect(client, ArgbColor(.878, .878, .878, 1.0), b);
        b.y -= thickness;
        draw_colored_rect(client, ArgbColor(.949, .949, .949, 1.0), b);
    };
    int total_pad = 24 * config->dpi;
    int size = 10 * config->dpi;
    auto songs = underbar->child(get_text_width(client, size, "Songs") + total_pad, FILL_SPACE);
    setup_tab(client, songs, "Songs", size);
    songs->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        active_tab = 0;
        client_layout(app, client);
    };
    auto albums = underbar->child(get_text_width(client, size, "Albums") + total_pad, FILL_SPACE);
    albums->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        active_tab = 1;
        client_layout(app, client);
    };
    setup_tab(client, albums, "Albums", size);
    auto artists = underbar->child(get_text_width(client, size, "Artists") + total_pad, FILL_SPACE);
    artists->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        active_tab = 2;
         client_layout(app, client);
   };
    setup_tab(client, artists, "Artists", size);
    auto playlists = underbar->child(get_text_width(client, size, "Playlists") + total_pad, FILL_SPACE);
    playlists->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        active_tab = 3;
        client_layout(app, client);
    };
    setup_tab(client, playlists, "Playlists", size);
    
    // Will probably be stack? which will change or something
    auto content = root->child(FILL_SPACE, FILL_SPACE);
    auto songs_root = content->child(FILL_SPACE, FILL_SPACE);
    songs_root->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        //draw_colored_rect(client, ArgbColor(0, 0, 1, 1), c->real_bounds);
    };
     
    ScrollPaneSettings scroll_settings(config->dpi);
    scroll_settings.right_inline_track = true;
    auto songs_scroll_root = make_newscrollpane_as_child(songs_root, scroll_settings);
    struct Filter : UserData {
        std::string previous_filter;
    };
    songs_scroll_root->content->user_data = new Filter;
    songs_scroll_root->content->name = "songs_content";
    songs_scroll_root->content->pre_layout = [](AppClient *client, Container *c, const Bounds &b) {
        auto filter = (Filter *) c->user_data;
        
        if (auto filter_textarea = container_by_name("filter_textarea", client->root)) {
            auto data = (TextAreaData *) filter_textarea->user_data;
            if (data->state->text != filter->previous_filter) {
                filter->previous_filter = data->state->text;
                
                if (filter->previous_filter.empty()) {
                    for (auto child: c->children) {
                        auto data = (ListOption *) child->user_data;
                        data->selected = false;
                        child->exists = true;
                    }    
                } else {
                    std::string needle = toLower(filter->previous_filter);
                    bool first = true;
                    for (auto child: c->children) {
                        auto data = (ListOption *) child->user_data;
                        child->exists = data->title_all_lower.find(needle) != std::string::npos;
                        data->selected =false;
                        if (first && child->exists) { // When you type in a new filter query, it auto selects first
                            first = false;
                            data->selected = true;
                        }
                    }    
                }
            }
        }
        
        
        //draw_colored_rect(client, ArgbColor(0, 0, 1, 1), c->real_bounds);
    };
    
    
    namespace fs = std::filesystem;
    
    struct Option {
        std::string full;
        std::string name;
    };
    std::vector<Option> options;
    
    char *home = getenv("HOME");
    std::string cache_path(home);
    cache_path += "/.cache/lfplayer.cache";
    
    std::string pathToSearch(home);
    pathToSearch += "/Music";
    
    {
        if (fs::exists(cache_path)) {
            {
#ifdef TRACY_ENABLE
                ZoneScopedN("From cache");
#endif
 
                std::ifstream file(cache_path); // Replace with your actual file name
                std::string full;
                std::string name;
                while (std::getline(file, full) && std::getline(file, name)) {
                    options.push_back({full, name});
                }
                file.close();    
            }
        } else {
            {
#ifdef TRACY_ENABLE
                ZoneScopedN("From files");
#endif
 
                try {
                    if (fs::exists(pathToSearch) && fs::is_directory(pathToSearch)) {
                        std::ofstream file(cache_path); // Replace with your actual file name
                        
                        for (const auto& entry : fs::recursive_directory_iterator(pathToSearch)) {
                            if (fs::is_regular_file(entry.path())) {
                                std::string full_path = entry.path().string();
                                
                                if (!endsWith(full_path, ".flac") && !endsWith(full_path, ".wav") 
                                        && !endsWith(full_path, ".mp3")) {
                                    // TODO: should actually do a check against the file header to see file type first 512 bytes
                                    continue;
                                }
                                file << full_path << std::endl;
                                std::string name = entry.path().filename().string();
                                file << name << std::endl;
        
                                options.push_back({full_path, name});
                            }
                        }
                        
                        file.close();    
                        
                    } else {
                        std::cerr << "Provided path is not a directory or doesn't exist." << std::endl;
                    }
                } catch (const fs::filesystem_error& e) {
                    std::cerr << "Filesystem error: " << e.what() << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "General error: " << e.what() << std::endl;
                }
            }
        }
    }
    
    {
#ifdef TRACY_ENABLE
        ZoneScopedN("Create options");
#endif
        int even = 0;
        for (auto o: options) {
            auto list_option = songs_scroll_root->content->child(FILL_SPACE, 30 * config->dpi);
            list_option->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
                auto data = (ListOption *) c->user_data;
                bool was_selected = data->selected;
                for (auto child : c->parent->children) {
                    auto d = (ListOption *) child->user_data;
                    d->selected = false;
                }
                data->selected = true;
                if (client->app->current - data->last_time_clicked < 500) {
                    player->play_track(data->label->text);
                }
                data->last_time_clicked = client->app->current;
            };
            auto label = new Label(o.full);
            label->size = 12 * config->dpi;
            auto list_option_data = new ListOption;
            list_option_data->title = o.name;
            list_option_data->title_all_lower = toLower(o.name);
            list_option_data->label = label;
            list_option->user_data = list_option_data;
            if (even++ % 2 == 0) {
                list_option->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
                    auto data = (ListOption *) c->user_data;
                    if (data->selected) {
                        draw_colored_rect(client, ArgbColor(.545, .655, .788, 1), c->real_bounds);
                    } else {
                        draw_colored_rect(client, ArgbColor(.945, .953, .973, 1), c->real_bounds);
                    }
                    draw_text(client, data->label->size, config->font, EXPAND(ArgbColor(0, 0, 0, 1)), data->title, c->real_bounds, 5, 50 * config->dpi);
                };    
            } else {
                list_option->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
                    auto data = (ListOption *) c->user_data;
                    if (data->selected) {
                        draw_colored_rect(client, ArgbColor(.545, .655, .788, 1), c->real_bounds);
                    } else {
                        draw_colored_rect(client, ArgbColor(.98, .98, .988, 1), c->real_bounds);
                    }
                    draw_text(client, data->label->size, config->font, EXPAND(ArgbColor(0, 0, 0, 1)), data->title, c->real_bounds, 5, 50 * config->dpi);
                };    
            }   
        }
    }
    

    auto albums_root = content->child(FILL_SPACE, FILL_SPACE);
    albums_root->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        draw_colored_rect(client, ArgbColor(0, 1, 1, 1), c->real_bounds);
    };


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
    active_tab = 0;
}

int main(int argc, char* argv[]) {    
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
    
    //config->font = "Ubuntu";
    config->font = "SF Display";
    config->icons = "Ubuntu";
    
    AppClient *client;
    {
#ifdef TRACY_ENABLE
        ZoneScopedN("create client");
#endif
        client = client_new(app, settings, "lfplayer");
    }
    
    std::string title = "Local First Music Player";
    xcb_ewmh_set_wm_name(&app->ewmh, client->window, title.length(), title.c_str());
    
    std::string icon = "iTunes";
    xcb_ewmh_set_wm_icon_name(&app->ewmh, client->window, icon.length(), icon.c_str());
    
    fill_root(client);

    client_show(app, client);
    xcb_set_input_focus(app->connection, XCB_INPUT_FOCUS_PARENT, client->window, XCB_CURRENT_TIME);
    
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


#include <fontconfig/fontconfig.h>
#include <filesystem>

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

