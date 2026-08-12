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
#include "context.h"
#include <algorithm>
#include <cmath>
#include "d2/common.h"
#include "d2/item_color_lut.h"
#include "helpers.h"
#include "modules/hd_cursor.h"
#include "modules/hd_text.h"
#include "modules/mini_map.h"
#include "modules/motion_prediction.h"
#include "modules/stats.h"
#include "option/menu.h"
#include "upscaler.h"
#include "win32.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_opengl3.h>
#include <imgui/imgui_impl_win32.h>

namespace d2gl {

#ifdef _STATS
// Sampled timing state for Context::pushVertex (hot path, game thread only).
static stats::HotTimer g_push_timer(512, stats::TIMER_CPU_HOT_PUSH);
#endif

Context::Context()
{
	stats::init();

	PIXELFORMATDESCRIPTOR pfd;
	memset(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR));
	pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER | PFD_SUPPORT_OPENGL | PFD_GENERIC_ACCELERATED;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 24;
	pfd.cStencilBits = 8;
	pfd.iLayerType = PFD_MAIN_PLANE;

	SetPixelFormat(App.hdc, ChoosePixelFormat(App.hdc, &pfd), &pfd);

	HGLRC context = wglCreateContext(App.hdc);
	wglMakeCurrent(App.hdc, context);

	if (glewInit() != GLEW_OK) {
		MessageBoxA(NULL, "OpenGL loader failed!", "OpenGL failed!", MB_OK);
		exit(1);
	}

	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(context);

	std::vector<glm::vec<2, uint8_t>> versions = { { 4, 6 }, { 4, 5 }, { 4, 4 }, { 4, 3 }, { 4, 2 }, { 4, 1 }, { 4, 0 }, { 3, 3 } };
	for (auto& version : versions) {
		if (App.gl_ver.x < version.x)
			continue;
		if (App.gl_ver.y < version.y)
			continue;

		int attribs[] = {
			WGL_CONTEXT_MAJOR_VERSION_ARB, version.x,
			WGL_CONTEXT_MINOR_VERSION_ARB, version.y,
			WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
			WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0
		};
		if (App.debug)
			attribs[5] |= WGL_CONTEXT_DEBUG_BIT_ARB;

		if (m_context = wglCreateContextAttribsARB(App.hdc, 0, attribs)) {
			App.gl_ver = version;
			break;
		}
	}

	if (!m_context) {
		MessageBoxA(App.hwnd, "Requires OpenGL 3.3 or newer!", "Unsupported OpenGL version!", MB_OK | MB_ICONERROR);
		error_log("Requires OpenGL 3.3 or newer! exiting.");
		exit(1);
	}

	wglMakeCurrent(App.hdc, m_context);
	glewInit();

	GLint major_version, minor_version;
	glGetIntegerv(GL_MAJOR_VERSION, &major_version);
	glGetIntegerv(GL_MINOR_VERSION, &minor_version);

	char version_str[50] = { 0 };
	sprintf_s(version_str, "%d.%d", major_version, minor_version);
	trace_log("OpenGL: %s (%s | %s)", version_str, glGetString(GL_RENDERER), glGetString(GL_VENDOR));
	trace_log("OpenGL: Shading Language: %s", version_str, glGetString(GL_SHADING_LANGUAGE_VERSION));
	App.gl_ver_str = version_str;

