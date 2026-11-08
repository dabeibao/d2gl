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
#include "sdf_font.h"

#include <dwrite.h>
#include <cmath>
#include <cstdlib>
#include <vector>

#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "ole32.lib")

namespace d2gl {

namespace {

	void ensureThreadCom()
	{
		static thread_local bool initialized = false;
		if (initialized)
			return;
		// DWriteCreateFactory wants a COM apartment. The game thread may already
		// be STA; RPC_E_CHANGED_MODE is harmless (the thread is still initialized).
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		initialized = true;
	}

	std::wstring widen(const std::string& str)
	{
		const int len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, nullptr, 0);
		if (len <= 1)
			return {};
		std::wstring out((size_t)len, L'\0');
		MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, out.data(), len);
		out.resize((size_t)len - 1);
		return out;
	}

	// 8SSEDT: one offset vector per pixel, seeded with zeros on the target
	// class. 64x64 grids, so the simple two-pass version is plenty fast.
	struct SdfPoint {
		int x, y;
	};

	inline int pointDist2(const SdfPoint& p)
	{
		return p.x * p.x + p.y * p.y;
	}

	inline void comparePoint(SdfPoint& p, const SdfPoint& n, int dx, int dy)
	{
		const SdfPoint candidate = { n.x + dx, n.y + dy };
		if (pointDist2(candidate) < pointDist2(p))
			p = candidate;
	}

	void distancePass(std::vector<SdfPoint>& grid, int w, int h)
	{
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				SdfPoint& p = grid[y * w + x];
				if (x > 0)
					comparePoint(p, grid[y * w + x - 1], 1, 0);
				if (y > 0) {
					if (x > 0)
						comparePoint(p, grid[(y - 1) * w + x - 1], 1, 1);
					comparePoint(p, grid[(y - 1) * w + x], 0, 1);
					if (x + 1 < w)
						comparePoint(p, grid[(y - 1) * w + x + 1], -1, 1);
				}
			}
		}

		for (int y = h - 1; y >= 0; y--) {
			for (int x = w - 1; x >= 0; x--) {
				SdfPoint& p = grid[y * w + x];
				if (x + 1 < w)
					comparePoint(p, grid[y * w + x + 1], -1, 0);
				if (y + 1 < h) {
					if (x + 1 < w)
						comparePoint(p, grid[(y + 1) * w + x + 1], -1, -1);
					comparePoint(p, grid[(y + 1) * w + x], 0, -1);
					if (x > 0)
						comparePoint(p, grid[(y + 1) * w + x - 1], 1, -1);
				}
			}
		}
	}

	// Converts a binary coverage mask to a single-channel signed distance field.
	// 0.5 == edge, the value range maps to +-kSpread pixels around it.
	void buildSdf(const std::vector<uint8_t>& mask, int w, int h, int spread, uint8_t* out)
	{
		constexpr int INF = 4096;
		std::vector<SdfPoint> to_inside((size_t)w * h, { INF, 0 });
		std::vector<SdfPoint> to_outside((size_t)w * h, { INF, 0 });

		for (int i = 0; i < w * h; i++) {
			if (mask[i]) {
				to_inside[i] = { 0, 0 };
			} else {
				to_outside[i] = { 0, 0 };
			}
		}

		distancePass(to_inside, w, h);
		distancePass(to_outside, w, h);

		const float inv_range = 1.0f / (2.0f * (float)spread);
		for (int i = 0; i < w * h; i++) {
			float dist;
			if (mask[i]) {
				// Depth inside the shape == distance to the nearest outside pixel.
				dist = sqrtf((float)pointDist2(to_outside[i])) - 0.5f;
			} else {
				dist = -(sqrtf((float)pointDist2(to_inside[i])) - 0.5f);
			}
			float value = 0.5f + dist * inv_range;
			value = glm::clamp(value, 0.0f, 1.0f);
			out[i] = (uint8_t)(value * 255.0f + 0.5f);
		}
	}

	// Coverage raster is thresholded at this multiple of kEmSize; the stored
	// distance field stays at kEmSize. A 1-bit mask at 64px em puts the contour
	// on a 1-texel grid (+-0.5 texel == +-0.35 px on a 44 px glyph), which is
	// what makes large SDF text look ragged next to an outline-based atlas.
	// 3x leaves +-0.17 texel (+-0.12 px) and costs 1.2 ms per CJK glyph instead
	// of 2.1 ms at 4x (measured 264^2 vs 352^2 EDT), which matters for the
	// first-time burst when a new panel appears.
	constexpr int kRasterScale = 3;

}

