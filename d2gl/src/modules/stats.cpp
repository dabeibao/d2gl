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
#include "stats.h"
#include "d2/common.h"

namespace d2gl {
namespace stats {

#ifdef _STATS

// Per-sample (row) interval. Lower = finer bottleneck correlation, more rows.
static const uint64_t kSampleIntervalMs = 500;

static std::atomic<uint64_t> g_timer_ticks[TIMER_COUNT] = {};
static std::atomic<uint64_t> g_counts[CTR_COUNT] = {};

static uint64_t g_qpf = 0;
static uint64_t g_start_ticks = 0;
static uint64_t g_last_sample_ticks = 0;
static uint64_t g_prev_timer_ticks[TIMER_COUNT] = {};
static uint64_t g_prev_counts[CTR_COUNT] = {};

// Per-frame frame-times (game thread only), drained every sample.
static std::deque<double> g_frame_times;
static FILE* g_file = nullptr;

// CPU draw phase: beginFrame() -> presentFrame() (game thread only).
static uint64_t g_draw_start_ticks = 0;

static void sample();

uint64_t nowTicks()
{
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return (uint64_t)li.QuadPart;
}

void addTicks(TimerId id, uint64_t ticks)
{
	g_timer_ticks[id].fetch_add(ticks, std::memory_order_relaxed);
}

static uint64_t ticksToUs(uint64_t ticks)
{
	return g_qpf ? (ticks * 1000000ull / g_qpf) : 0ull;
}

const char* timerNames[TIMER_COUNT] = {
	"cpu_draw",
	"cpu_present",
	"cpu_gpu_wait",
	"cpu_stage",
	"cpu_modules",
	"cpu_hot_push",
	"gpu_process",
	"gpu_upload_vertex",
	"gpu_upload_tex",
	"gpu_commands",
	"gpu_prefx",
	"gpu_submit",
	"gpu_overlay",
	"gpu_flush",
	"gpu_swap",
	"gpu_limiter",
};

const char* counterNames[CTR_COUNT] = {
	"frames",
	"vertices_sd",
	"vertices_mod",
	"overlay_runs",
	"drawcalls",
	"commands",
	"set_blend",
	"ubo_updates",
	"tex_updates",
	"tex_bytes",
	"units",
	"monsters",
	"players",
	"items",
	"missiles",
	"weather",
	"tiles",
	"images",
	"shadows",
	"rects",
	"lines",
	"texts",
	"hd_objects",
	"font_pages",
	"external_uploads",
	"flushes",
};

static const char* screenName(GameScreen screen)
{
	switch (screen) {
		case GameScreen::Menu: return "Menu";
		case GameScreen::InGame: return "InGame";
		case GameScreen::Movie: return "Movie";
		case GameScreen::Loading: return "Loading";
	}
	return "Unknown";
}

void init()
{
	if (g_qpf)
		return;

	LARGE_INTEGER qpf;
	QueryPerformanceFrequency(&qpf);
	g_qpf = (uint64_t)qpf.QuadPart;
	g_start_ticks = nowTicks();
	g_last_sample_ticks = g_start_ticks;

	for (uint32_t i = 0; i < TIMER_COUNT; i++)
		g_prev_timer_ticks[i] = 0;
	for (uint32_t i = 0; i < CTR_COUNT; i++)
		g_prev_counts[i] = 0;

	if (fopen_s(&g_file, "d2gl_stats.csv", "w") != 0)
		g_file = nullptr;
	if (!g_file)
		return;

	fprintf(g_file, "# D2GL profiling stats (compiled with _STATS). One row per %llu ms sample.\n", kSampleIntervalMs);
	fprintf(g_file, "# Consume with: python tools/analyze_stats.py d2gl_stats.csv\n");
	fprintf(g_file, "time,elapsed_s,");
	fprintf(g_file, "fps_min,fps_avg,fps_max,frame_ms_min,frame_ms_avg,frame_ms_p99,frame_ms_max,");
	for (uint32_t i = 0; i < TIMER_COUNT; i++)
		fprintf(g_file, "%s_ms,", timerNames[i]);
	for (uint32_t i = 0; i < CTR_COUNT; i++)
		fprintf(g_file, "%s,", counterNames[i]);
	fprintf(g_file, "screen,game_w,game_h,window_w,window_h,vsync,frame_latency,active\n");
	fflush(g_file);
}

void shutdown()
{
	sample();
	if (g_file) {
		fclose(g_file);
		g_file = nullptr;
	}
}

void beginDraw()
{
	g_draw_start_ticks = nowTicks();
}

void endDraw()
{
	if (g_draw_start_ticks != 0)
		g_timer_ticks[TIMER_CPU_DRAW].fetch_add(nowTicks() - g_draw_start_ticks, std::memory_order_relaxed);
	g_draw_start_ticks = 0;
}

void frameDone(double frame_time_ms)
{
	g_counts[CTR_FRAMES].fetch_add(1, std::memory_order_relaxed);
	g_frame_times.push_back(frame_time_ms);
	sample();
}

Scope::Scope(TimerId id_)
	: id(id_), start_ticks(nowTicks())
{
}

Scope::~Scope()
{
	addTicks(id, nowTicks() - start_ticks);
}

void addCount(CounterId id, uint32_t n)
{
	g_counts[id].fetch_add(n, std::memory_order_relaxed);
}

void addBytes(CounterId id, uint64_t n)
{
	g_counts[id].fetch_add(n, std::memory_order_relaxed);
}

static double percentile(std::vector<double> sorted, double p)
{
	if (sorted.empty())
		return 0.0;
	size_t idx = (size_t)((sorted.size() - 1) * p);
	return sorted[idx];
}

void sample()
{
	if (!g_qpf)
		return;

	const uint64_t now = nowTicks();
	const uint64_t elapsed_ms = (now - g_last_sample_ticks) * 1000ull / g_qpf;
	if (elapsed_ms < kSampleIntervalMs)
		return;

	uint64_t timers_us[TIMER_COUNT];
	for (uint32_t i = 0; i < TIMER_COUNT; i++) {
		const uint64_t total = g_timer_ticks[i].load(std::memory_order_relaxed);
		timers_us[i] = ticksToUs(total - g_prev_timer_ticks[i]);
		g_prev_timer_ticks[i] = total;
	}

	uint64_t counts[CTR_COUNT];
	for (uint32_t i = 0; i < CTR_COUNT; i++) {
		const uint64_t total = g_counts[i].load(std::memory_order_relaxed);
		counts[i] = total - g_prev_counts[i];
		g_prev_counts[i] = total;
	}

	// Frame-time stats for the window.
	double frame_min = 0.0, frame_avg = 0.0, frame_p99 = 0.0, frame_max = 0.0;
	double fps_min = 0.0, fps_avg = 0.0, fps_max = 0.0;
	std::vector<double> sorted;
	sorted.reserve(g_frame_times.size());
	for (double t : g_frame_times)
		sorted.push_back(t);
	g_frame_times.clear();

	if (!sorted.empty()) {
		std::sort(sorted.begin(), sorted.end());
		frame_min = sorted.front();
		frame_max = sorted.back();
		frame_p99 = percentile(sorted, 0.99);
		double sum = 0.0;
		for (double t : sorted)
			sum += t;
		frame_avg = sum / sorted.size();

		auto fps_of = [](double ms) { return ms > 0.0 ? 1000.0 / ms : 0.0; };
		fps_min = fps_of(frame_max);
		fps_avg = fps_of(frame_avg);
		fps_max = fps_of(frame_min);
	}

	g_last_sample_ticks = now;

	if (!g_file)
		return;

	const uint64_t elapsed_s = (now - g_start_ticks) * 1000ull / g_qpf / 1000ull;

	time_t now_time = time(0);
	tm tmv;
	localtime_s(&tmv, &now_time);

	fprintf(g_file, "%.2d:%.2d:%.2d,%llu,", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, elapsed_s);
	fprintf(g_file, "%.1f,%.1f,%.1f,%.3f,%.3f,%.3f,%.3f,",
		fps_min, fps_avg, fps_max, frame_min, frame_avg, frame_p99, frame_max);
	for (uint32_t i = 0; i < TIMER_COUNT; i++)
		fprintf(g_file, "%.3f,", double(timers_us[i]) / 1000.0);
	for (uint32_t i = 0; i < CTR_COUNT; i++)
		fprintf(g_file, "%llu,", counts[i]);

	fprintf(g_file, "%s,%u,%u,%u,%u,%d,%u,%d\n",
		screenName(App.game.screen),
		App.game.size.x, App.game.size.y,
		App.window.size.x, App.window.size.y,
		App.vsync ? 1 : 0, App.frame_latency,
		App.window.active ? 1 : 0);
	fflush(g_file);
}

#else // !_STATS

#endif // _STATS

}
}

#ifdef _STATS
extern "C" void d2glStatsCountUnitDraw()
{
	d2gl::d2::UnitAny* unit = d2gl::d2::currently_drawing_unit;
	if (!unit)
		return;

	d2gl::stats::addCount(d2gl::stats::CTR_UNITS);
	switch (unit->dwType) {
		case d2gl::d2::UnitType::Monster: d2gl::stats::addCount(d2gl::stats::CTR_MONSTERS); break;
		case d2gl::d2::UnitType::Player: d2gl::stats::addCount(d2gl::stats::CTR_PLAYERS); break;
		case d2gl::d2::UnitType::Item: d2gl::stats::addCount(d2gl::stats::CTR_ITEMS); break;
		case d2gl::d2::UnitType::Missile: d2gl::stats::addCount(d2gl::stats::CTR_MISSILES); break;
		default: break;
	}
}

extern "C" void d2glStatsCountWeatherDraw()
{
	d2gl::stats::addCount(d2gl::stats::CTR_WEATHER);
}
#endif // _STATS