	GLint max_texture_unit;
	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_texture_unit);
	trace_log("OpenGL: GL_MAX_TEXTURE_IMAGE_UNITS = %d", max_texture_unit);

	if ((App.debug || App.log) && glewIsSupported("GL_KHR_debug")) {
		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(Context::debugMessageCallback, nullptr);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
		trace_log("OpenGL: GL_KHR_debug enabled!");
	}

	if (App.use_compute_shader && (glewIsSupported("GL_VERSION_4_3") || glewIsSupported("GL_ARB_compute_shader"))) {
		App.gl_caps.compute_shader = true;
		trace_log("OpenGL: Compute shader available.");
	}

	if (glewIsSupported("GL_VERSION_4_0")) {
		App.gl_caps.independent_blending = true;
		trace_log("OpenGL: Independent blending available.");
	}

	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);

	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);

	uint32_t offset = 0;
	uint32_t* indices = new uint32_t[MAX_INDICES];
	for (size_t i = 0; i < MAX_INDICES; i += 6) {
		indices[i + 0] = offset + 0;
		indices[i + 1] = offset + 1;
		indices[i + 2] = offset + 2;
		indices[i + 3] = offset + 2;
		indices[i + 4] = offset + 3;
		indices[i + 5] = offset + 0;
		offset += 4;
	}

	glGenVertexArrays(1, &m_vertex_array);
	glBindVertexArray(m_vertex_array);

	glGenBuffers(1, &m_index_buffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_index_buffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices[0]) * MAX_INDICES, indices, GL_STATIC_DRAW);
	delete[] indices;

	glGenBuffers(1, &m_vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * MAX_VERTICES, NULL, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glGenBuffers(1, &m_pixel_buffer);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pixel_buffer);
	glBufferData(GL_PIXEL_UNPACK_BUFFER, PIXEL_BUFFER_SIZE, NULL, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

	m_vertices.resize(App.frame_latency + 1);
	m_vertices_mod.resize(App.frame_latency + 1);
	m_vertices_late.resize(1);

	imguiInit();

	PipelineCreateInfo movie_pipeline_ci = { "movie" };
	movie_pipeline_ci.shader = g_shader_movie;
	movie_pipeline_ci.bindings = { { BindingType::Texture, "u_Texture", TEXTURE_SLOT_DEFAULT, &m_game_texture } };
	m_movie_pipeline = Context::createPipeline(movie_pipeline_ci);

	UniformBufferCreateInfo postfx_ubo_ci;
	postfx_ubo_ci.variables = { { "sharpen", sizeof(glm::vec4) }, { "rel_size", sizeof(glm::vec2) } };
	m_postfx_ubo = Context::createUniformBuffer(postfx_ubo_ci);

	m_sharpen_data = { App.sharpen.strength.value, App.sharpen.clamp.value, App.sharpen.radius.value };
	m_postfx_ubo->updateDataVec4f("sharpen", glm::vec4(m_sharpen_data, 1.0f));

	PipelineCreateInfo postfx_pipeline_ci = { "postfx" };
	postfx_pipeline_ci.shader = g_shader_postfx;
	postfx_pipeline_ci.bindings = {
		{ BindingType::UniformBuffer, "ubo_Metrics", m_postfx_ubo->getBinding() },
		{ BindingType::FBTexture, "u_Texture0", TEXTURE_SLOT_POSTFX1, &m_postfx_framebuffer },
		{ BindingType::Texture, "u_Texture1", TEXTURE_SLOT_POSTFX2, &m_postfx_texture },
	};
	m_postfx_pipeline = Context::createPipeline(postfx_pipeline_ci);
	m_postfx_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f));

	if (App.gl_caps.compute_shader) {
		PipelineCreateInfo fxaa_pipeline_ci = { "compute fxaa" };
		fxaa_pipeline_ci.shader = g_shader_postfx;
		fxaa_pipeline_ci.version = { 4, 3 };
		fxaa_pipeline_ci.bindings = {
			{ BindingType::Texture, "u_InTexture", TEXTURE_SLOT_POSTFX2, &m_postfx_texture },
			{ BindingType::Image, "u_OutTexture", IMAGE_UNIT_FXAA },
		};
		fxaa_pipeline_ci.compute = true;
		m_fxaa_compute_pipeline = Context::createPipeline(fxaa_pipeline_ci);
	}

	// 3D Color LUT: 6 families x 21 tints, 126 layers of 1024x32 atlas RGB8
	{
		d2::ItemColorLut::Instance().init();

		TextureCreateInfo item_lut_ci;
		item_lut_ci.size = { LUT_ATLAS_WIDTH, LUT_ATLAS_HEIGHT };
		item_lut_ci.layer_count = LUT_LAYER_COUNT;
		item_lut_ci.slot = TEXTURE_SLOT_ITEMLUT;
		item_lut_ci.filter = { GL_LINEAR, GL_LINEAR };
		item_lut_ci.format = { GL_RGB8, GL_RGB };
		m_item_lut_texture = Context::createTexture(item_lut_ci);

		const uint8_t* lut_data = d2::ItemColorLut::Instance().getLutData();
		uint32_t layer_size = LUT_ATLAS_WIDTH * LUT_ATLAS_HEIGHT * 3;
		for (uint32_t i = 0; i < LUT_LAYER_COUNT; i++) {
			m_item_lut_texture->fill(lut_data + i * layer_size,
				LUT_ATLAS_WIDTH, LUT_ATLAS_HEIGHT, 0, 0, i);
		}
	}

	if (ISGLIDE3X()) {
		TextureCreateInfo glide_texture_ci;
		glide_texture_ci.size = { 512, 512 };
		glide_texture_ci.layer_count = 512;
		glide_texture_ci.format = { GL_R8, GL_RED };
		m_glide_texture = std::make_unique<Texture>(glide_texture_ci);

		TextureCreateInfo movie_texture_ci;
		movie_texture_ci.size = { 640, 480 };
		movie_texture_ci.filter = { GL_LINEAR, GL_LINEAR };
		movie_texture_ci.format = { GL_RGBA8, GL_BGRA };
		m_game_texture = Context::createTexture(movie_texture_ci);

		UniformBufferCreateInfo game_ubo_ci;
		game_ubo_ci.variables = { { "palette", 256 * sizeof(glm::vec4) }, { "gamma", 256 * sizeof(glm::vec4) } };
		m_game_color_ubo = Context::createUniformBuffer(game_ubo_ci);

		PipelineCreateInfo game_pipeline_ci = { "glide" };
		game_pipeline_ci.version = { 3, 3 };
		game_pipeline_ci.shader = g_shader_glide;
		game_pipeline_ci.bindings = {
			{ BindingType::UniformBuffer, "ubo_Colors", m_game_color_ubo->getBinding() },
			{ BindingType::Texture, "u_Texture", TEXTURE_SLOT_DEFAULT, &m_glide_texture },
		};
		game_pipeline_ci.attachment_blends.clear();
		for (auto& blend : g_blend_types)
			game_pipeline_ci.attachment_blends.push_back({ blend.second.second, BlendType::SAlpha_OneMinusSAlpha, BlendType::SAlpha_OneMinusSAlpha });
		m_game_pipeline = Context::createPipeline(game_pipeline_ci);

		TextureCreateInfo lut_texture_ci;
		lut_texture_ci.size = { 1024, 32 };
		lut_texture_ci.layer_count = 14;
		lut_texture_ci.slot = TEXTURE_SLOT_LUT;
		m_lut_texture = Context::createTexture(lut_texture_ci);

		auto image_data = helpers::loadImage("assets\\textures\\lut.png", false);
		m_lut_texture->fillImage(image_data, 1, 14);
		helpers::clearImage(image_data);

		if (App.gl_caps.compute_shader) {
			PipelineCreateInfo blur_pipeline_ci = { "compute blur" };
			blur_pipeline_ci.shader = g_shader_prefx;
			blur_pipeline_ci.version = { 4, 3 };
			blur_pipeline_ci.bindings = {
				{ BindingType::Texture, "u_InTexture", TEXTURE_SLOT_BLOOM2, &m_bloom_texture },
				{ BindingType::Image, "u_OutTexture", IMAGE_UNIT_BLUR },
			};
			blur_pipeline_ci.compute = true;
			m_blur_compute_pipeline = Context::createPipeline(blur_pipeline_ci);
		}

		UniformBufferCreateInfo bloom_ubo_ci;
		bloom_ubo_ci.variables = { { "bloom", sizeof(glm::vec2) }, { "rel_size", sizeof(glm::vec2) } };
		m_bloom_ubo = Context::createUniformBuffer(bloom_ubo_ci);

		m_bloom_data = { App.bloom.exposure.value, App.bloom.gamma.value };
		m_bloom_ubo->updateDataVec2f("bloom", m_bloom_data);

		PipelineCreateInfo prefx_pipeline_ci = { "prefx" };
		prefx_pipeline_ci.shader = g_shader_prefx;
		prefx_pipeline_ci.bindings = {
			{ BindingType::UniformBuffer, "ubo_Metrics", m_bloom_ubo->getBinding() },
			{ BindingType::Texture, "u_Texture", TEXTURE_SLOT_PREFX, &m_prefx_texture },
			{ BindingType::FBTexture, "u_BloomTexture1", TEXTURE_SLOT_BLOOM1, &m_bloom_framebuffer },
			{ BindingType::Texture, "u_BloomTexture2", TEXTURE_SLOT_BLOOM2, &m_bloom_texture },
			{ BindingType::Texture, "u_LUTTexture", m_lut_texture->getSlot(), &m_lut_texture },
		};
		m_prefx_pipeline = Context::createPipeline(prefx_pipeline_ci);
		m_prefx_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f));
	} else {
		UniformBufferCreateInfo ubo_ci;
		ubo_ci.variables = { { "palette", 256 * sizeof(glm::vec4) } };
		m_game_color_ubo = Context::createUniformBuffer(ubo_ci);

		PipelineCreateInfo game_pipeline_ci = { "ddraw" };
		game_pipeline_ci.shader = g_shader_ddraw;
		game_pipeline_ci.bindings = {
			{ BindingType::UniformBuffer, "ubo_Colors", m_game_color_ubo->getBinding() },
			{ BindingType::Texture, "u_Texture", TEXTURE_SLOT_DEFAULT, &m_game_texture },
		};
		m_game_pipeline = Context::createPipeline(game_pipeline_ci);
		m_game_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f, 1.0f, 1.0f, -1.0f));
	}

	// Overlay (uber) pipeline: single program for the whole overlay pass. HD
	// objects and post-HD game content share one vertex format and one buffer,
	// drawn in original order at viewport resolution. The extra sentinel picks
	// the SD (glide palette/gamma) or HD branch; blend indices 1-4 map to the
	// four game blend modes, index 0 is the HD alpha blend.
	PipelineCreateInfo overlay_pipeline_ci = { "overlay" };
	overlay_pipeline_ci.shader = g_shader_mod;
	overlay_pipeline_ci.attachment_blends = {
		{ BlendType::SAlpha_OneMinusSAlpha }, // 0: HD
		{ BlendType::One_Zero },              // 1: game blend 0
		{ BlendType::Zero_SColor },           // 2: game blend 1
		{ BlendType::One_One },               // 3: game blend 2
		{ BlendType::SAlpha_OneMinusSAlpha }, // 4: game blend 3
	};
	overlay_pipeline_ci.bindings = {
		{ BindingType::UniformBuffer, "ubo_Colors", m_game_color_ubo->getBinding() },
		{ BindingType::Texture, "u_CursorTexture", TEXTURE_SLOT_CURSOR },
		{ BindingType::Texture, "u_FontTexture", TEXTURE_SLOT_FONTS },
		{ BindingType::Texture, "u_ExternalTexture", TEXTURE_SLOT_EXTERNAL },
		{ BindingType::Texture, "u_ItemColorLUT", TEXTURE_SLOT_ITEMLUT, &m_item_lut_texture },
	};
	if (ISGLIDE3X()) {
		overlay_pipeline_ci.bindings.push_back({ BindingType::Texture, "u_Texture", TEXTURE_SLOT_DEFAULT, &m_glide_texture });
		overlay_pipeline_ci.bindings.push_back({ BindingType::FBTexture, "u_MapTexture", TEXTURE_SLOT_MAP, &m_game_framebuffer, 1 });
		overlay_pipeline_ci.bindings.push_back({ BindingType::FBTexture, "u_MaskTexture", TEXTURE_SLOT_MASK, &m_game_framebuffer, 2 });
	}
	m_overlay_pipeline = Context::createPipeline(overlay_pipeline_ci);
	m_overlay_pipeline->setUniform1i("u_IsGlide", ISGLIDE3X());

	TextureCreateInfo external_tex_ci;
	external_tex_ci.size = { TEXTURE_EXTERNAL_ATLAS_SIZE, TEXTURE_EXTERNAL_ATLAS_SIZE };
	external_tex_ci.layer_count = TEXTURE_EXTERNAL_MAX_LAYER;
	external_tex_ci.slot = TEXTURE_SLOT_EXTERNAL;
	external_tex_ci.filter = { GL_NEAREST, GL_NEAREST };
	external_tex_ci.use_sparse = true;
	external_tex_ci.format = { GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT };
	external_tex_ci.compressed = true;
	m_external_texture = Context::createTexture(external_tex_ci);

	onResize(App.window.size, App.game.size);

	LARGE_INTEGER qpf;
	QueryPerformanceFrequency(&qpf);
	m_frame.frequency = double(qpf.QuadPart) / 1000.0;
	m_frame.frame_times.assign(MAX_FRAMETIME_SAMPLE_COUNT, m_frame.frame_time);

	// Seed prev_time so the very first presentFrame() reports a real frame time
	// instead of `QPC_now - 0` (a multi-billion ms artifact that pollutes the
	// rolling average and the stats CSV).
	LARGE_INTEGER qpc_first;
	QueryPerformanceCounter(&qpc_first);
	m_frame.prev_time = double(qpc_first.QuadPart) / m_frame.frequency;

	m_limiter.timer = CreateWaitableTimer(NULL, TRUE, NULL);
	setFpsLimit(!App.vsync && App.foreground_fps.active, App.foreground_fps.range.value);

	m_vertices_mod.count = 0;
	m_vertices_mod.ptr = m_vertices_mod.data[m_frame_index].data();

	m_frame.vertex_count = 0;
	m_frame.drawcall_count = 0;

	for (uint32_t i = 0; i < MAX_FRAME_LATENCY; i++) {
		m_semaphore_cpu[i] = CreateSemaphore(NULL, 0, 1, NULL);
		m_semaphore_gpu[i] = CreateSemaphore(NULL, 0, 1, NULL);
		ReleaseSemaphore(m_semaphore_gpu[i], 1, NULL);
	}

	modules::HDText::Instance();
	modules::HDCursor::Instance();

	wglMakeCurrent(NULL, NULL);
	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Context::renderThread, reinterpret_cast<void*>(this), 0, NULL);
}

