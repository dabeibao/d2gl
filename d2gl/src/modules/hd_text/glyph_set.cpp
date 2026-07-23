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
#include "glyph_set.h"
#include "graphic/command_buffer.h"
#include "helpers.h"
#include <log.h>
#include <sysinfoapi.h>

#include "page_load_pool.h"

namespace d2gl {

const int GlyphSet::s_texture_size = 1024;
int GlyphSet::s_atlas_size = 1024;
int GlyphSet::s_index_per_row = 1;

void GlyphSet::setAtlasSize(int size)
{
	s_atlas_size = size;
	s_index_per_row = 1024 / size;
}

int GlyphSet::getLayerCount(int png)
{
	auto div = s_texture_size / s_atlas_size;
	auto pngs_per_layer = div * div;
	return (png + pngs_per_layer - 1) / pngs_per_layer;
}

static Glyph parseGlyphFromCSV(const std::string& line, wchar_t& out_cc)
{
	auto cols = helpers::splitToVector(line);
	out_cc = (wchar_t)std::atoi(cols[1].c_str());
	glm::vec4 coords = { std::stof(cols[7]), std::stof(cols[8]), std::stof(cols[9]), std::stof(cols[10]) };
	glm::vec4 bounds = { std::stof(cols[3]), std::stof(cols[4]), std::stof(cols[5]), std::stof(cols[6]) };

	Glyph g;
	g.advance = std::stof(cols[2]) * 32.0f;
	g.size = { coords.z - coords.x, coords.w - coords.y };
	g.offset = { bounds.x * 32.0f, -bounds.w * 32.0f };
	g.tex_coord = coords / (float)GlyphSet::s_texture_size;
	return g;
}

GlyphSet::GlyphSet(Texture* texture, const std::string& name, GlyphSet* symbol_set)
	: m_symbols(symbol_set ? symbol_set->getGlyphes() : nullptr)
	, m_name(name)
	, m_texture(texture)
{
	auto buffer = helpers::loadFile("assets\\atlases\\" + name + "\\data.csv");
	if (!buffer.size)
		return;

	std::string data((const char*)buffer.data, buffer.size);
	auto lines = helpers::strToLines(data);
	delete[] buffer.data;

	int max_page = -1;
	for (auto& line : lines) {
		auto cols = helpers::splitToVector(line);
		int page = std::atoi(cols[0].c_str());
		wchar_t cc = (wchar_t)std::atoi(cols[1].c_str());
		m_glyph_page[cc] = (uint8_t)page;
		if (page > max_page) max_page = page;
	}

	if (max_page >= 0) {
		m_page_count = max_page + 1;
		m_page_states.resize(m_page_count, PageState::NOT_LOADED);
		m_page_glyphs.resize(m_page_count);

		for (auto& line : lines) {
			wchar_t cc;
			auto g = parseGlyphFromCSV(line, cc);
			int page = m_glyph_page[cc];
			m_page_glyphs[page].emplace_back(cc, g);
		}
	}
}

GlyphSet::~GlyphSet()
{
	std::unique_lock<std::mutex> lock(m_mutex);
	m_destroy_cv.wait(lock, [this] { return m_pending_tasks == 0; });

	for (auto& comp : m_completed)
		helpers::clearImage(comp.image);
}

void GlyphSet::initLoadPages(const std::vector<int>& pages)
{
	for (int page : pages) {
		if (page < 0 || page >= m_page_count)
			continue;

		m_page_states[page] = PageState::LOADING;

		auto image = helpers::loadImage("assets\\atlases\\" + m_name + "\\" + std::to_string(page) + ".png");
		auto tex_data = m_texture->fillImage(image);

		for (auto& [cc, g] : m_page_glyphs[page]) {
			g.tex_id = (uint16_t)tex_data.start_layer;
			m_glyphes[cc] = g;
		}

		helpers::clearImage(image);
		m_page_states[page] = PageState::LOADED;
	}
}

const Glyph* GlyphSet::getGlyph(wchar_t c)
{
	m_is_symbol = false;
	if (m_glyphes.find(c) != m_glyphes.end())
		return &m_glyphes[c];

	if (c == L'\xa0')
		return &m_glyphes[L' '];

	auto it = m_glyph_page.find(c);
	if (it != m_glyph_page.end()) {
		int page = it->second;
		if (m_page_states[page] == PageState::NOT_LOADED)
			loadPageAsync(page);

		if (m_page_states[page] != PageState::LOADED)
			return nullptr;
	}

	if (m_symbols) {
		m_is_symbol = true;
		if (m_symbols->find(c) != m_symbols->end())
			return &m_symbols->at(c);

		if (c > 0x20)
			return &m_symbols->at(L'☹');
	}

	if (c > 0x20)
		return &m_glyphes[L'?'];

	return nullptr;
}

void GlyphSet::loadPageAsync(int page_index)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_page_states[page_index] != PageState::NOT_LOADED)
			return;
		m_page_states[page_index] = PageState::LOADING;
		m_pending_tasks++;
	}

	std::string name = m_name;
	auto glyphs = m_page_glyphs[page_index];

	PageLoadPool::instance().submit([this, page_index, name = std::move(name), glyphs = std::move(glyphs)]() mutable {

		uint64_t start_ts = GetTickCount64();

		auto buffer = helpers::loadFile("assets\\atlases\\" + name + "\\" + std::to_string(page_index) + ".png");


		auto image = helpers::loadImageFromMemory(buffer.data, buffer.size);
		delete[] buffer.data;

		if (!image.data) {
			trace_log("[HDText] Lazy-load page %d failed", page_index);
			m_pending_tasks--;
			m_destroy_cv.notify_one();
			return;
		}

		CompletedPage completed;
		completed.page_index = page_index;
		completed.image = image;
		completed.start_ts = start_ts;

		for (auto& [cc, g] : glyphs)
			completed.glyphs.emplace_back(cc, g);

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_completed.push_back(std::move(completed));
		}

		m_pending_tasks--;
		m_destroy_cv.notify_one();
	});
}

void GlyphSet::pollCompletions(CommandBuffer* cmd_buf)
{
	std::vector<CompletedPage> pages;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		pages.swap(m_completed);
	}

	for (auto& comp : pages) {
		auto idx = m_texture->advanceNextIndex();
		auto layer = idx.layer;
		auto index = idx.index;

		auto x_offset = (index % s_index_per_row) * s_atlas_size;
		auto y_offset = (index / s_index_per_row) * s_atlas_size;
		auto x_coord_offset = x_offset * 1.0f / s_texture_size;
		auto y_coord_offset = y_offset * 1.0f / s_texture_size;

		for (auto& [cc, glyph] : comp.glyphs) {
			glyph.tex_id = (uint16_t)layer;
			glyph.tex_coord.x += x_coord_offset;
			glyph.tex_coord.y += y_coord_offset;
			glyph.tex_coord.z += x_coord_offset;
			glyph.tex_coord.w += y_coord_offset;
			m_glyphes[cc] = glyph;
		}

		trace_log("[HDText] page %d (%zu glyphs) took %lldms [%d.%d/%d %.1f%%]",
			comp.page_index, comp.glyphs.size(), GetTickCount64() - comp.start_ts,
			layer, index, m_texture->getLayerCount(),
			(layer + 1) * 100.0f / m_texture->getLayerCount());

		cmd_buf->pushFontPage(comp.image, layer, m_texture, x_offset, y_offset);
		m_page_states[comp.page_index] = PageState::LOADED;
	}
}

}
