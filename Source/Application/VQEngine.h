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

#include "Types.h"
#include "Platform.h"
#include "Window.h"
#include "Settings.h"

#include "Source/Renderer/Renderer.h"

#include <memory>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>

#include <array>

// Data to be updated per frame
struct FFrameData
{
	std::array<float, 4> SwapChainClearColor;
};

class IWindowUpdateContext
{
public:
	virtual void Update() = 0;

protected:
	HWND hwnd;
};

class MainWindowUpdateContext : public IWindowUpdateContext
{
public:
	void Update() override;

private:
	std::vector<FFrameData> mFrameData;
};

class VQEngine : public IWindowOwner
{
public:

public:
	// ---------------------------------------------------------
	// Main Thread
	// ---------------------------------------------------------
	bool Initialize(const FStartupParameters& Params);
	void Exit();

	// Window event callbacks for the main Window
	void OnWindowCreate() override;
	void OnWindowResize(HWND hWnd) override;
	void OnWindowMinimize() override;
	void OnWindowFocus() override;
	void OnWindowKeyDown(WPARAM wParam) override;
	void OnWindowClose() override;
	
	void MainThread_Tick();

	// ---------------------------------------------------------
	// Render Thread
	// ---------------------------------------------------------
	void RenderThread_Main();
	void RenderThread_Inititalize();
	void RenderThread_Exit();

	void RenderThread_PreRender();
	void RenderThread_Render();

	// ---------------------------------------------------------
	// Update Thread
	// ---------------------------------------------------------
	void UpdateThread_Main();

	// ---------------------------------------------------------
	// Load Thread
	// ---------------------------------------------------------
	void LoadThread_Main();
	void LoadThread_WaitForLoadTask();

private:
	void InititalizeEngineSettings(const FStartupParameters& Params);
	void InitializeApplicationWindows(const FStartupParameters& Params);
	void InitializeThreads();
	void ExitThreads();

private:
	// threads
	std::atomic<bool> mbStopAllThreads;
	std::thread mRenderThread;
	std::thread mUpdateThread;
	std::thread mLoadThread;

	// sync
	std::condition_variable mCVLoadTasksReadyForProcess;
	std::mutex              mMtxLoadTasksReadyForProcess;
	std::atomic<bool>       mbRenderThreadInitialized;
	std::atomic<uint64>     mNumRenderLoopsExecuted;

	// windows
	std::unique_ptr<Window> mpWinMain;
	std::unique_ptr<Window> mpWinDebug;

	// render
	VQRenderer              mRenderer;

	// data
	FEngineSettings         mSettings;
	std::unordered_map<HWND, IWindowUpdateContext*> mWindowUpdateContextLookup;

private:
	// Reads EngineSettings.ini from next to the executable and returns a 
	// FStartupParameters struct as it readily has override booleans for engine settings
	static FStartupParameters ParseEngineSettingsFile();
};