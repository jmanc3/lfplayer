
#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif


#include "songs_tab.h"
#include "main.h"
#include "config.h"
#include "drawer.h"
#include "components.h"
#define FTS_FUZZY_MATCH_IMPLEMENTATION
#include "fts_fuzzy_match.h"
#include "ThreadPool.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include "player.h"
#include <sys/stat.h>
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/flacfile.h>
#include <taglib/xiphcomment.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>

#include "stb_image.h"
#include "stb_image_resize2.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static void paint_list_option_text(AppClient *client, cairo_t *cr, Container *c, bool only_drag = false) {
    auto data = (ListOption *) c->user_data;
    auto header = container_by_name("table_headers", client->root);
    auto table_data = (TableData *) header->user_data;
    
    std::string text;
    for (auto &col: table_data->cols) {
        if (col.name == "Name")
            text = data->title;
         if (col.name == "Time")
            text = data->length;
        if (col.name == "Artist")
            text = data->artist;
        if (col.name == "Album")
            text = data->album;
        if (col.name == "Genre")
            text = data->genre;
        if (col.name == "Year") {
            text = data->year;
            if (text == "0") 
                continue;
        }
        auto b = c->real_bounds;
        b.x = col.offset + 50 * config->dpi;
        b.w = col.size - 8 * config->dpi;
        
        if (col.name == table_data->target && table_data->dragging_col) {
            auto leading_x = client->mouse_current_x - 50 * config->dpi - table_data->col_drag_offset;
            b.x = leading_x + 50 * config->dpi;
            draw_clip_begin(client, b);
            draw_text(client, 10 * config->dpi, config->font, EXPAND(ArgbColor(0, 0, 0, 1)), text, c->real_bounds, 5, leading_x - c->real_bounds.x + 50 * config->dpi);
            draw_clip_end(client);
        } else {
            if (!only_drag) {
                draw_clip_begin(client, b);
                draw_text(client, 10 * config->dpi, config->font, EXPAND(ArgbColor(0, 0, 0, 1)), text, c->real_bounds, 5, col.offset + 50 * config->dpi);
                draw_clip_end(client);
            }
        }
        text = "";
    }
}

static void write_to(std::ofstream &file, std::string header, std::string body) {
    file << header;
    file << body;
    file << std::endl;
}

static void load_from_cache(std::string cache_path, std::vector<Option> &options) {
#ifdef TRACY_ENABLE
    ZoneScopedN("From cache");
#endif
    // TODO: if version #1 not found, create from cache first, and then load it here
    // TODO: if version #1 not found, create from cache first, and then load it here
    // TODO: if version #1 not found, create from cache first, and then load it here
    // TODO: if version #1 not found, create from cache first, and then load it here
    
    std::ifstream file(cache_path); // Replace with your actual file name
    std::string line;
    std::string path;
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    std::string year;
    std::string length;
    std::string track;
    std::string disc;
    bool last = false;
    while (std::getline(file, line)) {
        if (line.find("Path:") != std::string::npos) {
            line.erase(0, 6);
            path = line;
        } else if (line.find("Title:") != std::string::npos) {
            line.erase(0, 7);
            title = line;
        } else if (line.find("Artist:") != std::string::npos) {
            line.erase(0, 8);
            artist = line;
        } else if (line.find("Album:") != std::string::npos) {
            line.erase(0, 7);
            album = line;
        } else if (line.find("Genre:") != std::string::npos) {
            line.erase(0, 7);
            genre = line;
        } else if (line.find("Year:") != std::string::npos) {
            line.erase(0, 6);
            year = line;
        } else if (line.find("Length:") != std::string::npos) {
            line.erase(0, 8);
            length = line;
        } else if (line.find("Track:") != std::string::npos) {
            line.erase(0, 7);
            track = line;
        }else if (line.find("Disc:") != std::string::npos) {
            line.erase(0, 6);
            disc = line;
            last = true;
        }
        
        if (last) {
            last = false;
            options.push_back({path, title, artist, album, toLower(album), genre, year, length, track, disc});
            path = title = artist = album = genre = year = length = track = "";            
        }
    }
    file.close();    
}