SdfFont::~SdfFont()
{
	if (m_face)
		m_face->Release();
	if (m_factory)
		m_factory->Release();
}

bool SdfFont::ensureFactory()
{
	if (m_factory)
		return true;

	HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&m_factory));
	if (FAILED(hr) || !m_factory) {
		error_log("[HDText] DWriteCreateFactory failed (0x%08X)", (uint32_t)hr);
		m_factory = nullptr;
		return false;
	}
	return true;
}

bool SdfFont::loadFile(const std::wstring& path, int face_index)
{
	IDWriteFontFile* file = nullptr;
	if (FAILED(m_factory->CreateFontFileReference(path.c_str(), nullptr, &file)) || !file)
		return false;

	BOOL supported = FALSE;
	DWRITE_FONT_FILE_TYPE file_type = DWRITE_FONT_FILE_TYPE_UNKNOWN;
	DWRITE_FONT_FACE_TYPE face_type = DWRITE_FONT_FACE_TYPE_UNKNOWN;
	UINT32 face_count = 0;
	HRESULT hr = file->Analyze(&supported, &file_type, &face_type, &face_count);
	if (FAILED(hr) || !supported || face_type == DWRITE_FONT_FACE_TYPE_UNKNOWN) {
		file->Release();
		return false;
	}

	if (face_index < 0 || (face_count && (UINT32)face_index >= face_count))
		face_index = 0;

	hr = m_factory->CreateFontFace(face_type, 1, &file, (UINT32)face_index, DWRITE_FONT_SIMULATIONS_NONE, &m_face);
	if (FAILED(hr) && face_type == DWRITE_FONT_FACE_TYPE_OPENTYPE_COLLECTION) {
		// Some collections report the wrapper type; the inner faces are classic outlines.
		hr = m_factory->CreateFontFace(DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &file, (UINT32)face_index, DWRITE_FONT_SIMULATIONS_NONE, &m_face);
		if (FAILED(hr))
			hr = m_factory->CreateFontFace(DWRITE_FONT_FACE_TYPE_CFF, 1, &file, (UINT32)face_index, DWRITE_FONT_SIMULATIONS_NONE, &m_face);
	}
	file->Release();
	return SUCCEEDED(hr) && m_face;
}

bool SdfFont::loadFamily(const std::wstring& family)
{
	IDWriteFontCollection* collection = nullptr;
	if (FAILED(m_factory->GetSystemFontCollection(&collection, FALSE)) || !collection)
		return false;

	UINT32 index = 0;
	BOOL exists = FALSE;
	collection->FindFamilyName(family.c_str(), &index, &exists);

	IDWriteFontFamily* font_family = nullptr;
	if (exists)
		collection->GetFontFamily(index, &font_family);
	collection->Release();

	if (!font_family)
		return false;

	IDWriteFont* font = nullptr;
	font_family->GetFont(0, &font);
	font_family->Release();

	if (!font)
		return false;

	HRESULT hr = font->CreateFontFace(&m_face);
	font->Release();
	return SUCCEEDED(hr) && m_face;
}

