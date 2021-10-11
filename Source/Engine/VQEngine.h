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

#include "Core/Types.h"
#include "Core/Platform.h"
#include "Core/Window.h"
#include "Core/Events.h"
#include "Core/Input.h"
#include "Scene/Scene.h"
#include "Scene/Mesh.h"
#include "Scene/Camera.h"
#include "Scene/Transform.h"
#include "RenderPass/AmbientOcclusion.h"
#include "RenderPass/DepthPrePass.h"
#include "Settings.h"
#include "AssetLoader.h"
#include "VQUI.h"


#include "Libs/VQUtils/Source/Multithreading.h"
#include "Libs/VQUtils/Source/Timer.h"

#include "Source/Renderer/Renderer.h"

#include <memory>

//--------------------------------------------------------------------
// MUILTI-THREADING 
//--------------------------------------------------------------------

// Pipelined - saparate Update & Render threads
// Otherwise, Simulation thread for update + render
#define VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS 0


// Outputs Render/Update thread sync values on each Tick()
#define DEBUG_LOG_THREAD_SYNC_VERBOSE 0
//--------------------------------------------------------------------

struct ImGuiContext;

//
// DATA STRUCTS
//
struct FLoadingScreenData
{
	std::array<float, 4> SwapChainClearColor;

	int SelectedLoadingScreenSRVIndex = INVALID_ID;
	std::mutex Mtx;
	std::vector<SRV_ID> SRVs;

	SRV_ID GetSelectedLoadingScreenSRV_ID() const;
	void RotateLoadingScreenImageIndex();

	// TODO: animation resources
};


struct FEnvironmentMapDescriptor
{
	std::string Name;
	std::string FilePath;
	float MaxContentLightLevel = 0.0f;
};
struct FEnvironmentMapRenderingResources
{
	TextureID Tex_HDREnvironment = INVALID_ID; // equirect input
	TextureID Tex_IrradianceDiff = INVALID_ID; // Kd
	TextureID Tex_IrradianceSpec = INVALID_ID; // Ks
	
	// temporary resources
	TextureID Tex_BlurTemp = INVALID_ID;
	TextureID Tex_IrradianceDiffBlurred = INVALID_ID; // Kd

	RTV_ID RTV_IrradianceDiff = INVALID_ID;
	RTV_ID RTV_IrradianceSpec = INVALID_ID;

	SRV_ID SRV_HDREnvironment = INVALID_ID;
	SRV_ID SRV_IrradianceDiff = INVALID_ID;
	SRV_ID SRV_IrradianceSpec = INVALID_ID;
	SRV_ID SRV_IrradianceDiffFaces[6] = { INVALID_ID };
	SRV_ID SRV_IrradianceDiffBlurred = INVALID_ID;

	UAV_ID UAV_BlurTemp = INVALID_ID;
	UAV_ID UAV_IrradianceDiffBlurred = INVALID_ID;

	SRV_ID SRV_BlurTemp = INVALID_ID;

	SRV_ID SRV_BRDFIntegrationLUT = INVALID_ID;

	//
	// HDR10 Static Metadata Parameters -------------------------------
	// https://docs.microsoft.com/en-us/windows/win32/api/dxgi1_5/ns-dxgi1_5-dxgi_hdr_metadata_hdr10
	//
	// The maximum content light level (MaxCLL). This is the nit value 
	// corresponding to the brightest pixel used anywhere in the content.
	int MaxContentLightLevel = 0;

	// The maximum frame average light level (MaxFALL). 
	// This is the nit value corresponding to the average luminance of 
	// the frame which has the brightest average luminance anywhere in 
	// the content.
	int MaxFrameAverageLightLevel = 0;
};

struct FRenderingResources{};
struct FRenderingResources_MainWindow : public FRenderingResources
{
	TextureID Tex_ShadowMaps_Spot              = INVALID_ID;
	TextureID Tex_ShadowMaps_Point             = INVALID_ID;
	TextureID Tex_ShadowMaps_Directional       = INVALID_ID;