Context::~Context()
{
	m_rendering = false;
	for (uint32_t i = 0; i < MAX_FRAME_LATENCY; i++)
		ReleaseSemaphore(m_semaphore_cpu[i], 1, NULL);

	for (uint32_t i = 0; i < MAX_FRAME_LATENCY; i++)
		WaitForSingleObject(m_semaphore_gpu[i], INFINITE);

	wglMakeCurrent(App.hdc, m_context);
	imguiDestroy();

	glDeleteBuffers(1, &m_pixel_buffer);
	glDeleteBuffers(1, &m_vertex_buffer);
	glDeleteBuffers(1, &m_index_buffer);
	glDeleteVertexArrays(1, &m_vertex_array);

	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(m_context);
}

void Context::renderThread(void* context)
{
	Context* ctx = reinterpret_cast<Context*>(context);
	wglMakeCurrent(App.hdc, ctx->m_context);
	uint32_t frame_index = 0;

	glBindBuffer(GL_ARRAY_BUFFER, ctx->m_vertex_buffer);
	Vertex::enableAttribArray();

	while (ctx->m_rendering) {
		WaitForSingleObject(ctx->m_semaphore_cpu[frame_index], INFINITE);
		const auto cmd = &ctx->m_command_buffer[frame_index];
		const glm::ivec2 vp_size = { App.viewport.stretched.x ? App.window.size.x : App.viewport.size.x, App.viewport.stretched.y ? App.window.size.y : App.viewport.size.y };
		const glm::ivec2 vp_offset = { App.viewport.stretched.x ? 0 : App.viewport.offset.x, App.viewport.stretched.y ? 0 : App.viewport.offset.y };

		{
			stats::Scope gpu_process(stats::TIMER_GPU_PROCESS);

			if (cmd->m_resized)
				ctx->onResize(cmd->m_window_size, cmd->m_game_size, cmd->m_game_tex_bpp);

			if (ctx->m_current_shader != App.shader.selected)
				ctx->onShaderChange();

			ctx->processUploads(cmd, frame_index);
			ctx->processCommands(cmd, vp_size, vp_offset);
			ctx->drawOverlay(cmd, frame_index);

			stats::Scope gpu_flush(stats::TIMER_GPU_FLUSH);
			//GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
			glFlush();
			//glClientWaitSync(sync, 0, GL_TIMEOUT_IGNORED);
			//glDeleteSync(sync);
		}

		ReleaseSemaphore(ctx->m_semaphore_gpu[frame_index], 1, NULL);
		option::Menu::instance().draw();
		{
			stats::Scope gpu_swap(stats::TIMER_GPU_SWAP);
			SwapBuffers(App.hdc);
		}

		if (ctx->m_limiter.active) {
			stats::Scope gpu_limiter(stats::TIMER_GPU_LIMITER);
			WaitForSingleObject(ctx->m_limiter.timer, (DWORD)ctx->m_limiter.frame_len_ms + 1);
			ctx->m_limiter.due_time.QuadPart += ctx->m_limiter.frame_len_ns;
			SetWaitableTimer(ctx->m_limiter.timer, &ctx->m_limiter.due_time, 0, NULL, NULL, FALSE);
		}

		frame_index = (frame_index + 1) % (App.frame_latency + 1);
	}

	wglMakeCurrent(NULL, NULL);
	for (uint32_t i = 0; i < 2; i++)
		ReleaseSemaphore(ctx->m_semaphore_gpu[i], 1, NULL);
}

