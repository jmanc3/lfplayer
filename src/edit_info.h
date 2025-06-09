/* date = April 27th 2025 4:26 pm */

#ifndef EDIT_INFO_H
#define EDIT_INFO_H

#include <string>

enum EditType {
    ALBUM_TYPE,
    SONG_TYPE,
};

void edit_info(EditType type, std::string name);

#endif //EDIT_INFO_H
