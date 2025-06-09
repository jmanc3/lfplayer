/* date = April 26th 2025 4:33 am */

#ifndef ALBUM_TAB_H
#define ALBUM_TAB_H

#include "container.h"
#include "main.h"
#include <vector>

void fill_album_tab(AppClient *client, Container *albums_root, const std::vector<Option> &options);

void fade_out_edges_2(cairo_surface_t *surface, int pixels);

#endif //ALBUM_TAB_H
