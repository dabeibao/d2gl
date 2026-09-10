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

#include <vector>
#include "types.h"
#include "vertex.h"

#include "command_buffer.h"
#include "frame_buffer.h"
#include "object.h"
#include "pipeline.h"
#include "texture.h"
#include "uniform_buffer.h"

namespace d2gl {

#define MAX_FRAME_LATENCY 6 // Must be >= App.frame_latency + 1
#define MAX_INDICES 6 * 50000
#define MAX_VERTICES 4 * 50000
#define MAX_VERTICES_MOD 4 * 40000
#define PIXEL_BUFFER_SIZE 12 * 1024 * 1024
#define MAX_FRAMETIME_SAMPLE_COUNT 120

#define TEXTURE_SLOT_DEFAULT 0
#define TEXTURE_SLOT_GAME 1
#define TEXTURE_SLOT_POSTFX1 2
#define TEXTURE_SLOT_POSTFX2 3
#define TEXTURE_SLOT_PREFX 4
#define TEXTURE_SLOT_BLOOM1 5
#define TEXTURE_SLOT_BLOOM2 6

#define TEXTURE_SLOT_LUT 11
#define TEXTURE_SLOT_CURSOR 12
#define TEXTURE_SLOT_FONTS 13
#define TEXTURE_SLOT_MASK 14
#define TEXTURE_SLOT_MAP 15
#define TEXTURE_SLOT_EXTERNAL 16

#define TEXTURE_EXTERNAL_MAX_LAYER	128
#define TEXTURE_EXTERNAL_ATLAS_SIZE	512

#define TEXTURE_SLOT_ITEMLUT 17

#define IMAGE_UNIT_BLUR 0
#define IMAGE_UNIT_FXAA 1

template <typename T, size_t t_size>
struct Vertices {
	T* ptr = nullptr;
	uint32_t count = 0;
	uint32_t start = 0;
	size_t size = 0;
	std::vector<std::array<T, t_size>> data;

	void resize(size_t d_size)
	{
		size = d_size;
		data.resize(d_size);
	}
};

struct VertexParams {
	uint32_t color = 0;
	uint8_t tex_shift = 0;
	glm::vec<2, uint16_t> tex_ids = { 0, 0 };
	glm::vec<2, uint16_t> offsets = { 0, 0 };
	glm::vec<4, uint8_t> flags = { 0, 0, 0, 0 };
};

struct SharpenData {
	float strength = 0.405f;
	float clamp = 0.135f;
	float radius = 1.0f;
};

struct FrameMetrics {
	double frame_time = 0.0;
	double prev_time = 0.0;
	std::vector<double> frame_times;

	double total_frame_time = 0.0;
	double average_frame_time = 0.0;
	LARGE_INTEGER time = { 0 };
	double frequency = 0.0;

	uint32_t vertex_count = 0;
	uint32_t drawcall_count = 0;
	uint32_t frame_count = 0;
	uint32_t frame_sample_count = 0;
	uint32_t next_frame_index = 0;
};

struct LimiterMetrics {
	bool active = false;
	HANDLE timer = 0;
	LARGE_INTEGER due_time = { 0 };
	float frame_len_ms = 0.0f;
	uint64_t frame_len_ns = 0;
};

struct GLCaps {
	bool compute_shader = false;
	bool independent_blending = false;
};

class Context {
	HGLRC m_context = nullptr;
	HANDLE m_semaphore_cpu[MAX_FRAME_LATENCY];
	HANDLE m_semaphore_gpu[MAX_FRAME_LATENCY];
	// Sized to App.frame_latency + 1 (like the vertex arenas below): only
	// that many slots are ever cycled through, so pre-allocating all of
	// MAX_FRAME_LATENCY would waste one 12MB staging buffer per unused slot.
	std::vector<std::unique_ptr<CommandBuffer>> m_command_buffers;
	bool m_rendering = true;

	GLuint m_pixel_buffer;
	size_t m_pixel_buffer_size = PIXEL_BUFFER_SIZE;
	GLuint m_index_buffer;
	GLuint m_vertex_array;
	GLuint m_vertex_buffer;

