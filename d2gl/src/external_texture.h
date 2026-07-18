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

#include <vector>

namespace d2gl {

class Context;

class ExternalTextureManager {
	static constexpr uint32_t MAX_HANDLES = 1024;
	static constexpr uint16_t ATLAS_SIZE = 512;
	static constexpr uint16_t ATLAS_THRESHOLD = 256;
	static constexpr uint16_t MAX_ATLAS_LAYERS = 64;

	struct FreeRect {
		uint16_t x, y, w, h;
	};
	struct AtlasLayerInfo {
		uint16_t layer;
		std::vector<FreeRect> free_rects;
	};
	struct Slot {
		bool in_use = false;
		bool is_atlas = false;
		uint16_t layer = 0;
		uint16_t x = 0, y = 0, w = 0, h = 0;
	};

	Context& m_ctx;
	Slot m_slots[MAX_HANDLES];
	std::vector<AtlasLayerInfo> m_atlas_layers;
	uint16_t m_layer_used[8] = { 0 };
	uint32_t m_next_slot = 0;
	std::vector<uint32_t> m_free_slots;

	uint16_t allocLayer();
	void freeLayer(uint16_t layer);
	bool placeInAtlas(uint16_t w, uint16_t h, uint16_t& out_layer, uint16_t& out_x, uint16_t& out_y);
	void doSplit(AtlasLayerInfo& al, const FreeRect& rect, uint16_t w, uint16_t h);

public:
	ExternalTextureManager(Context& ctx);
	~ExternalTextureManager() = default;

	uint32_t loadTexture(const char* png_path, uint32_t* out_width, uint32_t* out_height);
	void drawTexture(uint32_t handle, float x, float y, uint32_t color, uint8_t color_idx = 0, float zoom = 1.0f);
	void releaseTexture(uint32_t handle);
	void clearAll();
	void flush();
};

}
