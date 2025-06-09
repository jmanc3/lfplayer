
#include "player.h"
#include "main.h"
#include "easing.h"
#include "config.h"
#include <thread>
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/mpegfile.h>
#include <taglib/flacfile.h>
#include <taglib/rifffile.h>
#include <taglib/wavfile.h>
#include "miniaudio.cc"
#include <filesystem>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

template <typename T>
class MessageQueue {
private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;

public:
    void push(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(value);
        cv_.notify_one();
    }

    T pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty(); });
        T value = queue_.front();
        queue_.pop();
        return value;
    }
    
    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = queue_.front();
        queue_.pop();
        return value;
    }
};

enum AudioMessage {
    PLAY,
    STOP,
};

struct AudioThreadMessage {
    AudioMessage type;
    std::string content;
};

MessageQueue<AudioThreadMessage> msg_queue;
Player *player = new Player;

void Player::clear_queue() {
    next_items.clear();
    queued_items.clear();
}

QueueItem wrapped_song(std::string track_path) {
    QueueItem item;
    item.type = QueueType::SONG;
    item.path = track_path;   
    return item;
}

void clear_alike(Player *player, const QueueItem &a);

void Player::play_last(std::string track_path) {
    QueueItem a = wrapped_song(track_path);
    clear_alike(this, a);
    queued_items.push_back(a);
}

void Player::play_after_all_next(std::string track_path) {
    QueueItem a = wrapped_song(track_path);
    clear_alike(this, a);
    next_items.insert(next_items.end(), a);
}

void Player::play_next(std::string track_path) {
    QueueItem a = wrapped_song(track_path);
    clear_alike(this, a);
    next_items.insert(next_items.begin(), a);
}

QueueItem wrapped_album(std::string album_name, int from_index) {
    QueueItem item;
    item.type = QueueType::ALBUM;
    item.path = album_name;
    
    for (auto slot: album_songs) {
        if (slot.first == album_name) {
            for (auto song : slot.second.songs) {
                item.items.push_back(wrapped_song(song.full));
            }
        }
    }
    
    item.items.erase(item.items.begin(), item.items.begin() + from_index);
    
    return item;
}

void clear_alike(Player *player, const QueueItem &a) {
    for (int i = player->queued_items.size() - 1; i >= 0; i--) {
        auto q = player->queued_items[i];
        if (q.path == a.path && q.type == a.type) {
            player->queued_items.erase(player->queued_items.begin() + i);
        }
    }
    for (int i = player->next_items.size() - 1; i >= 0; i--) {
        auto q = player->next_items[i];
        if (q.path == a.path && q.type == a.type) {
            player->next_items.erase(player->next_items.begin() + i);
        }
    }
}

void Player::album_play_last(std::string album_name, int from_index) {
    auto a = wrapped_album(album_name, from_index);
    clear_alike(this, a);
    queued_items.push_back(a);
}

void Player::album_play_after_all_next(std::string album_name, int from_index) {
    auto a = wrapped_album(album_name, from_index);
    clear_alike(this, a);
    next_items.insert(next_items.end(), a);
}

void Player::album_play_next(std::string album_name, int from_index) {
    auto a = wrapped_album(album_name, from_index);
    clear_alike(this, a);
    next_items.insert(next_items.begin(), a);
}
  
void Player::set_volume(float new_volume) {
    if (new_volume < 0)
        new_volume = 0;
    if (new_volume > 1)
        new_volume = 1;
    float actual = getEasingFunction(EaseInCubic)(new_volume);
    if (actual < 0)
        actual = 0;
    if (actual > 1)
        actual = 1;
    volume = new_volume;
    if (!data)
        return;   
    config->volume = volume;
    std::lock_guard<std::mutex> guard(data->mutex);
    ma_device_set_master_volume(data->device, actual);
}

