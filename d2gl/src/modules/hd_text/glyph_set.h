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

#include "sdf_font.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>

namespace d2gl {

struct Glyph {
	glm::vec2 size = { 0.0f, 0.0f };
	glm::vec2 offset = { 0.0f, 0.0f };
	float advance = 0.0f;
	uint16_t tex_id = 0;
	glm::vec4 tex_coord = { 0.0f, 0.0f, 0.0f, 0.0f };
};

struct CompletedSdfGlyph {
	wchar_t cc;
	SdfGlyphBitmap bitmap;
};

class GlyphSet {
	std::unordered_map<wchar_t, Glyph> m_glyphes;

	std::string m_name;
	Texture* m_texture = nullptr;

	std::mutex m_mutex;
	std::condition_variable m_destroy_cv;
	std::atomic<int> m_pending_tasks{ 0 };

	// Runtime SDF mode (DirectWrite). Glyphs live in a fixed 64px-em atlas and
	// are evicted LRU when full. Nodes are never erased, only invalidated: the
	// game thread can still hold a Glyph*.
	// Slots per layer are not hardcoded: they follow the atlas cell,
	// (s_texture_size / s_atlas_size)^2 == s_index_per_row^2.
	// Codepoints below this use the optional ASCII face; everything else keeps
	// the main face. U+0100 and above is CJK / symbols in D2's wchar text.
	static constexpr uint32_t kAsciiFaceLimit = 0x100;

	std::unique_ptr<SdfFont> m_sdf;
	std::unique_ptr<SdfFont> m_sdf_ascii;
	std::vector<CompletedSdfGlyph> m_sdf_completed;
	std::unordered_map<wchar_t, uint8_t> m_sdf_queued;
	std::unordered_map<wchar_t, uint32_t> m_sdf_slot;
	// Slot -> owner char (0 == free) and slot -> last frame it was used. Flat
	// arrays keep the LRU scan cache friendly: scanning the hash maps cost
	// ~200us per eviction at 4096 entries, i.e. milliseconds for a burst of new
	// glyphs once the cache is full.
	std::vector<wchar_t> m_sdf_slot_char;
	std::vector<uint32_t> m_sdf_slot_use;
	uint32_t m_sdf_next_slot = 0;

	Glyph* getSdfGlyph(wchar_t c);
	uint32_t allocSdfSlot(wchar_t c);
	void touchSdfSlot(wchar_t c, uint32_t frame);
	// Face selection per codepoint. The primary face is mandatory; the ASCII
	// face is optional and immutable after construction, so this is safe to
	// call from the raster worker thread.
	SdfFont* pickSdf(wchar_t c) { return (m_sdf_ascii && (uint32_t)c < kAsciiFaceLimit) ? m_sdf_ascii.get() : m_sdf.get(); }
public:
	// Glyph cache budget (LRU). 4096 slots x 16 KiB = 64 MiB worst case; the
	// texture layer count is derived from it (see HDText::setupRuntimeFonts).
	static constexpr uint32_t kMaxGlyphs = 4096;

	GlyphSet(Texture* texture, const std::string& name, const std::string& font_path, int face_index, int render_mode = 0,
		const std::string& ascii_path = std::string(), int ascii_index = 0);
	~GlyphSet();

	const Glyph* getGlyph(wchar_t c);
	inline bool isSdfLoaded() { return m_sdf && m_sdf->isLoaded(); }
	// True when this codepoint is drawn by the optional ASCII face (so the
	// caller can apply its own weight bias).
	inline bool isAsciiChar(wchar_t c) { return m_sdf_ascii && (uint32_t)c < kAsciiFaceLimit; }

	void pollCompletions(CommandBuffer* cmd_buf);

	static void setAtlasSize(int size);
	static const int s_texture_size;
	static int s_atlas_size;
	static int s_index_per_row;

private:
};

}
