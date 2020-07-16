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
#include "Transform.h"
#include "Camera.h"
#include "Input.h"

#include "Libs/VQUtils/Source/Multithreading.h"
#include "Libs/VQUtils/Source/Timer.h"

#include "Source/Renderer/Renderer.h"

#include <memory>

// Outputs Render/Update thread sync values on each Tick()
#define DEBUG_LOG_THREAD_SYNC_VERBOSE 0


//
// DATA STRUCTS
//
class IWindowUpdateContext
{
public:
	virtual void Update() = 0;

protected:
	HWND hwnd;
};

// Data to be updated per frame
struct FFrameData
{
	Camera SceneCamera;
	Transform TFCube;
	std::array<float, 4> SwapChainClearColor;
};
struct FLoadingScreenData
{
	std::array<float, 4> SwapChainClearColor;
	// TODO: loading screen background img resource
	// TODO: animation resources
	SRV_ID SRVLoadingScreen = INVALID_ID;
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

struct RenderingResources_MainWindow
{
	TextureID Tex_MainViewDepth = INVALID_ID;
	DSV_ID    DSV_MainViewDepth = INVALID_ID;
};
struct RenderingResources_DebugWindow
{
	// TODO
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



//
// VQENGINE
//
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
	void OnWindowKeyDown(HWND hwnd, WPARAM wParam) override;
	void OnWindowKeyUp(HWND hwnd, WPARAM wParam) override;
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

	void RenderThread_LoadWindowSizeDependentResources(HWND hwnd, int Width, int Height);
	void RenderThread_UnloadWindowSizeDependentResources(HWND hwnd);

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
	void UpdateThread_Inititalize();
	void UpdateThread_Exit();
	void UpdateThread_WaitForRenderThread();
	void UpdateThread_SignalRenderThread();

	// PRE_UPDATE()
	// - Updates timer
	// - Updates input state reading from Main Thread's input queue
	void UpdateThread_PreUpdate(float& dt);
	
	// UPDATE()
	// - Updates program state (init/load/sim/unload/exit)
	// - Starts loading tasks
	// - Animates loading screen
	// - Updates scene state
	void UpdateThread_UpdateAppState(const float dt);
	void UpdateThread_UpdateScene_MainWnd(const float dt);


	// POST_UPDATE()
	// - Computes visibility per SceneView
	void UpdateThread_PostUpdate();

	// Processes the event queue populated by the VQEngine_Main.cpp thread
	void UpdateThread_HandleEvents();

//-----------------------------------------------------------------------

private:
	using BuiltinMeshArray_t     = std::array<Mesh       , EBuiltInMeshes::NUM_BUILTIN_MESHES>;
	using BuiltinMeshNameArray_t = std::array<std::string, EBuiltInMeshes::NUM_BUILTIN_MESHES>;
	using EventQueue_t           = BufferedContainer<std::queue<std::unique_ptr<IEvent>>, std::unique_ptr<IEvent>>;
	using UpdateContextLookup_t  = std::unordered_map<HWND, IWindowUpdateContext*>;

	// threads
	std::thread                    mRenderThread;
	std::thread                    mUpdateThread;
	ThreadPool                     mUpdateWorkerThreads;
	ThreadPool                     mRenderWorkerThreads;

	// sync
	std::atomic<bool>              mbStopAllThreads;
	std::unique_ptr<Semaphore>     mpSemUpdate;
	std::unique_ptr<Semaphore>     mpSemRender;
	
	// windows
	std::unique_ptr<Window>        mpWinMain;
	std::unique_ptr<Window>        mpWinDebug;
	// todo: generic window mngmt

	// render
	VQRenderer                     mRenderer;
	BuiltinMeshArray_t             mBuiltinMeshes;
	BuiltinMeshNameArray_t         mBuiltinMeshNames;

	// data / state
	std::atomic<bool>              mbRenderThreadInitialized;
	std::atomic<uint64>            mNumRenderLoopsExecuted;
	std::atomic<uint64>            mNumUpdateLoopsExecuted;
	std::atomic<bool>              mbLoadingLevel;
	FEngineSettings                mSettings;
	EAppState                      mAppState;
	VQSystemInfo::FSystemInfo      mSysInfo;

	// scene
	MainWindowScene                mScene_MainWnd;
	DebugWindowScene               mScene_DebugWnd;
	UpdateContextLookup_t          mWindowUpdateContextLookup;
	RenderingResources_MainWindow  mResources_MainWnd;
	RenderingResources_DebugWindow mResources_DebugWnd;

	// input
	Input                          mInput; // input per HWND?

	// events
	EventQueue_t                   mWinEventQueue;
	EventQueue_t                   mInputEventQueue;

	Timer                          mTimer;


private:
	// Reads EngineSettings.ini from next to the executable and returns a 
	// FStartupParameters struct as it readily has override booleans for engine settings
	static FStartupParameters ParseEngineSettingsFile();

private:
	void                     InititalizeEngineSettings(const FStartupParameters& Params);
	void                     InitializeApplicationWindows(const FStartupParameters& Params);

	void                     InitializeThreads();
	void                     ExitThreads();

	void                     InitializeBuiltinMeshes();
	void                     LoadLoadingScreenData(); // data is loaded in parallel but it blocks the calling thread until load is complete
	void                     Load_SceneData_Dispatch();
	void                     Load_SceneData_Join();
	
	HRESULT                  RenderThread_RenderMainWindow_LoadingScreen(FWindowRenderContext& ctx);
	HRESULT                  RenderThread_RenderMainWindow_Scene(FWindowRenderContext& ctx);
	
	void                     RenderThread_HandleResizeWindowEvent(const IEvent* pEvent);
	void                     RenderThread_HandleToggleFullscreenEvent(const IEvent* pEvent);
	
	std::unique_ptr<Window>& GetWindow(HWND hwnd);
	const FWindowSettings&   GetWindowSettings(HWND hwnd) const;
	FWindowSettings&         GetWindowSettings(HWND hwnd);
};