#include "pch.h"
#include "command_buffer.h"
#include "d2/common.h"
#include "helpers.h"

namespace d2gl {

CommandBuffer::CommandBuffer()
{
	m_tex_buffer = new uint8_t[PIXEL_BUFFER_SIZE];
	reset();
}

CommandBuffer::~CommandBuffer()
{
	for (auto& upload : m_font_page_uploads) {
		ImageData img = { (int)upload.width, (int)upload.height, 4, upload.pixels };
		helpers::clearImage(img);
	}
	for (auto& upload : m_external_tex_uploads)
		delete[] upload.pixels;
	delete[] m_tex_buffer;
}

void CommandBuffer::reset()
{
	m_count = 0;
	m_command = &m_commands[0];
	m_ubo_update_queue.count = 0;
	m_tex_update_queue.count = 0;
	m_tex_update_queue.data_offset = 0;
	m_vertex_count = 0;
	m_vertex_mod_count = 0;
	m_tex_update.bit = 0;

	for (auto& upload : m_font_page_uploads) {
		ImageData img = { (int)upload.width, (int)upload.height, 4, upload.pixels };
		helpers::clearImage(img);
	}
	m_font_page_uploads.clear();

	for (auto& upload : m_external_tex_uploads)
		delete[] upload.pixels;
	m_external_tex_uploads.clear();

	m_screen = App.game.screen;
	m_window_size = App.window.size;
	m_game_size = App.game.size;
	m_resized = false;
}

inline void CommandBuffer::next()
{
	m_command++;
	m_count++;
}

void CommandBuffer::pushCommand(CommandType type, uint32_t index)
{
	m_command->type = type;
	m_command->index = index;
	next();
}

void CommandBuffer::drawIndexed(uint32_t start, uint32_t count)
{
	m_vertex_count += count;
	m_command->type = CommandType::DrawIndexed;
	m_command->draw.start = start;
	m_command->draw.count = count / 4 * 6;
	next();
}

void CommandBuffer::drawExternalRange(uint32_t start, uint32_t count)
{
	m_command->type = CommandType::DrawExternal;
	m_command->draw.start = start;
	m_command->draw.count = count;
	next();
}

void CommandBuffer::resize()
{
	m_resized = true;
	m_game_size = App.game.size;
	m_game_tex_bpp = App.game.bpp;
	m_window_size = App.window.size;
}

void CommandBuffer::colorUpdate(UBOType type, const void* data)
{
	memcpy(m_ubo_update_queue.data[m_ubo_update_queue.count].value, data, sizeof(glm::vec4) * 256);
	m_ubo_update_queue.data[m_ubo_update_queue.count].type = type;

	m_command->type = CommandType::UBOUpdate;
	m_command->index = m_ubo_update_queue.count++;
	next();
}

void CommandBuffer::textureUpdate(uint8_t* data, uint16_t tex_num, glm::vec<2, uint16_t> size, glm::vec<2, uint16_t> offset)
{
	const uint32_t data_size = size.x * size.y;
	memcpy((void*)((uint32_t)m_tex_buffer + m_tex_update_queue.data_offset), data, data_size);

	auto tex_data = &m_tex_update_queue.tex_data[m_tex_update_queue.count];
	tex_data->offset = m_tex_update_queue.data_offset;
	tex_data->tex_num = tex_num;
	tex_data->tex_size = size;
	tex_data->tex_offset = offset;

	m_tex_update_queue.data_offset += data_size;
	m_tex_update_queue.count++;
}

void CommandBuffer::gameTextureUpdate(uint8_t* data, glm::vec<2, uint16_t> size, uint32_t bit)
{
	memcpy(m_tex_buffer, data, size.x * size.y * bit);
	m_tex_update.bit = bit;
	m_tex_update.size = size;
}

void CommandBuffer::setHDTextMasking(bool masking, glm::vec4 metrics)
{
	m_hd_text_mask.active = true;
	m_hd_text_mask.masking = masking;
	m_hd_text_mask.metrics = metrics;
}

void CommandBuffer::pushFontPage(ImageData& image, uint32_t layer, Texture* texture, uint32_t x, uint32_t y)
{
	FontPageUpload upload;
	upload.pixels = image.data;
	upload.x = x;
	upload.y = y;
	upload.width = image.width;
	upload.height = image.height;
	upload.layer = layer;
	upload.texture = texture;
	m_font_page_uploads.push_back(upload);
	image.data = nullptr;
}

}
