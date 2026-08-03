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

/*
	Per-frame / per-second profiling statistics.

	Everything in this module compiles to nothing unless the `_STATS` macro is
	defined (see `app.h`), so the calls sprinkled across the codebase are free
	in normal builds.

	Two families of data are collected:
	  * Timers (QPC based). `Scope` is cheap and used a few times per frame.
	    Hot paths (e.g. `Context::pushVertex`) are measured with `hotTimer()`,
	    which only reads the clock once every `stride` calls.
	  * Counters. `addCount()` / `addBytes()` are relaxed atomic increments.

	Timers and counters accumulate continuously and are sampled every
	`kSampleIntervalMs` milliseconds (see `kSampleIntervalMs`), writing one
	row to `d2gl_stats.csv` (created next to the game). Per-frame frame times
	are buffered so each row also carries FPS min/avg/p99/max.

	The CSV is consumed by `tools/analyze_stats.py`.
*/

#pragma once

namespace d2gl {
namespace stats {

// Enums are always defined so call sites in normal (non-_STATS) builds compile;
// the implementation does nothing unless `_STATS` is defined.
enum TimerId : uint8_t {
	// CPU (game) thread
	TIMER_CPU_DRAW,       // beginFrame() -> presentFrame(): the whole game draw phase
	TIMER_CPU_PRESENT,    // presentFrame() submit work (excl. gpu wait)
	TIMER_CPU_GPU_WAIT,   // presentFrame() blocked waiting for the render thread
	TIMER_CPU_STAGE,      // onStageChange()
	TIMER_CPU_MODULES,    // module update / draw time (HDText, minimap, motion prediction)
	TIMER_CPU_HOT_PUSH,   // sampled time inside Context::pushVertex
	// GPU (render) thread
	TIMER_GPU_PROCESS,    // whole render thread pass (uploads + commands, excl. swap)
	TIMER_GPU_UPLOAD_VERTEX,
	TIMER_GPU_UPLOAD_TEX, // texture / font / external / game texture uploads
	TIMER_GPU_COMMANDS,   // command buffer loop (includes prefx + submit)
	TIMER_GPU_PREFX,      // PreFx / bloom pass
	TIMER_GPU_SUBMIT,     // Submit / postfx / upscaler / fxaa pass
	TIMER_GPU_OVERLAY,    // overlay (HD) pass
	TIMER_GPU_FLUSH,      // glFlush
	TIMER_GPU_SWAP,       // SwapBuffers (vsync cost)
	TIMER_GPU_LIMITER,    // manual FPS limiter wait
	TIMER_COUNT,
};

enum CounterId : uint8_t {
	CTR_FRAMES,
	CTR_VERTICES_SD,       // game (SD) vertices pushed
	CTR_VERTICES_MOD,      // overlay / HD vertices
	CTR_OVERLAY_RUNS,
	CTR_DRAWCALLS,         // gl draw calls issued on the render thread
	CTR_COMMANDS,          // commands processed on the render thread
	CTR_SET_BLEND,
	CTR_UBO_UPDATES,
	CTR_TEX_UPDATES,
	CTR_TEX_BYTES,
	CTR_UNITS,             // drawUnit() calls (installed independently of motion prediction)
	CTR_MONSTERS,
	CTR_PLAYERS,
	CTR_ITEMS,
	CTR_MISSILES,
	CTR_WEATHER,
	CTR_TILES,
	CTR_IMAGES,
	CTR_SHADOWS,
	CTR_RECTS,
	CTR_LINES,
	CTR_TEXTS,
	CTR_HD_OBJECTS,
	CTR_FONT_PAGES,
	CTR_EXTERNAL_UPLOADS,
	CTR_FLUSHES,
	CTR_COUNT,
};

#ifdef _STATS

extern const char* timerNames[TIMER_COUNT];
extern const char* counterNames[CTR_COUNT];

void init();
void shutdown();

// CPU (game) thread, once per frame.
void beginDraw();
void endDraw();
void frameDone(double frame_time_ms);

struct Scope {
	TimerId id;
	uint64_t start_ticks;
	explicit Scope(TimerId id_);
	~Scope();
};

uint64_t nowTicks();
void addTicks(TimerId id, uint64_t ticks);

// Cheap per-call timer for very hot functions (e.g. Context::pushVertex).
// Overhead is one counter increment per call plus a clock read on every
// `stride`-th call. Call enter() at the top and exit() on every return path;
// each sampled call's *own* duration is accumulated, so the sum is the real
// total time spent in the function (no interleaved-code overcount).
struct HotTimer {
	uint32_t stride;
	uint32_t counter = 0;
	uint64_t start = 0;
	bool active = false;
	TimerId id;

	explicit HotTimer(uint32_t stride_, TimerId id_)
		: stride(stride_), id(id_)
	{
	}

	inline void enter()
	{
		if (++counter >= stride) {
			counter = 0;
			active = true;
			start = nowTicks();
		}
	}

	inline void exit()
	{
		if (active) {
			active = false;
			// Only every `stride`-th call is timed, so scale up to approximate
			// the total time spent in the function.
			addTicks(id, (nowTicks() - start) * stride);
		}
	}
};

void addCount(CounterId id, uint32_t n = 1);
void addBytes(CounterId id, uint64_t n);

#else // !_STATS

struct Scope {
	explicit Scope(int) {}
};

struct HotTimer {
	explicit HotTimer(uint32_t, int) {}
	inline void enter() {}
	inline void exit() {}
};

inline uint64_t nowTicks() { return 0; }
inline void addTicks(int, uint64_t) {}
inline void init() {}
inline void shutdown() {}
inline void beginDraw() {}
inline void endDraw() {}
inline void frameDone(double) {}
inline void addCount(int, uint32_t = 1) {}
inline void addBytes(int, uint64_t) {}
#endif // _STATS

}

}

// Global-scope C entry points for the naked asm stubs (d2/stubs.cpp). The asm
// `call` operand can only reference unqualified global symbols, so these are
// declared outside the namespaces and given unmangled C linkage.
#ifdef _STATS
extern "C" void d2glStatsCountUnitDraw();
extern "C" void d2glStatsCountWeatherDraw();
#else
inline void d2glStatsCountUnitDraw() {}
inline void d2glStatsCountWeatherDraw() {}
#endif
