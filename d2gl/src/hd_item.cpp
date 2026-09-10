#include "pch.h"
#include <unordered_map>
#include <string>
#include "types.h"
#include "hd_item.hpp"
#include "helpers.h"
#include "d2/funcs.h"
#include "d2/structs.h"
#include "external_texture.h"

enum DrawMode
{
	DRAWMODE_TRANS25,
	DRAWMODE_TRANS50,
	DRAWMODE_TRANS75,
	DRAWMODE_MODULATE,
	DRAWMODE_BURN,
	DRAWMODE_NORMAL,
	DRAWMODE_TRANSHIGHLIGHT,
	DRAWMODE_HIGHLIGHT
};

namespace d2gl {

static std::unordered_map<std::string, d2gl::HDItemInfo *> s_hd_item_cache;

static uint32_t getItemColor(int draw_mode)
{
	uint32_t color = 0xffffff00;
	uint32_t alpha = 0xff;
	switch (draw_mode) {
	case DRAWMODE_TRANS25: alpha = 0xff / 4; break;
	case DRAWMODE_TRANS50: alpha = 0xff / 2; break;
	case DRAWMODE_TRANS75: alpha = 0xff * 3 / 4; break;
	default: break;
	}
	color += alpha;
	return color;
}

static uint8_t getItemColorIdx(uint8_t* palette)
{
	if (!palette) {
		return 0;
	}
	static uintptr_t gMixPaletteBase = 0;
	if (!gMixPaletteBase) {
		gMixPaletteBase = helpers::getProcOffset(DLL_D2CMP, 0x24AA8);
		if (!gMixPaletteBase) {
			return 0;
		}
	}
	intptr_t offset = (intptr_t)palette - (intptr_t)gMixPaletteBase;
	if (offset < 0 || (offset & 255) != 0) {
		return 0;
	}
	int idx = (int)(offset / 256);
	int ci = idx % 105;
	int cls = idx / 105;
	if (cls > 0 && cls < 9 && cls != 3 && cls != 4 && ci >= 0 && ci < 21)
		return (uint8_t)(cls * 21 + ci);
	return 0;
}

static HDItemInfo* loadHDItem(const char * item_code)
{
	std::string mpq_path = std::string("assets\\textures\\items\\") + item_code + ".sprite";
	auto buf = helpers::loadFile(mpq_path);
	if (!buf.size) {
		trace_log("HD: failed to load %s", item_code);
		return nullptr;
	}

	int total_width = 0, height = 0;
	uint32_t frame_count = 1;
	if (!helpers::loadSpriteInfo(buf.data, buf.size, total_width, height, frame_count)) {
		delete[] buf.data;
		return nullptr;
	}

	auto img = helpers::loadSpriteFromMemory(buf.data, buf.size);
	delete[] buf.data;

	if (!img.data) {
		trace_log("HD: failed to parse sprite info %s", item_code);
		return nullptr;
	}

	// v61 sprites hold a raw DXT5 stream: decode it to RGBA first so the
	// per-frame slicing below (w*h*4 stride) is valid. Same fallback as
	// ExternalTextureManager::loadTexture.
	if (img.compressed) {
		size_t bcw = (img.width + 3) / 4;
		size_t bch = (img.height + 3) / 4;
		ImageData decoded = helpers::decodeDXT5(img.data, img.width, img.height, bcw * bch * 16);
		helpers::clearImage(img);
		img = decoded;
		if (!img.data) {
			trace_log("HD: failed to decode sprite %s", item_code);
			return nullptr;
		}
	}

	auto ext_mgr = getExtTextureMgr();
	if (!ext_mgr) {
		helpers::clearImage(img);
		return nullptr;
	}

	auto info = HDItemInfo::allocate(frame_count);
	info->width = total_width / frame_count;
	info->height = height;

	const auto frame_size = info->width * info->height * 4;
	for (uint32_t i = 0; i < frame_count; ++i) {
		auto offset = i * frame_size;
		auto pixels = &img.data[offset];
		info->handles[i] = ext_mgr->loadTextureRGBA(pixels, info->width, info->height);
	}

	helpers::clearImage(img);
	return info;
}

static bool isSocketCell(d2::CellContext * ctx)
{
	static d2::CellFile ** gemSocketFile;
	if (gemSocketFile == nullptr) {
		gemSocketFile = (d2::CellFile **)helpers::getProcOffset(DLL_D2CLIENT, 0x11a778);
	}
	if (*gemSocketFile != nullptr && ctx->v113.pCellFile == *gemSocketFile) {
		return true;
	}
	return false;
}

static const HDItemInfo * getHDItem(const char * item_code)
{
	if (!item_code) {
		return nullptr;
	}
	auto iter = s_hd_item_cache.find(item_code);
	if (iter != s_hd_item_cache.end()) {
		return iter->second;
	}
	auto info = loadHDItem(item_code);

	s_hd_item_cache[item_code] = info;
	return info;
}

bool HDItemDraw(d2::CellContext *cell, int x, int y, int draw_mode, uint8_t *palette)
{
	if (cell == nullptr || !isVerMin(V_113c)) {
		return false;
	}

	// Item or socket
	if (cell->v113.nUnitType != (DWORD)d2::UnitType::Item && cell->v113.nUnitType != 0) {
		return false;
	}
	bool isSocket = isSocketCell(cell);
	const char * name = isSocket? "gemsocket" : cell->v113.pItemCode;
	auto info = getHDItem(name );
	if (info == nullptr) {
		return false;
	}
	uint32_t frame = d2::getCellNo(cell);
	if (frame >= info->count) {
		frame = info->count - 1;
	}
	auto handle = info->handles[frame];
	if (handle == 0) {
		return false;
	}
	auto color = getItemColor(draw_mode);
	auto color_idx = getItemColorIdx(palette);

	int orig_w = 0, orig_h = 0;
	if (isSocket) {
		orig_w = cell->v113.pCellFile->cells[frame]->width;
		orig_h = cell->v113.pCellFile->cells[frame]->height;
	} else if (cell->v113.pCurGfxCell) {
		orig_w = cell->v113.pCurGfxCell->width;
		orig_h = cell->v113.pCurGfxCell->height;
	}

	if (orig_w <= 0 || orig_h <= 0) {
		return false;
	}

	auto ext_mgr = getExtTextureMgr();
	if (ext_mgr == nullptr) {
		return false;
	}

	float nX = (float)x;
	float nY = (float)y - (float)orig_h;
	float scale_x = (float)orig_w / (float)info->width;
	float scale_y = (float)orig_h / (float)info->height;
	float draw_w = (float)info->width * scale_x;
	float draw_h = (float)info->height * scale_y;

	ext_mgr->drawTexture(handle, nX, nY, draw_w, draw_h, color, color_idx);
	return true;
}

void HDItemClearCache()
{
	for (auto [_, info]: s_hd_item_cache) {
		free(info);
	}
	s_hd_item_cache.clear();
}

}