bool SdfFont::load(const std::string& path_or_family, int face_index, int render_mode)
{
	ensureThreadCom();
	m_render_mode = render_mode;
	if (!ensureFactory())
		return false;

	const auto wide = widen(path_or_family);
	if (wide.empty())
		return false;

	const bool is_file = GetFileAttributesW(wide.c_str()) != INVALID_FILE_ATTRIBUTES;
	bool ok = is_file ? loadFile(wide, face_index) : loadFamily(wide);

	// A bare file name ("chi.ttf") is resolved against the bundled font folder
	// before giving up, so the config does not have to spell out the path.
	if (!ok && !is_file) {
		const std::wstring bundled = L"data\\assets\\fonts\\" + wide;
		if (GetFileAttributesW(bundled.c_str()) != INVALID_FILE_ATTRIBUTES)
			ok = loadFile(bundled, face_index);
	}
	if (!ok) {
		error_log("[HDText] SDF font load failed: %s", path_or_family.c_str());
		return false;
	}

	DWRITE_FONT_METRICS metrics = {};
	m_face->GetMetrics(&metrics);
	m_units_per_em = metrics.designUnitsPerEm ? (float)metrics.designUnitsPerEm : 2048.0f;
	m_scale = (float)kEmSize / m_units_per_em;

	trace_log("[HDText] SDF font loaded (%s): %s, %.0f units/em",
		is_file ? "file" : "family", path_or_family.c_str(), m_units_per_em);
	return true;
}

uint16_t SdfFont::glyphIndex(wchar_t c)
{
	uint16_t index = 0;
	const UINT32 code = (UINT32)c;
	if (FAILED(m_face->GetGlyphIndices(&code, 1, &index)))
		return 0;

	// D2's symbol fallback used '?' for unknown chars; keep that behaviour so
	// missing characters are visible instead of silently dropped.
	if (index == 0 && c != L' ' && c != L'?' && c != L'\t') {
		const UINT32 question = L'?';
		m_face->GetGlyphIndices(&question, 1, &index);
	}
	return index;
}

const SdfGlyphMetrics* SdfFont::getAdvance(wchar_t c)
{
	ensureThreadCom();
	if (!m_face)
		return nullptr;
	std::lock_guard<std::mutex> lock(m_metrics_mutex);

	auto it = m_metrics.find(c);
	if (it != m_metrics.end())
		return &it->second;

	SdfGlyphMetrics metrics = {};
	metrics.index = glyphIndex(c);
	if (metrics.index) {
		// Design metrics only: no glyph-run analysis, so this stays cheap enough
		// to run on the game thread for every character of a new string.
		DWRITE_GLYPH_METRICS design = {};
		if (SUCCEEDED(m_face->GetDesignGlyphMetrics(&metrics.index, 1, &design, FALSE)))
			metrics.advance = (float)design.advanceWidth * m_scale / (float)kUnitScale;
	}
	return &m_metrics.emplace(c, metrics).first->second;
}