	TextureID Tex_SceneColorMSAA               = INVALID_ID;
	TextureID Tex_SceneColor                   = INVALID_ID;
	TextureID Tex_SceneDepthMSAA               = INVALID_ID;
	TextureID Tex_SceneDepth                   = INVALID_ID;
	TextureID Tex_SceneDepthResolve            = INVALID_ID;
	TextureID Tex_SceneNormalsMSAA             = INVALID_ID;
	TextureID Tex_SceneNormals                 = INVALID_ID;
	TextureID Tex_AmbientOcclusion             = INVALID_ID;

	TextureID Tex_PostProcess_BlurIntermediate = INVALID_ID;
	TextureID Tex_PostProcess_BlurOutput       = INVALID_ID;
	TextureID Tex_PostProcess_TonemapperOut    = INVALID_ID;
	TextureID Tex_PostProcess_FFXCASOut        = INVALID_ID;
	TextureID Tex_PostProcess_FSR_EASUOut      = INVALID_ID;
	TextureID Tex_PostProcess_FSR_RCASOut      = INVALID_ID;

	RTV_ID    RTV_SceneColorMSAA               = INVALID_ID;
	RTV_ID    RTV_SceneColor                   = INVALID_ID;
	RTV_ID    RTV_SceneNormalsMSAA             = INVALID_ID;
	RTV_ID    RTV_SceneNormals                 = INVALID_ID;

	SRV_ID    SRV_PostProcess_BlurIntermediate = INVALID_ID;
	SRV_ID    SRV_PostProcess_BlurOutput       = INVALID_ID;
	SRV_ID    SRV_PostProcess_TonemapperOut    = INVALID_ID;
	SRV_ID    SRV_PostProcess_FFXCASOut        = INVALID_ID;
	SRV_ID    SRV_PostProcess_FSR_EASUOut      = INVALID_ID;
	SRV_ID    SRV_PostProcess_FSR_RCASOut      = INVALID_ID;
	SRV_ID    SRV_ShadowMaps_Spot              = INVALID_ID;
	SRV_ID    SRV_ShadowMaps_Point             = INVALID_ID;
	SRV_ID    SRV_ShadowMaps_Directional       = INVALID_ID;
	SRV_ID    SRV_SceneColor                   = INVALID_ID;
	SRV_ID    SRV_SceneNormals                 = INVALID_ID;
	SRV_ID    SRV_SceneDepth                   = INVALID_ID;
	SRV_ID    SRV_SceneDepthMSAA               = INVALID_ID;
	SRV_ID    SRV_FFXCACAO_Out                 = INVALID_ID;

	UAV_ID    UAV_FFXCACAO_Out                 = INVALID_ID;
	UAV_ID    UAV_PostProcess_BlurIntermediate = INVALID_ID;
	UAV_ID    UAV_PostProcess_BlurOutput       = INVALID_ID;
	UAV_ID    UAV_PostProcess_TonemapperOut    = INVALID_ID;
	UAV_ID    UAV_PostProcess_FFXCASOut        = INVALID_ID;
	UAV_ID    UAV_PostProcess_FSR_EASUOut      = INVALID_ID;
	UAV_ID    UAV_PostProcess_FSR_RCASOut      = INVALID_ID;
	UAV_ID    UAV_SceneDepth                   = INVALID_ID;

	DSV_ID    DSV_SceneDepth                   = INVALID_ID;
	DSV_ID    DSV_SceneDepthMSAA               = INVALID_ID;
	DSV_ID    DSV_ShadowMaps_Spot              = INVALID_ID;
	DSV_ID    DSV_ShadowMaps_Point             = INVALID_ID;
	DSV_ID    DSV_ShadowMaps_Directional       = INVALID_ID;

	FEnvironmentMapRenderingResources EnvironmentMap;

