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
#include <log.h>
#include <cstdlib>

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

GlyphSet::GlyphSet(Texture* texture, const std::string& name, const std::string& font_path, int face_index, int render_mode,
	const std::string& ascii_path, int ascii_index)
	: m_name(name)
	, m_texture(texture)
	, m_sdf(std::make_unique<SdfFont>())
{
	m_sdf_slot_char.assign(kMaxGlyphs, 0);
	m_sdf_slot_use.assign(kMaxGlyphs, 0);
	m_sdf->load(font_path, face_index, render_mode);

	// Optional ASCII face. Same path as the main font means "single face", and
	// a face that fails to load is simply dropped so the main font covers the
	// whole range (missing characters fall back to '?' as usual).
	if (!ascii_path.empty() && (ascii_path != font_path || ascii_index != face_index)) {
		auto ascii = std::make_unique<SdfFont>();
		if (ascii->load(ascii_path, ascii_index, render_mode))
			m_sdf_ascii = std::move(ascii);
	}
}

GlyphSet::~GlyphSet()
{
	std::unique_lock<std::mutex> lock(m_mutex);
	m_destroy_cv.wait(lock, [this] { return m_pending_tasks == 0; });

	for (auto& comp : m_sdf_completed) {
		free(comp.bitmap.pixels);
		comp.bitmap.pixels = nullptr;
	}
}

const Glyph* GlyphSet::getGlyph(wchar_t c)
{
	// Normalize non-breaking space to regular space
	if (c == L'\xa0')
		c = L' ';

	return getSdfGlyph(c);
}

static uint32_t currentFrame()
{
	return App.context ? App.context->getFrameCount() : 0;
}

uint32_t GlyphSet::allocSdfSlot(wchar_t c)
{
	if (m_sdf_next_slot < kMaxGlyphs) {
		const uint32_t slot = m_sdf_next_slot++;
		m_sdf_slot_char[slot] = c;
		m_sdf_slot_use[slot] = currentFrame();
		return slot;
	}

	// Cache full: evict the least recently used glyph. Its node is kept but
	// invalidated so a Glyph* held by the game thread stays valid. The scan runs
	// over flat arrays (slot -> char / frame) instead of the hash maps.
	uint32_t victim_slot = UINT32_MAX;
	uint32_t victim_tick = UINT32_MAX;
	for (uint32_t slot = 0; slot < kMaxGlyphs; slot++) {
		if (!m_sdf_slot_char[slot])
			continue;
		if (m_sdf_slot_use[slot] < victim_tick) {
			victim_tick = m_sdf_slot_use[slot];
			victim_slot = slot;
		}
	}
	if (victim_slot == UINT32_MAX)
		return UINT32_MAX;

	const wchar_t victim = m_sdf_slot_char[victim_slot];
	m_sdf_slot_char[victim_slot] = c;
	m_sdf_slot_use[victim_slot] = currentFrame();
	m_sdf_slot.erase(victim);
	m_sdf_queued.erase(victim);
	const auto glyph_it = m_glyphes.find(victim);
	if (glyph_it != m_glyphes.end())
		glyph_it->second.tex_id = 0xFFFF;
	return victim_slot;
}

void GlyphSet::touchSdfSlot(wchar_t c, uint32_t frame)
{
	const auto it = m_sdf_slot.find(c);
	if (it != m_sdf_slot.end())
		m_sdf_slot_use[it->second] = frame;
}