	uint32_t m_frame_index = 0;

	bool m_delay_push = false;
	// Top pass: after the first HD object is pushed, subsequent game (SD)
	// content is routed into the overlay stream (m_vertices_mod) so HD and
	// post-HD SD content can be replayed in original draw order at viewport
	// resolution by the single overlay pass.
	bool m_top_pass_active = false;
	// Per-frame: a non-text HD object (e.g. an HD ground item) was pushed, so
	// post-HD world content is replayed in the overlay and can occlude it.
	// Damage-number text never sets this: it sits on top of monsters anyway, so
	// the whole-scene viewport-resolution replay is avoided when there is no HD
	// item to be occluded.
	bool m_world_hd_occludable = false;
	bool m_overlay_open = false;
	uint8_t m_overlay_open_kind = 0;  // 0 = SD, 1 = HD
	uint8_t m_overlay_open_blend = 0; // overlay pipeline attachment blend index
	uint32_t m_overlay_open_start = 0;
	Vertices<Vertex, MAX_VERTICES> m_vertices;
	Vertices<VertexMod, MAX_VERTICES_MOD> m_vertices_mod;
	Vertices<VertexMod, MAX_VERTICES_MOD> m_vertices_late;
	VertexParams m_vertex_params;

	FrameMetrics m_frame;
	LimiterMetrics m_limiter;

	std::unique_ptr<Texture> m_game_texture;
	std::unique_ptr<UniformBuffer> m_game_color_ubo;
	std::unique_ptr<Pipeline> m_game_pipeline;
	std::unique_ptr<FrameBuffer> m_game_framebuffer;
	std::unique_ptr<Pipeline> m_movie_pipeline;

	glm::vec3 m_sharpen_data;
	glm::uvec2 m_fxaa_work_size = { 0, 0 };
	std::unique_ptr<UniformBuffer> m_postfx_ubo;
	std::unique_ptr<Texture> m_postfx_texture;
	std::unique_ptr<FrameBuffer> m_postfx_framebuffer;
	std::unique_ptr<Pipeline> m_postfx_pipeline;
	std::unique_ptr<Pipeline> m_fxaa_compute_pipeline;

	std::unique_ptr<Pipeline> m_overlay_pipeline;
	int m_current_shader = -1;

	// Glide only
	std::unique_ptr<Texture> m_glide_texture;
	std::map<uint32_t, std::pair<uint32_t, BlendType>> m_blend_types;
	uint32_t m_current_blend_index = 0;
	bool m_blend_locked = false;

	glm::vec2 m_bloom_data;
	glm::uvec2 m_bloom_tex_size = { 0, 0 };
	glm::uvec2 m_bloom_work_size = { 0, 0 };
	std::unique_ptr<UniformBuffer> m_bloom_ubo;
	std::unique_ptr<Texture> m_bloom_texture;
	std::unique_ptr<FrameBuffer> m_bloom_framebuffer;
	std::unique_ptr<Pipeline> m_blur_compute_pipeline;

	std::unique_ptr<Texture> m_lut_texture;
	std::unique_ptr<Texture> m_prefx_texture;
	std::unique_ptr<Pipeline> m_prefx_pipeline;

public:
	std::unique_ptr<Texture> m_external_texture;
	std::unique_ptr<Texture> m_item_lut_texture;

	Context();
	~Context();

	static void renderThread(void* context);

	void onResize(glm::uvec2 w_size, glm::uvec2 g_size, uint32_t bpp = 8);
	void onShaderChange();
	void onStageChange();
	void setBlendState(uint32_t index);

	void beginFrame();
	void bindDefaultFrameBuffer();
	void presentFrame();

	inline uint32_t getFrameIndex() { return m_frame_index; }
	inline CommandBuffer* getCommandBuffer() { return m_command_buffers[m_frame_index].get(); }

	void setViewport(glm::ivec2 size, glm::ivec2 offset = { 0, 0 });
	inline void bindFrameBuffer(const std::unique_ptr<FrameBuffer>& framebuffer, bool clear = true) { framebuffer->bind(clear); }
	inline void bindPipeline(const std::unique_ptr<Pipeline>& pipeline, uint32_t index = 0) { pipeline->bind(index); }