void Context::processUploads(CommandBuffer* cmd, uint32_t frame_index)
{
	if (cmd->m_vertex_count) {
		stats::Scope upload_vertex(stats::TIMER_GPU_UPLOAD_VERTEX);
		glBufferSubData(GL_ARRAY_BUFFER, 0, cmd->m_vertex_count * sizeof(Vertex), m_vertices.data[frame_index].data());
	}

	if (!cmd->m_tex_update_queue.tex_data.empty()) {
		stats::Scope upload_tex(stats::TIMER_GPU_UPLOAD_TEX);
		stats::addCount(stats::CTR_TEX_UPDATES, cmd->m_tex_update_queue.tex_data.size());
		stats::addBytes(stats::CTR_TEX_BYTES, cmd->m_tex_update_queue.data_offset);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pixel_buffer);
		glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, cmd->m_tex_update_queue.data_offset, cmd->m_tex_buffer);
		for (size_t i = 0; i < cmd->m_tex_update_queue.tex_data.size(); i++) {
			const auto data = &cmd->m_tex_update_queue.tex_data[i];
			m_glide_texture->fill((uint8_t*)data->offset, data->tex_size.x, data->tex_size.y, data->tex_offset.x, data->tex_offset.y, data->tex_num);
		}
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	}

	for (auto& upload : cmd->m_font_page_uploads) {
		stats::Scope upload_tex(stats::TIMER_GPU_UPLOAD_TEX);
		stats::addCount(stats::CTR_FONT_PAGES);
		upload.texture->fill(upload.pixels, upload.width, upload.height, upload.x, upload.y, upload.layer);
		ImageData img = { (int)upload.width, (int)upload.height, 4, upload.pixels };
		helpers::clearImage(img);
	}
	cmd->m_font_page_uploads.clear();

	for (auto& upload : cmd->m_external_tex_uploads) {
		stats::Scope upload_tex(stats::TIMER_GPU_UPLOAD_TEX);
		stats::addCount(stats::CTR_EXTERNAL_UPLOADS);
		if (upload.compressed)
			m_external_texture->fillCompressed(upload.pixels, upload.width, upload.height, upload.offset_x, upload.offset_y, upload.layer);
		else
			m_external_texture->fill(upload.pixels, upload.width, upload.height, upload.offset_x, upload.offset_y, upload.layer);
		delete[] upload.pixels;
		upload.pixels = nullptr;
	}
	cmd->m_external_tex_uploads.clear();

	if (cmd->m_tex_update.bit && m_game_texture) {
		stats::Scope upload_tex(stats::TIMER_GPU_UPLOAD_TEX);
		stats::addCount(stats::CTR_TEX_UPDATES);
		stats::addBytes(stats::CTR_TEX_BYTES, cmd->m_tex_update.size.x * cmd->m_tex_update.size.y * cmd->m_tex_update.bit);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pixel_buffer);
		glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, cmd->m_tex_update.size.x * cmd->m_tex_update.size.y * cmd->m_tex_update.bit, cmd->m_tex_buffer);
		m_game_texture->fill(0, cmd->m_tex_update.size.x, cmd->m_tex_update.size.y);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	}
}

void Context::processCommands(CommandBuffer* cmd, glm::ivec2 vp_size, glm::ivec2 vp_offset)
{
	Vertex::bindingDescription();

	uint32_t last_blend_index = 0;

	stats::Scope gpu_commands(stats::TIMER_GPU_COMMANDS);
	for (size_t i = 0; i < cmd->m_commands.size(); i++) {
		const auto command = &cmd->m_commands[i];
		stats::addCount(stats::CTR_COMMANDS);

		switch (command->type) {
			case CommandType::UBOUpdate: {
				stats::addCount(stats::CTR_UBO_UPDATES);
				const auto data = &cmd->m_ubo_update_queue.data[command->index];
				m_game_color_ubo->updateData(data->type == UBOType::Gamma ? "gamma" : "palette", data->value);
			} break;
			case CommandType::SetBlendState:
				stats::addCount(stats::CTR_SET_BLEND);
				last_blend_index = command->index;
				bindPipeline(m_game_pipeline, command->index);
				break;
			case CommandType::DrawIndexed:
				if (command->draw.count > 0) {
					stats::addCount(stats::CTR_DRAWCALLS);
					glDrawElementsBaseVertex(GL_TRIANGLES, command->draw.count, GL_UNSIGNED_INT, 0, command->draw.start);
				}
				break;
			case CommandType::PreFx:
				processPreFx(cmd, command->index);
				break;
			case CommandType::Begin:
				if (cmd->m_screen == GameScreen::Movie) {
					bindDefaultFrameBuffer();
					setViewport(App.window.size);
				} else {
					bindFrameBuffer(m_game_framebuffer, ISGLIDE3X());
					setViewport(cmd->m_game_size);
				}
				break;
			case CommandType::Submit:
				processSubmit(cmd, vp_size, vp_offset, command->index);
				break;
			case CommandType::TakeScreenShot:
				takeScreenShot();
				break;
		}
	}
}

