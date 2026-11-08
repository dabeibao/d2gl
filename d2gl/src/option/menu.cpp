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
#include "menu.h"
#include "d2/common.h"
#include "helpers.h"
#include "ini.h"
#include "modules/hd_text.h"
#include "modules/mini_map.h"
#include "modules/motion_prediction.h"
#include "win32.h"

namespace d2gl::option {

#define drawCombo_m(a, b, c, d, e, id)  \
	static int opt_##id = b##.selected; \
	if (drawCombo(a, &b, c, d, &opt_##id, e))

#define drawCheckbox_m(a, b, c, id) \
	static bool opt_##id = b;       \
	if (drawCheckbox(a, &b, c, &opt_##id))

#define drawSlider_m(type, a, b, c, d, id) \
	static type opt_##id = b##.value;      \
	if (drawSlider(#id, a, &b, c, d, &opt_##id))

#define checkChanged(x) \
	if (!changed && x)  \
	changed = true

// Glyphs that are always included in the menu fonts. Every other character,
// including the static menu labels and dynamic strings such as font file
// names, is added at runtime by Menu::requestGlyphs().
static const ImWchar base_glyph_ranges[] = {
	0x0020, 0x00FF, // Basic Latin + Latin Supplement
	0x2000, 0x206F, // General Punctuation
	0x3000, 0x30FF, // CJK Symbols and Punctuations, Hiragana, Katakana
	0x31F0, 0x31FF, // Katakana Phonetic Extensions
};

static bool isBaseGlyph(ImWchar c)
{
	for (size_t i = 0; i < IM_ARRAYSIZE(base_glyph_ranges); i += 2) {
		if (c >= base_glyph_ranges[i] && c <= base_glyph_ranges[i + 1])
			return true;
	}
	return false;
}

// Decode one UTF-8 code point. Returns the number of bytes consumed; `out` is
// set to 0 for a malformed sequence or a code point above the BMP (ImWchar is
// 16-bit in this build).
static int decodeUtf8(const char* str, ImWchar* out)
{
	const unsigned char* s = (const unsigned char*)str;
	unsigned int c = s[0];
	if (c < 0x80) {
		*out = (ImWchar)c;
		return 1;
	}

	int len;
	if ((c & 0xE0) == 0xC0) {
		c &= 0x1F;
		len = 2;
	} else if ((c & 0xF0) == 0xE0) {
		c &= 0x0F;
		len = 3;
	} else if ((c & 0xF8) == 0xF0) {
		c &= 0x07;
		len = 4;
	} else {
		*out = 0;
		return 1;
	}

	for (int i = 1; i < len; i++) {
		const unsigned char cc = s[i];
		if (cc == 0) {
			*out = 0;
			return i;
		}
		if ((cc & 0xC0) != 0x80) {
			*out = 0;
			return 1;
		}
		c = (c << 6) | (cc & 0x3F);
	}

	*out = c > 0xFFFF ? 0 : (ImWchar)c;
	return len;
}

typedef UINT (WINAPI *GetDpiForWindow_t)(HWND hwnd);

static float getScale(HWND hwnd) {
    static GetDpiForWindow_t pGetDpiForWindow =
        (GetDpiForWindow_t)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (!pGetDpiForWindow) {
	    return 1.0f;
    }
    auto dpi = pGetDpiForWindow(hwnd);
    return dpi / 96.0f;
}

// Scans data\assets\fonts for selectable SDF fonts. Index 0 uses the bundled
// default font.
static std::string pathToUtf8(const std::filesystem::path& path)
{
	// std::filesystem::path::string() uses the ANSI code page on Windows, which
	// ImGui cannot render. Convert explicitly so non-ASCII file names work.
	const auto str = path.u8string();
	return std::string(str.begin(), str.end());
}

static void refreshFontList(std::vector<std::string>& paths, Select<int>& select, const std::string& current, bool ascii_only = false)
{
	paths.clear();
	select.items.clear();

	paths.emplace_back();
	select.items.push_back({ ascii_only ? std::string("跟随主字体") : std::string("默认 (") + kDefaultFontFile + ")", 0 });

	std::vector<std::pair<std::string, std::string>> files;
	std::error_code ec;
	for (auto it = std::filesystem::directory_iterator("data\\assets\\fonts", ec);
		 !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
		if (!it->is_regular_file(ec))
			continue;

		auto ext = it->path().extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)::tolower(c); });
		if (ext != ".ttf" && ext != ".otf" && ext != ".ttc")
			continue;

		files.emplace_back(pathToUtf8(it->path().filename()), it->path().string());
	}
	std::sort(files.begin(), files.end());

	for (const auto& file : files) {
		select.items.push_back({ file.first, (int)paths.size() });
		paths.push_back(file.second);
	}

	select.selected = 0;
	if (!current.empty()) {
		// Match on the full entry or, for a bare file name (the default is just
		// "chi.ttf"), on the base name of the entry.
		const auto base = current.find_last_of("\\/");
		const std::string current_base = base == std::string::npos ? current : current.substr(base + 1);
		bool found = false;
		for (size_t i = 1; i < paths.size(); i++) {
			const auto sep = paths[i].find_last_of("\\/");
			const std::string entry_base = sep == std::string::npos ? paths[i] : paths[i].substr(sep + 1);
			if (_stricmp(paths[i].c_str(), current.c_str()) == 0 || _stricmp(entry_base.c_str(), current_base.c_str()) == 0) {
				select.selected = (int)i;
				found = true;
				break;
			}
		}
		if (!found) {
			select.items.push_back({ pathToUtf8(std::filesystem::path(current)), (int)paths.size() });
			paths.push_back(current);
			select.selected = (int)paths.size() - 1;
		}
	}
}

Menu::Menu()
{
	if (App.menu_scale) {
		m_scale = getScale(App.hwnd);
	} else {
		m_scale = 1.0;
	}

	m_colors[Color::Default] = ImColor(199, 179, 119);
	m_colors[Color::Orange] = ImColor(255, 150, 0);
	m_colors[Color::White] = ImColor(255, 255, 255, 180);
	m_colors[Color::Gray] = ImColor(150, 150, 150);

	ImGuiStyle& style = ImGui::GetStyle();

	style.Colors[ImGuiCol_WindowBg] = ImColor(0, 0, 0);
	style.Colors[ImGuiCol_Border] = ImColor(34, 34, 34);
	style.Colors[ImGuiCol_Separator] = ImColor(34, 34, 34);
	style.Colors[ImGuiCol_Text] = m_colors[Color::Default];
	style.Colors[ImGuiCol_TitleBg] = ImColor(0, 0, 0, 240);
	style.Colors[ImGuiCol_TitleBgActive] = ImColor(0, 0, 0, 240);
	style.Colors[ImGuiCol_Header] = ImColor(25, 25, 25);
	style.Colors[ImGuiCol_HeaderActive] = ImColor(45, 45, 45);
	style.Colors[ImGuiCol_HeaderHovered] = ImColor(35, 35, 35);
	style.Colors[ImGuiCol_SliderGrab] = ImColor(60, 60, 60);
	style.Colors[ImGuiCol_SliderGrabActive] = m_colors[Color::Orange];
	style.Colors[ImGuiCol_CheckMark] = m_colors[Color::Orange];
	style.Colors[ImGuiCol_FrameBg] = ImColor(25, 25, 25);
	style.Colors[ImGuiCol_FrameBgHovered] = ImColor(35, 35, 35);
	style.Colors[ImGuiCol_FrameBgActive] = ImColor(45, 45, 45);
	style.Colors[ImGuiCol_Button] = ImColor(25, 25, 25);
	style.Colors[ImGuiCol_ButtonHovered] = ImColor(35, 35, 35);
	style.Colors[ImGuiCol_ButtonActive] = ImColor(45, 45, 45);
	style.Colors[ImGuiCol_ScrollbarBg] = ImColor(10, 10, 10);
	style.Colors[ImGuiCol_ScrollbarGrab] = ImColor(25, 25, 25);
	style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImColor(35, 35, 35);
	style.Colors[ImGuiCol_ScrollbarGrabActive] = ImColor(45, 45, 45);
	style.Colors[ImGuiCol_Tab] = ImColor(30, 30, 30, 150);
	style.Colors[ImGuiCol_TabActive] = ImColor(40, 40, 40);
	style.Colors[ImGuiCol_TabHovered] = ImColor(40, 40, 40, 200);
	style.Colors[ImGuiCol_TabUnfocused] = ImColor(30, 30, 30, 150);
	style.Colors[ImGuiCol_TabUnfocusedActive] = ImColor(40, 40, 40);

	style.ScrollbarSize = 10.0f;
	style.ScrollbarRounding = 0.0f;
	style.WindowTitleAlign.x = 0.5f;
	style.WindowPadding = { 16.0f, 16.0f };
	style.WindowBorderSize = 1.0f;
	style.WindowRounding = 0.0f;
	style.ItemInnerSpacing = { 10.0f, 0.0f };
	style.FrameRounding = 2.0f;
	style.FramePadding = { 4.0f, 4.0f};
	style.FrameBorderSize = 1.0f;
	style.ChildRounding = 0.0f;
	style.TabRounding = 2.0f;
	style.PopupRounding = 2.0f;
	style.GrabRounding = 1.0f;
	style.GrabMinSize = 30.0f;
	style.DisabledAlpha = 0.4f;

	style.ScaleAllSizes(m_scale);

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	io.IniFilename = NULL;

	BufferData font = helpers::loadFile("assets\\fonts\\Chi.ttf");
	if (font.size) {
		m_font_data.assign(font.data, font.data + font.size);
		delete[] font.data;
	}

	rebuildFontAtlas(false);

	App.menu_title += (ISGLIDE3X() ? " (Glide / " : " (DDraw / ");
	App.menu_title += "OpenGL: " + App.gl_ver_str + " / D2LoD: " + helpers::getVersionString() + " / " + helpers::getLangString() + ")";
}

void Menu::requestGlyphs(const char* text)
{
	if (!text || !text[0] || m_font_data.empty())
		return;

	while (*text) {
		ImWchar c = 0;
		text += decodeUtf8(text, &c);
		if (c < 0x20 || isBaseGlyph(c))
			continue;
		if (m_extra_glyphs.insert(c).second)
			m_glyphs_dirty = true;
	}
}

void Menu::buildGlyphRanges()
{
	m_glyph_ranges.assign(base_glyph_ranges, base_glyph_ranges + IM_ARRAYSIZE(base_glyph_ranges));

	std::vector<ImWchar> extra(m_extra_glyphs.begin(), m_extra_glyphs.end());
	std::sort(extra.begin(), extra.end());
	for (const ImWchar c : extra) {
		// Merge runs so the font builder walks fewer ranges.
		if (m_glyph_ranges.size() > IM_ARRAYSIZE(base_glyph_ranges) &&
		    m_glyph_ranges[m_glyph_ranges.size() - 1] == (ImWchar)(c - 1)) {
			m_glyph_ranges.back() = c;
		} else {
			m_glyph_ranges.push_back(c);
			m_glyph_ranges.push_back(c);
		}
	}
	m_glyph_ranges.push_back(0);
}

void Menu::rebuildFontAtlas(bool recreate_texture)
{
	ImGuiIO& io = ImGui::GetIO();
	const bool has_texture = io.Fonts->TexID != 0;

	io.Fonts->Clear();
	buildGlyphRanges();

	io.Fonts->AddFontDefault();

	if (!m_font_data.empty()) {
		// The atlas takes ownership of this buffer when the font is added;
		// all sizes below share it, so only the first config keeps ownership.
		void* data = IM_ALLOC(m_font_data.size());
		memcpy(data, m_font_data.data(), m_font_data.size());

		const ImWchar* glyphs = m_glyph_ranges.data();
		const int first_config = io.Fonts->ConfigData.Size;
		const int data_size = (int)m_font_data.size();
		m_fonts[20] = io.Fonts->AddFontFromMemoryTTF(data, data_size, 20.0f * m_scale, nullptr, glyphs);
		m_fonts[17] = io.Fonts->AddFontFromMemoryTTF(data, data_size, 17.0f * m_scale, nullptr, glyphs);
		m_fonts[15] = io.Fonts->AddFontFromMemoryTTF(data, data_size, 15.0f * m_scale, nullptr, glyphs);
		m_fonts[14] = io.Fonts->AddFontFromMemoryTTF(data, data_size, 14.0f * m_scale, nullptr, glyphs);
		m_fonts[12] = io.Fonts->AddFontFromMemoryTTF(data, data_size, 12.0f * m_scale, nullptr, glyphs);

		for (int i = first_config + 1; i < io.Fonts->ConfigData.Size; i++)
			io.Fonts->ConfigData[i].FontDataOwnedByAtlas = false;
	} else {
		ImFont* font = io.Fonts->Fonts[0];
		for (const int size : { 20, 17, 15, 14, 12 })
			m_fonts[size] = font;
	}

	if (recreate_texture && has_texture)
		App.context->imguiRebuildFontTexture();
}

void Menu::toggle(bool force)
{
	m_visible = force ? true : !m_visible;

	if (m_visible) {
		m_options.vsync = App.vsync;
		m_options.window = App.window;
		m_options.foreground_fps = App.foreground_fps;
		m_options.background_fps = App.background_fps;
		m_options.unlock_cursor = App.cursor.unlock;

		m_closing = false;
	}
}

void Menu::check()
{
	if (m_closing) {
		win32::setCursorLock();
		SendMessage(App.hwnd, WM_SYSKEYUP, VK_CONTROL, 0);
		m_closing = false;
	}

	if (m_changed) {
		App.context->setFpsLimit(!App.vsync && App.foreground_fps.active, App.foreground_fps.range.value);

		win32::setWindowRect();
		win32::windowResize();
		m_changed = false;
	}
}

void Menu::draw()
{
	if (!m_visible)
		return;

	// Only safe point to rebuild the font atlas: outside a NewFrame/EndFrame
	// pair. Glyphs discovered while drawing are applied at the end of that
	// frame, which then gets discarded (see below).
	requestGlyphs(App.menu_title.c_str());
	if (m_glyphs_dirty) {
		m_glyphs_dirty = false;
		rebuildFontAtlas(true);
	}

	App.context->imguiStartFrame();

	ImVec2 window_pos = { (float)App.window.size.x * 0.5f, (float)App.window.size.y * 0.5f };
	ImVec2 max_size = { (float)App.window.size.x - 20.0f, (float)App.window.size.y - 20.0f };
	static ImGuiWindowFlags window_flags =
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
	static ImGuiCond window_pos_cond = ImGuiCond_Appearing;

	ImGui::SetNextWindowSize({ 680.0f * m_scale, 600.0f * m_scale }, ImGuiCond_Always);
	ImGui::SetNextWindowSizeConstraints({ 10.0f * m_scale, 10.0f * m_scale }, max_size);
	ImGui::SetNextWindowPos(window_pos, window_pos_cond, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowBgAlpha(0.90f);
	ImGui::PushFont(m_fonts[20]);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 10.0f * m_scale, 10.0f * m_scale});
	ImGui::Begin(App.menu_title.c_str(), &m_visible, window_flags);
	ImGui::PopStyleVar();
	window_pos_cond = ImGuiCond_Appearing;

	// clang-format off
	const ImVec4 col = ImColor(50, 50, 50);
	static int active_tab = 0;

	ImGui::PushStyleColor(ImGuiCol_Border, col);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 16.0f * m_scale, 10.0f * m_scale});
	if (ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_None)) {
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
		ImGui::SetCursorPos({ 530.0f * m_scale, 74.0f * m_scale});
		ImGui::PushFont(m_fonts[14]);
		ImGui::PushStyleColor(ImGuiCol_Text, m_colors[Color::Gray]);
		ImGui::Text("D2GL v%s by Bayaraa.", App.version_str.c_str());
		ImGui::PopStyleColor();
		ImGui::PopFont();	
		// ImGui::SetTabItemClosed("Screen");
		if (tabBegin("屏幕", 0, &active_tab)) {
			bool changed = m_options.pos_changed;
			childBegin("##w1", true, true);
			drawCheckbox_m("全屏", m_options.window.fullscreen, "如果未选中, 游戏将在窗口模式下运行.", fullscreen);
			checkChanged(m_options.window.fullscreen != App.window.fullscreen);
			ImGui::BeginDisabled(m_options.window.fullscreen);
				drawCheckbox_m("最大化", m_options.window.maximize, "如果选中, 游戏窗口将最大化.", maximize);
				checkChanged(m_options.window.maximize != App.window.maximize);
			ImGui::EndDisabled();
			ImGui::BeginDisabled(m_options.window.fullscreen);
				drawCheckbox_m("隐藏标题", m_options.window.hide_title_bar, "隐藏窗口标题栏", hide_title_bar);
				checkChanged(m_options.window.hide_title_bar != App.window.hide_title_bar);
			ImGui::EndDisabled();
			drawSeparator();
			ImGui::BeginDisabled(m_options.window.fullscreen || m_options.window.maximize);
				drawCombo_m("窗口大小", App.resolutions, "", false, 17, resolutions);
				checkChanged(App.resolutions.items[App.resolutions.selected].value != m_options.window.size_save);
				ImGui::Dummy({ 0.0f, 1.0f * m_scale});
				ImGui::BeginDisabled(App.resolutions.selected);
					drawInput2("##ws", "输入自定义宽度和高度(最小值: 800x600)", (glm::ivec2*)(&m_options.window.size_save),
						   { 800, 600 }, { App.desktop_resolution.z, App.desktop_resolution.w });
					checkChanged(!App.resolutions.selected && App.window.size != m_options.window.size_save);
				ImGui::EndDisabled();
				drawSeparator();
				drawCheckbox_m("窗口居中", m_options.window.centered, "在桌面上居中窗口.", centered_window);
				checkChanged(m_options.window.centered != App.window.centered);
				ImGui::Dummy({ 0.0f, 2.0f * m_scale });
				ImGui::BeginDisabled(m_options.window.centered);
					drawInput2("##wp", "窗口位置是从左上角开始的", &m_options.window.position, { App.desktop_resolution.x, App.desktop_resolution.y }, { App.desktop_resolution.z, App.desktop_resolution.w });
					checkChanged(!m_options.window.centered && App.window.position != m_options.window.position);
				ImGui::EndDisabled();
			ImGui::EndDisabled();
			childSeparator("##w2", true);
			drawCheckbox_m("V-Sync", m_options.vsync, "垂直同步", vsync);
			checkChanged(m_options.vsync != App.vsync);
			drawSeparator();
			ImGui::BeginDisabled(App.d2fps_mod);
				ImGui::BeginDisabled(m_options.vsync);
					drawCheckbox_m("前台最高 FPS", m_options.foreground_fps.active, "", foreground_fps);
					checkChanged(!m_options.vsync && m_options.foreground_fps.active != App.foreground_fps.active);
					ImGui::BeginDisabled(!m_options.foreground_fps.active);
						drawSlider_m(int, "", m_options.foreground_fps.range, "%d", "游戏窗口处于活动状态时的最高 FPS", foreground_fps_val);
						checkChanged(!m_options.vsync && m_options.foreground_fps.active && m_options.foreground_fps.range.value != App.foreground_fps.range.value);
					ImGui::EndDisabled();
				ImGui::EndDisabled();
				drawSeparator();
				drawCheckbox_m("后台最高 FPS", m_options.background_fps.active, "", background_fps);
				checkChanged(m_options.background_fps.active != App.background_fps.active);
				ImGui::BeginDisabled(!m_options.background_fps.active);
					drawSlider_m(int, "", m_options.background_fps.range, "%d", "游戏窗口处于非活动状态时的最高 FPS", background_fps_val);
					checkChanged(m_options.background_fps.active && m_options.background_fps.range.value != App.background_fps.range.value);
				ImGui::EndDisabled();
			ImGui::EndDisabled();
			drawSeparator();
			drawCheckbox_m("自动最小化", m_options.window.auto_minimize, "在全屏模式下失去焦点会自动最小化", auto_minimize);
			checkChanged(m_options.window.auto_minimize != App.window.auto_minimize);
			drawSeparator();
			drawCheckbox_m("深色模式", m_options.window.dark_mode, "深色窗口标题栏,设置后在下一次启动的时候会生效", dark_mode);
			checkChanged(m_options.window.dark_mode != App.window.dark_mode);
			drawSeparator();
			drawCheckbox_m("解锁光标", m_options.unlock_cursor, "光标将不会锁定在窗口内", unlock_cursor);
			checkChanged(m_options.unlock_cursor != App.cursor.unlock);
			childEnd();
			ImGui::BeginDisabled(!changed);
				if (drawNav("保存更改")) {
					if (App.resolutions.selected) {
						const auto val = App.resolutions.items[App.resolutions.selected].value;
						m_options.window.size_save = val;
					}

					if (App.window.size != m_options.window.size_save || App.window.fullscreen != m_options.window.fullscreen ||
					    App.window.maximize != m_options.window.maximize)
						window_pos_cond = ImGuiCond_Always;

					App.window.fullscreen = m_options.window.fullscreen;
					App.window.maximize = m_options.window.maximize;
					App.window.size = m_options.window.size_save;
					App.window.size_save = m_options.window.size_save;
					App.window.hide_title_bar = m_options.window.hide_title_bar;
					App.window.centered = m_options.window.centered;
					App.window.position = m_options.window.position;
					App.cursor.unlock = m_options.unlock_cursor;
					App.window.auto_minimize = m_options.window.auto_minimize;
					App.window.dark_mode = m_options.window.dark_mode;
					App.vsync = m_options.vsync;
					App.foreground_fps = m_options.foreground_fps;
					App.background_fps = m_options.background_fps;

					saveBool("Screen", "fullscreen", App.window.fullscreen);
					saveBool("Screen", "maximize", App.window.maximize);
					saveInt("Screen", "window_width", App.window.size.x);
					saveInt("Screen", "window_height", App.window.size.y);
					saveBool("Screen", "hide_title_bar", App.window.hide_title_bar);
					saveBool("Screen", "centered_window", App.window.centered);
					saveInt("Screen", "window_posx", App.window.position.x);
					saveInt("Screen", "window_posy", App.window.position.y);

					saveBool("Screen", "unlock_cursor", App.cursor.unlock);
					saveBool("Screen", "auto_minimize", App.window.auto_minimize);
					saveBool("Screen", "dark_mode", App.window.dark_mode);
					saveBool("Screen", "vsync", App.vsync);

					saveBool("Screen", "foreground_fps", App.foreground_fps.active);
					saveInt("Screen", "foreground_fps_value", App.foreground_fps.range.value);
					saveBool("Screen", "background_fps", App.background_fps.active);
					saveInt("Screen", "background_fps_value", App.background_fps.range.value);

					m_options.pos_changed = false;
					m_changed = true;
				}
			ImGui::EndDisabled();
			tabEnd();
		}
		if (tabBegin("图像", 1, &active_tab)) {
			drawCombo_m("高级着色器预设", App.shader.presets, "", true, 17, shader_preset);
			ImGui::SameLine(0.0f, 4.0f);
			ImGui::BeginDisabled(App.shader.selected == App.shader.presets.selected);
				if (drawButton("应用", { 100.0f, 0.0f }))
					App.shader.selected = App.shader.presets.selected;
			ImGui::EndDisabled();
			drawDescription("RetroArch's slang shader preset files (.slangp).", m_colors[Color::Gray]);
			childBegin("##w3", true);
			drawSeparator();
			drawCheckbox_m("光线锐化", App.sharpen.active, "", sharpen)
				saveBool("Graphic", "sharpen", App.sharpen.active);
			ImGui::BeginDisabled(!App.sharpen.active);
				drawSlider_m(float, "", App.sharpen.strength, "%.3f", "", sharpen_strength)
					saveFloat("Graphic", "sharpen_strength", App.sharpen.strength.value);
				drawDescription("锐化强度", m_colors[Color::Gray], 12);
				drawSlider_m(float, "", App.sharpen.clamp, "%.3f", "", sharpen_clamp)
					saveFloat("Graphic", "sharpen_clamp", App.sharpen.clamp.value);
				drawDescription("限制锐化像素的最大数量", m_colors[Color::Gray], 12);
				drawSlider_m(float, "", App.sharpen.radius, "%.3f", "", sharpen_radius)
					saveFloat("Graphic", "sharpen_radius", App.sharpen.radius.value);
				drawDescription("采样模型的半径", m_colors[Color::Gray], 12);
			ImGui::EndDisabled();
			drawSeparator();
			drawCheckbox_m("抗锯齿", App.fxaa.active, "快速近似抗锯齿", fxaa)
				saveBool("Graphic", "fxaa", App.fxaa.active);
			ImGui::BeginDisabled(!App.fxaa.active);
				drawCombo_m("", App.fxaa.presets, "抗锯齿品质预设", false, 17, fxaa_preset)
					saveInt("Graphic", "fxaa_preset", App.fxaa.presets.selected);
			ImGui::EndDisabled();
			childSeparator("##w4");
			drawSeparator();
			ImGui::BeginDisabled(!ISGLIDE3X());
				drawCombo_m("颜色校正", App.lut, "查找表 (LUT)", false, 17, lut)
					saveInt("Graphic", "lut", App.lut.selected);
				drawSeparator();
				drawCheckbox_m("光晕效果", App.bloom.active, "", bloom)
					saveBool("Graphic", "bloom", App.bloom.active);
				ImGui::BeginDisabled(!App.bloom.active);
					drawSlider_m(float, "", App.bloom.exposure, "%.3f", "", bloom_exposure)
						saveFloat("Graphic", "bloom_exposure", App.bloom.exposure.value);
					drawDescription("曝光设置", m_colors[Color::Gray], 12);
					drawSlider_m(float, "", App.bloom.gamma, "%.3f", "", bloom_gamma)
						saveFloat("Graphic", "bloom_gamma", App.bloom.gamma.value);
					drawDescription("伽马设置", m_colors[Color::Gray], 12);
				ImGui::EndDisabled();
			ImGui::EndDisabled();
			drawSeparator();
			drawLabel("拉伸视口", m_colors[Color::Orange]);
			drawCheckbox_m("水平", App.viewport.stretched.x, "", stretched_horizontal)
			{
				App.context->getCommandBuffer()->resize();
				saveBool("Graphic", "stretched_horizontal", App.viewport.stretched.x);
			}
			ImGui::SameLine(150.0f * m_scale);
			drawCheckbox_m("垂直", App.viewport.stretched.y, "", stretched_vertical)
			{
				App.context->getCommandBuffer()->resize();
				saveBool("Graphic", "stretched_vertical", App.viewport.stretched.y);
			}
			drawDescription("将视口拉伸到窗口大小", m_colors[Color::Gray], 14);
			childEnd();
			tabEnd();
		}
		if (tabBegin("功能", 2, &active_tab)) {
			childBegin("##w5", true);
			drawCheckbox_m("高清光标", App.hd_cursor, "游戏和菜单屏幕光标高清化", hd_cursor)
				saveBool("Feature", "hd_cursor", App.hd_cursor);
			drawSeparator();
			drawCheckbox_m("高清物品", App.hd_item.active, "游戏内物品高清化", hd_item)
				saveBool("Feature", "hd_item", App.hd_item.active);
			drawSeparator();
			drawCheckbox_m("高清字体", App.hd_text.active, "游戏内文字高清化", hd_text)
			{
				d2::patch_hd_text->toggle(App.hd_text.active);
				saveBool("Feature", "hd_text", App.hd_text.active);
			}
			drawSeparator();
			ImGui::BeginDisabled(!ISGLIDE3X() || !App.mini_map.available);
				drawCheckbox_m("小地图", App.mini_map.active, "始终显示在小地图组件上", mini_map)
				{
					d2::patch_minimap->toggle(App.mini_map.active);
					saveBool("Feature", "mini_map", App.mini_map.active);
				}
				ImGui::BeginDisabled(!App.mini_map.active);
					ImGui::Spacing();
					ImGui::Spacing();
					ImGui::SameLine(20.0f * m_scale);
					drawCheckbox_m("文字悬浮", App.mini_map.text_over, "", mini_map_text_over)
						saveBool("Feature", "mini_map_text_over", App.mini_map.text_over);
					ImGui::Dummy({ 0.0f, 2.0f });
					drawSlider_m(int, "", App.mini_map.width, "%d", "小地图宽度", mini_map_width_val)
						saveInt("Feature", "mini_map_width", App.mini_map.width.value);
					drawSlider_m(int, "", App.mini_map.height, "%d", "小地图高度", mini_map_height_val)
						saveInt("Feature", "mini_map_height", App.mini_map.height.value);
					if (App.mini_map.active)
						modules::MiniMap::Instance().resize();
				ImGui::EndDisabled();
			ImGui::EndDisabled();
			/*drawSeparator();
			ImGui::BeginDisabled(true);
				drawCheckbox_m("HD Orbs", App.hd_orbs.active, "High-definition life & mana orbs. (coming soon)", hd_orbs)
					saveBool("Feature", "hd_orbs", App.hd_orbs.active);
				ImGui::Spacing();
				ImGui::Spacing();
				ImGui::SameLine(36.0f);
				ImGui::BeginDisabled(!App.hd_orbs.active);
					drawCheckbox_m("Centered", App.hd_orbs.centered, "", hd_orbs_centered)
						saveBool("Feature", "hd_orbs_centered", App.hd_orbs.centered);
				ImGui::EndDisabled();
			ImGui::EndDisabled();*/
			childSeparator("##w6");
			ImGui::BeginDisabled(App.d2fps_mod);
				drawCheckbox_m("运动预测", App.motion_prediction, "D2DX 的运动预测功能", motion_prediction)
				{
					modules::MotionPrediction::Instance().toggle(App.motion_prediction);
					saveBool("Feature", "motion_prediction", App.motion_prediction);
				}
			ImGui::EndDisabled();
			drawSeparator();
			drawCheckbox_m("跳过介绍", App.skip_intro, "启动时自动跳过介绍视频", skip_intro)
				saveBool("Feature", "skip_intro", App.skip_intro);
			drawSeparator();
			drawCheckbox_m("No Pickup", App.no_pickup, "启动时自动运行 /nopickup 选项 (不包括1.09d)", no_pickup)
				saveBool("Feature", "no_pickup", App.no_pickup);
			drawSeparator();
			drawCheckbox_m("显示怪物抗性", App.show_monster_res, "在怪物生命条上显示抗性", show_monster_res)
				saveBool("Feature", "show_monster_res", App.show_monster_res);
			drawSeparator();
			drawCheckbox_m("显示物品数量", App.show_item_quantity, "在图标左下角显示商品数量", show_item_quantity)
				saveBool("Feature", "show_item_quantity", App.show_item_quantity);
			drawSeparator();
			drawCheckbox_m("显示 FPS", App.show_fps, "在底部中央显示 FPS 计数器", show_fps)
				saveBool("Feature", "show_fps", App.show_fps);
			childEnd();
			tabEnd();
		}
		if (tabBegin("字体", 4, &active_tab)) {
			static bool font_changed = false;
			static bool font_restart_pending = false;

			childBegin("##font0", true, true);
			ImGui::BeginDisabled(ISHDTEXT());

			static std::vector<std::string> font_paths;
			static Select<int> font_files;
			static std::vector<std::string> ascii_font_paths;
			static Select<int> ascii_font_files;
			static bool font_list_ready = false;
			static int opt_font_file = 0;
			static int opt_ascii_font = 0;
			if (!font_list_ready) {
				refreshFontList(font_paths, font_files, App.hd_text.font_file);
				refreshFontList(ascii_font_paths, ascii_font_files, App.hd_text.font_file_ascii, true);
				opt_font_file = font_files.selected;
				opt_ascii_font = ascii_font_files.selected;
				font_list_ready = true;
			}

			if (!App.hd_text.active)
				drawDescription("提示: 请先在 [功能] 页启用 高清字体。", m_colors[Color::Gray], 12);

			drawLabel("本地字体", m_colors[Color::Orange], 17);
			ImGui::SameLine();
			drawDescription("字体放入data\\assets\\fonts", m_colors[Color::Gray], 12);

			if (drawCombo("", &font_files, "", true, &opt_font_file, 17)) {
				App.hd_text.font_file = font_files.selected < (int)font_paths.size() ? font_paths[font_files.selected] : std::string();
				saveString("Feature", "hd_text_font", App.hd_text.font_file);
				font_changed = true;
				// Rebuild glyph sets and the atlas immediately; fall back to a
				// restart hint if the new font cannot be loaded live.
				font_restart_pending = !modules::HDText::Instance().reloadFonts();
			}
			ImGui::SameLine();
			if (drawButton("刷新列表", { 96.0f * m_scale, 29.0f * m_scale }, 15)) {
				refreshFontList(font_paths, font_files, App.hd_text.font_file);
				refreshFontList(ascii_font_paths, ascii_font_files, App.hd_text.font_file_ascii, true);
				opt_font_file = font_files.selected;
				opt_ascii_font = ascii_font_files.selected;
			}
			if (font_restart_pending)
				drawDescription("字体切换失败, 重启游戏后生效", m_colors[Color::Orange], 12);

			if (drawCombo("ASCII 字体", &ascii_font_files, "0-255 用这个字体, 其余 (中文等) 走上面的字体", false, &opt_ascii_font, 17)) {
				App.hd_text.font_file_ascii = ascii_font_files.selected < (int)ascii_font_paths.size() ? ascii_font_paths[ascii_font_files.selected] : std::string();
				saveString("Feature", "hd_text_font_ascii", App.hd_text.font_file_ascii);
				font_changed = true;
				font_restart_pending = !modules::HDText::Instance().reloadFonts();
			}
			drawSlider_m(float, "ASCII 加粗(px)", App.hd_text.ascii_bold, "%.2f", "仅 ASCII 字体的额外加粗, 用于和中文对齐字重", ascii_bold)
				saveFloat("Feature", "hd_text_ascii_bold", App.hd_text.ascii_bold.value);

			drawCombo_m("渲染方式", App.hd_text.render_mode, "", false, 17, hd_render_mode)
			{
				saveInt("Feature", "hd_text_render_mode", App.hd_text.render_mode.selected);
				font_changed = true;
				font_restart_pending = !modules::HDText::Instance().reloadFonts();
			}
			drawSeparator();

			drawSlider_m(float, "字号", App.hd_text.scale, "%.0f", "整体字体大小 (百分比)", hd_text_size)
			{
				font_changed = true;
				if (App.hd_text.active)
					modules::HDText::Instance().updateFontSize();
			}
			drawSlider_m(float, "字宽", App.hd_text.char_width, "%.0f", "字形横向缩放 (百分比)", hd_text_char_width)
				font_changed = true;
			drawSlider_m(float, "行高", App.hd_text.line_height, "%.0f", "行距 (百分比)", hd_text_line_height)
				font_changed = true;
			drawSlider_m(float, "字距", App.hd_text.letter_spacing, "%.2f", "额外字间距 (em)", hd_text_letter_spacing)
				font_changed = true;
			drawSlider_m(float, "基线", App.hd_text.baseline, "%.2f", "整体垂直偏移 (em)", hd_text_baseline)
				font_changed = true;
			drawCheckbox_m("像素对齐", App.hd_text.snap, "字形位置对齐到整数像素", snap)
				font_changed = true;

			childSeparator("##font1", true);

			drawSlider_m(float, "笔画增强", App.hd_text.stroke, "%.2f", "字形加粗强度", hd_text_stroke)
				font_changed = true;
			drawSlider_m(float, "加粗(px)", App.hd_text.bold, "%.2f", "额外的像素加粗量", hd_text_bold)
				font_changed = true;
			drawSlider_m(float, "实心度", App.hd_text.solidity, "%.2f", "提高字形内部实心程度", hd_text_solidity)
				font_changed = true;
			drawSlider_m(float, "中心提亮", App.hd_text.center, "%.2f", "字形中心向白色提亮, 边缘不变", hd_text_center)
				font_changed = true;
			drawSlider_m(float, "边缘锐化", App.hd_text.sharpness, "%.2f", "SDF 边缘过渡宽度 (越大越锐)", hd_text_sharpness)
				font_changed = true;
			drawSlider_m(float, "描边宽度", App.hd_text.border, "%.2f", "仅带边框字体槽 (2/3/7/18), 0 = 不描边", hd_text_border)
				font_changed = true;
			drawSlider_m(float, "阴影偏移", App.hd_text.shadow_offset, "%.2f", "阴影距离 (32px-em 单位)", hd_text_shadow_offset)
				font_changed = true;
			drawDescription("以上效果仅影响运行时 SDF 字体。", m_colors[Color::Gray], 12);

			ImGui::EndDisabled();
			childEnd();

			ImGui::BeginDisabled(!font_changed);
			if (drawNav("保存更改")) {
				saveFloat("Feature", "hd_text_scale", App.hd_text.scale.value);
				saveFloat("Feature", "hd_text_char_width", App.hd_text.char_width.value);
				saveFloat("Feature", "hd_text_line_height", App.hd_text.line_height.value);
				saveFloat("Feature", "hd_text_letter_spacing", App.hd_text.letter_spacing.value);
				saveFloat("Feature", "hd_text_baseline", App.hd_text.baseline.value);
				saveFloat("Feature", "hd_text_stroke", App.hd_text.stroke.value);
				saveFloat("Feature", "hd_text_bold", App.hd_text.bold.value);
				saveFloat("Feature", "hd_text_solidity", App.hd_text.solidity.value);
				saveFloat("Feature", "hd_text_center", App.hd_text.center.value);
				saveFloat("Feature", "hd_text_sharpness", App.hd_text.sharpness.value);
				saveFloat("Feature", "hd_text_border", App.hd_text.border.value);
				saveFloat("Feature", "hd_text_shadow_offset", App.hd_text.shadow_offset.value);
				saveBool("Feature", "hd_text_snap", App.hd_text.snap);
				saveInt("Feature", "hd_text_render_mode", App.hd_text.render_mode.selected);
				saveString("Feature", "hd_text_font", App.hd_text.font_file);
				saveInt("Feature", "hd_text_font_index", App.hd_text.font_index);
				font_changed = false;
			}
			ImGui::EndDisabled();
			tabEnd();
		}
#ifdef _HDTEXT
		if (tabBegin("HD Text", 3, &active_tab)) {
			ImGuiIO& io = ImGui::GetIO();
			ImGui::PushFont(io.Fonts->Fonts[0]);
			m_ignore_font = true;
			drawCombo_m("", App.hdt.fonts, "", "", 17, hdt_fonts)
			{
				const auto font_id = (uint32_t)App.hdt.fonts.items[App.hdt.fonts.selected].value;
				modules::HDText::Instance().getFont(font_id)->getMetrics();
			}
			drawSeparator();
			childBegin("##hdt0", true);
			drawSlider_m(float, "Font Size", App.hdt.size, "%.3f", "", hdt_size);
			drawSlider_m(float, "Font Weight", App.hdt.weight, "%.3f", "", hdt_weight);
			drawSlider_m(float, "Letter Spacing", App.hdt.letter_spacing, "%.3f", "", hdt_letter_spacing);
			drawSlider_m(float, "Line Height", App.hdt.line_height, "%.3f", "", hdt_line_height);
			drawSlider_m(float, "Shadow Intensity", App.hdt.shadow_intensity, "%.3f", "", hdt_shadow_intensity);
			drawSlider_m(float, "Text Offset (X Coordinate)", App.hdt.offset_x, "%.3f", "", hdt_offset_x);
			drawSlider_m(float, "Text Offset (Y Coordinate)", App.hdt.offset_y, "%.3f", "", hdt_offset_y);
			drawSlider_m(float, "Symbol Offset (Y Coordinate)", App.hdt.symbol_offset, "%.3f", "", hdt_symbol_offset);
			childSeparator("##hdt1");
			ImGui::BeginDisabled(App.game.screen != GameScreen::InGame);
			drawCheckbox_m("Show sample text", App.hdt.show_sample, "", hdt_show_sample);
			ImGui::EndDisabled();
			drawSeparator();
			const auto font_id = (uint32_t)App.hdt.fonts.items[App.hdt.fonts.selected].value;
			const auto font = modules::HDText::Instance().getFont(font_id);
			font->updateMetrics();
			char result[2100] = { "" };
			const auto& str = modules::HDText::Instance().getAllFontMetricString();
			strcpy_s(result, str.c_str());
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 8.0f, 7.0f });
			ImGui::SetNextItemWidth(ImGui::GetWindowContentRegionWidth());
			ImGui::InputTextMultiline("##result", result, IM_ARRAYSIZE(result), { 0, 249 }, ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoHorizontalScroll);
			ImGui::PopStyleVar();
			if (ImGui::Button("Copy Text"))
				ImGui::SetClipboardText(result);
			childEnd();
			m_ignore_font = false;
			ImGui::PopFont();
			tabEnd();
		}
