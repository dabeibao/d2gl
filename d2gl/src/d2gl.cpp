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
#include "external_texture.h"
#include "graphic/context.h"

using namespace d2gl;
#ifdef __cplusplus
extern "C" {
#endif

static ExternalTextureManager* g_ext_mgr = nullptr;

static ExternalTextureManager* getExtMgr()
{
	if (!g_ext_mgr && d2gl::App.context)
		g_ext_mgr = new ExternalTextureManager(*d2gl::App.context);
	return g_ext_mgr;
}

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

static uint32_t __fastcall d2glLoadTexture(const char* png_path, float zoom, uint32_t* out_width, uint32_t* out_height)
{
	auto mgr = getExtMgr();
	return mgr ? mgr->loadTexture(png_path, out_width, out_height, zoom) : 0;
}

static void __fastcall d2glDrawTexture(uint32_t handle, float x, float y, uint32_t color)
{
	auto mgr = getExtMgr();
	if (mgr) mgr->drawTexture(handle, x, y, color);
}

static void __fastcall d2glReleaseTexture(uint32_t handle)
{
	auto mgr = getExtMgr();
	if (mgr) mgr->releaseTexture(handle);
}

static void d2glClearAllTextures()
{
 	auto mgr = getExtMgr();
	if (mgr) mgr->clearAll();
}

static void d2glFlushExternalTextures()
{
	auto mgr = getExtMgr();
	if (mgr) mgr->flushExternal();
}

__declspec(dllexport) D2GLTextureAPI d2glTextureAPI = {
	.load = d2glLoadTexture,
	.draw = d2glDrawTexture,
	.release = d2glReleaseTexture,
	.clearAll = d2glClearAllTextures,
	.flush = d2glFlushExternalTextures,
};

#ifdef __cplusplus
}
#endif