bool SdfFont::rasterizeGlyph(wchar_t c, SdfGlyphBitmap& out)
{
	if (!m_face)
		return false;
	const uint16_t index = glyphIndex(c);
	if (index == 0)
		return false;

	// Design metrics only; the ink box comes from the fine raster below, so the
	// whole layout analysis happens on the worker instead of blocking the game
	// thread once per character of a new string.
	DWRITE_GLYPH_METRICS design = {};
	if (FAILED(m_face->GetDesignGlyphMetrics(&index, 1, &design, FALSE)))
		return false;

	// Metrics are 32-unit values; the coverage is rasterized at kRasterScale
	// times the stored em (see kRasterScale).
	const int em = kEmSize * kRasterScale;
	const float advance = (float)design.advanceWidth * m_scale * (float)kRasterScale;

	DWRITE_GLYPH_RUN run = {};
	run.fontFace = m_face;
	run.fontEmSize = (FLOAT)em;
	run.glyphCount = 1;
	run.glyphIndices = &index;
	run.glyphAdvances = &advance;
	const DWRITE_GLYPH_OFFSET offset = { 0.0f, 0.0f };
	run.glyphOffsets = &offset;
	run.isSideways = FALSE;
	run.bidiLevel = 0;

	const DWRITE_MATRIX transform = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
	IDWriteGlyphRunAnalysis* analysis = nullptr;
	const DWRITE_RENDERING_MODE mode = (m_render_mode == 1) ? DWRITE_RENDERING_MODE_GDI_NATURAL : DWRITE_RENDERING_MODE_NATURAL;
	HRESULT hr = m_factory->CreateGlyphRunAnalysis(&run, 1.0f, &transform, mode,
		DWRITE_MEASURING_MODE_NATURAL, 0.0f, 0.0f, &analysis);
	if (FAILED(hr) || !analysis)
		return false;

	RECT rect = {};
	hr = analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &rect);
	if (FAILED(hr) || rect.right <= rect.left || rect.bottom <= rect.top) {
		analysis->Release();
		return false;
	}

	const int ink_w = rect.right - rect.left;
	const int ink_h = rect.bottom - rect.top;
	// 3 bytes per pixel (R/G/B subpixel coverage). Averaging them recovers an
	// anti-aliased grayscale mask, which thresholds to a cleaner outline than
	// the aliased 1x1 raster.
	std::vector<uint8_t> coverage((size_t)ink_w * ink_h * 3);
	hr = analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &rect, coverage.data(), (UINT32)coverage.size());
	analysis->Release();
	if (FAILED(hr))
		return false;

	// Same cell geometry as the stored 32-unit metrics, in fine raster pixels.
	const int cell = kCellSize * kRasterScale;
	const int pad_x = std::min(kCellPad * kRasterScale, std::max(0, (cell - ink_w) / 2));
	const int pad_y = std::min(kCellPad * kRasterScale, std::max(0, (cell - ink_h) / 2));
	const int fine_w = std::min(ink_w + pad_x * 2, cell);
	const int fine_h = std::min(ink_h + pad_y * 2, cell);
	const int width = fine_w / kRasterScale;
	const int height = fine_h / kRasterScale;
	if (width <= 0 || height <= 0)
		return false;

	std::vector<uint8_t> mask((size_t)fine_w * fine_h, 0);
	bool has_ink = false;
	for (int y = 0; y < ink_h; y++) {
		const int dy = y + pad_y;
		if (dy >= fine_h)
			break;
		for (int x = 0; x < ink_w; x++) {
			const int dx = x + pad_x;
			if (dx >= fine_w)
				break;
			const size_t base = ((size_t)y * ink_w + x) * 3;
			const int gray = (coverage[base] + coverage[base + 1] + coverage[base + 2]) / 3;
			if (gray >= 128) {
				mask[(size_t)dy * fine_w + dx] = 1;
				has_ink = true;
			}
		}
	}
	if (!has_ink)
		return false;

	// Distance field at the fine resolution, then a box average down to the
	// stored one: the field is linear between edges, so the mean of a
	// kRasterScale x kRasterScale block is its value at the block centre, which
	// is exactly the stored texel centre. This keeps the contour sub-texel
	// instead of snapping it back to the 64px em grid.
	std::vector<uint8_t> fine((size_t)fine_w * fine_h);
	buildSdf(mask, fine_w, fine_h, kSpreadPx * kRasterScale, fine.data());

	uint8_t* pixels = (uint8_t*)malloc((size_t)width * height);
	if (!pixels)
		return false;

	constexpr int samples = kRasterScale * kRasterScale;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int sum = 0;
			for (int sy = 0; sy < kRasterScale; sy++) {
				const uint8_t* row = &fine[(size_t)(y * kRasterScale + sy) * fine_w + x * kRasterScale];
				for (int sx = 0; sx < kRasterScale; sx++)
					sum += row[sx];
			}
			pixels[(size_t)y * width + x] = (uint8_t)((sum + samples / 2) / samples);
		}
	}

	// Full metrics, all derived from the fine raster.
	out.metrics.index = index;
	out.metrics.advance = (float)design.advanceWidth * m_scale / (float)kUnitScale;
	out.metrics.width = (float)width / (float)kUnitScale;
	out.metrics.height = (float)height / (float)kUnitScale;
	out.metrics.left = (float)(rect.left - pad_x) / (float)(kUnitScale * kRasterScale);
	out.metrics.top = (float)(rect.top - pad_y) / (float)(kUnitScale * kRasterScale);
	out.width = width;
	out.height = height;
	out.pixels = pixels;
	return true;
}

bool SdfFont::rasterize(wchar_t c, SdfGlyphBitmap& out)
{
	ensureThreadCom();
	return rasterizeGlyph(c, out);
}

}
