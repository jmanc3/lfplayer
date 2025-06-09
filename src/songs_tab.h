/* date = April 26th 2025 2:27 pm */

#ifndef SONGS_TAB_H
#define SONGS_TAB_H


#include "container.h"
#include "main.h"
#include <vector>

void fill_songs_tab(AppClient *client, Container *songs_root, std::vector<Option> &options);

void put_selected_on_screen(AppClient *client);



#endif //SONGS_TAB_H