	SRV_ID SRV_NullCubemap   = INVALID_ID;
	SRV_ID SRV_NullTexture2D = INVALID_ID;
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

using BuiltinMeshNameArray_t = std::array<std::string, EBuiltInMeshes::NUM_BUILTIN_MESHES>;
struct FResourceNames
{
	BuiltinMeshNameArray_t      mBuiltinMeshNames;
	std::vector<std::string>    mEnvironmentMapPresetNames;
	std::vector<std::string>    mSceneNames;
};

struct FRenderStats
{
	uint NumDraws;
	uint NumDispatches;
};

//
// VQENGINE
//
class VQEngine : public IWindowOwner
{
public:
	VQEngine();

	// OS Window Events
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

	// Keyboard & Mouse Events
	void OnKeyDown(HWND hwnd, WPARAM wParam) override;
	void OnKeyUp(HWND hwnd, WPARAM wParam) override;
	void OnMouseButtonDown(HWND hwnd, WPARAM wParam, bool bIsDoubleClick) override;
	void OnMouseButtonUp(HWND hwnd, WPARAM wParam) override;
	void OnMouseScroll(HWND hwnd, short scroll) override;
	void OnMouseMove(HWND hwnd, long x, long y) override;
	void OnMouseInput(HWND hwnd, LPARAM lParam) override;


	// ---------------------------------------------------------
	// Main Thread
	// ---------------------------------------------------------
	void MainThread_Tick();
	bool Initialize(const FStartupParameters& Params);
	void Exit();

	// ---------------------------------------------------------
	// Render Thread
	// ---------------------------------------------------------
	void RenderThread_Main();
	void RenderThread_Tick();
	void RenderThread_Inititalize();
	void RenderThread_Exit();
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	void RenderThread_WaitForUpdateThread();
	void RenderThread_SignalUpdateThread();
#endif

	void RenderThread_LoadWindowSizeDependentResources(HWND hwnd, int Width, int Height);
	void RenderThread_LoadResources();
	void RenderThread_UnloadWindowSizeDependentResources(HWND hwnd);

	// PRE_RENDER()
	// - TBA
	void RenderThread_PreRender();

	// RENDER()
	// - Records command lists in parallel per FSceneView
	// - Submits commands to the GPU
	// - Presents SwapChain
	void RenderThread_RenderFrame();
	void RenderThread_RenderMainWindow();
	void RenderThread_RenderDebugWindow();


	void RenderThread_HandleStatusOccluded();
	void RenderThread_HandleDeviceRemoved();


	// ---------------------------------------------------------
	// Update Thread
	// ---------------------------------------------------------
	void  UpdateThread_Main();
	void  UpdateThread_Inititalize();
	void  UpdateThread_Tick(const float dt);
	void  UpdateThread_Exit();
	float UpdateThread_WaitForRenderThread();
	void  UpdateThread_SignalRenderThread();

	// PreUpdate()
	// - Updates input state reading from Main Thread's input queue
	void UpdateThread_PreUpdate();
	
	// Update()
	// - Updates program state (init/load/sim/unload/exit)
	// - Starts loading tasks
	// - Animates loading screen
	// - Updates scene state
	void UpdateThread_UpdateAppState(const float dt);
	void UpdateThread_UpdateScene_MainWnd(const float dt);
	void UpdateThread_UpdateScene_DebugWnd(const float dt);

	// PostUpdate()
	// - Computes visibility per FSceneView
	void UpdateThread_PostUpdate();



	// ---------------------------------------------------------
	// Simulation Thread
	// ---------------------------------------------------------
	void SimulationThread_Main();
	void SimulationThread_Initialize();
	void SimulationThread_Exit();
	void SimulationThread_Tick(const float dt);

//-----------------------------------------------------------------------
	
	void                       SetWindowName(HWND hwnd, const std::string& name);
	void                       SetWindowName(const std::unique_ptr<Window>& pWin, const std::string& name);
	const std::string&         GetWindowName(HWND hwnd) const;
	inline const std::string&  GetWindowName(const std::unique_ptr<Window>& pWin) const { return GetWindowName(pWin->GetHWND()); }
	inline const std::string&  GetWindowName(const Window* pWin) const { return GetWindowName(pWin->GetHWND()); }


	// ---------------------------------------------------------
	// Scene Interface
	// ---------------------------------------------------------
	void StartLoadingScene(int IndexScene);
	
