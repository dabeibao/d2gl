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

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

struct IDWriteFactory;
struct IDWriteFontFace;

namespace d2gl {

// Metrics are expressed in the same "32px em" units the MSDF atlas used:
// 1 unit == 1 pixel at a 32px em. Font::drawChar multiplies them by
// m_font_size / 32, so layout and scaling stay identical to the old path.
// The distance field itself is rasterized at 2x (64px em, 128px cells), but the
// coverage mask it is built from is thresholded at 4x that em so the contour is
// not quantized to the 64px em texel grid. Half of the previous 128px em: at
// the usual window/game ratio (~2x) one atlas texel still covers ~0.65 physical
// pixels, while the atlas drops from 64KB to 16KB per glyph.
struct SdfGlyphMetrics {
	uint16_t index = 0;
	float advance = 0.0f;
	float left = 0.0f;  // padded bbox left, relative to the pen position
	float top = 0.0f;   // padded bbox top, relative to the baseline (y down, usually negative)
	float width = 0.0f; // padded bitmap width in 32px-em units
	float height = 0.0f;// padded bitmap height in 32px-em units
};

struct SdfGlyphBitmap {
	SdfGlyphMetrics metrics;
	int width = 0;
	int height = 0;
	uint8_t* pixels = nullptr; // single channel SDF, malloc'd (freed by the upload path)
};

// Runtime SDF glyph source backed by DirectWrite. Replaces the pre-generated
// MSDF atlases: the TTF/OTF is rasterized on demand, the coverage bitmap is
// converted to a signed distance field and uploaded into the font texture array.
class SdfFont {
public:
	static constexpr int kEmSize = 64;        // rasterization em (2x the 32-unit base)
	static constexpr int kUnitScale = 2;      // 64px em -> 32-unit metrics divisor
	static constexpr int kSpread = 6;         // signed distance range in 32-units
	static constexpr int kSpreadPx = kSpread * kUnitScale; // same range in raster px (12)
	static constexpr int kCellSize = 128;     // atlas slot size at 64px em
	static constexpr int kCellPad = kSpreadPx; // blank margin around the glyph bbox

	SdfFont() = default;
	~SdfFont();

	// Accepts either a font file path (ttf/otf/ttc) or a system family name.
	// render_mode: 0 = DirectWrite natural, 1 = GDI natural (hinted).
	bool load(const std::string& path_or_family, int face_index = 0, int render_mode = 0);
	inline bool isLoaded() const { return m_face != nullptr; }

	// Cheap and cached; called from the layout thread on first use of a char so
	// text width/line wrapping stay valid before the texture is ready.
	const SdfGlyphMetrics* getMetrics(wchar_t c);

	// Called from a worker thread. Returns false for empty glyphs.
	bool rasterize(wchar_t c, SdfGlyphBitmap& out);

private:
	bool ensureFactory();
	bool loadFile(const std::wstring& path, int face_index);
	bool loadFamily(const std::wstring& family);
	bool makeMetrics(uint16_t glyph_index, float advance, int left, int top, int ink_w, int ink_h, SdfGlyphMetrics& out);
	const SdfGlyphMetrics* metricsLocked(wchar_t c);
	bool rasterizeLocked(wchar_t c, SdfGlyphBitmap& out);
	uint16_t glyphIndexLocked(wchar_t c);

	std::mutex m_mutex;
	IDWriteFactory* m_factory = nullptr;
	IDWriteFontFace* m_face = nullptr;
	float m_units_per_em = 2048.0f;
	float m_scale = 1.0f;   // kEmSize / units per em
	int m_render_mode = 0;  // 0 = DirectWrite natural, 1 = GDI natural
	std::unordered_map<wchar_t, SdfGlyphMetrics> m_metrics;
};

}
