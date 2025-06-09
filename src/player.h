/* date = April 26th 2025 4:58 am */

#ifndef PLAYER_H
#define PLAYER_H

#include <string>
#include <vector>
#include <atomic>
#include <mutex>

#include "miniaudio.hh"

struct AudioData {
    ma_decoder decoder;
    ma_uint64 currentFrame;
    std::atomic<bool> paused = false;
    std::atomic<bool> finished = false;
    ma_uint64 start = 0;
    ma_uint64 end = 0;
    std::mutex mutex;
    ma_device *device;
    bool preloaded_next_track = false;
    
    bool reached_end_of_song = false;  
};

enum QueueType {
    SONG,
    ALBUM,
    PLAYLIST,
    ARTIST,
    INVALID
};

struct QueueItem {
    QueueType type = QueueType::INVALID;
    
    std::string path; // Will be set for SONG types
    int active = 0;
    std::vector<QueueItem> items;
};

struct Player {
    // TODO: has to be atomic possibly
    AudioData *data = nullptr;
    float volume = 1.0;
    float volume_unthrottled = 1.0;
    bool start_paused = false;
    bool finished = false;
    
    std::string title;
    std::string artist;
    std::string album;
    
    std::string length_in_seconds;
    std::string cover;
    std::string path; // currently being played
        
    //std::vector<std::string> next_tracks;
    //std::vector<std::string> queued_tracks;
    
    std::vector<QueueItem> next_items;
    std::vector<QueueItem> queued_items;
    
    void start_audio_listening_thread();
    
    Player() {
        start_audio_listening_thread();
    }
    
    void toggle();
    
    void playback_start();
    
    void playback_stop();
    
    void forward_10();
    
    void back_10();    
    
    void pop_queue();
    
    void set_position(float scalar);
    
    bool animating = false;
    //cairo_surface_t *surface = nullptr;
    
    void play_track(std::string filePath);    
    
    void set_volume(float new_volume);
    
    void play_next(std::string track_path);
    
    void play_after_all_next(std::string track_path);
    
    void play_last(std::string track_path);
    
    void album_play_next(std::string track_path, int from_index = 0);
    
    void album_play_after_all_next(std::string track_path, int from_index = 0);
    
    void album_play_last(std::string track_path, int from_index = 0);
    
    void clear_queue();
    
    void wake();
};

extern Player *player;


#endif //PLAYER_H
