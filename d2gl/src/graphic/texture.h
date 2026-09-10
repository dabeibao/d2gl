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

#include "types.h"
#include <algorithm>
#include <vector>

namespace d2gl {

class FrameBuffer;

struct TextureData {
	uint32_t start_layer = 0;
	glm::vec2 coord = { 1.0f, 1.0f };
};

struct TextureCreateInfo {
	uint32_t slot = 0;
	glm::uvec2 size = { 0, 0 };
	std::pair<GLint, GLint> filter = { GL_NEAREST, GL_NEAREST };
	uint32_t layer_count = 1;
	std::pair<GLint, GLenum> format = { GL_RGBA8, GL_RGBA };
	std::pair<GLint, GLint> wrap_mode = { GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE };
	bool mip_map = false;
	bool use_sparse = false;
	bool compressed = false;
};

class Texture {
	GLuint m_id = 0;
	GLint m_internal_format;
	GLenum m_format, m_target, m_type;
	uint32_t m_width, m_height, m_channel, m_layer_count, m_slot;
	uint32_t m_next_layer = 0;
	uint32_t m_next_layer_index = 0;
	uint32_t m_index_count = 1;
	bool m_sparse = false;
	uint32_t m_sparse_page_depth = 1;
	bool m_compressed = false;
	uint32_t m_committed_layers = 0;

public:
	struct LayerIndex {
		uint32_t layer;
		uint32_t index;
	};

	Texture(const TextureCreateInfo& info);
	~Texture();

	void bind(bool force = false);
	void bindImage(uint32_t unit = 0);

	void fill(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t offset_x = 0, uint32_t offset_y = 0, uint32_t layer = 0);
	void fillCompressed(const uint8_t* data, uint32_t width, uint32_t height, uint32_t offset_x = 0, uint32_t offset_y = 0, uint32_t layer = 0);
	void fillFromBuffer(const std::unique_ptr<FrameBuffer>& fbo, uint32_t index = 0);
	TextureData fillImage(ImageData image, uint32_t div_x = 1, uint32_t div_y = 1);
	void fillImages(std::vector<ImageData>& images);

	inline const GLuint getId() const { return m_id; };
	inline const uint32_t getSlot() const { return m_slot; };
	inline const uint32_t getWidth() const { return m_width; }
	inline const uint32_t getHeight() const { return m_height; }
	inline const uint32_t getNextLayer() const { return m_next_layer; }
	inline void advanceNextLayer() { m_next_layer++; }
	inline uint32_t getCommittedLayers() const { return m_committed_layers; }
	inline uint32_t getLayerCount() const { return m_layer_count; }
	inline uint32_t getLayerIndexCount() const { return m_index_count; }

	inline LayerIndex advanceNextIndex()
	{
		LayerIndex idx = { m_next_layer, m_next_layer_index};
		m_next_layer_index++;
		if (m_next_layer_index >= m_index_count) {
			m_next_layer++;
			m_next_layer_index = 0;
		}
		return idx;
	}

	inline void set_sub_index_size(int width)
	{
		auto div = m_width / width;
		m_index_count = div * div;
	}

private:
	void commitLayer(uint32_t layer);
	inline uint32_t alignToPageDepth(uint32_t layers) const
	{
		// Commit requests must be multiples of the driver's virtual page
		// depth, otherwise glTexPageCommitmentARB fails with GL_INVALID_VALUE.
		return std::min((layers + m_sparse_page_depth - 1) / m_sparse_page_depth * m_sparse_page_depth, m_layer_count);
	}
};

}