	void pushVertex(const GlideVertex* vertex, glm::vec2 fix = { 0.0f, 0.0f }, glm::ivec2 offset = { 0, 0 });
	void flushVertices();
	void drawQuad(int8_t flag_x = 0, int8_t flag_y = 0, int16_t tex_id = 0);

	inline void toggleDelayPush(bool delay) { m_delay_push = delay; }
	void pushObject(const std::unique_ptr<Object>& object);
	void appendDelayedObjects();
	void ensureOverlayRun(uint8_t kind, uint8_t blend);
	void closeOverlayRun();
	void writeGameVertex(const GlideVertex* vertex, glm::vec2 fix, glm::ivec2 offset);
	void writeOverlayVertex(const GlideVertex* vertex, glm::vec2 fix, glm::ivec2 offset);

	inline void setVertexColor(uint32_t color) { m_vertex_params.color = color; }
	inline void setVertexTexShift(uint8_t shift) { m_vertex_params.tex_shift = shift; }
	inline void setVertexTexNum(glm::vec<2, uint16_t> tex_ids) { m_vertex_params.tex_ids = tex_ids; }
	inline void setVertexOffset(glm::vec<2, uint16_t> offsets) { m_vertex_params.offsets = offsets; }
	inline void setVertexFlagX(uint8_t flag) { m_vertex_params.flags.x = flag; }
	inline void setVertexFlagY(uint8_t flag) { m_vertex_params.flags.y = flag; }
	inline void setVertexFlagZ(uint8_t flag) { m_vertex_params.flags.z = flag; }
	inline void setVertexFlagW(uint8_t flag) { m_vertex_params.flags.w = flag; }

	inline const double getFrameTime() { return m_frame.frame_time; }
	inline const double getAvgFrameTime() { return m_frame.average_frame_time; }
	inline const uint32_t getFrameCount() { return m_frame.frame_count; }
	inline const uint32_t getVertexCount() { return m_frame.vertex_count; }
	inline const uint32_t getDrawCallCount() { return m_frame.drawcall_count; }

	void toggleVsync();
	void setFpsLimit(bool active, int max_fps);
	void takeScreenShot();
	void queueExternalTexUpload(uint32_t layer, const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t offset_x = 0, uint32_t offset_y = 0, bool compressed = false);

	void imguiStartFrame();
	void imguiRender();

	// Render-thread helpers (called by renderThread; kept separate so each
	// stats::Scope wraps a small body instead of a huge indented block).
	void processUploads(CommandBuffer* cmd, uint32_t frame_index);
	void processCommands(CommandBuffer* cmd, glm::ivec2 vp_size, glm::ivec2 vp_offset);
	void processPreFx(CommandBuffer* cmd, uint32_t index);
	void processSubmit(CommandBuffer* cmd, glm::ivec2 vp_size, glm::ivec2 vp_offset, uint32_t index);
	void drawOverlay(CommandBuffer* cmd, uint32_t frame_index);
	void uploadPixelBuffer(size_t size, const void* data);
	void createPreFxTargets();
	void createBloomTargets();
	void createPostfxTargets();

private:
	void resetFileTime();

	void imguiInit();
	void imguiDestroy();

public:
	static void APIENTRY debugMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* data);

	inline static std::unique_ptr<Context> createContext() { return std::make_unique<Context>(); }
	inline static std::unique_ptr<Texture> createTexture(const TextureCreateInfo& info) { return std::make_unique<Texture>(info); }
	inline static std::unique_ptr<Pipeline> createPipeline(const PipelineCreateInfo& info) { return std::make_unique<Pipeline>(info); }
	inline static std::unique_ptr<FrameBuffer> createFrameBuffer(const FrameBufferCreateInfo& info) { return std::make_unique<FrameBuffer>(info); }
	inline static std::unique_ptr<UniformBuffer> createUniformBuffer(const UniformBufferCreateInfo& info) { return std::make_unique<UniformBuffer>(info); }
};

}