#endif
#ifdef _DEBUG
		if (tabBegin("Debug", 3, &active_tab)) {
			ImGuiIO& io = ImGui::GetIO();
			ImGui::PushFont(io.Fonts->Fonts[0]);
			ImGui::Checkbox("Check6", (bool*)(&App.var[6]));
			ImGui::PopFont();
			tabEnd();
		}
#endif
		ImGui::EndTabBar();
	}
	ImGui::PopFont();
	if (active_tab != 3) {
		ImGui::SetCursorPos({ 16.0f * m_scale, 500.0f * m_scale });
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 0.0f, 0.0f });
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
		ImGui::BeginChildFrame(ImGui::GetID("#wiki"), { 300.0f * m_scale, 24.0f * m_scale}, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);
		ImGui::PopStyleVar(3);
		requestGlyphs(" 打开配置 Wiki 页面 >");
		ImGui::PushFont(m_fonts[15]);
		if (ImGui::Button(" 打开配置 Wiki 页面 >"))
			ShellExecute(0, 0, L"https://github.com/bayaraa/d2gl/wiki/Configuration", 0, 0, SW_SHOW);
		ImGui::PopFont();
		ImGui::EndChildFrame();
	}
	ImGui::End();

	// clang-format on
	if (!m_visible)
		m_closing = true;

	// New glyphs discovered while drawing are not in the current atlas yet.
	// Drop this frame's draw data and rebuild before the next frame so text is
	// never presented with fallback glyphs (which would also shift layout).
	if (m_glyphs_dirty) {
		m_glyphs_dirty = false;
		ImGui::Render(); // finalize the frame; its draw data is discarded
		rebuildFontAtlas(true);
		return;
	}

	App.context->imguiRender();
}

