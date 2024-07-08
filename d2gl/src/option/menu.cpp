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

#include "chinese.h"
static const ImWchar * buildSimplifiedChineseRange()
{
    static ImWchar base_ranges[] = // not zero-terminated
    {
        0x0020, 0x00FF, // Basic Latin + Latin Supplement
        0x2000, 0x206F, // General Punctuation
        0x3000, 0x30FF, // CJK Symbols and Punctuations, Hiragana, Katakana
        0x31F0, 0x31FF, // Katakana Phonetic Extensions
    };

    static ImWchar full_ranges[IM_ARRAYSIZE(base_ranges) + IM_ARRAYSIZE(simplified_chinese_chars) * 2 + 1] = { 0 };
    if (!full_ranges[0])
    {
        memcpy(full_ranges, base_ranges, sizeof(base_ranges));

	ImWchar * offset = &full_ranges[IM_ARRAYSIZE(base_ranges)];
	for (int i = 0; i < IM_ARRAYSIZE(simplified_chinese_chars); i += 1, offset += 2) {
		offset[0] = offset[1] = (ImWchar)simplified_chinese_chars[i];
	}
	offset[0] = 0;
    }
    return &full_ranges[0];
}

Menu::Menu()
{
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
	style.FramePadding = { 4.0f, 4.0f };
	style.FrameBorderSize = 1.0f;
	style.ChildRounding = 0.0f;
	style.TabRounding = 2.0f;
	style.PopupRounding = 2.0f;
	style.GrabRounding = 1.0f;
	style.GrabMinSize = 30.0f;
	style.DisabledAlpha = 0.4f;

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	io.IniFilename = NULL;

	BufferData font1 = helpers::loadFile("assets\\fonts\\Chi.ttf");
	BufferData &font2 = font1;
	auto glyphs = buildSimplifiedChineseRange();

	io.Fonts->AddFontDefault();
	m_fonts[20] = font1.size ? io.Fonts->AddFontFromMemoryTTF((void*)font1.data, font1.size, 20.0f,
								  nullptr, glyphs) : io.Fonts->Fonts[0];
	m_fonts[17] = font1.size ? io.Fonts->AddFontFromMemoryTTF((void*)font1.data, font1.size, 17.0f,
								  nullptr, glyphs) : io.Fonts->Fonts[0];
	m_fonts[15] = font1.size ? io.Fonts->AddFontFromMemoryTTF((void*)font1.data, font1.size, 15.0f,
								  nullptr, glyphs) : io.Fonts->Fonts[0];
	m_fonts[14] = font2.size ? io.Fonts->AddFontFromMemoryTTF((void*)font2.data, font2.size, 14.0f,
								  nullptr, glyphs) : io.Fonts->Fonts[0];
	m_fonts[12] = font2.size ? io.Fonts->AddFontFromMemoryTTF((void*)font2.data, font2.size, 12.0f,
								  nullptr, glyphs) : io.Fonts->Fonts[0];

	App.menu_title += (ISGLIDE3X() ? " (Glide / " : " (DDraw / ");
	App.menu_title += "OpenGL: " + App.gl_ver_str + " / D2LoD: " + helpers::getVersionString() + " / " + helpers::getLangString() + ")";
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

	App.context->imguiStartFrame();

	ImVec2 window_pos = { (float)App.window.size.x * 0.5f, (float)App.window.size.y * 0.5f };
	ImVec2 max_size = { (float)App.window.size.x - 20.0f, (float)App.window.size.y - 20.0f };
	static ImGuiWindowFlags window_flags =
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
	static ImGuiCond window_pos_cond = ImGuiCond_Appearing;

	ImGui::SetNextWindowSize({ 680.0f, 600.0f }, ImGuiCond_Always);
	ImGui::SetNextWindowSizeConstraints({ 10.0f, 10.0f }, max_size);
	ImGui::SetNextWindowPos(window_pos, window_pos_cond, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowBgAlpha(0.90f);
	ImGui::PushFont(m_fonts[20]);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 10.0f, 10.0f });
	ImGui::Begin(App.menu_title.c_str(), &m_visible, window_flags);
	ImGui::PopStyleVar();
	window_pos_cond = ImGuiCond_Appearing;

	// clang-format off
	const ImVec4 col = ImColor(50, 50, 50);
	static int active_tab = 0;

	ImGui::PushStyleColor(ImGuiCol_Border, col);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 16.0f, 10.0f });
	if (ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_None)) {
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
		ImGui::SetCursorPos({ 530.0f, 74.0f });
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
			drawSeparator();
			ImGui::BeginDisabled(m_options.window.fullscreen);
				drawCombo_m("窗口大小", App.resolutions, "", false, 17, resolutions);
				checkChanged(App.resolutions.items[App.resolutions.selected].value != m_options.window.size_save);
				ImGui::Dummy({ 0.0f, 1.0f });
				ImGui::BeginDisabled(App.resolutions.selected);
					drawInput2("##ws", "输入自定义宽度和高度(最小值: 800x600)", (glm::ivec2*)(&m_options.window.size_save), { 800, 600 }, { App.desktop_resolution.z, App.desktop_resolution.w });
					checkChanged(!App.resolutions.selected && App.window.size != m_options.window.size_save);
				ImGui::EndDisabled();
				drawSeparator();
				drawCheckbox_m("隐藏标题", m_options.window.hide_title_bar, "隐藏窗口标题栏", hide_title_bar);
				checkChanged(m_options.window.hide_title_bar != App.window.hide_title_bar);
				drawSeparator();
				drawCheckbox_m("窗口居中", m_options.window.centered, "在桌面上居中窗口.", centered_window);
				checkChanged(m_options.window.centered != App.window.centered);
				ImGui::Dummy({ 0.0f, 2.0f });
				ImGui::BeginDisabled(m_options.window.centered);
					drawInput2("##wp", "窗口位置是从左上角开始的", &m_options.window.position, { App.desktop_resolution.x, App.desktop_resolution.y }, { App.desktop_resolution.z, App.desktop_resolution.w });
					checkChanged(!m_options.window.centered && App.window.position != m_options.window.position);
				ImGui::EndDisabled();
			ImGui::EndDisabled();
			drawSeparator();
			drawCheckbox_m("解锁光标", m_options.unlock_cursor, "光标将不会锁定在窗口内", unlock_cursor);
			checkChanged(m_options.unlock_cursor != App.cursor.unlock);
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
			childEnd();
			ImGui::BeginDisabled(!changed);
				if (drawNav("保存更改")) {
					if (App.resolutions.selected) {
						const auto val = App.resolutions.items[App.resolutions.selected].value;
						m_options.window.size_save = val;
					}

					if (App.window.size != m_options.window.size_save || App.window.fullscreen != m_options.window.fullscreen)
						window_pos_cond = ImGuiCond_Always;

					App.window.fullscreen = m_options.window.fullscreen;
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
			ImGui::SameLine(150.0f);
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
			drawCheckbox_m("高清字体", App.hd_text.active, "游戏内文字高清化", hd_text)
			{
				d2::patch_hd_text->toggle(App.hd_text.active);
				saveBool("Feature", "hd_text", App.hd_text.active);
			}
			ImGui::BeginDisabled(!App.hd_text.active || ISHDTEXT());
				drawSlider_m(float, "", App.hd_text.scale, "%.3f", "全局高清字体缩放", hd_text_scale)
					saveFloat("Feature", "hd_text_scale", App.hd_text.scale.value);
				if (App.hd_text.active)
					modules::HDText::Instance().updateFontSize();
			ImGui::EndDisabled();
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
					ImGui::SameLine(20.0f);
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
		ImGui::SetCursorPos({ 16.0f, 500.0f });
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 0.0f, 0.0f });
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
		ImGui::BeginChildFrame(ImGui::GetID("#wiki"), { 300.0f, 24.0f }, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);
		ImGui::PopStyleVar(3);
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

	App.context->imguiRender();
}

