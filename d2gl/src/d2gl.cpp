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
#include "d2gl.h"
#include "helpers.h"
#include "graphic/object.h"
#include "log.h"

using namespace d2gl;
#ifdef __cplusplus
extern "C" {
#endif

static constexpr uint32_t MAX_EXTERNAL_TEX = 64;
static struct {
	bool in_use = false;
	uint32_t width = 0;
	uint32_t height = 0;
} s_ext_tex[MAX_EXTERNAL_TEX];

__declspec(dllexport) BOOL __stdcall d2glConfigQueryImpl(D2GLConfigId config_id)
{
	switch (config_id) {
		case D2GL_CONFIG_VSYNC: return d2gl::App.vsync;
		case D2GL_CONFIG_CURSOR_UNLOCKED: return d2gl::App.cursor.unlock;
		case D2GL_CONFIG_HD_CURSOR: return d2gl::App.hd_cursor;
		case D2GL_CONFIG_HD_TEXT: return d2gl::App.hd_text.active;
		case D2GL_CONFIG_MOTION_PREDICTION: return d2gl::App.motion_prediction;
		case D2GL_CONFIG_MINI_MAP: return d2gl::App.mini_map.active;
		case D2GL_CONFIG_SHOW_ITEM_QUANTITY: return d2gl::App.show_item_quantity;
		case D2GL_CONFIG_SHOW_MONSTER_RES: return d2gl::App.show_monster_res;
		case D2GL_CONFIG_SHOW_FPS: return d2gl::App.show_fps;
	}
	return FALSE;
}

__declspec(dllexport) void __stdcall setCustomScreenSize(uint32_t width, uint32_t height)
{
	d2gl::App.game.custom_size = { width, height };
}

__declspec(dllexport) bool isHDTextEnabled()
{
    return d2gl::App.hd_text.active;
}

__declspec(dllexport) uint32_t __stdcall d2glLoadTexture(const char* png_path)
{
	if (!d2gl::App.context || !d2gl::App.context->m_external_texture) {
		return 0;
	}

	uint32_t layer = MAX_EXTERNAL_TEX;
	for (uint32_t i = 0; i < MAX_EXTERNAL_TEX; i++) {
		if (!s_ext_tex[i].in_use) {
			layer = i;
			break;
		}
	}
	if (layer >= MAX_EXTERNAL_TEX)
		return 0;

	auto image = d2gl::helpers::loadImage(png_path, true);
	if (!image.data)
		return 0;

	uint32_t w = image.width;
	uint32_t h = image.height;
	if (w > 512) w = 512;
	if (h > 512) h = 512;

	if (w != (uint32_t)image.width || h != (uint32_t)image.height) {
		auto clipped = new uint8_t[w * h * 4];
		for (uint32_t y = 0; y < h; y++)
			memcpy(clipped + y * w * 4, image.data + y * image.width * 4, w * 4);
		d2gl::App.context->queueExternalTexUpload(layer, clipped, w, h);
		delete[] clipped;
	} else {
		d2gl::App.context->queueExternalTexUpload(layer, image.data, w, h);
	}
	d2gl::helpers::clearImage(image);

	s_ext_tex[layer].in_use = true;
	s_ext_tex[layer].width = w;
	s_ext_tex[layer].height = h;

	return layer + 1;
}

__declspec(dllexport) void __stdcall d2glDrawTexture(uint32_t handle, float x, float y, float w, float h, uint32_t color)
{
	if (!d2gl::App.context || handle == 0 || handle > MAX_EXTERNAL_TEX)
		return;

	const uint32_t layer = handle - 1;
	if (!s_ext_tex[layer].in_use)
		return;

	auto obj = std::make_unique<d2gl::Object>(glm::vec2(x, y), glm::vec2(w, h));
	obj->setTexIds({ (int16_t)layer, 0 });
	obj->setFlags(8);
	obj->setColor(color);
	obj->setExtra({ 1.0f, 0.0f });

	d2gl::App.context->pushObject(obj);
}

__declspec(dllexport) void __stdcall d2glReleaseTexture(uint32_t handle)
{
	if (handle == 0 || handle > MAX_EXTERNAL_TEX)
		return;

	const uint32_t layer = handle - 1;
	s_ext_tex[layer].in_use = false;
	s_ext_tex[layer].width = 0;
	s_ext_tex[layer].height = 0;
}

#ifdef __cplusplus
}
#endif