bool Menu::tabBegin(const char* title, int tab_num, int* active_tab)
{
	static const ImVec4 inactive_col = ImColor(100, 100, 100, 200);
	requestGlyphs(title);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 16.0f * m_scale, 10.0f * m_scale});
	ImGui::PushStyleColor(ImGuiCol_Text, tab_num == *active_tab ? m_colors[Color::Orange] : inactive_col);
	bool ret = ImGui::BeginTabItem(title);
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
	if (ret) {
		*active_tab = tab_num;
		drawSeparator(2.0f, 0.0f);
	}
	return ret;
}

void Menu::tabEnd()
{
	ImGui::EndTabItem();
}

void Menu::childBegin(const char* id, bool half_width, bool with_nav)
{
	ImVec2 size = { 0.0f, with_nav ? -60.0f * m_scale: 0.0f };
	if (half_width)
		size.x = ImGui::GetWindowContentRegionWidth() / 2.0f - 8.0f;

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 0.0f, 0.0f });
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
	ImGui::BeginChildFrame(ImGui::GetID(id), size, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);
	ImGui::PopStyleVar(3);
}

void Menu::childSeparator(const char* id, bool with_nav)
{
	childEnd();
	ImGui::SameLine(0.0f, 16.0f * m_scale);
	childBegin(id, true, with_nav);
}