bool Menu::tabBegin(const char* title, int tab_num, int* active_tab)
{
	static const ImVec4 inactive_col = ImColor(100, 100, 100, 200);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 16.0f, 10.0f });
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
	ImVec2 size = { 0.0f, with_nav ? -60.0f : 0.0f };
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
	ImGui::SameLine(0.0f, 16.0f);
	childBegin(id, true, with_nav);
}

void Menu::childEnd()
{
	ImGui::EndChildFrame();
}

bool Menu::drawNav(const char* btn_label)
{
	static bool ret = false;
	const ImVec4 sep_col = ImColor(40, 40, 40);
	ImGui::PushStyleColor(ImGuiCol_Separator, sep_col);
	ImGui::PushStyleColor(ImGuiCol_Text, m_colors[Color::Orange]);
	drawSeparator(6.0f);
	ImGui::Dummy({ ImGui::GetWindowContentRegionWidth() - 200.0f, 0.0f });
	ImGui::SameLine();
	ret = ImGui::Button(btn_label, { 192.0f, 36.0f });
	ImGui::PopStyleColor(2);
	return ret;
}

bool Menu::drawCheckbox(const char* title, bool* option, const char* desc, bool* opt)
{
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

	drawLabel(title, m_colors[Color::Orange], 17);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 8.0f, 5.0f });
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0f, 8.0f });
	if (!m_ignore_font)
		ImGui::PushFont(m_fonts[size]);
	ImGui::SetNextItemWidth(ImGui::GetWindowContentRegionWidth() - (have_btn ? 104.0f : 0.0f));
	auto& selected = select->items[select->selected];
	if (ImGui::BeginCombo(("##" + std::string(title)).c_str(), selected.name.c_str(), ImGuiComboFlags_PopupAlignLeft | ImGuiComboFlags_HeightLargest)) {
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 8.0f, 8.0f });
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
	float width = ImGui::GetWindowContentRegionWidth() / 2 - 2.0f;
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 8.0f, 5.0f });
	ImGui::PushFont(m_fonts[17]);
	ImGui::SetNextItemWidth(width);
	ImGui::InputInt((id + "x").c_str(), &input->x, 0, 0);
	ImGui::SameLine(0.0f, 4.0f);
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

	if (!m_ignore_font)
		ImGui::PushFont(m_fonts[size]);
	ImGui::PushStyleColor(ImGuiCol_Text, color);
	ImGui::Text(desc);
	ImGui::PopStyleColor();
	if (!m_ignore_font)
		ImGui::PopFont();
}

}
