/*
    D2GL: Diablo 2 LoD Glide/DDraw to OpenGL Wrapper.
    Copyright (C) 2023  Bayaraa

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "pch.h"
#include "item_color_lut.h"
#include "palette_data.h"
#include "colormap_data.h"
#include "log.h"

#include <cstring>
#include <algorithm>
#include <climits>

namespace d2gl::d2 {

int ItemColorLut::transformToIndex(int transform)
{
    switch (transform) {
        case 1: return 0;   // grey.dat
        case 2: return 1;   // grey2.dat
        case 5: return 2;   // greybrown.dat
        case 6: return 3;   // invgrey.dat
        case 7: return 4;   // invgrey2.dat
        case 8: return 5;   // invgreybrown.dat
        default: return -1;
    }
}

uint16_t ItemColorLut::getLayerIndex(int transform, int tint) const
{
    int idx = transformToIndex(transform);
    if (idx < 0 || tint < 0 || tint >= CMAP_TINT_COUNT)
        return 0;
    return (uint16_t)(idx * CMAP_TINT_COUNT + tint);
}

int ItemColorLut::findNearestPalette(const uint8_t* palette, uint8_t r, uint8_t g, uint8_t b)
{
    int best_idx = 0;
    int best_dist = INT_MAX;
    for (int i = 0; i < 256; i++) {
        int dr = (int)r - (int)palette[i * 3 + 2];  // R
        int dg = (int)g - (int)palette[i * 3 + 1];  // G
        int db = (int)b - (int)palette[i * 3 + 0];  // B
        int dist = dr * dr + dg * dg + db * db;
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = i;
        }
    }
    return best_idx;
}

bool ItemColorLut::init()
{
    if (m_ready)
        return true;

    uint32_t layer_size = LUT_ATLAS_WIDTH * LUT_ATLAS_HEIGHT * 3;
    uint32_t buf_size = LUT_LAYER_COUNT * layer_size;
    m_lut_data = new uint8_t[buf_size];
    if (!m_lut_data) {
        error_log("ItemColorLut: Failed to allocate %u bytes for LUT buffer", buf_size);
        return false;
    }

    // Pre-compute nearest palette index for all 32x32x32 input colors (independent of colormap)
    uint8_t nearest_idx[LUT_SIZE][LUT_SIZE][LUT_SIZE];
    for (int r = 0; r < LUT_SIZE; r++) {
        uint8_t tr = (uint8_t)(r * 8 + 4);
        for (int g = 0; g < LUT_SIZE; g++) {
            uint8_t tg = (uint8_t)(g * 8 + 4);
            for (int b = 0; b < LUT_SIZE; b++) {
                uint8_t tb = (uint8_t)(b * 8 + 4);
                nearest_idx[r][g][b] = (uint8_t)findNearestPalette(g_embedded_palette, tr, tg, tb);
            }
        }
    }

    for (int ci = 0; ci < CMAP_FAMILY_COUNT; ci++) {
        for (int tint = 0; tint < CMAP_TINT_COUNT; tint++) {
            int layer = ci * CMAP_TINT_COUNT + tint;

            for (int r = 0; r < LUT_SIZE; r++) {
                for (int g = 0; g < LUT_SIZE; g++) {
                    for (int b = 0; b < LUT_SIZE; b++) {
                        int src_idx = nearest_idx[r][g][b];
                        int dst_idx = g_embedded_colormap[ci][tint][src_idx];

                        uint8_t out_r = g_embedded_palette[dst_idx * 3 + 2];
                        uint8_t out_g = g_embedded_palette[dst_idx * 3 + 1];
                        uint8_t out_b = g_embedded_palette[dst_idx * 3 + 0];

                        uint32_t col = r + g * LUT_SIZE;
                        uint32_t row = b;
                        uint32_t offset = layer * layer_size + (row * LUT_ATLAS_WIDTH + col) * 3;
                        m_lut_data[offset + 0] = out_r;
                        m_lut_data[offset + 1] = out_g;
                        m_lut_data[offset + 2] = out_b;
                    }
                }
            }
        }
    }

    m_ready = true;
    trace_log("ItemColorLut: Built %d 3D LUTs (%dx%d atlas, %.1f MB)",
        LUT_COUNT, LUT_ATLAS_WIDTH, LUT_ATLAS_HEIGHT,
        (float)buf_size / (1024.0f * 1024.0f));
    return true;
}

} // namespace d2gl::d2
