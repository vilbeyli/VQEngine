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
struct FPostProcessParameters
{
	EColorSpace   ContentColorSpace = EColorSpace::REC_709;
	EDisplayCurve OutputDisplayCurve = EDisplayCurve::sRGB;
	float         DisplayReferenceBrightnessLevel = 200.0f;
};
struct FFrameData
{
	std::array<float, 4> SwapChainClearColor;

	// scene 
	Camera SceneCamera;
	Transform TFCube;
	bool bCubeAnimating;

	// post process
	FPostProcessParameters PPParams;
};
struct FLoadingScreenData
{
	std::array<float, 4> SwapChainClearColor;
	
	SRV_ID SRVLoadingScreen = INVALID_ID;
	// TODO: animation resources
};
class IWindowUpdateContext
{
public:
	HWND hwnd;
	std::vector<FFrameData> mFrameData;
	std::vector<FLoadingScreenData> mLoadingScreenData;
};
class MainWindowSceneData : public IWindowUpdateContext{};
class DebugWindowSceneData : public IWindowUpdateContext{};


struct FEnvironmentMap
{
	TextureID Tex_HDREnvironment = INVALID_ID;
	TextureID Tex_Irradiance     = INVALID_ID;
	SRV_ID    SRV_HDREnvironment = INVALID_ID;
	SRV_ID    SRV_Irradiance     = INVALID_ID;
};

struct FRenderingResources{};
struct FRenderingResources_MainWindow : public FRenderingResources
{
	TextureID Tex_MainViewColorMSAA  = INVALID_ID;
	TextureID Tex_MainViewColor      = INVALID_ID;
	TextureID Tex_MainViewDepthMSAA  = INVALID_ID;
	TextureID Tex_MainViewDepth      = INVALID_ID;
	TextureID Tex_PostProcess_TonemapperOut = INVALID_ID;

	RTV_ID    RTV_MainViewColorMSAA  = INVALID_ID;
	RTV_ID    RTV_MainViewColor      = INVALID_ID;
	SRV_ID    SRV_MainViewColor      = INVALID_ID;
	DSV_ID    DSV_MainViewDepthMSAA  = INVALID_ID;
	DSV_ID    DSV_MainViewDepth      = INVALID_ID;
	SRV_ID    SRV_PostProcess_TonemapperOut = INVALID_ID;
	UAV_ID    UAV_PostProcess_TonemapperOut = INVALID_ID;

	FEnvironmentMap SelectedEnvironmentMap;
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

	// OS event callbacks for the application
	void OnWindowCreate(HWND hWnd) override;
	void OnWindowResize(HWND hWnd) override;
	void OnWindowMinimize(HWND hwnd) override;
	void OnWindowFocus(HWND hwnd) override;
	void OnWindowLoseFocus(HWND hwnd) override;
	void OnWindowClose(HWND hwnd) override;
	void OnToggleFullscreen(HWND hWnd) override;
	void OnWindowActivate(HWND hWnd) override;
	void OnWindowDeactivate(HWND hWnd) override;
	void OnWindowMove(HWND hwnd_, int x, int y) override;
	void OnDisplayChange(HWND hwnd_, int ImageDepthBitsPerPixel, int ScreenWidth, int ScreenHeight) override;

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
	
