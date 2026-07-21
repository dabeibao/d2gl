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
#include <shlwapi.h>
#include "d2/item_color_lut.h"
#include "external_texture.h"
#include "graphic/context.h"
#include "graphic/object.h"
#include "helpers.h"

#pragma comment(lib, "shlwapi.lib")
namespace d2gl {

ExternalTextureManager::ExternalTextureManager(Context& ctx)
	: m_ctx(ctx)
{
}

uint16_t ExternalTextureManager::allocLayer()
{
	for (uint16_t i = 0; i < 128; i++) {
		if (!(m_layer_used[i >> 4] & (1 << (i & 0xF)))) {
			m_layer_used[i >> 4] |= (1 << (i & 0xF));
			return i;
		}
	}
	return 0xFFFF;
}

void ExternalTextureManager::freeLayer(uint16_t layer)
{
	if (layer < 128)
		m_layer_used[layer >> 4] &= ~(1 << (layer & 0xF));
}

void ExternalTextureManager::doSplit(AtlasLayerInfo& al, const FreeRect& rect, uint16_t w, uint16_t h)
{
	auto is_valid_free_rect = [](uint16_t width, uint16_t height) {
		return (width > 8 && height >= 5) || (width >= 5 && height > 8);
	};
	uint16_t remain_w = rect.w - w;
	uint16_t remain_h = rect.h - h;

	bool split_horizontally = (w <= h);

	if (split_horizontally) {
		if (is_valid_free_rect(remain_w, h))
			al.free_rects.push_back({ (uint16_t)(rect.x + w), rect.y, remain_w, h });
		if (is_valid_free_rect(rect.w, remain_h))
			al.free_rects.push_back({ rect.x, (uint16_t)(rect.y + h), rect.w, remain_h });
	} else {
		if (is_valid_free_rect(remain_w, rect.h))
			al.free_rects.push_back({ (uint16_t)(rect.x + w), rect.y, remain_w, rect.h });
		if (is_valid_free_rect(w, remain_h))
			al.free_rects.push_back({ rect.x, (uint16_t)(rect.y + h), w, remain_h });
	}
}

bool ExternalTextureManager::placeInAtlas(uint16_t w, uint16_t h, uint16_t& out_layer, uint16_t& out_x, uint16_t& out_y)
{
	for (uint16_t i = 0; i < (uint16_t)m_atlas_layers.size(); i++) {
		auto& al = m_atlas_layers[i];

		int best_idx = -1;
		uint32_t best_waste = UINT32_MAX;
		for (uint16_t j = 0; j < (uint16_t)al.free_rects.size(); j++) {
			const auto& r = al.free_rects[j];
			if (r.w >= w && r.h >= h) {
				uint16_t leftover_w = r.w - w;
				uint16_t leftover_h = r.h - h;
				// BSSF
				uint32_t short_side_fit = std::min(leftover_w, leftover_h);
				if (short_side_fit < best_waste) {
					best_waste = short_side_fit;
					best_idx = j;
				}
			}
		}

		if (best_idx < 0)
			continue;

		const auto rect = al.free_rects[best_idx];
		al.free_rects.erase(al.free_rects.begin() + best_idx);

		doSplit(al, rect, w, h);

		out_x = rect.x;
		out_y = rect.y;
		out_layer = al.layer;

		return true;
	}

	if (m_atlas_layers.size() >= MAX_ATLAS_LAYERS)
		return false;

	uint16_t layer = allocLayer();
	if (layer >= 128)
		return false;

	AtlasLayerInfo al;
	al.layer = layer;

	FreeRect rect{0, 0, ATLAS_SIZE, ATLAS_SIZE};
	doSplit(al, rect, w, h);

	m_atlas_layers.push_back(std::move(al));

	out_layer = layer;
	out_x = rect.x;
	out_y = rect.y;
	return true;
}

uint32_t ExternalTextureManager::loadTexture(const char* png_path, uint32_t* out_width, uint32_t* out_height)
{
	if (!m_ctx.m_external_texture) {
		if (out_width) *out_width = 0;
		if (out_height) *out_height = 0;
		return 0;
	}

	uint32_t slot;
	if (!m_free_slots.empty()) {
		slot = m_free_slots.back();
		m_free_slots.pop_back();
	} else if (m_next_slot < MAX_HANDLES) {
		slot = m_next_slot++;
	} else {
		if (out_width) *out_width = 0;
		if (out_height) *out_height = 0;
		return 0;
	}


	ImageData src = { 0 };
	if (strlen(png_path) > 7 && _stricmp(png_path + strlen(png_path) - 7, ".sprite") == 0) {
		src = helpers::loadSprite(png_path);
	} else {
		bool flipped = false;
		src = PathIsRelativeA(png_path) ?
			helpers::loadImage(png_path, false) :
			helpers::loadImageFromFile(png_path, false);
	}
	if (!src.data) {
		if (out_width) *out_width = 0;
		if (out_height) *out_height = 0;
		return 0;
	}

	ImageData image;
	uint32_t w;
	uint32_t h;
	w = src.width;
	h = src.height;
	image = src;

	if (w > ATLAS_SIZE) w = ATLAS_SIZE;
	if (h > ATLAS_SIZE) h = ATLAS_SIZE;

	if (out_width) *out_width = w;
	if (out_height) *out_height = h;

	const bool large = image.width > ATLAS_THRESHOLD && image.height > ATLAS_THRESHOLD;

	uint16_t layer = 0, offset_x = 0, offset_y = 0;
	bool is_atlas = false;

	if (large) {
		layer = allocLayer();
		if (layer >= 128) {
			helpers::clearImage(image);
			return 0;
		}
	} else {
		if (!placeInAtlas((uint16_t)w, (uint16_t)h, layer, offset_x, offset_y)) {
			helpers::clearImage(image);
			return 0;
		}
		is_atlas = true;
	}

	uint8_t* upload_pixels = image.data;
	if (w != (uint32_t)image.width || h != (uint32_t)image.height) {
		auto clipped = new uint8_t[w * h * 4];
		for (uint32_t y = 0; y < h; y++)
			memcpy(clipped + y * w * 4, image.data + y * image.width * 4, w * 4);
		upload_pixels = clipped;
	}

	m_ctx.queueExternalTexUpload(layer, upload_pixels, w, h, offset_x, offset_y);

	if (upload_pixels != image.data)
		delete[] upload_pixels;
	helpers::clearImage(image);

	auto& s = m_slots[slot];
	s.in_use = true;
	s.is_atlas = is_atlas;
	s.layer = layer;
	s.x = offset_x;
	s.y = offset_y;
	s.w = (uint16_t)w;
	s.h = (uint16_t)h;

	return (uint32_t)(slot + 1);
}

void ExternalTextureManager::drawTexture(uint32_t handle, float x, float y, uint32_t color, uint8_t color_idx, float zoom)
{
	if (handle == 0 || handle > MAX_HANDLES)
		return;

	const uint32_t slot = handle - 1;
	auto& info = m_slots[slot];
	if (!info.in_use)
		return;

	float sw = (float)info.w * zoom;
	float sh = (float)info.h * zoom;
	float sx = x - sw / 2.0f;
	float sy = y - sh / 2.0f;

	drawTexture(info, sx, sy, sw, sh, color, color_idx);
}

void ExternalTextureManager::drawTexture(uint32_t handle, float x, float y, float w, float h, uint32_t color, uint8_t color_idx)
{
	if (handle == 0 || handle > MAX_HANDLES)
		return;

	const uint32_t slot = handle - 1;
	auto& info = m_slots[slot];
	if (!info.in_use)
		return;

	drawTexture(info, x, y, w, h, color, color_idx);
}

void ExternalTextureManager::drawTexture(const Slot& info, float x, float y, float w, float h, uint32_t color, uint8_t color_idx)
{
	auto obj = std::make_unique<Object>(glm::vec2(x, y), glm::vec2(w, h));
	float u0 = (float)info.x / ATLAS_SIZE;
	float v1 = (float)(info.y + info.h) / ATLAS_SIZE;
	float u1 = (float)(info.x + info.w) / ATLAS_SIZE;
	float v0 = (float)info.y / ATLAS_SIZE;
	obj->setTexCoord({ u0, v1, u1, v0 });
	obj->setTexIds({ (int16_t)info.layer, 0 });
	obj->setFlags(8, 0, 0, color_idx);
	obj->setColor(color);

	float extra_y = 0.0f;
	if (color_idx != 0) {
		int cls = color_idx / 21;
		int ci = color_idx % 21;
		int16_t lut_layer = 0;
		if (cls == 1 || cls == 2 || (cls >= 5 && cls <= 8)) {
			lut_layer = (int16_t)d2::ItemColorLut::Instance().getLayerIndex(cls, ci);
			if (lut_layer > 0) {
				extra_y = 1.0f;
				obj->setTexIds({ (int16_t)info.layer, lut_layer });
			}
		}
	}
	obj->setExtra({ 1.0f, extra_y });

	App.context->pushObject(obj);
}

void ExternalTextureManager::releaseTexture(uint32_t handle)
{
	if (handle == 0 || handle > MAX_HANDLES)
		return;

	const uint32_t slot = handle - 1;
	auto& info = m_slots[slot];
	if (!info.in_use)
		return;

	if (!info.is_atlas)
		freeLayer(info.layer);

	info = {};
	m_free_slots.push_back(slot);
}

void ExternalTextureManager::clearAll()
{
	for (uint32_t i = 0; i < MAX_HANDLES; i++)
		m_slots[i] = {};

	for (uint16_t i = 0; i < 8; i++)
		m_layer_used[i] = 0;

	m_atlas_layers.clear();
	m_next_slot = 0;
	m_free_slots.clear();
}

}
