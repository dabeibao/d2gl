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
#include "page_load_pool.h"

namespace d2gl {

PageLoadPool& PageLoadPool::instance()
{
	static PageLoadPool pool;
	return pool;
}

PageLoadPool::PageLoadPool()
{
	int num_workers = 4;
	for (int i = 0; i < num_workers; i++)
		m_threads.emplace_back(&PageLoadPool::worker, this);
}

PageLoadPool::~PageLoadPool()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stop = true;
	}
	m_cv.notify_all();
	for (auto& t : m_threads) {
		if (t.joinable())
			t.join();
	}
}

void PageLoadPool::submit(std::function<void()> task)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_tasks.push(std::move(task));
	}
	m_cv.notify_one();
}

void PageLoadPool::worker()
{
	while (true) {
		std::function<void()> task;
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_cv.wait(lock, [this] { return !m_tasks.empty() || m_stop; });
			if (m_stop && m_tasks.empty())
				return;
			task = std::move(m_tasks.front());
			m_tasks.pop();
		}
		task();
	}
}

}
