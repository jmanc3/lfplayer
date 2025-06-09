/* date = May 19th 2020 7:13 pm */

#ifndef MAIN_H
#define MAIN_H

#include "application.h"
#include "utility.h"

extern App *app;

extern bool restart;

struct Option {
    std::string full;
    std::string name;
    std::string artist;
    std::string album;
    std::string album_all_lower;
    std::string genre;
    std::string year;
    std::string length;
    std::string track;
    std::string disc;
    int track_num = 1000;
    int disc_num = 1000;
};

struct AlbumOption {
    std::vector<Option> songs;
};

extern std::unordered_map<std::string, AlbumOption> album_songs;

struct ListOption : UserData {
    Label *label = nullptr;
    std::string title;
    std::string title_all_lower;
    std::string artist;
    std::string album;
    std::string genre;
    std::string year;
    std::string length;
    std::string track;

    long last_time_clicked = 0;
    bool selected = false;
};

struct CachedArt {
    std::string name;
    int width;
    int height;
    unsigned char *data = nullptr;
    unsigned char *large_data = nullptr;
};

extern std::vector<CachedArt *> cached_art;

extern int top_size;
extern int top_underbar_size;
extern int active_tab;
extern int album_height;
extern int album_width;
extern int album_target_width;
extern int song_width_in_album;
extern int song_height_in_album;
extern bool converting; // a track to a playable format using ffmpeg
extern long converting_start;

struct AlbumData : UserData {
    std::string text;
    cairo_surface_t *surface = nullptr;
    double hover_scalar = 0;
    cairo_surface_t *large_album_art = nullptr;
    cairo_surface_t *play_surface = nullptr;
    bool attempted = false;
    Option option;
    bool selected = false;
    long time_when_selected = 0;
    long time_when_image_loaded = 0;
    double open_hover = 0.0;
    bool calculated_avg_color = false;
    ArgbColor avg_color = ArgbColor(1, 1, 1, 1);
    ArgbColor accent_color = ArgbColor(0, 0, 0, 1);
    ArgbColor second_color = ArgbColor(0, 0, 0, 1);
    std::map<ArgbColor, float> map;
    long last_time_left_clicked = 0;
};        

struct SurfaceButton : UserData {
    cairo_surface_t *surface = nullptr;
    std::string name;
    bool attempted = false;
};

void right_click_song(AppClient *client, std::string full_path, std::string title);

void activate_coverlet(AppClient *client, std::string album_name);

struct SortOption {
    std::string name;
    
    int offset = 0;
    float size = 0; // This is read-only for consumers
    
    float perc_size = 0; 
};

struct TableData : UserData {
    std::vector<SortOption> cols;
    
    std::string target;
    bool dragging_col = false;
    int col_drag_offset = 0;
    int leading_x = 0;
    
    bool dragging_edge = false;
    float initial_offset = 0;
    float initial_total = 0;
    float initial_x = 0;
    float initial_w = 0;
};

void cache_art();



#endif// MAIN_H