void Menu::childEnd()
{
	ImGui::EndChildFrame();
}

bool Menu::drawNav(const char* btn_label)
{
	static bool ret = false;
	requestGlyphs(btn_label);
	const ImVec4 sep_col = ImColor(40, 40, 40);
	ImGui::PushStyleColor(ImGuiCol_Separator, sep_col);
	ImGui::PushStyleColor(ImGuiCol_Text, m_colors[Color::Orange]);
	drawSeparator(6.0f);
	ImGui::Dummy({ ImGui::GetWindowContentRegionWidth() - 200.0f * m_scale, 0.0f });
	ImGui::SameLine();
	ret = ImGui::Button(btn_label, { 192.0f * m_scale, 36.0f * m_scale});
	ImGui::PopStyleColor(2);
	return ret;
}

bool Menu::drawCheckbox(const char* title, bool* option, const char* desc, bool* opt)
{
	requestGlyphs(title);
	requestGlyphs(desc);

	ImGui::PushStyleColor(ImGuiCol_Text, m_colors[Color::Orange]);
	if (!m_ignore_font)
		ImGui::PushFont(m_fonts[17]);
	ImGui::Checkbox(title, option);
	if (!m_ignore_font)
		ImGui::PopFont();
	ImGui::PopStyleColor();
	drawDescription(desc, m_colors[Color::Gray]);

	if (*option != *opt) {
		*opt = *option;
		return true;
	}
	return false;
}