	void StartLoadingEnvironmentMap(int IndexEnvMap);
	void PreFilterEnvironmentMap(ID3D12GraphicsCommandList* pCmd, FEnvironmentMapRenderingResources& env);
	void ComputeBRDFIntegrationLUT(ID3D12GraphicsCommandList* pCmd, SRV_ID& outSRV_ID);
	void UnloadEnvironmentMap();

	// Getters
	MeshID GetBuiltInMeshID(const std::string& MeshName) const;

	inline const FResourceNames& GetResourceNames() const { return mResourceNames; }
	inline AssetLoader& GetAssetLoader() { return mAssetLoader; }


private:
	//-------------------------------------------------------------------------------------------------
	using EnvironmentMapDescLookup_t  = std::unordered_map<std::string, FEnvironmentMapDescriptor>;
	//-------------------------------------------------------------------------------------------------
	using EventPtr_t                  = std::shared_ptr<IEvent>;
	using EventQueue_t                = BufferedContainer<std::queue<EventPtr_t>, EventPtr_t>;
	//-------------------------------------------------------------------------------------------------
	using RenderingResourcesLookup_t  = std::unordered_map<HWND, std::shared_ptr<FRenderingResources>>;
	using WindowLookup_t              = std::unordered_map<HWND, std::unique_ptr<Window>>;
	using WindowNameLookup_t          = std::unordered_map<HWND, std::string>;
	//-------------------------------------------------------------------------------------------------

	// threads
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	std::thread                     mRenderThread;
	std::thread                     mUpdateThread;
	ThreadPool                      mWorkers_Update;
	ThreadPool                      mWorkers_Render;
#else
	std::thread                     mSimulationThread;
	ThreadPool                      mWorkers_Simulation;
#endif
	ThreadPool                      mWorkers_ModelLoading;
	ThreadPool                      mWorkers_TextureLoading;

	// sync
	std::atomic<bool>               mbStopAllThreads;
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	std::unique_ptr<Semaphore>      mpSemUpdate;
	std::unique_ptr<Semaphore>      mpSemRender;
#endif
	
	// windows
#if 0 // TODO
	WindowLookup_t                  mpWindows;
#else
	std::unique_ptr<Window>         mpWinMain;
	std::unique_ptr<Window>         mpWinDebug;
#endif
	WindowNameLookup_t              mWinNameLookup;
	POINT                           mMouseCapturePosition;

	// input
	std::unordered_map<HWND, Input> mInputStates;

	// events 
	EventQueue_t                    mEventQueue_VQEToWin_Main;
	EventQueue_t                    mEventQueue_WinToVQE_Renderer;
	EventQueue_t                    mEventQueue_WinToVQE_Update;

	// renderer
	VQRenderer                      mRenderer;

	// assets 
	AssetLoader                     mAssetLoader;
	BuiltinMeshArray_t              mBuiltinMeshes;
	std::vector<FDisplayHDRProfile> mDisplayHDRProfiles;
	EnvironmentMapDescLookup_t      mLookup_EnvironmentMapDescriptors;

	// data: strings
	FResourceNames                  mResourceNames;

	// state
	EAppState                       mAppState;
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	std::atomic<bool>               mbRenderThreadInitialized;
	std::atomic<uint64>             mNumRenderLoopsExecuted;
	std::atomic<uint64>             mNumUpdateLoopsExecuted;
#else
	uint64                          mNumSimulationTicks;
#endif
	std::atomic<bool>               mbLoadingLevel;
	std::atomic<bool>               mbLoadingEnvironmentMap;
	std::atomic<bool>               mbEnvironmentMapPreFilter;
	std::atomic<bool>               mbMainWindowHDRTransitionInProgress; // see DispatchHDRSwapchainTransitionEvents()

	// system & settings
	FEngineSettings                 mSettings;
	VQSystemInfo::FSystemInfo       mSysInfo;

	// scene
	FLoadingScreenData              mLoadingScreenData;
	std::queue<std::string>         mQueue_SceneLoad;
	int                             mIndex_SelectedScene;
	std::unique_ptr<Scene>          mpScene;
	