	void                       SetWindowName(HWND hwnd, const std::string& name);
	void                       SetWindowName(const std::unique_ptr<Window>& pWin, const std::string& name);
	const std::string&         GetWindowName(HWND hwnd) const;
	inline const std::string&  GetWindowName(const std::unique_ptr<Window>& pWin) const { return GetWindowName(pWin->GetHWND()); }
	inline const std::string&  GetWindowName(const Window* pWin) const { return GetWindowName(pWin->GetHWND()); }

private:
	//-------------------------------------------------------------------------------------------------
	using BuiltinMeshArray_t          = std::array<Mesh       , EBuiltInMeshes::NUM_BUILTIN_MESHES>;
	using BuiltinMeshNameArray_t      = std::array<std::string, EBuiltInMeshes::NUM_BUILTIN_MESHES>;
	//-------------------------------------------------------------------------------------------------
	using EventPtr_t                  = std::shared_ptr<IEvent>;
	using EventQueue_t                = BufferedContainer<std::queue<EventPtr_t>, EventPtr_t>;
	//-------------------------------------------------------------------------------------------------
	using UpdateContextLookup_t       = std::unordered_map<HWND, IWindowUpdateContext*>;
	using RenderingResourcesLookup_t  = std::unordered_map<HWND, std::shared_ptr<FRenderingResources>>;
	using WindowLookup_t              = std::unordered_map<HWND, std::unique_ptr<Window>>;
	using WindowNameLookup_t          = std::unordered_map<HWND, std::string>;
	//-------------------------------------------------------------------------------------------------

	// threads
	std::thread                     mRenderThread;
	std::thread                     mUpdateThread;
	ThreadPool                      mUpdateWorkerThreads;
	ThreadPool                      mRenderWorkerThreads;

	// sync
	std::atomic<bool>               mbStopAllThreads;
	std::unique_ptr<Semaphore>      mpSemUpdate;
	std::unique_ptr<Semaphore>      mpSemRender;
	
	// windows
#if 0 // TODO
	WindowLookup_t                  mpWindows;
#else
	std::unique_ptr<Window>         mpWinMain;
	std::unique_ptr<Window>         mpWinDebug;
#endif
	WindowNameLookup_t              mWinNameLookup;
	POINT                           mMouseCapturePosition;

	// render
	VQRenderer                      mRenderer;
	BuiltinMeshArray_t              mBuiltinMeshes;
	BuiltinMeshNameArray_t          mBuiltinMeshNames;

	// state
	std::atomic<bool>               mbRenderThreadInitialized;
	std::atomic<uint64>             mNumRenderLoopsExecuted;
	std::atomic<uint64>             mNumUpdateLoopsExecuted;
	std::atomic<bool>               mbLoadingLevel;
	EAppState                       mAppState;
	std::atomic<bool>               mbMainWindowHDRTransitionInProgress; // see DispatchHDRSwapchainTransitionEvents()

	// system & settings
	FEngineSettings                 mSettings;
	VQSystemInfo::FSystemInfo       mSysInfo;

	// scene
	MainWindowSceneData             mScene_MainWnd;
	DebugWindowSceneData            mScene_DebugWnd;
	UpdateContextLookup_t           mWindowUpdateContextLookup;

#if 0
	RenderingResourcesLookup_t      mRenderingResources;
#else
	FRenderingResources_MainWindow  mResources_MainWnd;
	FRenderingResources_DebugWindow mResources_DebugWnd;
#endif

	// input
	std::unordered_map<HWND, Input> mInputStates;

	// events 
	EventQueue_t                    mEventQueue_WinToVQE_Renderer;
	EventQueue_t                    mEventQueue_WinToVQE_Update;
	EventQueue_t                    mEventQueue_VQEToWin_Main;

	// timer / profiler
	Timer                           mTimer;

	// misc.
	// One Swapchain.Resize() call is required for the first time 
	// transition of swapchains which are initialzied fullscreen.
	std::unordered_map<HWND, bool>  mInitialSwapchainResizeRequiredWindowLookup;


private:
	void                            InitializeEngineSettings(const FStartupParameters& Params);
	void                            InitializeWindows(const FStartupParameters& Params);

	void                            InitializeThreads();
	void                            ExitThreads();

	void                            HandleWindowTransitions(std::unique_ptr<Window>& pWin, const FWindowSettings& settings);
	void                            SetMouseCaptureForWindow(HWND hwnd, bool bCaptureMouse);
	inline void                     SetMouseCaptureForWindow(Window* pWin, bool bCaptureMouse) { this->SetMouseCaptureForWindow(pWin->GetHWND(), bCaptureMouse); };