template <typename T>
bool Menu::drawCombo(const char* title, Select<T>* select, const char* desc, bool have_btn, int* opt, int size)
{
	bool ret = false;

	requestGlyphs(title);
	requestGlyphs(desc);
	for (const auto& item : select->items)
		requestGlyphs(item.name.c_str());

	drawLabel(title, m_colors[Color::Orange], 17);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 8.0f * m_scale, 5.0f * m_scale });
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0f, 8.0f * m_scale });
	if (!m_ignore_font)
		ImGui::PushFont(m_fonts[size]);
	ImGui::SetNextItemWidth(ImGui::GetWindowContentRegionWidth() - (have_btn ? 104.0f * m_scale : 0.0f));
	auto& selected = select->items[select->selected];
	if (ImGui::BeginCombo(("##" + std::string(title)).c_str(), selected.name.c_str(), ImGuiComboFlags_PopupAlignLeft | ImGuiComboFlags_HeightLargest)) {
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 8.0f * m_scale, 8.0f * m_scale });
		for (size_t i = 0; i < select->items.size(); i++) {
			const bool is_selected = (select->selected == i);
			if (is_selected)
				ImGui::PushStyleColor(ImGuiCol_Text, m_colors[Color::Orange]);
			if (ImGui::Selectable(select->items[i].name.c_str(), is_selected)) {
				select->selected = i;
				if (select->selected != *opt) {
					*opt = select->selected;
					ret = true;
				}
			}
			if (is_selected) {
				ImGui::SetItemDefaultFocus();
				ImGui::PopStyleColor();
			}
		}
		ImGui::PopStyleVar();
		ImGui::EndCombo();
	}
	if (!m_ignore_font)
		ImGui::PopFont();
	ImGui::PopStyleVar(2);
	drawDescription(desc, m_colors[Color::Gray]);
	return ret;
}

