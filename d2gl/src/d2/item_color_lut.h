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

#pragma once
#include <stdint.h>

namespace d2gl::d2 {

// 3D Color LUT system: replaces the lossy shader tint approximation
// with exact colormap lookup tables sampled as a 3D texture.
//
// Layout: 126 layers of 1024×32 RGB8 (sampler2DArray).
//   - Each LUT = 1 layer = 1024×32 atlas
//   - Column = R + G×32, Row = B
//   - Hardware bilinear: R (within G-block) × B (across rows)
//   - Manual lerp between G-blocks: 2 texture samples per pixel
// Total: 126 layers × 1024×32 pixels × 3 bytes = ~12.6 MB GPU memory.
//
// At init time, builds all 126 LUTs from the embedded D2 palette and
// colormap data.  At render time, the shader samples the LUT with
// bilinear + linear interpolation for exact colormap color reproduction.

#define LUT_SIZE            32     // per-axis resolution
#define LUT_LAYER_COUNT     126    // 6 families × 21 tints
#define LUT_COUNT           126
#define LUT_ATLAS_WIDTH     (LUT_SIZE * LUT_SIZE)  // 1024 (R + G×32)
#define LUT_ATLAS_HEIGHT    LUT_SIZE               // 32 (B)

class ItemColorLut {
public:
    static ItemColorLut& Instance()
    {
        static ItemColorLut instance;
        return instance;
    }

    bool init();
    bool isReady() const { return m_ready; }

    // Get the raw LUT pixel data (for texture upload).
    // Format: RGB8, 126 layers, each 1024×32 atlas.
    // Column = R + G*32, Row = B.
    const uint8_t* getLutData() const { return m_lut_data; }
    uint32_t getLutDataSize() const { return LUT_LAYER_COUNT * LUT_ATLAS_WIDTH * LUT_ATLAS_HEIGHT * 3; }

    // Given a D2 transform family and tint index, return the LUT layer index (0-125).
    // Returns 0 for invalid inputs.
    uint16_t getLayerIndex(int transform, int tint) const;

    // Free the CPU-side copy once the data has been uploaded to the GPU
    // (~12MB). m_ready stays true: the LUT lives on in the texture.
    void release()
    {
        delete[] m_lut_data;
        m_lut_data = nullptr;
    }

private:
    ItemColorLut() = default;
    ~ItemColorLut() { delete[] m_lut_data; }

    // Maps D2 transform family ID to internal index (0-5)
    static int transformToIndex(int transform);

    // Find nearest palette index for an 8-bit RGB color
    static int findNearestPalette(const uint8_t* palette, uint8_t r, uint8_t g, uint8_t b);

    bool m_ready = false;
    uint8_t* m_lut_data = nullptr;  // 126 * 1024 * 32 * 3 bytes
};

} // namespace d2gl::d2
