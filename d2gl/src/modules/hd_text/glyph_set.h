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

#include <atomic>
#include <condition_variable>
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

enum class PageState : uint8_t { NOT_LOADED, LOADING, LOADED };

struct CompletedPage {
	int page_index;
	ImageData image;
	std::vector<std::pair<wchar_t, Glyph>> glyphs;
	uint64_t start_ts;
};

class GlyphSet {
	std::unordered_map<wchar_t, Glyph> m_glyphes;
	std::unordered_map<wchar_t, Glyph>* m_symbols = nullptr;
	bool m_is_symbol = false;

	std::string m_name;
	Texture* m_texture = nullptr;

	std::unordered_map<wchar_t, uint8_t> m_glyph_page;
	std::vector<PageState> m_page_states;
	std::vector<std::vector<std::pair<wchar_t, Glyph>>> m_page_glyphs;
	int m_page_count = 0;

	std::mutex m_mutex;
	std::condition_variable m_destroy_cv;
	std::vector<CompletedPage> m_completed;
	std::atomic<int> m_pending_tasks{ 0 };

public:
	GlyphSet(Texture* texture, const std::string& name, GlyphSet* symbol_set = nullptr);
	~GlyphSet();

	const Glyph* getGlyph(wchar_t c);
	inline bool isSymbol() { return m_is_symbol; }
	inline std::unordered_map<wchar_t, Glyph>* getGlyphes() { return &m_glyphes; }

	void initLoadPages(const std::vector<int>& pages);
	void loadPageAsync(int page_index);
	void pollCompletions(CommandBuffer* cmd_buf);

	static void setAtlasSize(int size);
	static int getLayerCount(int png);
	static const int s_texture_size;
	static int s_atlas_size;
	static int s_index_per_row;

private:
};

}