void Context::processPreFx(CommandBuffer* cmd, uint32_t index)
{
	stats::Scope gpu_prefx(stats::TIMER_GPU_PREFX);

	m_prefx_texture->fillFromBuffer(m_game_framebuffer);
	bindPipeline(m_prefx_pipeline);

	if (App.bloom.active) {
		bindFrameBuffer(m_bloom_framebuffer, false);
		setViewport(m_bloom_tex_size);
		drawQuad();

		if (App.gl_caps.compute_shader) {
			m_bloom_texture->fillFromBuffer(m_bloom_framebuffer);
			m_blur_compute_pipeline->dispatchCompute(0, m_bloom_work_size, GL_PIXEL_BUFFER_BARRIER_BIT);
			m_bloom_texture->fillFromBuffer(m_bloom_framebuffer);
			m_blur_compute_pipeline->dispatchCompute(1, m_bloom_work_size, GL_PIXEL_BUFFER_BARRIER_BIT);
			m_bloom_texture->fillFromBuffer(m_bloom_framebuffer);
			m_blur_compute_pipeline->dispatchCompute(0, m_bloom_work_size, GL_PIXEL_BUFFER_BARRIER_BIT);
			m_bloom_texture->fillFromBuffer(m_bloom_framebuffer);
			m_blur_compute_pipeline->dispatchCompute(1, m_bloom_work_size, GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
		} else {
			m_bloom_texture->fillFromBuffer(m_bloom_framebuffer);
			drawQuad(1);
			m_bloom_texture->fillFromBuffer(m_bloom_framebuffer);
			drawQuad(2);
			m_bloom_texture->fillFromBuffer(m_bloom_framebuffer);
			drawQuad(1);
			m_bloom_texture->fillFromBuffer(m_bloom_framebuffer);
			drawQuad(2);
		}

		bindFrameBuffer(m_game_framebuffer, false);
		setViewport(cmd->m_game_size);
		bindPipeline(m_prefx_pipeline);
		FrameBuffer::setDrawBuffers(1);
	}
	drawQuad(3 + App.bloom.active, 0, App.lut.selected);

	bindPipeline(m_game_pipeline, index);
	FrameBuffer::setDrawBuffers(m_game_framebuffer->getAttachmentCount());
}

void Context::processSubmit(CommandBuffer* cmd, glm::ivec2 vp_size, glm::ivec2 vp_offset, uint32_t index)
{
	stats::Scope gpu_submit(stats::TIMER_GPU_SUBMIT);

	if (cmd->m_screen == GameScreen::Movie) {
		bindPipeline(m_movie_pipeline);
		drawQuad();
	} else {
		if (App.sharpen.active) {
			const auto sharpen_data = glm::vec3(App.sharpen.strength.value, App.sharpen.clamp.value, App.sharpen.radius.value);
			if (m_sharpen_data != sharpen_data) {
				m_postfx_ubo->updateDataVec4f("sharpen", glm::vec4(sharpen_data, 1.0f));
				m_sharpen_data = sharpen_data;
			}
		}

		if (ISGLIDE3X()) {
			if (App.bloom.active) {
				const auto bloom_data = glm::vec2(App.bloom.exposure.value, App.bloom.gamma.value);
				if (m_bloom_data != bloom_data) {
					m_bloom_ubo->updateDataVec2f("bloom", bloom_data);
					m_bloom_data = bloom_data;
				}
			}
		} else {
			bindPipeline(m_game_pipeline);
			drawQuad();
		}

		if (App.sharpen.active || App.fxaa.active)
			Upscaler::Instance().process(m_game_framebuffer, vp_size, vp_offset, m_postfx_framebuffer);
		else
			Upscaler::Instance().process(m_game_framebuffer, vp_size, vp_offset);

		if (App.sharpen.active) {
			if (App.fxaa.active)
				m_postfx_texture->fillFromBuffer(m_postfx_framebuffer);
			else {
				bindDefaultFrameBuffer();
				setViewport(vp_size, vp_offset);
			}
			bindPipeline(m_postfx_pipeline);
			drawQuad(App.fxaa.active);
		}

		if (App.fxaa.active) {
			if (App.gl_caps.compute_shader) {
				m_postfx_texture->fillFromBuffer(m_postfx_framebuffer);
				m_fxaa_compute_pipeline->dispatchCompute(App.fxaa.presets.selected, m_fxaa_work_size, GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
			}
			bindDefaultFrameBuffer();
			setViewport(vp_size, vp_offset);
			bindPipeline(m_postfx_pipeline);
			drawQuad(2 + App.gl_caps.compute_shader, App.fxaa.presets.selected);
		}
	}
}

void Context::drawOverlay(CommandBuffer* cmd, uint32_t frame_index)
{
	if (cmd->m_vertex_mod_count) {
		stats::Scope gpu_overlay(stats::TIMER_GPU_OVERLAY);
		stats::addCount(stats::CTR_OVERLAY_RUNS, cmd->m_overlay_run_count);
		glBufferSubData(GL_ARRAY_BUFFER, 0, cmd->m_vertex_mod_count * sizeof(VertexMod), m_vertices_mod.data[frame_index].data());

		VertexMod::bindingDescription();
		bindPipeline(m_overlay_pipeline, 0);
		m_overlay_pipeline->setUniform1i("u_HasMask", cmd->m_has_mask);
		if (cmd->m_hd_text_mask.active) {
			m_overlay_pipeline->setUniformVec4f("u_TextMask", cmd->m_hd_text_mask.metrics);
			m_overlay_pipeline->setUniform1i("u_IsMasking", cmd->m_hd_text_mask.masking);
			cmd->m_hd_text_mask.active = false;
		}

		if (cmd->m_overlay_run_count) {
			uint8_t last_blend = 0xFF;
			for (uint32_t i = 0; i < cmd->m_overlay_run_count; i++) {
				const auto& run = cmd->m_overlay_runs[i];
				if (run.blend != last_blend) {
					m_overlay_pipeline->setBlendState(run.blend);
					last_blend = run.blend;
				}
				stats::addCount(stats::CTR_DRAWCALLS);
				glDrawElementsBaseVertex(GL_TRIANGLES, run.count / 4 * 6, GL_UNSIGNED_INT, 0, run.start);
			}
		} else {
			stats::addCount(stats::CTR_DRAWCALLS);
			glDrawElements(GL_TRIANGLES, cmd->m_vertex_mod_count / 4 * 6, GL_UNSIGNED_INT, 0);
		}
	}
}

void Context::onResize(glm::uvec2 w_size, glm::uvec2 g_size, uint32_t bpp)
{
	static glm::uvec2 game_size = { 0, 0 };
	static uint32_t color_bpp = 8;
	bool game_resized = game_size != g_size || color_bpp != bpp;
	game_size = g_size;
	color_bpp = bpp;

	static glm::uvec2 window_size = { 0, 0 };
	bool window_resized = window_size != w_size;
	window_size = w_size;

	if (game_resized) {
		glm::mat4 mvp = glm::ortho(0.0f, (float)game_size.x, (float)game_size.y, 0.0f);
		modules::HDText::Instance().setMVP(mvp);
		m_overlay_pipeline->setUniformMat4f("u_MVP", mvp);

		FrameBufferCreateInfo frambuffer_ci;
		frambuffer_ci.size = game_size;
		if (ISGLIDE3X())
			frambuffer_ci.attachments = {
				{ TEXTURE_SLOT_GAME, { 0.0f, 0.0f, 0.0f, 1.0f }, { GL_LINEAR, GL_LINEAR } },
				{ TEXTURE_SLOT_MAP, { 0.0f, 0.0f, 0.0f, 0.0f } },
				{ TEXTURE_SLOT_MASK, { 0.0f, 0.0f, 0.0f, 0.0f }, { GL_LINEAR, GL_LINEAR }, { GL_R8, GL_RED } },
			};
		else
			frambuffer_ci.attachments = { { TEXTURE_SLOT_GAME } };
		m_game_framebuffer = Context::createFrameBuffer(frambuffer_ci);

		if (ISGLIDE3X()) {
			m_game_pipeline->setUniformMat4f("u_MVP", mvp);

			m_bloom_ubo->updateDataVec2f("rel_size", { 4.0f / game_size.x, 4.0f / game_size.y });
			m_bloom_tex_size = { game_size.x / 4, game_size.y / 4 };
			m_bloom_work_size = { ceil((float)m_bloom_tex_size.x / 16), ceil((float)m_bloom_tex_size.y / 16) };

			FrameBufferCreateInfo bloom_frambuffer_ci;
			bloom_frambuffer_ci.size = m_bloom_tex_size;
			bloom_frambuffer_ci.attachments = { { TEXTURE_SLOT_BLOOM1, {}, { GL_LINEAR, GL_LINEAR } } };
			m_bloom_framebuffer = Context::createFrameBuffer(bloom_frambuffer_ci);
			if (App.gl_caps.compute_shader)
				m_bloom_framebuffer->getTexture()->bindImage(IMAGE_UNIT_BLUR);

			TextureCreateInfo bloom_texture_ci;
			bloom_texture_ci.size = m_bloom_tex_size;
			bloom_texture_ci.slot = TEXTURE_SLOT_BLOOM2;
			bloom_texture_ci.filter = { GL_LINEAR, GL_LINEAR };
			m_bloom_texture = Context::createTexture(bloom_texture_ci);

			TextureCreateInfo prefx_texture_ci;
			prefx_texture_ci.size = game_size;
			prefx_texture_ci.slot = TEXTURE_SLOT_PREFX;
			m_prefx_texture = Context::createTexture(prefx_texture_ci);
		} else {
			TextureCreateInfo texture_ci;
			texture_ci.size = game_size;
			if (color_bpp == 8)
				texture_ci.format = { GL_R8, GL_RED };
			else
				texture_ci.format = { GL_RGBA8, GL_BGRA };
			texture_ci.filter = { GL_LINEAR, GL_LINEAR };
			m_game_texture = Context::createTexture(texture_ci);
		}
	}

	if (window_resized) {
		glm::vec2 scale = { (float)window_size.x / 640, (float)window_size.y / 360 };
		float offset_x = ((scale.x > scale.y) ? scale.x / scale.y : 1.0f) * 1.00f;
		float offset_y = ((scale.x < scale.y) ? scale.y / scale.x : 1.0f) * 0.75f;
		m_movie_pipeline->setUniformMat4f("u_MVP", glm::ortho(-1.0f * offset_x, 1.0f * offset_x, 1.0f * offset_y, -1.0f * offset_y));
	}

	if (game_resized || window_resized) {
		FrameBufferCreateInfo frambuffer_ci;
		frambuffer_ci.size = App.viewport.size;
		frambuffer_ci.attachments = { { TEXTURE_SLOT_POSTFX1, {}, { GL_LINEAR, GL_LINEAR } } };
		m_postfx_framebuffer = Context::createFrameBuffer(frambuffer_ci);
		if (App.gl_caps.compute_shader)
			m_postfx_framebuffer->getTexture()->bindImage(IMAGE_UNIT_FXAA);

		m_fxaa_work_size = { ceil((float)App.viewport.size.x / 16), ceil((float)App.viewport.size.y / 16) };

		TextureCreateInfo texture_ci;
		texture_ci.size = App.viewport.size;
		texture_ci.slot = TEXTURE_SLOT_POSTFX2;
		texture_ci.filter = { GL_LINEAR, GL_LINEAR };
		m_postfx_texture = Context::createTexture(texture_ci);

		onShaderChange();
	}

	m_postfx_ubo->updateDataVec2f("rel_size", { 1.0f / App.viewport.size.x, 1.0f / App.viewport.size.y });
	m_overlay_pipeline->setUniformVec2f("u_Scale", App.viewport.scale);

	const glm::ivec2 vp_size = { App.viewport.stretched.x ? App.window.size.x : App.viewport.size.x, App.viewport.stretched.y ? App.window.size.y : App.viewport.size.y };
	const glm::ivec2 vp_offset = { App.viewport.stretched.x ? 0 : App.viewport.offset.x, App.viewport.stretched.y ? 0 : App.viewport.offset.y };
	m_overlay_pipeline->setUniformVec4f("u_Viewport", { (float)vp_offset.x, (float)vp_offset.y, (float)vp_size.x, (float)vp_size.y });

	modules::MiniMap::Instance().resize();
	toggleVsync();
}

void Context::onShaderChange()
{
	if (m_current_shader != App.shader.selected) {
		if (!Upscaler::Instance().loadPreset())
			Upscaler::Instance().loadDefaultPreset();
	}

	Upscaler::Instance().setupPasses();
	m_current_shader = App.shader.selected;
}

void Context::onStageChange()
{
	stats::Scope stage_scope(stats::TIMER_CPU_STAGE);

	if (App.game.screen == GameScreen::Movie)
		return;

	switch (App.game.draw_stage) {
		case DrawStage::World:
			break;
		case DrawStage::UI:
			if (ISGLIDE3X() && (App.bloom.active || App.lut.selected) && *d2::screen_shift != SCREENPANEL_BOTH) {
				flushVertices();
				m_command_buffer[m_frame_index].pushCommand(CommandType::PreFx, m_current_blend_index);
			}
			break;
		case DrawStage::Map:
			if (modules::MiniMap::Instance().isActive()) {
				flushVertices();
				m_blend_locked = true;
				m_command_buffer[m_frame_index].pushCommand(CommandType::SetBlendState, 3);
				setVertexFlagW(1 + !*d2::automap_on);
			}
			break;
		case DrawStage::HUD:
			if (modules::MiniMap::Instance().isActive()) {
				flushVertices();
				m_blend_locked = false;
				m_command_buffer[m_frame_index].pushCommand(CommandType::SetBlendState, m_current_blend_index);
				setVertexFlagW(0);

				modules::MiniMap::Instance().draw();
			}
			modules::HDText::Instance().drawEntryText();
			modules::HDText::drawFpsCounter();
			break;
		case DrawStage::CursorItem:
			flushVertices();
			setVertexFlagW(10);
			break;
		case DrawStage::Cursor:
#ifdef _HDTEXT
			modules::HDText::showSampleText();
#endif
			flushVertices();
			setVertexFlagW(10);
			appendDelayedObjects();
			modules::HDCursor::Instance().draw();
			break;
	}
}

void Context::setBlendState(uint32_t index)
{
	flushVertices();
	m_current_blend_index = g_blend_types.at(index).first;
	if (!m_blend_locked)
		m_command_buffer[m_frame_index].pushCommand(CommandType::SetBlendState, m_current_blend_index);
}

void Context::beginFrame()
{
	if (!App.wndproc && App.game.screen == GameScreen::Menu)
		App.wndproc = (WNDPROC)SetWindowLongA(App.hwnd, GWL_WNDPROC, (LONG)win32::WndProc);

	m_vertices.count = m_vertices.start = 0;
	m_vertices.ptr = m_vertices.data[m_frame_index].data();

	m_vertices_mod.count = 0;
	m_vertices_mod.ptr = m_vertices_mod.data[m_frame_index].data();

	m_delay_push = false;
	m_top_pass_active = false;
	m_world_hd_occludable = false;
	m_overlay_open = false;
	m_vertices_late.count = 0;
	m_vertices_late.ptr = m_vertices_late.data[0].data();

	m_frame.vertex_count = 0;
	m_frame.drawcall_count = 0;

	App.game.draw_stage = DrawStage::World;
	{
		stats::Scope modules_scope(stats::TIMER_CPU_MODULES);
		modules::HDText::Instance().reset();
		modules::MotionPrediction::Instance().update();
	}

	m_command_buffer[m_frame_index].pushCommand(CommandType::Begin);
	stats::beginDraw();
}

void Context::bindDefaultFrameBuffer()
{
	FrameBuffer::unBind();
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

void Context::presentFrame()
{
	stats::endDraw();

#ifdef _STATS
	const uint32_t sd_vertices = m_frame.vertex_count > m_vertices_mod.count ? m_frame.vertex_count - m_vertices_mod.count : 0;
	stats::addCount(stats::CTR_VERTICES_SD, sd_vertices);
	stats::addCount(stats::CTR_VERTICES_MOD, m_vertices_mod.count);
#endif

	stats::Scope present_scope(stats::TIMER_CPU_PRESENT);
	flushVertices();
	setVertexFlagW(0);
	closeOverlayRun();
	m_command_buffer[m_frame_index].pushCommand(CommandType::Submit);

	modules::HDText::Instance().update();

	if (m_vertices_mod.count) {
		m_command_buffer[m_frame_index].m_vertex_mod_count = m_vertices_mod.count;
		m_frame.drawcall_count++;
	}
	option::Menu::instance().check();

	ReleaseSemaphore(m_semaphore_cpu[m_frame_index], 1, NULL);
	m_frame_index = (m_frame_index + 1) % (App.frame_latency + 1);

	{
		stats::Scope gpu_wait_scope(stats::TIMER_CPU_GPU_WAIT);
		WaitForSingleObject(m_semaphore_gpu[m_frame_index], INFINITE);
	}
	m_command_buffer[m_frame_index].reset();

	QueryPerformanceCounter(&m_frame.time);
	double cur_time = (double(m_frame.time.QuadPart) / m_frame.frequency);
	m_frame.frame_time = cur_time - m_frame.prev_time;
	m_frame.prev_time = cur_time;

	auto reduced = m_frame.frame_times[m_frame.next_frame_index];
	m_frame.frame_times[m_frame.next_frame_index] = m_frame.frame_time;
	m_frame.total_frame_time -= reduced;
	m_frame.total_frame_time += m_frame.frame_time;
	m_frame.average_frame_time = m_frame.total_frame_time / m_frame.frame_sample_count;

	m_frame.next_frame_index += 1;
	if (m_frame.next_frame_index >= m_frame.frame_times.size()) {
		m_frame.next_frame_index = 0;
	}
	if (m_frame.frame_sample_count < MAX_FRAMETIME_SAMPLE_COUNT) {
		m_frame.frame_sample_count += 1;
	}
	m_frame.frame_count++;
	stats::frameDone(m_frame.frame_time);
}

void Context::ensureOverlayRun(uint8_t kind, uint8_t blend)
{
	if (m_overlay_open && m_overlay_open_kind == kind && m_overlay_open_blend == blend)
		return;

	closeOverlayRun();
	m_overlay_open = true;
	m_overlay_open_kind = kind;
	m_overlay_open_blend = blend;
	m_overlay_open_start = m_vertices_mod.count;
}

void Context::closeOverlayRun()
{
	if (!m_overlay_open)
		return;

	const uint32_t count = m_vertices_mod.count - m_overlay_open_start;
	if (count > 0)
		m_command_buffer[m_frame_index].addOverlayRun(m_overlay_open_start, count, m_overlay_open_blend);

	m_overlay_open = false;
}

void Context::setViewport(glm::ivec2 size, glm::ivec2 offset)
{
	static glm::ivec4 viewport_metrics = { 0, 0, 0, 0 };
	const auto metrics = glm::ivec4(size, offset);
	if (viewport_metrics == metrics)
		return;

	glViewport(offset.x, offset.y, size.x, size.y);
	viewport_metrics = metrics;
}

void Context::pushVertex(const GlideVertex* vertex, glm::vec2 fix, glm::ivec2 offset)
{
#ifdef _STATS
	g_push_timer.enter();
#endif

	// Post-HD game content is routed into the overlay stream so it is replayed
	// at viewport resolution after the HD content drawn before it. World content
	// (monsters, tiles) is only replayed when an HD item was pushed somewhere
	// (m_world_hd_occludable): re-rasterizing the whole scene costs too much when
	// only damage-number text covers the screen. UI/HUD content (small) is always
	// replayed for correct z-order with HD. Cursor-stage content (flags.w==10,
	// the held item / game cursor) is drawn last, so it must stay on top of every
	// overlay run. Map-stage content (flags.w 1/2) stays in the game FBO to keep
	// the MAP attachment intact.
	const auto stage = App.game.draw_stage;
	if (m_top_pass_active && m_vertices_mod.count < MAX_VERTICES_MOD - 4 &&
		(m_vertex_params.flags.w == 10 ||
			(m_vertex_params.flags.w == 0 &&
				(stage == DrawStage::UI || stage == DrawStage::HUD || m_world_hd_occludable)))) {
		ensureOverlayRun(0, 1 + m_current_blend_index);
		writeOverlayVertex(vertex, fix, offset);
#ifdef _STATS
		g_push_timer.exit();
#endif
		return;
	}

	// Game FBO path: pre-HD content, world content without an HD item, map-stage
	// content, and cursor content when the overlay buffer is full.
	if (m_vertex_params.flags.w == 10)
		m_command_buffer[m_frame_index].m_has_mask = true;

	if (m_vertices.count >= MAX_VERTICES - 4)
		flushVertices();

	writeGameVertex(vertex, fix, offset);
#ifdef _STATS
	g_push_timer.exit();
#endif
}

void Context::writeGameVertex(const GlideVertex* vertex, glm::vec2 fix, glm::ivec2 offset)
{
	m_vertices.ptr->position = {
		glm::detail::toFloat16(vertex->x - (float)offset.x),
		glm::detail::toFloat16(vertex->y - (float)offset.y),
	};
	m_vertices.ptr->tex_coord = {
		((float)((uint32_t)vertex->s >> m_vertex_params.tex_shift) + (float)m_vertex_params.offsets.x) / (512.0f + fix.x),
		((float)((uint32_t)vertex->t >> m_vertex_params.tex_shift) + (float)m_vertex_params.offsets.y) / (512.0f + fix.y),
	};
	m_vertices.ptr->color1 = vertex->pargb;
	m_vertices.ptr->color2 = m_vertex_params.color;
	m_vertices.ptr->tex_ids = m_vertex_params.tex_ids;
	m_vertices.ptr->flags = m_vertex_params.flags;

	m_vertices.ptr++;
	m_vertices.count++;
	m_frame.vertex_count++;
}

void Context::writeOverlayVertex(const GlideVertex* vertex, glm::vec2 fix, glm::ivec2 offset)
{
	VertexMod* dst = m_vertices_mod.ptr;
	dst->position = {
		glm::detail::toFloat16(vertex->x - (float)offset.x),
		glm::detail::toFloat16(vertex->y - (float)offset.y),
	};
	dst->tex_coord = {
		((float)((uint32_t)vertex->s >> m_vertex_params.tex_shift) + (float)m_vertex_params.offsets.x) / (512.0f + fix.x),
		((float)((uint32_t)vertex->t >> m_vertex_params.tex_shift) + (float)m_vertex_params.offsets.y) / (512.0f + fix.y),
	};
	dst->color1 = vertex->pargb;
	dst->color2 = m_vertex_params.color;
	dst->tex_ids = m_vertex_params.tex_ids;
	dst->flags = m_vertex_params.flags;
	dst->extra = { glm::detail::toFloat16(-1.0f), 0 };

	m_vertices_mod.ptr++;
	m_vertices_mod.count++;
	m_frame.vertex_count++;
}

void Context::flushVertices()
{
	if (m_vertices.count == 0)
		return;

	m_command_buffer[m_frame_index].drawIndexed(m_vertices.start, m_vertices.count);

	m_vertices.start += m_vertices.count;
	m_vertices.count = 0;
	m_frame.drawcall_count++;
	stats::addCount(stats::CTR_FLUSHES);
}

void Context::drawQuad(int8_t flag_x, int8_t flag_y, int16_t tex_id)
{
	static Vertex quad[4] = {
		{ { glm::detail::toFloat16(-1.0f), glm::detail::toFloat16(-1.0f) }, { 0.0f, 0.0f } },
		{ { glm::detail::toFloat16(+1.0f), glm::detail::toFloat16(-1.0f) }, { 1.0f, 0.0f } },
		{ { glm::detail::toFloat16(+1.0f), glm::detail::toFloat16(+1.0f) }, { 1.0f, 1.0f } },
		{ { glm::detail::toFloat16(-1.0f), glm::detail::toFloat16(+1.0f) }, { 0.0f, 1.0f } },
	};
	for (size_t i = 0; i < 4; i++) {
		quad[i].tex_ids = { tex_id, 0 };
		quad[i].flags = { flag_x, flag_y, 0, 0 };
	}

	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quad), &quad[0]);
	stats::addCount(stats::CTR_DRAWCALLS);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

void Context::pushObject(const std::unique_ptr<Object>& object)
{
	const auto vertices = object->getVertices();

	// The first HD object activates the top pass: game content drawn before it
	// stays in the game FBO, everything drawn after (HD and SD) is replayed in
	// original order by the overlay pass at viewport resolution.
	if (!m_top_pass_active) {
		flushVertices();
		m_top_pass_active = true;
	}

	// An external-texture HD object (flags.x==8: HD ground items / orbs) needs
	// occlusion by monsters drawn after it, so world content is replayed that
	// frame. Text (3), cursor (1), minimap (2/5) and gradients never set this:
	// they sit on top of world content anyway.
	if (vertices[0].flags.x == 8)
		m_world_hd_occludable = true;

	if (m_delay_push) {
		if (m_vertices_late.count >= MAX_VERTICES_MOD - 4)
			return;

		memcpy(m_vertices_late.ptr, vertices, sizeof(VertexMod) * 4);

		m_vertices_late.ptr += 4;
		m_vertices_late.count += 4;
	} else {
		if (m_vertices_mod.count >= MAX_VERTICES_MOD - 4)
			return;

		ensureOverlayRun(1, 0);

		memcpy(m_vertices_mod.ptr, vertices, sizeof(VertexMod) * 4);

		m_vertices_mod.ptr += 4;
		m_vertices_mod.count += 4;
	}
	m_frame.vertex_count += 4;
	stats::addCount(stats::CTR_HD_OBJECTS);
}

void Context::appendDelayedObjects()
{
	if (m_vertices_late.count == 0)
		return;

	if (m_vertices_mod.count + m_vertices_late.count > MAX_VERTICES_MOD) {
		m_delay_push = false;
		m_vertices_late.count = 0;
		m_vertices_late.ptr = m_vertices_late.data[0].data();
		return;
	}

	ensureOverlayRun(1, 0);

	memcpy(m_vertices_mod.ptr, m_vertices_late.data[0].data(), m_vertices_late.count * sizeof(VertexMod));

	m_vertices_mod.count += m_vertices_late.count;
	m_vertices_mod.ptr += m_vertices_late.count;

	m_delay_push = false;
	m_vertices_late.count = 0;
	m_vertices_late.ptr = m_vertices_late.data[0].data();
}

void Context::queueExternalTexUpload(uint32_t layer, const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t offset_x, uint32_t offset_y, bool compressed)
{
	auto& cmd = m_command_buffer[m_frame_index];
	ExternalTexUpload upload;
	size_t pixel_bytes = compressed
		? (size_t)(((width + 3) / 4) * ((height + 3) / 4) * 16)
		: (size_t)width * height * 4;
	upload.pixels = new uint8_t[pixel_bytes];
	memcpy(upload.pixels, pixels, pixel_bytes);
	upload.width = width;
	upload.height = height;
	upload.layer = layer;
	upload.offset_x = offset_x;
	upload.offset_y = offset_y;
	upload.compressed = compressed;
	cmd.m_external_tex_uploads.push_back(upload);
}

void Context::toggleVsync()
{
	wglSwapIntervalEXT(App.vsync);
	resetFileTime();
}

void Context::setFpsLimit(bool active, int max_fps)
{
	m_limiter.active = active;
	m_limiter.frame_len_ms = 1000.0f / max_fps;
	m_limiter.frame_len_ns = (uint64_t)(m_limiter.frame_len_ms * 10000);
	m_frame.frame_sample_count = 1;

	resetFileTime();
}

void Context::resetFileTime()
{
	FILETIME ft = { 0 };
	GetSystemTimeAsFileTime(&ft);
	memcpy(&m_limiter.due_time, &ft, sizeof(LARGE_INTEGER));
}

void Context::takeScreenShot()
{
	uint8_t* data = new GLubyte[App.viewport.size.x * App.viewport.size.y * 3];
	memset(data, 0, App.viewport.size.x * App.viewport.size.y * 3);

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(App.viewport.offset.x, App.viewport.offset.y, App.viewport.size.x, App.viewport.size.y, GL_RGB, GL_UNSIGNED_BYTE, data);

	std::string file_name = helpers::saveScreenShot(data, App.viewport.size.x, App.viewport.size.y);
	delete[] data;
}

void Context::imguiInit()
{
	ImGui::CreateContext();
	ImGui_ImplWin32_Init(App.hwnd);
	ImGui_ImplOpenGL3_Init("#version 150");
}

void Context::imguiDestroy()
{
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void Context::imguiStartFrame()
{
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

void Context::imguiRender()
{
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void APIENTRY Context::debugMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* data)
{
	const char* source_str;
	const char* severity_str;

	// clang-format off
	switch (type) {
		case GL_DEBUG_TYPE_ERROR: logTrace(C_RED, false, "\nError: "); break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: logTrace(C_YELLOW, false, "\nDeprecated behavior: "); break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: logTrace(C_YELLOW, false, "\nUndefined behavior: "); break;
		case GL_DEBUG_TYPE_PORTABILITY: logTrace(C_BLUE, false, "\nPortability: "); break;
		case GL_DEBUG_TYPE_PERFORMANCE: logTrace(C_BLUE, false, "\nPerformance: "); break;
		case GL_DEBUG_TYPE_MARKER: logTrace(C_MAGENTA, false, "\nMarker: "); break;
		case GL_DEBUG_TYPE_OTHER: logTrace(C_GRAY, false, "\nOther: "); break;
		default: logTrace(C_RED, false, "\nUnknown: ");
	}

	switch (source) {
		case GL_DEBUG_SOURCE_API: source_str = "Api"; break;
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM: source_str = "Window system"; break;
		case GL_DEBUG_SOURCE_SHADER_COMPILER: source_str = "Shader compiler"; break;
		case GL_DEBUG_SOURCE_THIRD_PARTY: source_str = "Third party"; break;
		case GL_DEBUG_SOURCE_APPLICATION: source_str = "Application"; break;
		case GL_DEBUG_SOURCE_OTHER: source_str = "Other"; break;
		default: source_str = "Unknown";
	}

	switch (severity) {
		case GL_DEBUG_SEVERITY_HIGH: severity_str = "High"; break;
		case GL_DEBUG_SEVERITY_MEDIUM: severity_str = "Medium"; break;
		case GL_DEBUG_SEVERITY_LOW: severity_str = "Low"; break;
		case GL_DEBUG_SEVERITY_NOTIFICATION: severity_str = "Notification"; break;
		default: severity_str = "Unknown";
	}
	// clang-format on

	logTrace(C_WHITE, false, "[%u / %s]: ", id, severity_str);
	logTrace(C_GRAY, true, "%s", source_str);
	trace("%s", message);

	if (App.log)
		logFileWrite(0, "OpenGL: [%u / %s]: %s | %s", id, severity_str, source_str, message);
}

}