Glyph* GlyphSet::getSdfGlyph(wchar_t c)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const uint32_t frame = currentFrame();

	auto queue_raster = [this, c]() {
		if (m_sdf_queued.find(c) != m_sdf_queued.end())
			return;
		m_sdf_queued[c] = 1;
		m_pending_tasks++;
		PageLoadPool::instance().submit([this, c]() {
			SdfGlyphBitmap bitmap;
			if (pickSdf(c)->rasterize(c, bitmap)) {
				std::lock_guard<std::mutex> lock(m_mutex);
				CompletedSdfGlyph completed;
				completed.cc = c;
				completed.bitmap = bitmap;
				m_sdf_completed.push_back(std::move(completed));
			}
			m_pending_tasks--;
			m_destroy_cv.notify_one();
		});
	};

	auto it = m_glyphes.find(c);
	if (it != m_glyphes.end()) {
		Glyph& glyph = it->second;
		touchSdfSlot(c, frame);
		// Evicted by LRU: rasterize it again. The ink box (size) is only filled
		// once the bitmap arrives, so use the advance as the "known glyph" flag.
		if (glyph.tex_id == 0xFFFF && c != L' ' && glyph.advance > 0.0f)
			queue_raster();
		return &glyph;
	}

	if (c < 0x20) {
		Glyph dummy = {};
		dummy.tex_id = 0xFFFF;
		return &m_glyphes.emplace(c, dummy).first->second;
	}

	// Advance only: the ink box arrives with the bitmap (see pollCompletions), so
	// a new string never blocks the game thread on a DWrite layout analysis.
	const SdfGlyphMetrics* metrics = pickSdf(c)->getAdvance(c);
	Glyph glyph = {};
	glyph.tex_id = 0xFFFF;

	if (!metrics || metrics->index == 0)
		return &m_glyphes.emplace(c, glyph).first->second;

	glyph.advance = metrics->advance;
	// size/offset stay empty until the bitmap is uploaded; drawChar skips the
	// glyph while tex_id == 0xFFFF, so it is never drawn half-initialised.
	auto& stored = m_glyphes.emplace(c, glyph).first->second;
	touchSdfSlot(c, frame);

	if (c != L' ')
		queue_raster();

	return &stored;
}

void GlyphSet::pollCompletions(CommandBuffer* cmd_buf)
{
	if (!m_sdf)
		return;

	std::lock_guard<std::mutex> lock(m_mutex);
	std::vector<CompletedSdfGlyph> glyphs;
	glyphs.swap(m_sdf_completed);

	// Atlas grid for the current cell size (setAtlasSize keeps
	// s_index_per_row == s_texture_size / s_atlas_size).
	const uint32_t slots_per_layer = (uint32_t)(s_index_per_row * s_index_per_row);

	for (auto& comp : glyphs) {
		auto it = m_glyphes.find(comp.cc);
		if (it == m_glyphes.end() || it->second.tex_id != 0xFFFF) {
			free(comp.bitmap.pixels);
			continue;
		}

		const uint32_t slot = allocSdfSlot(comp.cc);
		if (slot == UINT32_MAX) {
			error_log("[HDText] SDF cache full, glyph U+%04X dropped", (uint32_t)comp.cc);
			free(comp.bitmap.pixels);
			continue;
		}

		const uint32_t layer = slot / slots_per_layer;
		const uint32_t index = slot % slots_per_layer;
		if (layer >= m_texture->getLayerCount()) {
			error_log("[HDText] SDF atlas layer out of range, glyph U+%04X dropped", (uint32_t)comp.cc);
			free(comp.bitmap.pixels);
			continue;
		}

		const uint32_t cell_x = (index % (uint32_t)s_index_per_row) * (uint32_t)s_atlas_size;
		const uint32_t cell_y = (index / (uint32_t)s_index_per_row) * (uint32_t)s_atlas_size;
		const float inv_size = 1.0f / (float)s_texture_size;

		Glyph& glyph = it->second;
		glyph.tex_id = (uint16_t)layer;
		// Object::setTexCoord maps the quad's top edge to tex_coord.w and
		// the bottom edge to tex_coord.y; our SDF rows are uploaded top-down.
		glyph.tex_coord = {
			(float)cell_x * inv_size,
			(float)(cell_y + comp.bitmap.height) * inv_size,
			(float)(cell_x + comp.bitmap.width) * inv_size,
			(float)cell_y * inv_size
		};
		// Quad size stays in 32-em units; the bitmap itself is at 64em.
		// The ink box only exists now that the bitmap is ready, so the glyph
		// offset (top bearing) is filled in here as well.
		glyph.size = { comp.bitmap.metrics.width, comp.bitmap.metrics.height };
		glyph.offset = { comp.bitmap.metrics.left, comp.bitmap.metrics.top };

		m_sdf_slot[comp.cc] = slot;
		m_sdf_slot_use[slot] = currentFrame();

		ImageData image = { comp.bitmap.width, comp.bitmap.height, 1, comp.bitmap.pixels };
		cmd_buf->pushFontPage(image, layer, m_texture, cell_x, cell_y);
	}
}

}
