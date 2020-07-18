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


struct FRenderingResources{};
struct FRenderingResources_MainWindow : public FRenderingResources
{
	TextureID Tex_MainViewDepth = INVALID_ID;
	DSV_ID    DSV_MainViewDepth = INVALID_ID;
};
struct FRenderingResources_DebugWindow : public FRenderingResources
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
	void OnWindowMinimize(IWindow* pWnd) override;
	void OnWindowFocus(IWindow* pWindow) override;
	void OnWindowClose(HWND hwnd_) override;
	void OnToggleFullscreen(HWND hWnd) override;
	void OnKeyDown(HWND hwnd, WPARAM wParam) override;
	void OnKeyUp(HWND hwnd, WPARAM wParam) override;
	void OnMouseButtonDown(HWND hwnd, WPARAM wParam, bool bIsDoubleClick) override;
	void OnMouseButtonUp(HWND hwnd, WPARAM wParam) override;
	void OnMouseScroll(HWND hwnd, short scroll) override;
	void OnMouseMove(HWND hwnd, long x, long y) override;
	void OnMouseInput(HWND hwnd, LPARAM lParam) override;

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
	void UpdateThread_UpdateScene_DebugWnd(const float dt);


	// POST_UPDATE()
	// - Computes visibility per SceneView
	void UpdateThread_PostUpdate();


//-----------------------------------------------------------------------

private:
	//-----------------------------------------------------------------------------------------
	using BuiltinMeshArray_t         = std::array<Mesh       , EBuiltInMeshes::NUM_BUILTIN_MESHES>;
	using BuiltinMeshNameArray_t     = std::array<std::string, EBuiltInMeshes::NUM_BUILTIN_MESHES>;
	//-----------------------------------------------------------------------------------------
	using EventPtr_t                 = std::shared_ptr<IEvent>;
	using EventQueue_t               = BufferedContainer<std::queue<EventPtr_t>, EventPtr_t>;
	//-----------------------------------------------------------------------------------------
	using UpdateContextLookup_t      = std::unordered_map<HWND, IWindowUpdateContext*>;
	using RenderingResourcesLookup_t = std::unordered_map<HWND, std::shared_ptr<FRenderingResources>>;
	using WindowLookup_t             = std::unordered_map<HWND, std::unique_ptr<Window>>;
	//-----------------------------------------------------------------------------------------

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
	WindowLookup_t                 mpWindows;

	std::unique_ptr<Window>        mpWinMain;
	std::unique_ptr<Window>        mpWinDebug;
	

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

	RenderingResourcesLookup_t     mRenderingResources;

	FRenderingResources_MainWindow  mResources_MainWnd;
	FRenderingResources_DebugWindow mResources_DebugWnd;

	// input
	std::unordered_map<HWND, Input> mInputStates;

	// events Windows->VQE
	EventQueue_t                   mEventQueue_WinToVQE_Renderer;
	EventQueue_t                   mEventQueue_WinToVQE_Update;

	// events VQE->Windows
	EventQueue_t                   mEventQueue_VQEToWin_Main;

	// timer / profiler
	Timer                          mTimer;

	// misc.
	// One Swapchain.Resize() call is required for the first time 
	// transition of swapchains which are initialzied fullscreen.
	std::unordered_map<HWND, bool> mInitialSwapchainResizeRequiredWindowLookup;

private:
	// Reads EngineSettings.ini from next to the executable and returns a 
	// FStartupParameters struct as it readily has override booleans for engine settings
	static FStartupParameters ParseEngineSettingsFile();

private:
	void                     InitializeEngineSettings(const FStartupParameters& Params);
	void                     InitializeWindows(const FStartupParameters& Params);

	void                     InitializeThreads();
	void                     ExitThreads();

	void                     HandleWindowTransitions(std::unique_ptr<Window>& pWin, const FWindowSettings& settings);
	void                     SetMouseCaptureForWindow(HWND hwnd, bool bCaptureMouse);

	void                     InitializeBuiltinMeshes();
	void                     LoadLoadingScreenData(); // data is loaded in parallel but it blocks the calling thread until load is complete
	void                     Load_SceneData_Dispatch();
	void                     Load_SceneData_Join();
	
	HRESULT                  RenderThread_RenderMainWindow_LoadingScreen(FWindowRenderContext& ctx);
	HRESULT                  RenderThread_RenderMainWindow_Scene(FWindowRenderContext& ctx);
	

	void                     UpdateThread_HandleEvents();
	void                     RenderThread_HandleEvents();
	void                     MainThread_HandleEvents();

	void                     RenderThread_HandleWindowResizeEvent(const IEvent* pEvent);
	void                     RenderThread_HandleWindowCloseEvent(const IEvent* pEvent);
	void                     RenderThread_HandleToggleFullscreenEvent(const IEvent* pEvent);
	
	std::unique_ptr<Window>& GetWindow(HWND hwnd);
	const FWindowSettings&   GetWindowSettings(HWND hwnd) const;
	FWindowSettings&         GetWindowSettings(HWND hwnd);

	void                     RegisterWindowForInput(const std::unique_ptr<Window>& pWnd);
	void                     UnregisterWindowForInput(const std::unique_ptr<Window>& pWnd);

	void                     HandleEngineInput();
};