	void                            InitializeBuiltinMeshes();
	void                            LoadLoadingScreenData(); // data is loaded in parallel but it blocks the calling thread until load is complete
	void                            Load_SceneData_Dispatch();
	void                            Load_SceneData_Join();

	HRESULT                         RenderThread_RenderMainWindow_LoadingScreen(FWindowRenderContext& ctx);
	HRESULT                         RenderThread_RenderMainWindow_Scene(FWindowRenderContext& ctx);

	void                            UpdateThread_HandleEvents();
	void                            RenderThread_HandleEvents();
	void                            MainThread_HandleEvents();

	void                            RenderThread_HandleWindowResizeEvent(const std::shared_ptr<IEvent>& pEvent);
	void                            RenderThread_HandleWindowCloseEvent(const IEvent* pEvent);
	void                            RenderThread_HandleToggleFullscreenEvent(const IEvent* pEvent);
	void                            RenderThread_HandleSetVSyncEvent(const IEvent* pEvent);
	void                            RenderThread_HandleSetSwapchainFormatEvent(const IEvent* pEvent);

	void                            UpdateThread_HandleWindowResizeEvent(const std::shared_ptr<IEvent>& pEvent);

	//
	// FRAME RENDERING PIPELINE
	//
	void                            TransitionForSceneRendering(FWindowRenderContext& ctx);
	void                            RenderShadowMaps(FWindowRenderContext& ctx);
	void                            RenderSceneColor(FWindowRenderContext& ctx, const FFrameData& FrameData);
	void                            ResolveMSAA(FWindowRenderContext& ctx);
	void                            TransitionForPostProcessing(FWindowRenderContext& ctx);
	void                            RenderPostProcess(FWindowRenderContext& ctx, const FPostProcessParameters& PPParams);
	void                            RenderUI(FWindowRenderContext& ctx);
	HRESULT                         PresentFrame(FWindowRenderContext& ctx);
	void                            CompositUIToHDRSwapchain(FWindowRenderContext& ctx); // TODO

	// temp
	struct FrameConstantBuffer { DirectX::XMMATRIX matModelViewProj; };

	void                            DrawMesh(ID3D12GraphicsCommandList* pCmd, const Mesh& mesh);


	std::unique_ptr<Window>&        GetWindow(HWND hwnd);
	const std::unique_ptr<Window>&  GetWindow(HWND hwnd) const;
	const FWindowSettings&          GetWindowSettings(HWND hwnd) const;
	FWindowSettings&                GetWindowSettings(HWND hwnd);
	FFrameData&                     GetCurrentFrameData(HWND hwnd);

	void                            RegisterWindowForInput(const std::unique_ptr<Window>& pWnd);
	void                            UnregisterWindowForInput(const std::unique_ptr<Window>& pWnd);

	void                            HandleEngineInput();

	void                            DispatchHDRSwapchainTransitionEvents(HWND hwnd);

	bool                            IsWindowRegistered(HWND hwnd) const;
	bool                            ShouldRenderHDR(HWND hwnd) const;

private:
	// Reads EngineSettings.ini from next to the executable and returns a 
	// FStartupParameters struct as it readily has override booleans for engine settings
	static FStartupParameters       ParseEngineSettingsFile();

public:
	// Supported HDR Formats { DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT  }
	// Supported SDR Formats { DXGI_FORMAT_R8G8B8A8_UNORM   , DXGI_FORMAT_R8G8B8A8_UNORM_SRGB }
	static const DXGI_FORMAT PREFERRED_HDR_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
	static const DXGI_FORMAT PREFERRED_SDR_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
};



struct FWindowDesc
{
	int width = -1;
	int height = -1;
	HINSTANCE hInst = NULL;
	pfnWndProc_t pfnWndProc = nullptr;
	IWindowOwner* pWndOwner = nullptr;
	bool bFullscreen = false;
	int preferredDisplay = 0;
	int iShowCmd;
	std::string windowName;

	using Registrar_t = VQEngine;
	void (Registrar_t::* pfnRegisterWindowName)(HWND hwnd, const std::string& WindowName);
	Registrar_t* pRegistrar;
};