	// ui
	ImGuiContext*                   mpImGuiContext;
	FUIState                        mUIState;
	FRenderStats                    mRenderStats;

	// rendering resources per window
#if 0
	RenderingResourcesLookup_t      mRenderingResources;
#else
	FRenderingResources_MainWindow  mResources_MainWnd;
	FRenderingResources_DebugWindow mResources_DebugWnd;
#endif

	// RenderPasses (WIP design)
	FDepthPrePass                   mRenderPass_DepthPrePass;
	FAmbientOcclusionPass           mRenderPass_AO;

	// timer / profiler
	Timer                           mTimer;
	Timer                           mTimerRender;
	float                           mEffectiveFrameRateLimit_ms;

	// misc.
	// One Swapchain.Resize() call is required for the first time 
	// transition of swapchains which are initialzied fullscreen.
	std::unordered_map<HWND, bool>  mInitialSwapchainResizeRequiredWindowLookup;


private:
	void                            InitializeInput();
	void                            InitializeEngineSettings(const FStartupParameters& Params);
	void                            InitializeWindows(const FStartupParameters& Params);
	void                            InitializeHDRProfiles();
	void                            InitializeEnvironmentMaps();
	void                            InitializeScenes();
	void                            InitializeUI(HWND hwnd);
	void                            InitializeThreads();

	void                            ExitThreads();
	void                            ExitUI();

	void                            HandleWindowTransitions(std::unique_ptr<Window>& pWin, const FWindowSettings& settings);
	void                            SetMouseCaptureForWindow(HWND hwnd, bool bCaptureMouse);
	inline void                     SetMouseCaptureForWindow(Window* pWin, bool bCaptureMouse) { this->SetMouseCaptureForWindow(pWin->GetHWND(), bCaptureMouse); };

	void                            InitializeBuiltinMeshes();
	void                            LoadLoadingScreenData(); // data is loaded in parallel but it blocks the calling thread until load is complete
	void                            Load_SceneData_Dispatch();
	void                            LoadEnvironmentMap(const std::string& EnvMapName);

	HRESULT                         RenderThread_RenderMainWindow_LoadingScreen(FWindowRenderContext& ctx);
	HRESULT                         RenderThread_RenderMainWindow_Scene(FWindowRenderContext& ctx);

	//
	// EVENTS
	//
	void                            UpdateThread_HandleEvents();
	void                            RenderThread_HandleEvents();
	void                            MainThread_HandleEvents();

	void                            RenderThread_HandleWindowResizeEvent(const std::shared_ptr<IEvent>& pEvent);
	void                            RenderThread_HandleWindowCloseEvent(const IEvent* pEvent);
	void                            RenderThread_HandleToggleFullscreenEvent(const IEvent* pEvent);
	void                            RenderThread_HandleSetVSyncEvent(const IEvent* pEvent);
	void                            RenderThread_HandleSetSwapchainFormatEvent(const IEvent* pEvent);
	void                            RenderThread_HandleSetHDRMetaDataEvent(const IEvent* pEvent);

	void                            UpdateThread_HandleWindowResizeEvent(const std::shared_ptr<IEvent>& pEvent);