void Menu::drawInput2(const std::string& id, const char* desc, glm::ivec2* input, glm::ivec2 min, glm::ivec2 max)
{
	requestGlyphs(desc);

	float width = ImGui::GetWindowContentRegionWidth() / 2 - 2.0f;
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 8.0f * m_scale, 5.0f * m_scale});
	ImGui::PushFont(m_fonts[17]);
	ImGui::SetNextItemWidth(width);
	ImGui::InputInt((id + "x").c_str(), &input->x, 0, 0);
	ImGui::SameLine(0.0f, 4.0f * m_scale);
	ImGui::SetNextItemWidth(width);
	ImGui::InputInt((id + "y").c_str(), &input->y, 0, 0);
	ImGui::PopFont();
	ImGui::PopStyleVar();
	drawDescription(desc, m_colors[Color::Gray]);

	if (input->x < min.x)
		input->x = min.x;
	if (input->x > max.x)
		input->x = max.x;

	if (input->y < min.y)
		input->y = min.y;
	if (input->y > max.y)
		input->y = max.y;
}

template <typename T>
bool Menu::drawSlider(const std::string& id, const char* title, Range<T>* range, const char* format, const char* desc, T* opt)
{
	requestGlyphs(title);
	requestGlyphs(desc);

	drawLabel(title, m_colors[Color::Orange]);
	ImGuiDataType type = strcmp(typeid(T).name(), "int") == 0 ? ImGuiDataType_S32 : ImGuiDataType_Float;
	ImGui::SetNextItemWidth(80.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 4.0f, 3.0f });
	if (!m_ignore_font)
		ImGui::PushFont(m_fonts[15]);
	ImGui::InputScalar(("##" + id).c_str(), type, &range->value, 0, 0, format);
	ImGui::SameLine(0.0f, 4.0f);
	ImGui::SetNextItemWidth(ImGui::GetWindowContentRegionWidth() - 84.0f);
	ImGui::SliderScalar(id.c_str(), type, &range->value, &range->min, &range->max, "", ImGuiSliderFlags_NoInput);
	if (!m_ignore_font)
		ImGui::PopFont();
	ImGui::PopStyleVar();
	drawDescription(desc, m_colors[Color::Gray]);

	if (range->value < range->min)
		range->value = range->min;
	if (range->value > range->max)
		range->value = range->max;

	if (range->value != *opt && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		*opt = range->value;
		return true;
	}
	return false;
}

