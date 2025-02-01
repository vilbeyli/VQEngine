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

#include "Renderer/Rendering/EnvironmentMapRendering.h"
#include "EnvironmentMap.h"
#include "Settings.h"
#include "AssetLoader.h"
#include "UI/VQUI.h"

#include "RenderPass/AmbientOcclusion.h"
#include "RenderPass/DepthPrePass.h"
#include "RenderPass/DepthMSAAResolve.h"
#include "RenderPass/ScreenSpaceReflections.h"
#include "RenderPass/ApplyReflections.h"
#include "RenderPass/MagnifierPass.h"
#include "RenderPass/ObjectIDPass.h"
#include "RenderPass/OutlinePass.h"

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
	void Destroy();
	inline bool ShouldExit() const { return mbExitApp.load(); }

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

	void RenderThread_LoadWindowSizeDependentResources(HWND hwnd, int Width, int Height, float ResolutionScale);
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
	
	void UnloadEnvironmentMap();

	void WaitForBuiltinMeshGeneration();

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
	ThreadPool                      mWorkers_MeshLoading;

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
	std::vector<Fence>              mAsyncComputeSSAOReadyFence;
	std::vector<Fence>              mAsyncComputeSSAODoneFence;
	std::vector<Fence>              mCopyObjIDDoneFence; // GPU->CPU
	std::atomic<bool>               mAsyncComputeWorkSubmitted = false;
	std::atomic<bool>               mSubmitWorkerFinished = true;
	bool                            mWaitForSubmitWorker = false;

	// load events
	std::future<bool>              mbLoadingScreenLoaded;

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
	std::atomic<bool>               mbExitApp;
	std::atomic<bool>               mbBuiltinMeshGenFinished;
	Signal                          mBuiltinMeshGenSignal;


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

	// RenderPasses (WIP design)
	std::vector<IRenderPass*>       mRenderPasses;
	DepthPrePass                    mRenderPass_ZPrePass;
	AmbientOcclusionPass            mRenderPass_AO;
	ScreenSpaceReflectionsPass      mRenderPass_SSR;
	DepthMSAAResolvePass            mRenderPass_DepthResolve;
	ApplyReflectionsPass            mRenderPass_ApplyReflections;
	MagnifierPass                   mRenderPass_Magnifier;
	ObjectIDPass                    mRenderPass_ObjectID;
	OutlinePass                     mRenderPass_Outline;


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
	void                            InitializeImGUI(HWND hwnd);
	void                            InitializeUI(HWND hwnd);
	void                            InitializeEngineThreads();

	void                            ExitThreads();
	void                            ExitUI();

	void                            HandleWindowTransitions(std::unique_ptr<Window>& pWin, const FWindowSettings& settings);
	void                            SetMouseCaptureForWindow(HWND hwnd, bool bCaptureMouse, bool bReleaseAtCapturedPosition);
	inline void                     SetMouseCaptureForWindow(Window* pWin, bool bCaptureMouse, bool bReleaseAtCapturedPosition) { this->SetMouseCaptureForWindow(pWin->GetHWND(), bCaptureMouse, bReleaseAtCapturedPosition); };

	void                            InitializeBuiltinMeshes();
	void                            LoadLoadingScreenData(); // data is loaded in parallel but it blocks the calling thread until load is complete
	void                            Load_SceneData_Dispatch();
	void                            LoadEnvironmentMap(const std::string& EnvMapName, int SpecularMapMip0Resolution);

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
	void                            TransitionForSceneRendering(ID3D12GraphicsCommandList* pCmd, FWindowRenderContext& ctx, const FPostProcessParameters& PPParams);
	void                            RenderDirectionalShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowViews& ShadowView);
	void                            RenderSpotShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowViews& ShadowView);
	void                            RenderPointShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowViews& ShadowView, size_t iBegin, size_t NumPointLights);
	void                            RenderDepthPrePass(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, const std::vector< D3D12_GPU_VIRTUAL_ADDRESS>& CBAddresses, D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr, D3D12_GPU_VIRTUAL_ADDRESS perFrameCBAddr);
	void                            RenderObjectIDPass(ID3D12GraphicsCommandList* pCmd, ID3D12CommandList* pCmdCopy, DynamicBufferHeap* pCBufferHeap, const std::vector< D3D12_GPU_VIRTUAL_ADDRESS>& CBAddresses, D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr, const FSceneView& SceneView, const int BACK_BUFFER_INDEX);
	void                            TransitionDepthPrePassForWrite(ID3D12GraphicsCommandList* pCmd, bool bMSAA);
	void                            TransitionDepthPrePassForRead(ID3D12GraphicsCommandList* pCmd, bool bMSAA);
	void                            TransitionDepthPrePassForReadAsyncCompute(ID3D12GraphicsCommandList* pCmd);
	void                            TransitionDepthPrePassMSAAResolve(ID3D12GraphicsCommandList* pCmd);
	void                            ResolveMSAA_DepthPrePass(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap);
	void                            CopyDepthForCompute(ID3D12GraphicsCommandList* pCmd);
	void                            RenderAmbientOcclusion(ID3D12GraphicsCommandList* pCmd, const FSceneView& SceneView);
	void                            RenderSceneColor(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, const FPostProcessParameters& PPParams, const std::vector< D3D12_GPU_VIRTUAL_ADDRESS>& CBAddresses, D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr, D3D12_GPU_VIRTUAL_ADDRESS perFrameCBAddr);
	void                            RenderBoundingBoxes(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, bool bMSAA);
	void                            RenderOutline(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr, const FSceneView& SceneView, bool bMSAA, const std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>& rtvHandles);
	void                            RenderLightBounds(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, bool bMSAA, bool bReflectionsEnabled);
	void                            RenderDebugVertexAxes(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, bool bMSAA);
	void                            ResolveMSAA(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FPostProcessParameters& PPParams);
	void                            DownsampleDepth(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, TextureID DepthTextureID, SRV_ID SRVDepth);
	void                            RenderReflections(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView);
	void                            RenderSceneBoundingVolumes(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr, const FSceneView& SceneView, bool bMSAA);
	void                            CompositeReflections(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView);
	void                            TransitionForPostProcessing(ID3D12GraphicsCommandList* pCmd, const FPostProcessParameters& PPParams);
	ID3D12Resource*                 RenderPostProcess(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FPostProcessParameters& PPParams);
	void                            RenderUI(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, FWindowRenderContext& ctx, const FPostProcessParameters& PPParams, ID3D12Resource* pRscIn);
	void                            CompositUIToHDRSwapchain(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, FWindowRenderContext& ctx, const FPostProcessParameters& PPParams);
	HRESULT                         PresentFrame(FWindowRenderContext& ctx);
	
	bool                            ShouldEnableAsyncCompute();

	//
	// UI
	//
	void                            UpdateUIState(HWND hwnd, float dt);
	void                            DrawProfilerWindow(const FSceneStats& FrameStats, float dt);
	void                            DrawSceneControlsWindow(int& iSelectedCamera, int& iSelectedEnvMap, FSceneRenderParameters& SceneRenderParams);
	void                            DrawPostProcessSettings(FPostProcessParameters& PPParams);
	void                            DrawKeyMappingsWindow();
	void                            DrawGraphicsSettingsWindow(FSceneRenderParameters& SceneRenderParams, FPostProcessParameters& PPParams);
	void                            DrawEditorWindow();
	void                            DrawMaterialEditor();
	void                            DrawLightEditor();
	void                            DrawObjectEditor();

	//
	// RENDER HELPERS
	//
	void                            DrawShadowViewMeshList(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FShadowView& shadowView, size_t iDepthMode);

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
	bool                            IsHDRSettingOn() const;

	void                            SetEffectiveFrameRateLimit(int FrameRateLimitEnumVal);
	float                           FramePacing(const float dt);
	const FDisplayHDRProfile*       GetHDRProfileIfExists(const wchar_t* pwStrLogicalDisplayName);
	FSetHDRMetaDataParams           GatherHDRMetaDataParameters(HWND hwnd);

	// Busy waits until render thread catches up with update thread
	void                            WaitUntilRenderingFinishes();

	// temp data
	struct FFrameConstantBuffer { DirectX::XMMATRIX matModelViewProj; };
	struct FFrameConstantBuffer2 { DirectX::XMMATRIX matModelViewProj; int iTextureConfig; int iTextureOutput; };
	struct FFrameConstantBufferUnlit { DirectX::XMMATRIX matModelViewProj; DirectX::XMFLOAT4 color; };
	struct FFrameConstantBufferUnlitInstanced { DirectX::XMMATRIX matModelViewProj[MAX_INSTANCE_COUNT__UNLIT_SHADER]; DirectX::XMFLOAT4 color; };
	struct FObjectConstantBufferDebugVertexVectors 
	{ 
		DirectX::XMMATRIX matWorld;
		DirectX::XMMATRIX matNormal;
		DirectX::XMMATRIX matViewProj;
		float LocalAxisSize;
	};
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