	//
	// FRAME RENDERING PIPELINE
	//
	void                            TransitionForSceneRendering(ID3D12GraphicsCommandList* pCmd, FWindowRenderContext& ctx);
	void                            RenderDirectionalShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowView& ShadowView);
	void                            RenderSpotShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowView& ShadowView);
	void                            RenderPointShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowView& ShadowView, size_t iBegin, size_t NumPointLights);
	void                            RenderDepthPrePass(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView);
	void                            RenderAmbientOcclusion(ID3D12GraphicsCommandList* pCmd, const FSceneView& SceneView);
	void                            RenderSceneColor(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView);
	void                            ResolveMSAA(ID3D12GraphicsCommandList* pCmd);
	void                            ResolveDepth(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, TextureID DepthTexture, SRV_ID SRVDepthTexture, UAV_ID UAVDepthResolveTexture);
	void                            TransitionForPostProcessing(ID3D12GraphicsCommandList* pCmd, const FPostProcessParameters& PPParams);
	void                            RenderPostProcess(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FPostProcessParameters& PPParams);
	void                            RenderUI(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, FWindowRenderContext& ctx, const FPostProcessParameters& PPParams);
	void                            CompositUIToHDRSwapchain(ID3D12GraphicsCommandList* pCmd, FWindowRenderContext& ctx); // TODO
	HRESULT                         PresentFrame(FWindowRenderContext& ctx);

	//
	// UI
	//
	void UpdateUIState(HWND hwnd, float dt);
	void DrawProfilerWindow(const FSceneStats& FrameStats, float dt);
	void DrawSceneControlsWindow(int& iSelectedCamera, int& iSelectedEnvMap, FSceneRenderParameters& SceneRenderParams);
	void DrawPostProcessControlsWindow(FPostProcessParameters& PPParams);
	void DrawDebugPanelWindow(FSceneRenderParameters& SceneParams);
	void DrawGraphicsSettingsWindow(FSceneRenderParameters& SceneRenderParams);

	//
	// RENDER HELPERS
	//
	void                            DrawMesh(ID3D12GraphicsCommandList* pCmd, const Mesh& mesh);
	void                            DrawShadowViewMeshList(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowView::FShadowView& shadowView);

	std::unique_ptr<Window>&        GetWindow(HWND hwnd);
	const std::unique_ptr<Window>&  GetWindow(HWND hwnd) const;
	const FWindowSettings&          GetWindowSettings(HWND hwnd) const;
	FWindowSettings&                GetWindowSettings(HWND hwnd);

	const FEnvironmentMapDescriptor& GetEnvironmentMapDesc(const std::string& EnvMapName) const;
           FEnvironmentMapDescriptor GetEnvironmentMapDescCopy(const std::string& EnvMapName) const;

	void                            RegisterWindowForInput(const std::unique_ptr<Window>& pWnd);
	void                            UnregisterWindowForInput(const std::unique_ptr<Window>& pWnd);

	void                            HandleEngineInput();
	void                            HandleMainWindowInput(Input& input, HWND hwnd);
	void                            HandleUIInput();
	
	void                            DispatchHDRSwapchainTransitionEvents(HWND hwnd);

	bool                            IsWindowRegistered(HWND hwnd) const;
	bool                            ShouldRenderHDR(HWND hwnd) const;

	void                            CalculateEffectiveFrameRateLimit(HWND hwnd);
	float                           FramePacing(const float dt);
	const FDisplayHDRProfile*       GetHDRProfileIfExists(const wchar_t* pwStrLogicalDisplayName);
	FSetHDRMetaDataParams           GatherHDRMetaDataParameters(HWND hwnd);

	// Busy waits until render thread catches up with update thread
	void                            WaitUntilRenderingFinishes();

	// temp data
	struct FFrameConstantBuffer { DirectX::XMMATRIX matModelViewProj; };
	struct FFrameConstantBuffer2 { DirectX::XMMATRIX matModelViewProj; int iTextureConfig; int iTextureOutput; };
	struct FFrameConstantBufferUnlit { DirectX::XMMATRIX matModelViewProj; DirectX::XMFLOAT3 color; };

// ------------------------------------------------------------------------------------------------------
//
// VQENGINE STATIC
// 
// ------------------------------------------------------------------------------------------------------
private:
	// Reads EngineSettings.ini from next to the executable and returns a 
	// FStartupParameters struct as it readily has override booleans for engine settings
	static FStartupParameters                       ParseEngineSettingsFile();
	static std::vector<std::pair<std::string, int>> ParseSceneIndexMappingFile();
	static std::vector<FEnvironmentMapDescriptor>   ParseEnvironmentMapsFile();
	static std::vector<FDisplayHDRProfile>          ParseHDRProfilesFile();
	static FSceneRepresentation                     ParseSceneFile(const std::string& SceneFile);
public:
	static std::vector<FMaterialRepresentation>     ParseMaterialFile(const std::string& MaterialFilePath);

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
