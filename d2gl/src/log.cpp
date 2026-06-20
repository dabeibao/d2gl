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
#include <mutex>

namespace d2gl {

static std::mutex g_log_mutex;
static FILE * log_fp;

FILE * logFileOpen()
{
	if (log_fp != nullptr) {
		return log_fp;
	}
	FILE * fp;
	if (fopen_s(&fp, App.log_file.c_str(), "w") != 0)  {
		return nullptr;
	}
	log_fp = fp;
	return fp;
}

void logToFile(int type, const char *fmt, va_list args)
{
	std::lock_guard<std::mutex> lock(g_log_mutex);

	FILE * fp = logFileOpen();
	if (fp == nullptr) {
		return;
	}

	time_t now = time(0);
	tm gmt_time;
	localtime_s(&gmt_time, &now);
	fprintf(fp, "[%.2d:%.2d:%.2d][%s] ",
		gmt_time.tm_hour, gmt_time.tm_min, gmt_time.tm_sec,
		(type == 0 ? "INFO" : (type == 1 ? "ERROR" : "WARNING")));
	vfprintf(fp, fmt, args);
	fprintf(fp, "\n");
	fflush(fp);
}

void logToFile(int type, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	logToFile(type, fmt, args);
	va_end(args);
}


void logInit()
{
#ifdef _DEBUG
	AllocConsole();
	freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
#endif
	if (!App.log) {
		return;
	}

	logToFile(0, "== D2GL v%s logging started. ==\n", App.version_str.c_str());
}


void logTrace(WORD color, bool newline, const char* format, ...)
{
	static HANDLE hnd = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hnd, color);

	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);

	if (newline)
		printf("\n");
}

void logTraceDef(uint8_t type, const char* format, ...)
{
	static HANDLE hnd = GetStdHandle(STD_OUTPUT_HANDLE);
	if (type == 1) {
		SetConsoleTextAttribute(hnd, C_RED);
		printf("Error: ");
	} else if (type == 2) {
		SetConsoleTextAttribute(hnd, C_YELLOW);
		printf("Warning: ");
	}
	SetConsoleTextAttribute(hnd, C_WHITE);

	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	printf("\n");
}

void logFileWrite(uint8_t type, const char* format, ...)
{
	if (!App.log) {
		return;
	}

	va_list args;
	va_start(args, format);
	logToFile(type, format, args);
	va_end(args);
}


}