static void create_animation_loop(AppClient *client) {
    for (auto t: app->timeouts) {
        if (t->text == "progress_bar_animation") {
            return;
        }
    }
    app_timeout_create(app, client, 1000, [](App *app, AppClient *client, Timeout *timeout, void *userdata){
                               timeout->keep_running = player->animating;
                               request_refresh(app, client);
                           }, nullptr, "progress_bar_animation");
}

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
    
    float scalar = ((float) userData->currentFrame / userData->end);
    if (!userData->preloaded_next_track && scalar > .9) {
        userData->preloaded_next_track = true;
        // TODO: call a function which starts a thread which preloads the next file and creates a decoder,
        // if the decoder is the same stats as our current we can append it,
        // otherwise we have to switch devices and recreate a device based on the file requirements
        printf("preload\n");
    }
    
    // TODO: might need to be changed (due to 'cue' files, and gapless playback)
    if (userData->currentFrame >= userData->end) {
        userData->finished = true;
        userData->reached_end_of_song = true;
        //if (second_player->data) {
            //second_player->data->paused = false;
        //}
    }
}

void audio_listening_thread() {
    bool skip_first = false;
    AudioThreadMessage msg;
    while (!player->finished) {
        if (player->animating && msg.type != PLAY) {
            player->animating = false;
        }
 
        if (!skip_first) {
            msg = msg_queue.pop();
        }
        skip_first = false;
        if (msg.type == PLAY) {            
            auto filePath = msg.content;
            std::string originalPath = filePath;
            TagLib::FileRef file(filePath.c_str());
            if (!file.isNull() && file.file()) {
                if (dynamic_cast<TagLib::MPEG::File *>(file.file())) {
                    //std::cout << "MP3 file" << std::endl;
                } else if (dynamic_cast<TagLib::FLAC::File *>(file.file())) {
                    //std::cout << "FLAC file" << std::endl;
                } else if (dynamic_cast<TagLib::RIFF::WAV::File *>(file.file())) {
                    //std::cout << "WAV file" << std::endl;
                } else {
                    char *home = getenv("HOME");
                    std::string lfp_converted_songs(home);
                    lfp_converted_songs += "/.cache";
                    mkdir(lfp_converted_songs.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
                    lfp_converted_songs += "/lfp_converted_songs";
                    mkdir(lfp_converted_songs.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
                    
                    std::filesystem::path p = filePath;
                    std::string parentPath = p.parent_path().string();
                    std::string stem = p.stem().string();
                    
                    std::string output_path = lfp_converted_songs + "/" + sanitize_file_name(stem) + ".flac";
                    std::string tmp_path = lfp_converted_songs + "/" + sanitize_file_name(stem) + ".tmp.flac";
                    if (!std::filesystem::exists(output_path)) {
                        std::string command = "ffmpeg -y -i \"" + filePath + "\" -map 0 -c copy -c:a flac \"" + tmp_path + "\"";
                        converting = true;
                        converting_start = app->current;
                        system(command.c_str());
                        converting = false;
                        
                        std::filesystem::copy_file(tmp_path, output_path);
                        std::filesystem::remove(tmp_path);
                    }
                    
                    filePath = output_path;
                    if (!std::filesystem::exists(output_path)) {
                        return;
                    }
                }
            }
            
            int attempts = 0;
            int max_attempts = 10;
            if (player->data != nullptr)
                player->data->finished = true;
            while (player->data != nullptr) {
                if (attempts > max_attempts)
                    return;
                attempts++;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            auto client = client_by_name(app, "lfplayer");
            
            if (!player->animating) {
                player->animating = true;
                create_animation_loop(client);
            }
            
            if (!file.isNull() && file.tag()) {
                TagLib::Tag *tag = file.tag();
                player->title = tag->title().to8Bit(true);  // Convert to std::string
                player->artist = tag->artist().to8Bit(true);  // Convert to std::string
                player->album = tag->album().to8Bit(true);  // Convert to std::string
            }
            player->path = originalPath;
            
            if (player->title.empty()) {
                xcb_ewmh_set_wm_name(&app->ewmh, client->window, player->path.length(), player->path.c_str());
            } else {
                if (player->artist.empty()) {
                    xcb_ewmh_set_wm_name(&app->ewmh, client->window, player->title.length(), player->title.c_str());
                } else {
                    std::string text = player->artist + " - " + player->title;
                    xcb_ewmh_set_wm_name(&app->ewmh, client->window, text.length(), text.c_str());
                }
            }
            bool worked = extract_album_art(originalPath, "/tmp/cover");
            if (worked) {
                player->cover = "/tmp/cover.jpg";
            } else {
                player->cover = "";
            }
            
            // TODO: only do these on main thread
            //client_layout(client->app, client);
            request_refresh(client->app, client);
            
            {
                AudioData userData = {0};
                player->data = &userData;
                if (player->start_paused) {
                    userData.paused = true;
                }
                
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
                
                // TODO: for cue files this can be wrong because it's not taking start offset into account
                int seconds = (double) userData.end / (double) userData.decoder.outputSampleRate;
                player->length_in_seconds = seconds_to_mmss(seconds);
                
                ma_device device;
                if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
                    printf("Failed to initialize playback device.\n");
                    ma_decoder_uninit(&userData.decoder);
                    return;
                }
                
                userData.device = &device;
                player->set_volume(player->volume);
                if (ma_device_start(&device) != MA_SUCCESS) {
                    printf("Failed to start playback.\n");
                    ma_device_uninit(&device);
                    ma_decoder_uninit(&userData.decoder);
                    return;
                }
                
                while (!userData.finished) {
                    std::optional<AudioThreadMessage> tmsg = msg_queue.try_pop();
                    if (tmsg.has_value()) {
                        userData.finished = true;
                        msg = tmsg.value();
                        skip_first = true;
                        break;
                    }
                    if (app && !app->running) {
                        userData.finished = true;
                        player->finished = true;
                        break;
                    }
                    ma_sleep(100);  // sleep for 100ms
                }
                player->data = nullptr;
                if (!skip_first) {
                    player->path = "";
                }
                
                ma_device_uninit(&device);
                ma_decoder_uninit(&userData.decoder);
                
                if (userData.reached_end_of_song) {
                    auto client = client_by_name(app, "lfplayer");
                    std::string title = "Local First Music Player";
                    xcb_ewmh_set_wm_name(&app->ewmh, client->window, title.length(), title.c_str());
                    player->pop_queue();
                }
            }
        }
    }
}

void Player::start_audio_listening_thread() {
    static bool started_already = false;
    if (started_already) return;
    started_already = true;
    
    std::thread t(audio_listening_thread);
    t.detach();
}

void second(std::string filePath) {
    AudioThreadMessage at;
    at.type = PLAY;
    at.content = filePath;
    msg_queue.push(at);
}

void Player::play_track(std::string filePath) {
    second(filePath);
    return;
}

void Player::pop_queue() {
    std::string next_track;
    
    auto try_pop = [&next_track](std::vector<QueueItem> *list) {
        if (!list->empty()) {
            auto q = (*list)[0];
            if (q.type == QueueType::SONG) {
                next_track = q.path;
            } else if (q.type == QueueType::ALBUM) {
                if (!q.items.empty()) {
                    auto list_q = q.items[0];
                    next_track = list_q.path;
                    (*list)[0].items.erase((*list)[0].items.begin());
                }
            }
            if (q.items.empty()) {
                list->erase(list->begin());
            }
        }        
    };
    try_pop(&player->next_items);
    if (!next_track.empty()) {
        this->play_track(next_track);
        return;
    }
    try_pop(&player->queued_items);
    if (!next_track.empty()) {
        this->play_track(next_track);
    }
}

void Player::set_position(float scalar) {
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


void Player::back_10() {
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


void Player::playback_stop() {
    if (!data)
        return;   
    std::lock_guard<std::mutex> guard(data->mutex);
    
    data->paused = true;
}

void Player::forward_10() {
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


void Player::playback_start() {
    if (!data)
        return;   
    std::lock_guard<std::mutex> guard(data->mutex);
    
    data->paused = false;
}



void Player::toggle() { 
    if (!data)
        return;   
    std::lock_guard<std::mutex> guard(data->mutex);
    
    data->paused = !data->paused;
}


void Player::wake() {
    auto msg = AudioThreadMessage();
    msg.type = STOP;
    msg_queue.push(msg);
}