void put_selected_on_screen(AppClient *client) {
    if (auto songs_content = container_by_name("songs_content", client->root)) {
        for (int i = 0; i < songs_content->children.size(); i++) {
            auto child = songs_content->children[i];
            if (child->exists) {
                auto data = (ListOption *) child->user_data;
                if (data->selected) {
                    // Check if it's off screen
                    auto smaller = songs_content->parent->real_bounds;
                    smaller.shrink(child->real_bounds.h * 5 * config->dpi);
                    
                    auto barely_offscreen = !overlaps(child->real_bounds, smaller) && overlaps(child->real_bounds, songs_content->parent->real_bounds);
                    
                    if (barely_offscreen) {
                        if (songs_content->parent->when_fine_scrolled) {
                            if (child->real_bounds.y < smaller.y + smaller.y / 2) {
                                songs_content->parent->when_fine_scrolled(client, client->cr, songs_content->parent,
                                                                  0, child->real_bounds.h, false);
                            } else {
                                songs_content->parent->when_fine_scrolled(client, client->cr, songs_content->parent,
                                                                              0, -child->real_bounds.h, false);
                            }
                        }
                    } else if (!overlaps(child->real_bounds, smaller)) {
                        // TODO: stop the fine_scroll
                        
                        int offset = -get_offset(child, (ScrollContainer *) songs_content->parent);
                        offset += smaller.h / 2;
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

static void layout_table_headers(AppClient *client, Container *c) {
    auto data = (TableData *) c->user_data;
    
    int offset = 0;
    for (auto &col: data->cols) {
        col.offset = offset;
        col.size = (c->real_bounds.w - 50 * config->dpi) * col.perc_size;
        offset += col.size;
    }
}

static void paint_table_header(AppClient *client, cairo_t *cr, Container *c) {
    cairo_pattern_t* pattern = cairo_pattern_create_linear(0, 0, 0, c->real_bounds.h);
    defer(cairo_pattern_destroy(pattern));
    cairo_pattern_add_color_stop_rgb(pattern, 0.0, .867, .867, .867);
    cairo_pattern_add_color_stop_rgb(pattern, 1.0, .937, .937, .937);
    set_rect(cr, c->real_bounds);
    cairo_set_source(cr, pattern);
    cairo_fill(cr);
    
    auto line = c->real_bounds;
    line.h = 1;
    line.y = c->real_bounds.y + c->real_bounds.h - 1;
    draw_colored_rect(client, ArgbColor(.847, .847, .847, 1), line);
    
    layout_table_headers(client, c);
    
    auto data = (TableData *) c->user_data;
    for (auto &col: data->cols) {
        auto [f, w, h] = draw_text_begin(client, 10 * config->dpi, config->font, EXPAND(ArgbColor(.44, .44, .44, 1)), col.name, true);
        auto x = c->real_bounds.x + 50 * config->dpi + col.offset;
        f->draw_text_end(x, c->real_bounds.y + c->real_bounds.h / 2 - h / 2);
        draw_colored_rect(client, ArgbColor(.812, .812, .812, 1), Bounds(x - 8 * config->dpi, c->real_bounds.y, std::floor(1 * config->dpi), c->real_bounds.h));
    }
    
    // Paint over target, and re-draw it over
    if (data->dragging_col) {
        for (auto &col: data->cols) {
            if (col.name == data->target) {
                cairo_pattern_t* pattern = cairo_pattern_create_linear(0, 0, 0, c->real_bounds.h);
                defer(cairo_pattern_destroy(pattern));
                cairo_pattern_add_color_stop_rgb(pattern, 0.0, .867, .867, .867);
                cairo_pattern_add_color_stop_rgb(pattern, 1.0, .937, .937, .937);
                set_rect(cr, Bounds(col.offset + 50 * config->dpi - 8 * config->dpi, c->real_bounds.y, col.size, c->real_bounds.h - 2 * config->dpi));
                cairo_set_source(cr, pattern);
                cairo_fill(cr);
                
                set_rect(cr, Bounds(client->mouse_current_x - data->col_drag_offset - 8 * config->dpi, c->real_bounds.y, col.size, c->real_bounds.h - 2 * config->dpi));
                cairo_set_source(cr, pattern);
                cairo_fill(cr);
                
                auto [f, w, h] = draw_text_begin(client, 10 * config->dpi, config->font, EXPAND(ArgbColor(.44, .44, .44, 1)), col.name, true);
                auto x = client->mouse_current_x - data->col_drag_offset;
                f->draw_text_end(x, c->real_bounds.y + c->real_bounds.h / 2 - h / 2);
                draw_colored_rect(client, ArgbColor(.812, .812, .812, 1), Bounds(x - 8 * config->dpi, c->real_bounds.y, std::floor(1 * config->dpi), c->real_bounds.h));
                draw_colored_rect(client, ArgbColor(.812, .812, .812, 1), Bounds(x + col.size - 8 * config->dpi, c->real_bounds.y, std::floor(1 * config->dpi), c->real_bounds.h));

            }
        }
    }
}

static void create_table_headers(AppClient *client, Container *c) {
    auto data = new TableData;
    c->user_data = data;
    c->name = "table_headers";
    
    data->cols.push_back({"Name", 0, 0, 0.3});
    data->cols.push_back({"Time", 0, 0, 0.1});
    data->cols.push_back({"Artist", 0, 0, 0.2});
    data->cols.push_back({"Album", 0, 0, 0.2});
    data->cols.push_back({"Genre", 0, 0, 0.1});
    data->cols.push_back({"Year", 0, 0, 0.1});
    
    c->when_drag_start = [](AppClient *client, cairo_t *cr, Container *c) {
        auto data = (TableData *) c->user_data;
        for (auto &col: data->cols) {
            auto dist = std::abs((col.offset + 50 * config->dpi - 10 * config->dpi) - client->mouse_initial_x);
            if (dist < 10) {
                data->dragging_edge = true;
                data->target = col.name;
                data->initial_offset = col.offset;
                data->initial_total = -1;
                return;
            }
        }
        for (auto &col: data->cols) {
            Bounds b = Bounds(col.offset + 50 * config->dpi, c->real_bounds.y, col.size, c->real_bounds.h);
            if (bounds_contains(b, client->mouse_initial_x, client->mouse_initial_y) && !data->dragging_col) {
                data->dragging_col = true;
                data->target = col.name;
                data->col_drag_offset = (client->mouse_initial_x - 50 * config->dpi) - col.offset;
                break;
            }
        }
    };
    c->when_drag = [](AppClient *client, cairo_t *cr, Container *c) {
        auto data = (TableData *) c->user_data;
        if (data->dragging_edge) {
            // '1' to prevent dragging leading edge
            for (int i = 1; i < data->cols.size(); i++) {
                auto col = &data->cols[i];
                if (col->name == data->target) {
                    auto prev_col = &data->cols[i - 1];
                    if (data->initial_total == -1) {
                        data->initial_total = prev_col->perc_size + col->perc_size;
                        data->initial_x = prev_col->offset + 50 * config->dpi - 8 * config->dpi;
                        data->initial_w = prev_col->size + col->size;
                    }
                    
                    auto scalar = (client->mouse_current_x - data->initial_x) / data->initial_w;
                    if (scalar < .1)
                        scalar = .1;
                    if (scalar > .9)
                        scalar = .9;
                    auto right = 1 - scalar;
                    prev_col->perc_size = scalar * data->initial_total;
                    col->perc_size = right * data->initial_total;
                    
                    break;   
                }
            }
        }
        if (data->dragging_col) {
            int size = data->cols.size();
            
            SortOption target_col;
            for (int i = 0; i < size; i++) {
                if (data->cols[i].name == data->target) {
                    target_col = data->cols[i];
                    data->cols.erase(data->cols.begin() + i);
                    break;
                }
            }

            
            // TODO: might be + data->col_drag_offset
            auto leading_x = client->mouse_current_x - 50 * config->dpi - data->col_drag_offset;
            data->leading_x = leading_x;
            int offset = 0;
            bool seen_target = false;
            int min_dist = 10000000;
            int min_index = 0;
            for (int i = 0; i < size; i++) {
                auto col = &data->cols[i];
                if (col->name == data->target) {
                    seen_target = true;
                } else {
                    int distance = std::abs(offset - leading_x);
                    if (distance < min_dist) {
                        min_dist = distance;
                        min_index = i;
                    }
                    
                    offset += col->size;
                    
                    distance = std::abs(offset - leading_x);
                    if (distance < min_dist) {
                        min_dist = distance;
                        min_index = i + 1;
                    }

                }
            }
                         
            data->cols.insert(data->cols.begin() + min_index, target_col);
            /*
            SortOption col;
            for (int i = 0; i < size; i++)
                if (data->cols[i].name == data->target)
                    col = data->cols[i];
            int min_dist = 10000000;
            int min_index = 0;
            for (int i = 0; i < size; i++) {
                int distance = std::abs(dist_of_col_at_position(col, i, data->cols, client->mouse_current_x - 50 * config->dpi));
                if (distance < min_dist) {
                    min_dist = distance;
                    min_index = i;
                }
            }
            if (min_index < 0)
                min_index = 0;
            if (min_index >= size)
                min_index = size - 1;
            
            for (int i = 0; i < data->cols.size(); i++) {
                if (data->cols[i].name == data->target) {
                    data->cols.erase(data->cols.begin() + i);
                    data->cols.insert(data->cols.begin() + min_index, col);
                    break;
                }
            }
            */
        }
    };
    c->when_drag_end = [](AppClient *client, cairo_t *cr, Container *c) {
        auto data = (TableData *) c->user_data;
        data->dragging_edge = false;
        data->dragging_col = false;
        client_layout(app, client);
        client_layout(app, client);
        request_refresh(app, client);
    };
}


// Helper to read 4-byte little-endian unsigned int safely
uint32_t readUInt32LE(const char *data) {
    return (static_cast<uint8_t>(data[0])) |
           (static_cast<uint8_t>(data[1]) << 8) |
           (static_cast<uint8_t>(data[2]) << 16) |
           (static_cast<uint8_t>(data[3]) << 24);
}

// Case-insensitive string comparison (portable)
bool iequals(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

int extractDiscNumberFromFlac(const std::string &filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file.\n";
        return -1;
    }

    // Check FLAC header
    char header[4];
    file.read(header, 4);
    if (file.gcount() != 4 || std::string(header, 4) != "fLaC") {
        std::cerr << "Not a valid FLAC file.\n";
        return -1;
    }

    bool lastBlock = false;
    while (!lastBlock && file) {
        // Read metadata block header
        unsigned char blockHeader[4];
        file.read(reinterpret_cast<char*>(blockHeader), 4);
        if (file.gcount() != 4) {
            std::cerr << "Failed to read metadata block header.\n";
            return -1;
        }

        lastBlock = blockHeader[0] & 0x80;
        uint8_t blockType = blockHeader[0] & 0x7F;
        uint32_t blockSize = (blockHeader[1] << 16) | (blockHeader[2] << 8) | blockHeader[3];

        if (blockType == 4) {  // VORBIS_COMMENT
            std::vector<char> blockData(blockSize);
            file.read(blockData.data(), blockSize);
            if (file.gcount() != static_cast<std::streamsize>(blockSize)) {
                std::cerr << "Failed to read Vorbis comment block.\n";
                return -1;
            }

            const char *ptr = blockData.data();
            const char *end = ptr + blockSize;

            // Read vendor string length (4 bytes LE)
            if (ptr + 4 > end) return -1;
            uint32_t vendorLen = readUInt32LE(ptr);
            ptr += 4;
            if (ptr + vendorLen > end) return -1;
            ptr += vendorLen;  // Skip vendor string

            // Read user comment list length (4 bytes LE)
            if (ptr + 4 > end) return -1;
            uint32_t userCommentListLen = readUInt32LE(ptr);
            ptr += 4;

            for (uint32_t i = 0; i < userCommentListLen; ++i) {
                if (ptr + 4 > end) return -1;
                uint32_t commentLen = readUInt32LE(ptr);
                ptr += 4;
                if (ptr + commentLen > end) return -1;
                std::string comment(ptr, commentLen);
                ptr += commentLen;

                auto eqPos = comment.find('=');
                if (eqPos != std::string::npos) {
                    std::string key = comment.substr(0, eqPos);
                    std::string value = comment.substr(eqPos + 1);
                    if (iequals(key, "DISCNUMBER")) {
                        try {
                            return std::stoi(value);
                        } catch (...) {
                            std::cerr << "Failed to parse DISCNUMBER value.\n";
                            return -1;
                        }
                    }
                }
            }
            // No DISCNUMBER found
            return -1;
        } else {
            // Skip block
            file.seekg(blockSize, std::ios::cur);
        }
    }

    std::cerr << "No Vorbis comment block found.\n";
    return -1;
}


// Reads 4-byte synchsafe int (7 bits per byte)
uint32_t readSynchsafeInt(const unsigned char *data) {
    return ((data[0] & 0x7F) << 21) |
           ((data[1] & 0x7F) << 14) |
           ((data[2] & 0x7F) << 7) |
           (data[3] & 0x7F);
}

// Reads 4-byte big-endian int
uint32_t readUInt32BE(const unsigned char *data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           (static_cast<uint32_t>(data[3]));
}

int extractDiscNumberFromMp3(const std::string &filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file.\n";
        return -1;
    }

    // Check ID3 header
    unsigned char header[10];
    file.read(reinterpret_cast<char*>(header), 10);
    if (file.gcount() != 10 || std::string(reinterpret_cast<char*>(header), 3) != "ID3") {
        std::cerr << "No ID3v2 tag found.\n";
        return -1;
    }

    uint8_t versionMajor = header[3];
    uint8_t versionMinor = header[4];
    uint8_t flags = header[5];
    uint32_t tagSize = readSynchsafeInt(&header[6]);

    // Optional extended header for ID3v2.3+ (skip if present)
    if ((flags & 0x40) != 0) {
        unsigned char extHeader[4];
        file.read(reinterpret_cast<char*>(extHeader), 4);
        if (file.gcount() != 4) return -1;
        uint32_t extSize = readUInt32BE(extHeader);
        file.seekg(extSize - 4, std::ios::cur);
    }

    uint32_t bytesRead = 0;
    while (bytesRead + 10 <= tagSize) {
        unsigned char frameHeader[10];
        file.read(reinterpret_cast<char*>(frameHeader), 10);
        if (file.gcount() != 10) break;

        std::string frameID(reinterpret_cast<char*>(frameHeader), 4);
        uint32_t frameSize;
        if (versionMajor == 4) {
            frameSize = readSynchsafeInt(&frameHeader[4]);
        } else {
            frameSize = readUInt32BE(&frameHeader[4]);
        }

        uint16_t frameFlags = (frameHeader[8] << 8) | frameHeader[9];
        bytesRead += 10;

        if (frameSize == 0) break;  // Padding

        if (bytesRead + frameSize > tagSize) {
            // Corrupt frame
            break;
        }

        if (frameID == "TPOS") {
            std::vector<char> frameData(frameSize);
            file.read(frameData.data(), frameSize);
            if (file.gcount() != static_cast<std::streamsize>(frameSize)) {
                std::cerr << "Failed to read TPOS frame data.\n";
                return -1;
            }

            // First byte: encoding (0 = ISO-8859-1, 1 = UTF-16 etc.)
            std::string text(frameData.begin() + 1, frameData.end());
            try {
                size_t pos = 0;
                int disc = std::stoi(text, &pos);
                return disc;
            } catch (...) {
                std::cerr << "Failed to parse TPOS value.\n";
                return -1;
            }
        } else {
            // Skip this frame
            file.seekg(frameSize, std::ios::cur);
        }
        bytesRead += frameSize;
    }

    std::cerr << "TPOS frame not found.\n";
    return -1;
}

int getDiscNumber(const std::string &filePath) {
    int disc = extractDiscNumberFromFlac(filePath);
    if (disc != -1) {
        return disc;
    }

    disc = extractDiscNumberFromMp3(filePath);
    if (disc != -1) {
        return disc;
    }
    
    TagLib::MP4::File mp4File(filePath.c_str());
    if (mp4File.isValid()) {
        TagLib::MP4::Tag *tag = mp4File.tag();
        if (tag) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            TagLib::MP4::ItemListMap items = tag->itemListMap();
#pragma GCC diagnostic pop
            if (items.contains("disk")) {
                TagLib::MP4::Item item = items["disk"];
                if (item.isValid() && item.toIntPair().first > 0) {
                    return item.toIntPair().first;
                }
            }
        }
    }

    // Check for MP4 (M4A) files
    /*
    TagLib::MP4::File mp4File(filePath.c_str());
    if (mp4File.isValid()) {
        TagLib::MP4::Tag *tag = mp4File.tag();
        if (tag) {
            if (tag->itemListMap().contains("disk")) {
                TagLib::MP4::Item item = tag->itemListMap()["disk"];
                if (!item.isEmpty() && item.toIntPair().first > 0) {
                    return item.toIntPair().first;
                }
            }
        }
    }
*/

    return 0;
}


static void cache_creation_thread(std::string cache_path, std::string path_to_search, std::string lfp_album_art) {
    namespace fs = std::filesystem;

    unsigned int threads = std::thread::hardware_concurrency();
    if (threads == 0)
        threads = 8;
    ThreadPool pool(threads);
    std::vector< std::future<Option> > results;
 
    std::vector<std::string> albums;
    for (const auto& entry : fs::recursive_directory_iterator(path_to_search)) {
        if (fs::is_regular_file(entry.path())) {
            std::string full_path = entry.path().string();

            results.emplace_back(pool.enqueue([full_path, entry] {
                Option o;
                TagLib::FileRef tag_file(full_path.c_str());
                if (tag_file.isNull()) {
                   return o;   
                }
                TagLib::Tag *tag = tag_file.tag();
                if (tag) {
                    o.full = full_path;
                    o.name = tag->title().to8Bit(true);  // Convert to std::string
                    if (o.name.empty()) {
                        o.name = entry.path().filename().string();
                    }
                    o.artist = tag->artist().to8Bit(true);  // Convert to std::string
                    o.album = tag->album().to8Bit(true);  // Convert to std::string
                    o.genre = tag->genre().to8Bit(true);  // Convert to std::string
                    o.disc = std::to_string(getDiscNumber(full_path));
                    o.year = std::to_string((int) tag->year());  // Convert to std::string
                    o.track = std::to_string((int) tag->track());  // Convert to std::string
                }
                
                int length_in_seconds;
                TagLib::AudioProperties *properties = tag_file.audioProperties();
                if (properties) {
                   o.length = std::to_string(properties->length());
                }
                return o;
                                                                           
                //options.push_back(o);
            }));
        }
    }
    
    std::vector<Option> options;
    for (auto && result: results) {
        options.push_back(result.get());
    }
       
    std::ofstream file(cache_path); // Replace with your actual file name
    file << "version: 1" << std::endl;
    for (auto o: options) {
        if (o.full.empty())
            continue;
        write_to(file, "Path: ", o.full);
        //file << "Path: " << full_path.c_str() << std::end;
        write_to(file, "Title: ", o.name);
        write_to(file, "Artist: ", o.artist);
        write_to(file, "Album: ", o.album);
        write_to(file, "Genre: ", o.genre);
        write_to(file, "Year: ", o.year);
        write_to(file, "Length: ", o.length);
        write_to(file, "Track: ", o.track);
        write_to(file, "Disc: ", o.disc);
    }
 
    file.close();     
}

static void create_cache(std::string cache_path, std::string path_to_search) {
    char *home = getenv("HOME");
    std::string lfp_album_art(home);
    lfp_album_art += "/.cache";
    mkdir(lfp_album_art.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    lfp_album_art += "/lfp_album_art";
    mkdir(lfp_album_art.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
 
    std::thread t([&](){
        cache_creation_thread(cache_path, path_to_search, lfp_album_art);
    });
    t.join();
}

void fill_songs_tab(AppClient *client, Container *songs_root, std::vector<Option> &options) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
    songs_root->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        //draw_colored_rect(client, ArgbColor(0, 0, 1, 1), c->real_bounds);
    };
    
    auto table_headers = songs_root->child(FILL_SPACE, 28 * config->dpi);
    table_headers->when_paint = paint_table_header;
    create_table_headers(client, table_headers);
        
     
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
                        //data->selected = false;
                        child->exists = true;
                    }    
                } else {
                    std::string needle = toLower(filter->previous_filter);
                    bool first = true;
                    for (auto child: c->children) {
                        auto data = (ListOption *) child->user_data;
                        child->exists = fts::fuzzy_match_simple(needle.c_str(), data->title.c_str());
                        data->selected = false;
                        if (first && child->exists) { // When you type in a new filter query, it auto selects first
                            first = false;
                            data->selected = true;
                            put_selected_on_screen(client);
                        }
                    }    
                }
            }
        }
        
        
        //draw_colored_rect(client, ArgbColor(0, 0, 1, 1), c->real_bounds);
    };
    
    
    namespace fs = std::filesystem;
    
    char *home = getenv("HOME");
    std::string cache_path(home);
    cache_path += "/.cache/lfplayer.cache";
    
    std::string path_to_search(home);
    path_to_search += "/Music";
    
    {
        if (fs::exists(cache_path)) {
            load_from_cache(cache_path, options);
        } else {
            {
#ifdef TRACY_ENABLE
                ZoneScopedN("From files");
#endif                
                try {
                    if (fs::exists(path_to_search) && fs::is_directory(path_to_search)) {
                        create_cache(cache_path, path_to_search);    
                        
                        load_from_cache(cache_path, options);
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
    
    std::string lfp_album_art(home);
    lfp_album_art += "/.cache";
    mkdir(lfp_album_art.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    lfp_album_art += "/lfp_album_art";
    mkdir(lfp_album_art.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

    std::vector<std::string> albums_seen;
    for (auto &o: options) {
        if (!o.track.empty()) {
            bool inside = false;
            for (auto a : albums_seen) {
                if (a == o.album) {
                    inside = true;
                }
            }
            if (!inside) {
                std::string art = lfp_album_art + "/" + sanitize_file_name(o.album);
                if (!fs::exists(art)) {
                    //extract_album_art(o.full, art);
                    albums_seen.push_back(o.album);
                }
            }
            
            
            try {
                o.track_num = std::atoi(o.track.c_str());
                o.disc_num = std::atoi(o.disc.c_str());
            } catch (...) {
                
            }
        }
    }
   
    std::sort(options.begin(), options.end(), [](Option a, Option b) {
                      if (a.album.empty()) {
                          return false;
                      }
                      if (b.album.empty()) {
                          return true;
                      }
                      if (a.album == b.album) {
                          if (a.disc_num == b.disc_num) {
                              return a.track_num < b.track_num;
                          } else {
                              return a.disc_num < b.disc_num;
                          }
                      } else {
                          return a.album < b.album;
                      }
                  });
    
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
                if (c->state.mouse_button_pressed == 3) {
                    right_click_song(client, data->label->text, data->title);
                    return;
                }
                if (client->app->current - data->last_time_clicked < 500) {
                    player->play_track(data->label->text);
                }
                data->last_time_clicked = client->app->current;
            };
            auto label = new Label(o.full);
            label->size = 10 * config->dpi;
            auto list_option_data = new ListOption;
            list_option_data->title = o.name;
            list_option_data->title_all_lower = toLower(o.name);
            list_option_data->artist = o.artist;
            list_option_data->album = o.album;
            list_option_data->genre = o.genre;
            list_option_data->year = o.year;
            list_option_data->length = o.length;
            if (!o.length.empty()) {
                try {
                    list_option_data->length = seconds_to_mmss(std::atoi(o.length.c_str()));
                } catch (...) {
                    
                }
            }
            
            list_option_data->track = o.track;
            
            list_option_data->label = label;
            list_option->user_data = list_option_data;
            if (even++ % 2 == 0) {
                list_option->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
                    auto data = (ListOption *) c->user_data;
                    if (data->selected) {
                        draw_colored_rect(client, ArgbColor(.545, .655, .788, 1), c->real_bounds);
                    } else {
                        draw_colored_rect(client, ArgbColor(.945, .953, .973, 1), c->real_bounds);
                        
                        auto line = c->real_bounds;
                        line.h = 1;
                        draw_colored_rect(client, ArgbColor(.953, .961, .965, 1), line);
                        line.y += 1;
                        draw_colored_rect(client, ArgbColor(.957, .969, .98, 1), line);
                        
                        line.y = c->real_bounds.y + c->real_bounds.h - 1;
                        draw_colored_rect(client, ArgbColor(.906, .914, .925, 1), line);
                        line.y = c->real_bounds.y + c->real_bounds.h - 2;
                        draw_colored_rect(client, ArgbColor(.933, .941, .953, 1), line);
                    }
                    paint_list_option_text(client, cr, c);
                    auto header = container_by_name("table_headers", client->root);
                    auto table_data = (TableData *) header->user_data;
                    if (table_data->dragging_col) {
                        SortOption tcol;
                        for (auto col : table_data->cols) {
                            if (col.name == table_data->target) {
                                tcol = col;
                                break;
                            }
                        }
                        auto leading_x = client->mouse_current_x - table_data->col_drag_offset - 8 * config->dpi;
                        auto bb = Bounds(leading_x, c->real_bounds.y, tcol.size, c->real_bounds.h);
                        if (data->selected) {
                            draw_colored_rect(client, ArgbColor(.545, .655, .788, 1), bb);
                        } else {
                            draw_colored_rect(client, ArgbColor(.945, .953, .973, 1), bb);
                            
                            auto line = bb;
                            line.h = 1;
                            draw_colored_rect(client, ArgbColor(.953, .961, .965, 1), line);
                            line.y += 1;
                            draw_colored_rect(client, ArgbColor(.957, .969, .98, 1), line);
                            
                            line.y = bb.y + bb.h - 1;
                            draw_colored_rect(client, ArgbColor(.906, .914, .925, 1), line);
                            line.y = bb.y + bb.h - 2;
                            draw_colored_rect(client, ArgbColor(.933, .941, .953, 1), line);
                        }
                        
                    }
                    paint_list_option_text(client, cr, c, true);
                };    
            } else {
                // White Option
                list_option->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
                    auto data = (ListOption *) c->user_data;
                    if (data->selected) {
                        draw_colored_rect(client, ArgbColor(.545, .655, .788, 1), c->real_bounds);
                    } else {
                        draw_colored_rect(client, ArgbColor(.98, .98, .988, 1), c->real_bounds);
                        
                        auto line = c->real_bounds;
                        line.h = 1;
                        draw_colored_rect(client, ArgbColor(.965, .969, .973, 1), line);
                        line.y += 1;
                        draw_colored_rect(client, ArgbColor(.992, .992, .992, 1), line);
                        
                        line.y = c->real_bounds.y + c->real_bounds.h - 1;
                        draw_colored_rect(client, ArgbColor(.925, .925, .925, 1), line);
                        line.y = c->real_bounds.y + c->real_bounds.h - 2;
                        draw_colored_rect(client, ArgbColor(.961, .961, .961, 1), line);
                    }
                    paint_list_option_text(client, cr, c);
                    auto header = container_by_name("table_headers", client->root);
                    auto table_data = (TableData *) header->user_data;
                    if (table_data->dragging_col) {
                        SortOption tcol;
                        for (auto col : table_data->cols) {
                            if (col.name == table_data->target) {
                                tcol = col;
                                break;
                            }
                        }
                        auto leading_x = client->mouse_current_x - table_data->col_drag_offset - 8 * config->dpi;
                        auto bb = Bounds(leading_x, c->real_bounds.y, tcol.size, c->real_bounds.h);
                        if (data->selected) {
                            draw_colored_rect(client, ArgbColor(.545, .655, .788, 1), bb);
                        } else {
                            draw_colored_rect(client, ArgbColor(.98, .98, .988, 1), bb);
                            
                            auto line = bb;
                            line.h = 1;
                            draw_colored_rect(client, ArgbColor(.965, .969, .973, 1), line);
                            line.y += 1;
                            draw_colored_rect(client, ArgbColor(.992, .992, .992, 1), line);
                            
                            line.y = bb.y + bb.h - 1;
                            draw_colored_rect(client, ArgbColor(.925, .925, .925, 1), line);
                            line.y = bb.y + bb.h - 2;
                            draw_colored_rect(client, ArgbColor(.961, .961, .961, 1), line);
                        }
                    }
                    paint_list_option_text(client, cr, c, true);
                };    
            }   
        }
    }
}
    
    