//	VQE
//	Copyright(C) 2020  - Volkan Ilbeyli
//
//	This program is free software : you can redistribute it and / or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program.If not, see <http://www.gnu.org/licenses/>.
//
//	Contact: volkanilbeyli@gmail.com

#pragma once

#include "Platform.h"

#include <vector>
#include <thread>
#include <string>
#include <mutex>
#include <queue>
#include <future>
#include <condition_variable>

// http://www.cplusplus.com/reference/thread/thread/
// https://stackoverflow.com/a/32593825/2034041
// todo: finish implementation for shader hotswapping

using Task = std::function<void()>;

struct TaskQueue
{
	int activeTasks = 0;
	std::mutex		 mutex;
	std::queue<Task> queue;
};

template<typename R>
bool is_ready(std::future<R> const& f) { return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }

// src: https://www.youtube.com/watch?v=eWTGtp3HXiw

class ThreadPool
{
public:
	const static size_t ThreadPool::sHardwareThreadCount;

	void Initialize(size_t numWorkers);
	void Exit();

	int GetNumActiveTasks() /*const*/;

	// Notes on C++11 Threading:
	// ------------------------------------------------------------------------------------
	// 
	// To get a return value from an executing thread, we use std::packaged_task<> together
	// with std::future to access the results later.
	//
	// e.g. http://en.cppreference.com/w/cpp/thread/packaged_task
	//
	// int f(int x, int y) { return std::pow(x,y); }
	//
	// 	std::packaged_task<int(int, int)> task([](int a, int b) {
	// 		return std::pow(a, b);
	// 	});
	// 	std::future<int> result = task.get_future();
	// 	task(2, 9);
	//
	// *****
	// 
	// 	std::packaged_task<int()> task(std::bind(f, 2, 11));
	// 	std::future<int> result = task.get_future();
	// 	task();
	// 
	// *****
	//
	// 	std::packaged_task<int(int, int)> task(f);
	// 	std::future<int> result = task.get_future(); 
	// 	std::thread task_td(std::move(task), 2, 10);
	// 
	// ------------------------------------------------------------------------------------


	// Adds a task to the thread pool and returns the std::future<> 
	// containing the return type of the added task.
	//
	template<class T>
	//std::future<decltype(task())> AddTask(T task)	// (why doesn't this compile)
	auto AddTask(T task) -> std::future<decltype(task())>
	{
		// use a shared_ptr<> of packaged tasks here as we execute them in the thread pool workers as well
		// as accesing its get_future() on the thread that calls this AddTask() function.
		using typename task_return_t = decltype(task());
		auto pTask = std::make_shared< std::packaged_task<task_return_t()>>(std::move(task));
		{
			std::unique_lock<std::mutex> lock(mTaskQueue.mutex);
			mTaskQueue.queue.emplace([=]
			{					// Add a lambda function to the task queue which 
				(*pTask)();		// calls the packaged_task<>'s callable object -> T task 
			});
			++mTaskQueue.activeTasks;
		}

		mSignal.NotifyOne();
		return pTask->get_future();
	}

private:
	void Execute(); // workers run Execute();

	std::vector<std::thread> mWorkers;
	Signal                   mSignal;
	std::atomic<bool>        mbStopWorkers;
	TaskQueue                mTaskQueue;
};

