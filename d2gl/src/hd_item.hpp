#pragma once

#include <stdint.h>
#include <windows.h>
#include "d2/structs.h"


namespace d2gl {
struct HDItemInfo {
	int width;
	int height;
	uint32_t count;
	uint32_t handles[1];

	static HDItemInfo* allocate(uint32_t count)
	{
		auto item = (HDItemInfo *)malloc(sizeof(HDItemInfo) + (count - 1) * sizeof(uint32_t));
		item->count = count;
		return item;
	}
};

void HDItemClearCache();
bool HDItemDraw(d2::CellContext * cell, int x, int y, int draw_mode, uint8_t * palette);

}
