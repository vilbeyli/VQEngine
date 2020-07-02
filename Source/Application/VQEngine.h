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
#include "Events.h"
#include "Mesh.h"

#include "Libs/VQUtils/Source/Multithreading.h"
#include "Source/Renderer/Renderer.h"

#include <memory>

// Outputs Render/Update thread sync values on each Tick()
#define DEBUG_LOG_THREAD_SYNC_VERBOSE 0


class IWindowUpdateContext
{
public:
	virtual void Update() = 0;

protected:
	HWND hwnd;
};

class MainWindowScene : public IWindowUpdateContext
{
public:
	void Update() override;

//private:
	std::vector<FFrameData> mFrameData;
	std::vector<FLoadingScreenData> mLoadingScreenData;
};
class DebugWindowScene : public IWindowUpdateContext
{
public:
	void Update() override;

//private:
	std::vector<FFrameData> mFrameData;
	std::vector<FLoadingScreenData> mLoadingScreenData;
};


enum EAppState
{
	INITIALIZING = 0,
	LOADING,
	SIMULATING,
	UNLOADING,
	EXITING,
	NUM_APP_STATES
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
	void OnWindowCreate(IWindow* pWnd) override;
	void OnWindowResize(HWND hWnd) override;
	void OnToggleFullscreen(HWND hWnd) override;
	void OnWindowMinimize(IWindow* pWnd) override;
	void OnWindowFocus(IWindow* pWindow) override;
	void OnWindowKeyDown(WPARAM wParam) override;
	void OnWindowClose(IWindow* pWindow) override;
	

	void MainThread_Tick();

	// ---------------------------------------------------------
	// Render Thread
	// ---------------------------------------------------------
	void RenderThread_Main();
	void RenderThread_Inititalize();
	void RenderThread_Exit();
	void RenderThread_WaitForUpdateThread();
	void RenderThread_SignalUpdateThread();

	// PRE_RENDER()
	// - TBA
	void RenderThread_PreRender();

	// RENDER()
	// - Records command lists in parallel per SceneView
	// - Submits commands to the GPU
	// - Presents SwapChain
	void RenderThread_Render();
	void RenderThread_RenderMainWindow();
	void RenderThread_RenderDebugWindow();


	// Processes the event queue populated by the VQEngine_Main.cpp thread
	void RenderThread_HandleEvents();

	// ---------------------------------------------------------
	// Update Thread
	// ---------------------------------------------------------
	void UpdateThread_Main();
	void UpdateThread_WaitForRenderThread();
	void UpdateThread_SignalRenderThread();

	// PRE_UPDATE()
	// - Updates timer
	// - Updates input state reading from Main Thread's input queue
	void UpdateThread_PreUpdate();
	
	// UPDATE()
	// - Updates program state (init/load/sim/unload/exit)
	// - Starts loading tasks
	// - Animates loading screen
	// - Updates scene data
	void UpdateThread_UpdateAppState();

	// POST_UPDATE()
	// - Computes visibility per SceneView
	void UpdateThread_PostUpdate();


//-----------------------------------------------------------------------
private:
	void InititalizeEngineSettings(const FStartupParameters& Params);
	void InitializeApplicationWindows(const FStartupParameters& Params);

	void InitializeThreads();
	void ExitThreads();

	void InitializeBuiltinMeshes();
	void LoadLoadingScreenData(); // data is loaded in parallel but it blocks the calling thread until load is complete
	void Load_SceneData_Dispatch();
	void Load_SceneData_Join();

	HRESULT RenderThread_RenderMainWindow_LoadingScreen(FWindowRenderContext& ctx);
	HRESULT RenderThread_RenderMainWindow_Scene(FWindowRenderContext& ctx);

	void RenderThread_HandleResizeWindowEvent(const IEvent* pEvent);
	void RenderThread_HandleToggleFullscreenEvent(const IEvent* pEvent);

	std::unique_ptr<Window>& GetWindow(HWND hwnd);
	const FWindowSettings& GetWindowSettings(HWND hwnd) const;
	FWindowSettings& GetWindowSettings(HWND hwnd);

private:
	// threads
	std::atomic<bool> mbStopAllThreads;
	std::thread mRenderThread;
	std::thread mUpdateThread;
	ThreadPool  mUpdateWorkerThreads;
	ThreadPool  mRenderWorkerThreads;

	// sync
	std::unique_ptr<Semaphore> mpSemUpdate;
	std::unique_ptr<Semaphore> mpSemRender;
	
	// windows
	std::unique_ptr<Window>   mpWinMain;
	std::unique_ptr<Window>   mpWinDebug;

	// render
	VQRenderer mRenderer;
	std::array<Mesh       , EBuiltInMeshes::NUM_BUILTIN_MESHES> mBuiltinMeshes;
	std::array<std::string, EBuiltInMeshes::NUM_BUILTIN_MESHES> mBuiltinMeshNames;

	// data / state
	std::atomic<bool>         mbRenderThreadInitialized;
	std::atomic<uint64>       mNumRenderLoopsExecuted;
	std::atomic<uint64>       mNumUpdateLoopsExecuted;
	std::atomic<bool>         mbLoadingLevel;
	FEngineSettings           mSettings;
	EAppState                 mAppState;
	VQSystemInfo::FSystemInfo mSysInfo;

	// scene
	MainWindowScene         mScene_MainWnd;
	DebugWindowScene        mScene_DebugWnd;
	std::unordered_map<HWND, IWindowUpdateContext*> mWindowUpdateContextLookup;

	// input

	// events
	BufferedContainer<std::queue<std::unique_ptr<IEvent>>, std::unique_ptr<IEvent>> mWinEventQueue;

private:
	// Reads EngineSettings.ini from next to the executable and returns a 
	// FStartupParameters struct as it readily has override booleans for engine settings
	static FStartupParameters ParseEngineSettingsFile();
};