bool Menu::drawButton(const char* label, const ImVec2& btn_size, int size)
{
	bool ret = false;

	requestGlyphs(label);

	ImGui::PushStyleColor(ImGuiCol_Text, m_colors[Color::Orange]);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 8.0f, 5.0f });
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0f, 8.0f });
	if (!m_ignore_font)
		ImGui::PushFont(m_fonts[size]);
	if (ImGui::Button(label, btn_size))
		ret = true;
	if (!m_ignore_font)
		ImGui::PopFont();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();
	return ret;
}

void Menu::drawSeparator(float y_padd, float alpha)
{
	ImGui::Dummy({ 0.0f, y_padd - 1.0f });
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0f, 0.0f });
	ImGui::Separator();
	ImGui::PopStyleVar(2);
	ImGui::Dummy({ 0.0f, y_padd });
}

void Menu::drawLabel(const char* title, const ImVec4& color, int size)
{
	if (!title || !title[0] || title[0] == ' ')
		return;

	requestGlyphs(title);

	if (!m_ignore_font)
		ImGui::PushFont(m_fonts[size]);
	ImGui::PushStyleColor(ImGuiCol_Text, color);
	ImGui::Text(title);
	ImGui::PopStyleColor();
	if (!m_ignore_font)
		ImGui::PopFont();
}

void Menu::drawDescription(const char* desc, const ImVec4& color, int size)
{
	if (!desc || !desc[0])
		return;

	requestGlyphs(desc);

	if (!m_ignore_font)
		ImGui::PushFont(m_fonts[size]);
	ImGui::PushStyleColor(ImGuiCol_Text, color);
	ImGui::Text(desc);
	ImGui::PopStyleColor();
	if (!m_ignore_font)
		ImGui::PopFont();
}

}
