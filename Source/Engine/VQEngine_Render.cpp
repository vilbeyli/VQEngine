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

#include "VQEngine.h"
#include "GPUMarker.h"
#include "../VQUtils/Source/utils.h"

#include "VQEngine_RenderCommon.h"

#include "Shaders/LightingConstantBufferData.h"

#include "Scene/MeshGenerator.h"

#define EXECUTE_CMD_LISTS_ON_WORKER 1

// https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-error
static const std::unordered_map<HRESULT, std::string> DEVICE_REMOVED_MESSAGE_LOOKUP =
{
	{ DXGI_ERROR_ACCESS_DENIED, "You tried to use a resource to which you did not have the required access privileges. This error is most typically caused when you write to a shared resource with read-only access." },
	{ DXGI_ERROR_ACCESS_LOST, "The desktop duplication interface is invalid. The desktop duplication interface typically becomes invalid when a different type of image is displayed on the desktop."},
	{ DXGI_ERROR_ALREADY_EXISTS, "The desired element already exists. This is returned by DXGIDeclareAdapterRemovalSupport if it is not the first time that the function is called."},
	{ DXGI_ERROR_CANNOT_PROTECT_CONTENT, "DXGI can't provide content protection on the swap chain. This error is typically caused by an older driver, or when you use a swap chain that is incompatible with content protection."},
	{ DXGI_ERROR_DEVICE_HUNG, "The application's device failed due to badly formed commands sent by the application. This is an design-time issue that should be investigated and fixed."},
	{ DXGI_ERROR_DEVICE_REMOVED, "The video card has been physically removed from the system, or a driver upgrade for the video card has occurred. The application should destroy and recreate the device. For help debugging the problem, call ID3D10Device::GetDeviceRemovedReason."},
	{ DXGI_ERROR_DEVICE_RESET, "The device failed due to a badly formed command. This is a run-time issue; The application should destroy and recreate the device."},
	{ DXGI_ERROR_DRIVER_INTERNAL_ERROR, "The driver encountered a problem and was put into the device removed state."},
	{ DXGI_ERROR_FRAME_STATISTICS_DISJOINT, "An event (for example, a power cycle) interrupted the gathering of presentation statistics."},
	{ DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE, "The application attempted to acquire exclusive ownership of an output, but failed because some other application (or device within the application) already acquired ownership."},
	{ DXGI_ERROR_INVALID_CALL, "The application provided invalid parameter data; this must be debugged and fixed before the application is released."},
	{ DXGI_ERROR_MORE_DATA, "The buffer supplied by the application is not big enough to hold the requested data."},
	{ DXGI_ERROR_NAME_ALREADY_EXISTS, "The supplied name of a resource in a call to IDXGIResource1::CreateSharedHandle is already associated with some other resource."},
	{ DXGI_ERROR_NONEXCLUSIVE, "A global counter resource is in use, and the Direct3D device can't currently use the counter resource."},
	{ DXGI_ERROR_NOT_CURRENTLY_AVAILABLE, "The resource or request is not currently available, but it might become available later."},
	{ DXGI_ERROR_NOT_FOUND, "When calling IDXGIObject::GetPrivateData, the GUID passed in is not recognized as one previously passed to IDXGIObject::SetPrivateData or IDXGIObject::SetPrivateDataInterface. When calling IDXGIFactory::EnumAdapters or IDXGIAdapter::EnumOutputs, the enumerated ordinal is out of range."},
	{ DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED, "Reserved"},
	{ DXGI_ERROR_REMOTE_OUTOFMEMORY, "Reserved" },
	{ DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE, "The DXGI output (monitor) to which the swap chain content was restricted is now disconnected or changed."},
	{ DXGI_ERROR_SDK_COMPONENT_MISSING, "The operation depends on an SDK component that is missing or mismatched."},
	{ DXGI_ERROR_SESSION_DISCONNECTED, "The Remote Desktop Services session is currently disconnected."},
	{ DXGI_ERROR_UNSUPPORTED, "The requested functionality is not supported by the device or the driver."},
	{ DXGI_ERROR_WAIT_TIMEOUT, "The time-out interval elapsed before the next desktop frame was available." },
	{ DXGI_ERROR_WAS_STILL_DRAWING, "The GPU was busy at the moment when a call was made to perform an operation, and did not execute or schedule the operation." },
	{ S_OK, "The method succeeded without an error." }
};

// ------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// MAIN
//
// ------------------------------------------------------------------------------------------------------------------------------------------------------------
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
void VQEngine::RenderThread_Main()
{
	Log::Info("RenderThread Created.");
	RenderThread_Inititalize();

	RenderThread_HandleEvents();

	float dt = 0.0f;
	while (!this->mbStopAllThreads)
	{
		float dt = mTimerRender.Tick();

		RenderThread_Tick();

		float SleepTime = FramePacing(dt);

		// RenderThread_Logging()
		constexpr int LOGGING_PERIOD = 4; // seconds
		static float LAST_LOG_TIME = mTimerRender.TotalTime();
		const float TotalTime = mTimerRender.TotalTime();
		if (TotalTime - LAST_LOG_TIME > 4)
		{
			Log::Info("RenderTick() : dt=%.2f ms (Sleep=%.2f)", dt * 1000.0f, SleepTime);
			LAST_LOG_TIME = TotalTime;
		}
	}

	RenderThread_Exit();
	Log::Info("RenderThread_Main() : Exit");
}

void VQEngine::RenderThread_WaitForUpdateThread()
{
	SCOPED_CPU_MARKER("RenderThread_WaitForUpdateThread()");

#if DEBUG_LOG_THREAD_SYNC_VERBOSE
	Log::Info("r:wait : u=%llu, r=%llu", mNumUpdateLoopsExecuted.load(), mNumRenderLoopsExecuted.load());
#endif

	mpSemRender->Wait();
}

void VQEngine::RenderThread_SignalUpdateThread()
{
	mpSemUpdate->Signal();
}
#endif


void VQEngine::RenderThread_Tick()
{
	SCOPED_CPU_MARKER_C("RenderThread_Tick()", 0xFF007700);

	RenderThread_HandleEvents();

	if (this->mbStopAllThreads)
		return; // HandleEvents() can set @this->mbStopAllThreads true with WindowCloseEvent;

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	RenderThread_WaitForUpdateThread();
#endif

#if DEBUG_LOG_THREAD_SYNC_VERBOSE
	Log::Info(/*"RenderThread_Tick() : */"r%d (u=%llu)", mNumRenderLoopsExecuted.load(), mNumUpdateLoopsExecuted.load());
#endif

	RenderThread_PreRender();
	RenderThread_RenderFrame();

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	++mNumRenderLoopsExecuted;

	RenderThread_SignalUpdateThread();
#endif

	RenderThread_HandleEvents();
}


// ------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// INITIALIZE
//
// ------------------------------------------------------------------------------------------------------------------------------------------------------------
static bool CheckInitialSwapchainResizeRequired(std::unordered_map<HWND, bool>& map, const FWindowSettings& setting, HWND hwnd)
{
	const bool bExclusiveFullscreen = setting.DisplayMode == EDisplayMode::EXCLUSIVE_FULLSCREEN;
	if (bExclusiveFullscreen)
	{
		map[hwnd] = true;
	}
	return bExclusiveFullscreen;
}

static void PreAssignPSOIDs(PSOCollection& psoCollection, int& i, std::vector<FPSODesc>& descs)
{
	for (auto it = psoCollection.mapLoadDesc.begin(); it != psoCollection.mapLoadDesc.end(); ++it)
	{
		descs[i] = std::move(it->second);
		psoCollection.mapPSO[it->first] = EBuiltinPSOs::NUM_BUILTIN_PSOs + i++; // assign PSO_IDs beforehand
	}
	psoCollection.mapLoadDesc.clear();
}
std::vector<FPSODesc> VQRenderer::LoadBuiltinPSODescs()
{
	SCOPED_CPU_MARKER("LoadPSODescs_Builtin");

	mLightingPSOs.GatherPSOLoadDescs(mRootSignatureLookup);
	mZPrePassPSOs.GatherPSOLoadDescs(mRootSignatureLookup);
	mShadowPassPSOs.GatherPSOLoadDescs(mRootSignatureLookup);

	std::vector<FPSODesc> descs(
		mLightingPSOs.mapLoadDesc.size() 
		+ mZPrePassPSOs.mapLoadDesc.size() 
		+ mShadowPassPSOs.mapLoadDesc.size()
	);

	{
		SCOPED_CPU_MARKER("PreAssignPSOIds");
		int i = 0;
		PreAssignPSOIDs(mLightingPSOs, i, descs);
		PreAssignPSOIDs(mZPrePassPSOs, i, descs);
		PreAssignPSOIDs(mShadowPassPSOs, i, descs);
	}
	return descs;
}

void VQEngine::RenderThread_Inititalize()
{
	SCOPED_CPU_MARKER_C("RenderThread_Inititalize()", 0xFF007700);

	const bool bExclusiveFullscreen_MainWnd = CheckInitialSwapchainResizeRequired(mInitialSwapchainResizeRequiredWindowLookup, mSettings.WndMain, mpWinMain->GetHWND());

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	mNumRenderLoopsExecuted.store(0);
#endif

#if THREADED_CTX_INIT
	mWorkers_Simulation.AddTask([=]() {
#endif
		// Initialize swapchains for each rendering window
		// all windows use the same number of swapchains as the main window
		const int NUM_SWAPCHAIN_BUFFERS = mSettings.gfx.bUseTripleBuffering ? 3 : 2;
		{
			SCOPED_CPU_MARKER("mpWinMainInitContext");
			const bool bIsContainingWindowOnHDRScreen = VQSystemInfo::FMonitorInfo::CheckHDRSupport(mpWinMain->GetHWND());
			const bool bCreateHDRSwapchain = mSettings.WndMain.bEnableHDR && bIsContainingWindowOnHDRScreen;
			if (mSettings.WndMain.bEnableHDR && !bIsContainingWindowOnHDRScreen)
			{
				Log::Warning("RenderThread_Initialize(): HDR Swapchain requested, but the containing monitor does not support HDR. Falling back to SDR Swapchain, and will enable HDR swapchain when the window is moved to a HDR-capable display");
			}

			mRenderer.InitializeRenderContext(mpWinMain.get(), NUM_SWAPCHAIN_BUFFERS, mSettings.gfx.bVsync, bCreateHDRSwapchain);
			mEventQueue_VQEToWin_Main.AddItem(std::make_shared<HandleWindowTransitionsEvent>(mpWinMain->GetHWND()));
		}
		if(mpWinDebug)
		{
			SCOPED_CPU_MARKER("mpWinDebugInitContext");
			const bool bIsContainingWindowOnHDRScreen = VQSystemInfo::FMonitorInfo::CheckHDRSupport(mpWinDebug->GetHWND());
			constexpr bool bCreateHDRSwapchain = false; // only main window in HDR for now
			mRenderer.InitializeRenderContext(mpWinDebug.get(), NUM_SWAPCHAIN_BUFFERS, false, bCreateHDRSwapchain);
			mEventQueue_VQEToWin_Main.AddItem(std::make_shared<HandleWindowTransitionsEvent>(mpWinDebug->GetHWND()));
		}

#if THREADED_CTX_INIT
	});
#endif




#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	mbRenderThreadInitialized.store(true);
#endif
	 
	// load builtin resources, compile shaders, load PSOs
	
	mRenderer.Load();
	
	mWorkers_Simulation.AddTask([=]() { LoadLoadingScreenData(); });
	mWorkers_Simulation.AddTask([=]() { InitializeBuiltinMeshes(); });
	mRenderer.StartPSOCompilation_MT();

	// load window resources
	const bool bFullscreen = mpWinMain->IsFullscreen();
	const int W = bFullscreen ? mpWinMain->GetFullscreenWidth() : mpWinMain->GetWidth();
	const int H = bFullscreen ? mpWinMain->GetFullscreenHeight() : mpWinMain->GetHeight();
	const float fResolutionScale = 1.0f; // Post process parameters are not initialized at this stage to determine the resolution scale

#if THREADED_CTX_INIT
	mRenderer.WaitMainSwapchainReady();

	// initialize queue fences
	{
		SCOPED_CPU_MARKER("InitQueueFences");
		ID3D12Device* pDevice = mRenderer.GetDevicePtr();
		const int NumBackBuffers = mRenderer.GetSwapChainBackBufferCount(mpWinMain);
		mAsyncComputeSSAOReadyFence.resize(NumBackBuffers);
		mAsyncComputeSSAODoneFence.resize(NumBackBuffers);
		for (int i = 0; i < NumBackBuffers; ++i)
		{
			mAsyncComputeSSAOReadyFence[i].Create(pDevice, "AsyncComputeSSAOReadyFence");
			mAsyncComputeSSAODoneFence[i].Create(pDevice, "AsyncComputeSSAODoneFence");
		}
		mRenderer.InitializeFences(mpWinMain->GetHWND());
	}
#endif

	RenderThread_LoadWindowSizeDependentResources(mpWinMain->GetHWND(), W, H, fResolutionScale);

	mRenderer.WaitPSOCompilation();
	mRenderer.AssignPSOs();

	mTimerRender.Reset();
	mTimerRender.Start();
}

void VQEngine::RenderThread_Exit()
{
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	mpSemUpdate->Signal();
#endif

	const int NumBackBuffers = mRenderer.GetSwapChainBackBufferCount(mpWinMain);
	mRenderer.DestroyFences(mpWinMain->GetHWND());
	for (int i = 0; i < NumBackBuffers; ++i)
	{
		mAsyncComputeSSAOReadyFence[i].Destroy();
		mAsyncComputeSSAODoneFence[i].Destroy();
	}
}

void VQEngine::InitializeBuiltinMeshes()
{
	SCOPED_CPU_MARKER("InitializeBuiltinMeshes()");
	using VertexType = FVertexWithNormalAndTangent;
	{
		const EBuiltInMeshes eMesh = EBuiltInMeshes::TRIANGLE;
		GeometryData<VertexType> data = GeometryGenerator::Triangle<VertexType>(1.0f);
		mResourceNames.mBuiltinMeshNames[eMesh] = "Triangle";
		mBuiltinMeshes[eMesh] = Mesh(&mRenderer, data.LODVertices[0], data.LODIndices[0], mResourceNames.mBuiltinMeshNames[eMesh]);
	}
	{
		const EBuiltInMeshes eMesh = EBuiltInMeshes::CUBE;
		GeometryData<VertexType> data = GeometryGenerator::Cube<VertexType>();
		mResourceNames.mBuiltinMeshNames[eMesh] = "Cube";
		mBuiltinMeshes[eMesh] = Mesh(&mRenderer, data.LODVertices[0], data.LODIndices[0], mResourceNames.mBuiltinMeshNames[eMesh]);
	} 
	{
		SCOPED_CPU_MARKER("Cylinder");
		const EBuiltInMeshes eMesh = EBuiltInMeshes::CYLINDER;
		GeometryData<VertexType> data = GeometryGenerator::Cylinder<VertexType>(3.0f, 1.0f, 1.0f, 45, 6, 4);
		mResourceNames.mBuiltinMeshNames[eMesh] = "Cylinder";
		mBuiltinMeshes[eMesh] = Mesh(&mRenderer, data, mResourceNames.mBuiltinMeshNames[eMesh]);
	}
	{
		SCOPED_CPU_MARKER("Sphere");
		const EBuiltInMeshes eMesh = EBuiltInMeshes::SPHERE;
		GeometryData<VertexType> data = GeometryGenerator::Sphere<VertexType>(1.0f, 30, 30, 5);
		mResourceNames.mBuiltinMeshNames[eMesh] = "Sphere";
		mBuiltinMeshes[eMesh] = Mesh(&mRenderer, data, mResourceNames.mBuiltinMeshNames[eMesh]);
	}
	{
		const EBuiltInMeshes eMesh = EBuiltInMeshes::CONE;
		GeometryData<VertexType> data = GeometryGenerator::Cone<VertexType>(1, 1, 42, 4);
		mResourceNames.mBuiltinMeshNames[eMesh] = "Cone";
		mBuiltinMeshes[eMesh] = Mesh(&mRenderer, data, mResourceNames.mBuiltinMeshNames[eMesh]);
	}
	{
		SCOPED_CPU_MARKER("SimpleGrid");
		const EBuiltInMeshes eMesh = EBuiltInMeshes::GRID_SIMPLE_QUAD;
		const float GridLength = 1.0f;
		GeometryData<VertexType> data = GeometryGenerator::Grid<VertexType>(GridLength, GridLength, 2, 2, 1);
		mResourceNames.mBuiltinMeshNames[eMesh] = "SimpleGrid";
		mBuiltinMeshes[eMesh] = Mesh(&mRenderer, data, mResourceNames.mBuiltinMeshNames[eMesh]);
	}
	{
		SCOPED_CPU_MARKER("DetailGrid0");
		const EBuiltInMeshes eMesh = EBuiltInMeshes::GRID_DETAILED_QUAD0;
		const float GridLength = 1.0f;
		GeometryData<VertexType> data = GeometryGenerator::Grid<VertexType>(GridLength, GridLength, 3, 3, 1);
		mResourceNames.mBuiltinMeshNames[eMesh] = "DetaildGrid0";
		mBuiltinMeshes[eMesh] = Mesh(&mRenderer, data, mResourceNames.mBuiltinMeshNames[eMesh]);
	}
	{
		SCOPED_CPU_MARKER("DetailGrid1");
		const EBuiltInMeshes eMesh = EBuiltInMeshes::GRID_DETAILED_QUAD1;
		const float GridLength = 1.0f;
		GeometryData<VertexType> data = GeometryGenerator::Grid<VertexType>(GridLength, GridLength, 12, 12, 4);
		mResourceNames.mBuiltinMeshNames[eMesh] = "DetaildGrid1";
		mBuiltinMeshes[eMesh] = Mesh(&mRenderer, data, mResourceNames.mBuiltinMeshNames[eMesh]);
	}
	{
		SCOPED_CPU_MARKER("DetailGrid2");
		const EBuiltInMeshes eMesh = EBuiltInMeshes::GRID_DETAILED_QUAD2;
		const float GridLength = 1.0f;
		GeometryData<VertexType> data = GeometryGenerator::Grid<VertexType>(GridLength, GridLength, 1200, 1200, 6);
		mResourceNames.mBuiltinMeshNames[eMesh] = "DetaildGrid2";
		mBuiltinMeshes[eMesh] = Mesh(&mRenderer, data, mResourceNames.mBuiltinMeshNames[eMesh]);
	}
	{
		SCOPED_CPU_MARKER("TessellationGrids");
		using PatchVertexType = FVertexDefault;
		constexpr int NUM_TESSELLATION_GEOMETRY = 5;
		const EBuiltInMeshes eMesh[NUM_TESSELLATION_GEOMETRY] =
		{ 
			EBuiltInMeshes::TESSELLATION_CONTROL_POINTS__QUAD1,
			EBuiltInMeshes::TESSELLATION_CONTROL_POINTS__QUAD4,
			EBuiltInMeshes::TESSELLATION_CONTROL_POINTS__QUAD9,
			EBuiltInMeshes::TESSELLATION_CONTROL_POINTS__QUAD16,
			EBuiltInMeshes::TESSELLATION_CONTROL_POINTS__QUAD25
		};
		const char* szMeshNames[NUM_TESSELLATION_GEOMETRY] =
		{
			"TessellationGrid_Quad1",
			"TessellationGrid_Quad4",
			"TessellationGrid_Quad9",
			"TessellationGrid_Quad16",
			"TessellationGrid_Quad25"
		};
		for (int i = 0; i < NUM_TESSELLATION_GEOMETRY; ++i)
		{
			GeometryData<PatchVertexType> data = GeometryGenerator::TessellationPatch_Quad<PatchVertexType>(i+1);
			mResourceNames.mBuiltinMeshNames[eMesh[i]] = szMeshNames[i];
			mBuiltinMeshes[eMesh[i]] = Mesh(&mRenderer, data, mResourceNames.mBuiltinMeshNames[eMesh[i]]);
		}
	}
	// ...

	mRenderer.UploadVertexAndIndexBufferHeaps();
	mbBuiltinMeshGenFinished.store(true);
	mBuiltinMeshGenSignal.NotifyAll();
}

void VQEngine::WaitForBuiltinMeshGeneration()
{
	SCOPED_CPU_MARKER_C("WaitForBuiltinMeshGeneration", 0xFFAA0000);
	mBuiltinMeshGenSignal.Wait([&]() { return mbBuiltinMeshGenFinished.load(); });
}


// ------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// LOAD
//
// ------------------------------------------------------------------------------------------------------------------------------------------------------------
void VQEngine::RenderThread_LoadWindowSizeDependentResources(HWND hwnd, int Width, int Height, float fResolutionScale)
{
	SCOPED_CPU_MARKER("RenderThread_LoadWindowSizeDependentResources()");
	assert(Width >= 1 && Height >= 1);

	const uint RenderResolutionX = static_cast<uint>(Width * fResolutionScale);
	const uint RenderResolutionY = static_cast<uint>(Height * fResolutionScale);

	if (hwnd == mpWinMain->GetHWND())
	{
		mRenderer.LoadWindowSizeDependentResources(hwnd, Width, Height, fResolutionScale, this->ShouldRenderHDR(hwnd));
	}
	// TODO: generic implementation of other window procedures for load
}

void VQEngine::RenderThread_UnloadWindowSizeDependentResources(HWND hwnd)
{
	SCOPED_CPU_MARKER("RenderThread_UnloadWindowSizeDependentResources()");
	if (hwnd == mpWinMain->GetHWND())
	{
		mRenderer.UnloadWindowSizeDependentResources(hwnd);
	}
	// TODO: generic implementation of other window procedures for unload
}


// ------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// RENDER
//
// ------------------------------------------------------------------------------------------------------------------------------------------------------------

static uint32_t GetNumShadowViewCmdRecordingThreads(const FSceneShadowViews& ShadowView)
{
#if RENDER_THREAD__MULTI_THREADED_COMMAND_RECORDING
	return (ShadowView.ShadowView_Directional.meshRenderParams.size() > 0 ? 1 : 0) // 1 thread for directional light (assumes long list of mesh render cmds)
		+ ShadowView.NumPointShadowViews  // each point light view (6x frustums per point light)
		+ (ShadowView.NumSpotShadowViews > 0 ? 1 : 0); // process spot light render lists in own thread
#else
	return 0;
#endif
}

bool VQEngine::ShouldEnableAsyncCompute()
{
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	const int NUM_BACK_BUFFERS = ctx.GetNumSwapchainBuffers();
	const int BACK_BUFFER_INDEX = ctx.GetCurrentSwapchainBufferIndex();
	const int FRAME_DATA_INDEX = mNumRenderLoopsExecuted % NUM_BACK_BUFFERS;
#else
	const int FRAME_DATA_INDEX = 0;
#endif

	const FSceneView& SceneView = mpScene->GetSceneView(FRAME_DATA_INDEX);
	const FSceneShadowViews& ShadowView = mpScene->GetShadowView(FRAME_DATA_INDEX);

	return mRenderer.ShouldEnableAsyncCompute(mSettings.gfx, SceneView, ShadowView/*, mbLoadingLevel, mbLoadingEnvironmentMap*/);
}

void VQEngine::RenderThread_PreRender()
{
	SCOPED_CPU_MARKER("RenderThread_PreRender()");
	FWindowRenderContext& ctx = mRenderer.GetWindowRenderContext(mpWinMain->GetHWND());
	
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	const int NUM_BACK_BUFFERS = ctx.GetNumSwapchainBuffers();
	const int BACK_BUFFER_INDEX = ctx.GetCurrentSwapchainBufferIndex();
	const int FRAME_DATA_INDEX  = mNumRenderLoopsExecuted % NUM_BACK_BUFFERS;
#else
	const int FRAME_DATA_INDEX = 0;
#endif

	const bool bAsyncSubmit = mWaitForSubmitWorker;

	if(mWaitForSubmitWorker) // not really used, need to offload submit to a non-worker thread (sync issues on main)
	{
		SCOPED_CPU_MARKER_C("BUSY_WAIT_WORKERS", 0xFFFF0000);
		while (!mSubmitWorkerFinished.load());
		mSubmitWorkerFinished.store(false);
		mWaitForSubmitWorker = false;
	}

	const FSceneView&        SceneView       = mpScene->GetSceneView(FRAME_DATA_INDEX);
	const FSceneShadowViews& SceneShadowView = mpScene->GetShadowView(FRAME_DATA_INDEX);

	const bool bUseAsyncCompute = ShouldEnableAsyncCompute();

#if RENDER_THREAD__MULTI_THREADED_COMMAND_RECORDING
	const uint32_t NumCmdRecordingThreads_GFX
		= 1 // worker thrd: DepthPrePass
		+ 1 // worker thrd: ObjectIDPass
		+ 1 // this thread: AO+SceneColor+PostProcess+Submit+Present
		+ GetNumShadowViewCmdRecordingThreads(SceneShadowView);
	const uint32_t NumCmdRecordingThreads_CMP = 0;
	const uint32_t NumCmdRecordingThreads_CPY = 0;
	const uint32_t NumCmdRecordingThreads = NumCmdRecordingThreads_GFX + NumCmdRecordingThreads_CPY + NumCmdRecordingThreads_CMP;
	const uint32_t ConstantBufferBytesPerThread = (128+256) * MEGABYTE;
#else
	const uint32_t NumCmdRecordingThreads_GFX = 1;
	const uint32_t NumCmdRecordingThreads = NumCmdRecordingThreads_GFX;
	const uint32_t ConstantBufferBytesPerThread = 36 * MEGABYTE;
#endif
	const uint NumCopyCmdLists = 1;
	const uint NumComputeCmdLists = 1;

#if 0
	Log::Info("");
	Log::Info("PreRender(): Swapchain[%d] %s | GFX: %d",
		ctx.GetCurrentSwapchainBufferIndex()
		, (bAsyncSubmit ? "(Async Submit)" : "")
		, NumCmdRecordingThreads_GFX
	);
#endif

	{
		SCOPED_CPU_MARKER("AllocCmdLists");
		ctx.AllocateCommandLists(CommandQueue::EType::GFX, NumCmdRecordingThreads_GFX);
		if (mSettings.gfx.bEnableAsyncCopy)
		{
			ctx.AllocateCommandLists(CommandQueue::EType::COPY, NumCopyCmdLists);
		}

		if (bUseAsyncCompute)
		{
			ctx.AllocateCommandLists(CommandQueue::EType::COMPUTE, NumComputeCmdLists);
		}
	}

	{
		SCOPED_CPU_MARKER("ResetCmdLists");
		ctx.ResetCommandLists(CommandQueue::EType::GFX, NumCmdRecordingThreads_GFX);
		if (mAppState == EAppState::SIMULATING) 
		{
			if (mSettings.gfx.bEnableAsyncCopy)
			{ 
				ctx.ResetCommandLists(CommandQueue::EType::COPY, NumCopyCmdLists);
			}
			if (bUseAsyncCompute)
			{
				ctx.ResetCommandLists(CommandQueue::EType::COMPUTE, NumComputeCmdLists);
			}
		}
	}
	{
		SCOPED_CPU_MARKER("AllocCBMem");
		ctx.AllocateConstantBufferMemory(NumCmdRecordingThreads, ConstantBufferBytesPerThread);
	}
	{
		SCOPED_CPU_MARKER("CB.BeginFrame");
		for (size_t iThread = 0; iThread < NumCmdRecordingThreads; ++iThread)
		{
			ctx.GetConstantBufferHeap(iThread).OnBeginFrame();
		}
	}

	ID3D12DescriptorHeap* ppHeaps[] = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };
	
	{
		SCOPED_CPU_MARKER("Cmd.SetDescHeap");
		auto& vpGFXCmds = ctx.GetGFXCommandListPtrs();
		for (uint32_t iGFX = 0; iGFX < NumCmdRecordingThreads_GFX; ++iGFX)
		{
			static_cast<ID3D12GraphicsCommandList*>(vpGFXCmds[iGFX])->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		}

		if (bUseAsyncCompute)
		{
			auto& vpCMPCmds = ctx.GetComputeCommandListPtrs();
			for (uint iCMP = 0; iCMP < NumComputeCmdLists; ++iCMP)
			{
				// TODO: do we need this?
				static_cast<ID3D12GraphicsCommandList*>(vpCMPCmds[iCMP])->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
			}
		}
	}
}

void VQEngine::RenderThread_RenderFrame()
{
	SCOPED_CPU_MARKER("RenderThread_RenderFrame()");

	// Render window contexts
	RenderThread_RenderMainWindow();

	if(mpWinDebug && !mpWinDebug->IsClosed())
		RenderThread_RenderDebugWindow();
}


void VQEngine::RenderThread_RenderMainWindow()
{
	SCOPED_CPU_MARKER("RenderThread_RenderMainWindow()");
	FWindowRenderContext& ctx = mRenderer.GetWindowRenderContext(mpWinMain->GetHWND());

	//
	// Handle one-time & infrequent events
	//
	if (mbEnvironmentMapPreFilter.load())
	{
		ID3D12GraphicsCommandList* pCmd = (ID3D12GraphicsCommandList*)ctx.GetCommandListPtr(CommandQueue::EType::GFX, 0);
		mRenderer.PreFilterEnvironmentMap(pCmd, mBuiltinMeshes[EBuiltInMeshes::CUBE], mpWinMain->GetHWND());
		mbEnvironmentMapPreFilter.store(false);
	}


	HRESULT hr = S_OK; 
	hr = mbLoadingLevel || mbLoadingEnvironmentMap
		? RenderThread_RenderMainWindow_LoadingScreen(ctx)
		: RenderThread_RenderMainWindow_Scene(ctx);
}


void VQEngine::RenderThread_HandleStatusOccluded()
{
	// TODO: remove copy paste and use encapsulation of context rendering properly
	// currently check only for hr0 for the mainWindow
	if (mpWinMain->IsFullscreen())
	{
		mpWinMain->SetFullscreen(false);
		mpWinMain->Show();
	}
}

void VQEngine::RenderThread_HandleDeviceRemoved()
{
	HRESULT hr = this->mRenderer.GetDevicePtr()->GetDeviceRemovedReason();

	this->mbStopAllThreads.store(true);

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	RenderThread_SignalUpdateThread();
#endif

	Log::Warning("Device Removed Reason: %s", DEVICE_REMOVED_MESSAGE_LOOKUP.at(hr).c_str());

	MessageBox(NULL, "Device Removed.\n\nVQEngine will shutdown.", "VQEngine Renderer Error", MB_OK);
	mbExitApp.store(true);
}



void VQEngine::RenderThread_RenderDebugWindow()
{
	// temporarily disabled
#if 0
	if (mScene_DebugWnd.mFrameData.empty())
		return;

	HRESULT hr = S_OK;
	FWindowRenderContext& ctx   = mRenderer.GetWindowRenderContext(mpWinDebug->GetHWND());
	const int NUM_BACK_BUFFERS  = ctx.SwapChain.GetNumBackBuffers();
	const int BACK_BUFFER_INDEX = ctx.SwapChain.GetCurrentBackBufferIndex();
	const int FRAME_DATA_INDEX  = mNumRenderLoopsExecuted % NUM_BACK_BUFFERS;
	const FFrameData& FrameData = mScene_DebugWnd.mFrameData[FRAME_DATA_INDEX];
	assert(ctx.mCommandAllocatorsGFX.size() >= NUM_BACK_BUFFERS);
	// ----------------------------------------------------------------------------
	
	//
	// PRE RENDER
	//
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	ID3D12CommandAllocator* pCmdAlloc = ctx.mCommandAllocatorsGFX[BACK_BUFFER_INDEX];
	ThrowIfFailed(pCmdAlloc->Reset());

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	ID3D12PipelineState* pInitialState = nullptr;
	ThrowIfFailed(ctx.pCmdList_GFX->Reset(pCmdAlloc, pInitialState));

	//
	// RENDER
	//
	ID3D12GraphicsCommandList*& pCmd = ctx.pCmdList_GFX;

	// Transition SwapChain RT
	ID3D12Resource* pSwapChainRT = ctx.SwapChain.GetCurrentBackBufferRenderTarget();
	pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT
		, D3D12_RESOURCE_STATE_PRESENT
		, D3D12_RESOURCE_STATE_RENDER_TARGET)
	);

	// Clear RT
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle = ctx.SwapChain.GetCurrentBackBufferRTVHandle();
	const float clearColor[] =
	{
		FrameData.SwapChainClearColor[0],
		FrameData.SwapChainClearColor[1],
		FrameData.SwapChainClearColor[2],
		FrameData.SwapChainClearColor[3]
	};
	pCmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

#if 1
	// Draw Triangle
	const Mesh& TriangleMesh = mBuiltinMeshes[EBuiltInMeshes::TRIANGLE];
	const auto& VBIBIDs      = TriangleMesh.GetIABufferIDs();
	const BufferID& VB_ID    = VBIBIDs.first;
	const BufferID& IB_ID    = VBIBIDs.second;
	const VBV& vbv = mRenderer.GetVertexBufferView(VB_ID);
	const IBV& ibv = mRenderer.GetIndexBufferView(IB_ID);
	const float RenderResolutionX = static_cast<float>(ctx.WindowDisplayResolutionX);
	const float RenderResolutionY = static_cast<float>(ctx.WindowDisplayResolutionY);
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };


	pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::HELLO_WORLD_TRIANGLE_PSO));
	pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(0));

	pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmd->IASetVertexBuffers(0, 1, &vbv);
	pCmd->IASetIndexBuffer(&ibv);

	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);

	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);

	pCmd->DrawIndexedInstanced(TriangleMesh.GetNumIndices(), 1, 0, 0, 0);
#endif

	// Transition SwapChain for Present
	pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT
		, D3D12_RESOURCE_STATE_RENDER_TARGET
		, D3D12_RESOURCE_STATE_PRESENT)
	);

	pCmd->Close();

	ID3D12CommandList* ppCommandLists[] = { ctx.pCmdList_GFX };
	ctx.PresentQueue.pQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);


	//
	// PRESENT
	//
	hr = ctx.SwapChain.Present();
	ctx.SwapChain.MoveToNextFrame();
	//return hr;
#endif
}

HRESULT VQEngine::RenderThread_RenderMainWindow_LoadingScreen(FWindowRenderContext& ctx)
{
	HRESULT hr = S_OK;
	const bool bUseHDRRenderPath = this->ShouldRenderHDR(mpWinMain->GetHWND());
	// ----------------------------------------------------------------------------

	//
	// RENDER
	//
#if RENDER_THREAD__MULTI_THREADED_COMMAND_RECORDING
	ID3D12GraphicsCommandList* pCmd = (ID3D12GraphicsCommandList*)ctx.GetCommandListPtr(CommandQueue::EType::GFX, 1); // RenderThreadID == 1
#else
	ID3D12GraphicsCommandList* pCmd = (ID3D12GraphicsCommandList*)ctx.GetCommandListPtr(CommandQueue::EType::GFX, 0);
#endif
	// Transition SwapChain RT
	ID3D12Resource* pSwapChainRT = ctx.SwapChain.GetCurrentBackBufferRenderTarget();
	CD3DX12_RESOURCE_BARRIER barrierPW = CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	CD3DX12_RESOURCE_BARRIER barrierWP = CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	pCmd->ResourceBarrier(1, &barrierPW);

	// Clear RT
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle = ctx.SwapChain.GetCurrentBackBufferRTVHandle();
	const float clearColor[] =
	{
		mLoadingScreenData.SwapChainClearColor[0],
		mLoadingScreenData.SwapChainClearColor[1],
		mLoadingScreenData.SwapChainClearColor[2],
		mLoadingScreenData.SwapChainClearColor[3]
	};
	pCmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

	// Draw Triangle
	const float           RenderResolutionX = static_cast<float>(ctx.WindowDisplayResolutionX);
	const float           RenderResolutionY = static_cast<float>(ctx.WindowDisplayResolutionY);
	D3D12_VIEWPORT        viewport          { 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	const auto            VBIBIDs           = mBuiltinMeshes[EBuiltInMeshes::TRIANGLE].GetIABufferIDs();
	const BufferID&       IB_ID             = VBIBIDs.second;
	const IBV&            ib                = mRenderer.GetIndexBufferView(IB_ID);
	D3D12_RECT            scissorsRect      { 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };

	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);

	pCmd->SetPipelineState(mRenderer.GetPSO(bUseHDRRenderPath ? EBuiltinPSOs::HDR_FP16_SWAPCHAIN_PSO : EBuiltinPSOs::FULLSCREEN_TRIANGLE_PSO));
	pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__FullScreenTriangle));

	pCmd->SetGraphicsRootDescriptorTable(0, mRenderer.GetShaderResourceView(mLoadingScreenData.GetSelectedLoadingScreenSRV_ID()).GetGPUDescHandle());

	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);

	pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmd->IASetVertexBuffers(0, 1, NULL);
	pCmd->IASetIndexBuffer(&ib);

	pCmd->DrawIndexedInstanced(3, 1, 0, 0, 0);

	// Transition SwapChain for Present
	pCmd->ResourceBarrier(1, &barrierWP); 

	std::vector<ID3D12CommandList*>& vCmdLists = ctx.GetGFXCommandListPtrs();
	const UINT NumCommandLists = ctx.GetNumCurrentlyRecordingThreads(CommandQueue::EType::GFX);
	for (UINT i = 0; i < NumCommandLists; ++i)
	{
		static_cast<ID3D12GraphicsCommandList*>(vCmdLists[i])->Close();
	}
	{
		SCOPED_CPU_MARKER("ExecuteCommandLists()");
		ctx.PresentQueue.pQueue->ExecuteCommandLists(NumCommandLists, (ID3D12CommandList**)vCmdLists.data());
	}

	if (ShouldEnableAsyncCompute())
	{
		//static_cast<ID3D12GraphicsCommandList*>(ctx.GetComputeCommandListPtrs()[0])->Close();
	}

	hr = PresentFrame(ctx);
	if (hr == DXGI_STATUS_OCCLUDED) { RenderThread_HandleStatusOccluded(); }
	if (hr == DXGI_ERROR_DEVICE_REMOVED) { RenderThread_HandleDeviceRemoved(); }
	ctx.SwapChain.MoveToNextFrame();
	return hr;
}


//====================================================================================================
//
// RENDER FRAME
//
//====================================================================================================
static void CopyPerObjectConstantBufferData(
	std::vector< D3D12_GPU_VIRTUAL_ADDRESS>& cbAddresses,
	DynamicBufferHeap* pCBufferHeap,
	const FSceneView& SceneView,
	const std::unique_ptr<Scene>& pScene
)
{
	SCOPED_CPU_MARKER("CopyPerObjectConstantBufferData");
	using namespace DirectX;
	using namespace VQ_SHADER_DATA;

	int iCB = 0;
	for (const MeshRenderCommand_t& meshRenderCmd : SceneView.meshRenderParams)
	{
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		PerObjectData* pPerObj = {};
		const size_t sz = sizeof(PerObjectData);
		
		pCBufferHeap->AllocConstantBuffer(sz, (void**)(&pPerObj), &cbAddr);
		cbAddresses[iCB++] = cbAddr;

#if RENDER_INSTANCED_SCENE_MESHES
		const uint32 NumInstances = (uint32)meshRenderCmd.matNormal.size();
		
		const size_t memcpySrcSize = meshRenderCmd.matWorldViewProj.size() * sizeof(XMMATRIX);
		const size_t memcpyDstSize = _countof(pPerObj->matWorldViewProj) * sizeof(XMMATRIX);
		if (memcpyDstSize < memcpySrcSize)
		{
			Log::Error("Batch data (scene) too big (%d: %s) for destination cbuffer (%d: %s): "
				, meshRenderCmd.matWorldViewProj.size()
				, StrUtil::FormatByte(memcpySrcSize)
				, _countof(pPerObj->matWorldViewProj)
				, StrUtil::FormatByte(memcpyDstSize)
			);
		}

		memcpy(pPerObj->matWorldViewProj    , meshRenderCmd.matWorldViewProj.data()    , sizeof(XMMATRIX) * NumInstances);
		memcpy(pPerObj->matWorldViewProjPrev, meshRenderCmd.matWorldViewProjPrev.data(), sizeof(XMMATRIX) * NumInstances);
		memcpy(pPerObj->matNormal           , meshRenderCmd.matNormal.data()           , sizeof(XMMATRIX) * NumInstances);
		memcpy(pPerObj->matWorld            , meshRenderCmd.matWorld.data()            , sizeof(XMMATRIX) * NumInstances);
		//memcpy(pPerObj->ObjID               , meshRenderCmd.objectID.data()            , sizeof(int) * NumInstances); // not 16B aligned
		for (uint i = 0; i < NumInstances; ++i)
		{
			pPerObj->ObjID[i].x = meshRenderCmd.objectID[i];
			pPerObj->ObjID[i].y = -222;
			pPerObj->ObjID[i].z = -333;
			pPerObj->ObjID[i].w = (int)(meshRenderCmd.projectedArea[i] * 10000); // float value --> int render target
		}
#else
		const uint32 NumInstances = 1;
		pPerObj->matWorldViewProj = meshRenderCmd.matWorldTransformation * SceneView.viewProj;
		pPerObj->matWorldViewProjPrev = meshRenderCmd.matWorldTransformation;
		pPerObj->matWorld = meshRenderCmd.matWorldTransformation;
		pPerObj->matNormal = meshRenderCmd.matNormalTransformation;
		pPerObj->mObjID = meshRenderCmd.objectID;
#endif

		const Material& mat = *meshRenderCmd.pMaterial;
		pPerObj->materialData = std::move(mat.GetCBufferData());
		//pPerObj->meshID = meshRenderCmd.meshID;
		pPerObj->materialID = meshRenderCmd.matID;
	}
}


HRESULT VQEngine::RenderThread_RenderMainWindow_Scene(FWindowRenderContext& ctx)
{
	const uint32 W = mpWinMain->GetWidth();
	const uint32 H = mpWinMain->GetHeight();

	SCOPED_CPU_MARKER("RenderThread_RenderMainWindow_Scene()");
	HRESULT hr                              = S_OK;
	const int NUM_BACK_BUFFERS              = ctx.GetNumSwapchainBuffers();
	const int BACK_BUFFER_INDEX             = ctx.GetCurrentSwapchainBufferIndex();
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	const int FRAME_DATA_INDEX              = mNumRenderLoopsExecuted % NUM_BACK_BUFFERS;
#else
	const int FRAME_DATA_INDEX = 0;
#endif
	const bool bReflectionsEnabled = mSettings.gfx.Reflections != EReflections::REFLECTIONS_OFF && mSettings.gfx.Reflections == EReflections::SCREEN_SPACE_REFLECTIONS__FFX; // TODO: remove the && after RayTracing is added
	const bool bDownsampleDepth    = mSettings.gfx.Reflections == EReflections::SCREEN_SPACE_REFLECTIONS__FFX;
	const bool bUseHDRRenderPath   = this->ShouldRenderHDR(mpWinMain->GetHWND());
	const bool& bMSAA              = mSettings.gfx.bAntiAliasing;
	const bool bAsyncCompute       = ShouldEnableAsyncCompute();

	const FSceneView& SceneView             = mpScene->GetSceneView(FRAME_DATA_INDEX);
	const FSceneShadowViews& SceneShadowView = mpScene->GetShadowView(FRAME_DATA_INDEX);
	const FPostProcessParameters& PPParams  = mpScene->GetPostProcessParameters(FRAME_DATA_INDEX);

	const FRenderingResources_MainWindow& rsc = mRenderer.GetRenderingResources_MainWindow();
	auto pRscNormals     = mRenderer.GetTextureResource(rsc.Tex_SceneNormals);
	auto pRscNormalsMSAA = mRenderer.GetTextureResource(rsc.Tex_SceneNormalsMSAA);
	auto pRscDepthResolve= mRenderer.GetTextureResource(rsc.Tex_SceneDepthResolve);
	auto pRscDepthMSAA   = mRenderer.GetTextureResource(rsc.Tex_SceneDepthMSAA);
	auto pRscDepth       = mRenderer.GetTextureResource(rsc.Tex_SceneDepth);


	ID3D12Resource* pRsc = nullptr;
	ID3D12CommandList* pCmdCpy = (ID3D12CommandList*)ctx.GetCommandListPtr(CommandQueue::EType::COPY, 0);
	CommandQueue& GFXCmdQ = mRenderer.GetCommandQueue(CommandQueue::EType::GFX);
	CommandQueue& CPYCmdQ = mRenderer.GetCommandQueue(CommandQueue::EType::COPY);
	CommandQueue& CMPCmdQ = mRenderer.GetCommandQueue(CommandQueue::EType::COMPUTE);


	// TODO: undo const cast and assign in a proper spot -------------------------------------------------
	FSceneView& RefSceneView = const_cast<FSceneView&>(SceneView);
	FPostProcessParameters& RefPPParams = const_cast<FPostProcessParameters&>(PPParams);

	RefSceneView.SceneRTWidth  = static_cast<int>(ctx.WindowDisplayResolutionX * (PPParams.IsFSREnabled() ? PPParams.ResolutionScale : 1.0f));
	RefSceneView.SceneRTHeight = static_cast<int>(ctx.WindowDisplayResolutionY * (PPParams.IsFSREnabled() ? PPParams.ResolutionScale : 1.0f));
	RefPPParams.SceneRTWidth  = SceneView.SceneRTWidth;
	RefPPParams.SceneRTHeight = SceneView.SceneRTHeight;
	RefPPParams.DisplayResolutionWidth  = ctx.WindowDisplayResolutionX;
	RefPPParams.DisplayResolutionHeight = ctx.WindowDisplayResolutionY;

	// do some settings override for some render paths
	if (bUseHDRRenderPath)
	{
#if !DISABLE_FIDELITYFX_CAS
		if (RefPPParams.IsFFXCASEnabled())
		{
			Log::Warning("FidelityFX CAS HDR not implemented, turning CAS off");
			RefPPParams.bEnableCAS = false;
		}
#endif
		if (RefPPParams.IsFSREnabled())
		{
			// TODO: HDR conversion pass to handle color range and precision/packing, shader variants etc.
			Log::Warning("FidelityFX Super Resolution HDR not implemented yet, turning FSR off"); 
			// RefPPParams.bEnableFSR = false;
			RefPPParams.UpscalingAlgorithm = FPostProcessParameters::EUpscalingAlgorithm::NONE; // TODO: enable resolution scaling for HDR
#if 0
			// this causes UI pass PSO to not match the render target format
			mEventQueue_WinToVQE_Renderer.AddItem(std::make_unique<WindowResizeEvent>(W, H, mpWinMain->GetHWND()));
			mEventQueue_WinToVQE_Update.AddItem(std::make_unique<WindowResizeEvent>(W, H, mpWinMain->GetHWND()));
#endif
		}
	}

	assert(PPParams.DisplayResolutionHeight != 0);
	assert(PPParams.DisplayResolutionWidth != 0);
	// TODO: undo const cast and assign in a proper spot -------------------------------------------------

	const float RenderResolutionX = static_cast<float>(SceneView.SceneRTWidth);
	const float RenderResolutionY = static_cast<float>(SceneView.SceneRTHeight);
	mRenderStats = {};

	std::vector< D3D12_GPU_VIRTUAL_ADDRESS> cbAddresses(SceneView.meshRenderParams.size());
	UINT64 SSAODoneFenceValue = mAsyncComputeSSAODoneFence[BACK_BUFFER_INDEX].GetValue();
	D3D12_GPU_VIRTUAL_ADDRESS cbPerFrame = {};
	{
		SCOPED_CPU_MARKER("CopyPerFrameConstantBufferData");
		DynamicBufferHeap& CBHeap = ctx.GetConstantBufferHeap(0);
		using namespace VQ_SHADER_DATA;
		PerFrameData* pPerFrame = {};
		CBHeap.AllocConstantBuffer(sizeof(PerFrameData), (void**)(&pPerFrame), &cbPerFrame);

		assert(pPerFrame);
		pPerFrame->Lights = SceneView.GPULightingData;
		pPerFrame->fAmbientLightingFactor = SceneView.sceneParameters.fAmbientLightingFactor;
		pPerFrame->f2PointLightShadowMapDimensions       = { 1024.0f, 1024.f  }; // TODO
		pPerFrame->f2SpotLightShadowMapDimensions        = { 1024.0f, 1024.f  }; // TODO
		pPerFrame->f2DirectionalLightShadowMapDimensions = { 2048.0f, 2048.0f }; // TODO
		pPerFrame->fHDRIOffsetInRadians = SceneView.HDRIYawOffset;

		if (bUseHDRRenderPath)
		{
			// adjust ambient factor as the tonemapper changes the output curve for HDR displays 
			// and makes the ambient lighting too strong.
			pPerFrame->fAmbientLightingFactor *= 0.005f;
		}
	}

	D3D12_GPU_VIRTUAL_ADDRESS cbPerView = {};
	{
		SCOPED_CPU_MARKER("CopyPerViewConstantBufferData");
		DynamicBufferHeap& CBHeap = ctx.GetConstantBufferHeap(0);
		using namespace VQ_SHADER_DATA;
		PerViewData* pPerView = {};
		CBHeap.AllocConstantBuffer(sizeof(decltype(*pPerView)), (void**)(&pPerView), &cbPerView);

		assert(pPerView);
		XMStoreFloat3(&pPerView->CameraPosition, SceneView.cameraPosition);
		pPerView->ScreenDimensions.x = RenderResolutionX;
		pPerView->ScreenDimensions.y = RenderResolutionY;
		pPerView->MaxEnvMapLODLevels = static_cast<float>(rsc.EnvironmentMap.GetNumSpecularIrradianceCubemapLODLevels(mRenderer));
		pPerView->EnvironmentMapDiffuseOnlyIllumination = mSettings.gfx.Reflections == EReflections::SCREEN_SPACE_REFLECTIONS__FFX;
		const FFrustumPlaneset planes = FFrustumPlaneset::ExtractFromMatrix(SceneView.viewProj, true);
		memcpy(pPerView->WorldFrustumPlanes, planes.abcd, sizeof(planes.abcd));
	}

	if constexpr (!RENDER_THREAD__MULTI_THREADED_COMMAND_RECORDING)
	{
		constexpr size_t THREAD_INDEX = 0;

		ID3D12GraphicsCommandList* pCmd = (ID3D12GraphicsCommandList*)ctx.GetCommandListPtr(CommandQueue::EType::GFX, THREAD_INDEX);
		DynamicBufferHeap& CBHeap = ctx.GetConstantBufferHeap(THREAD_INDEX);

		CopyPerObjectConstantBufferData(cbAddresses, &CBHeap, SceneView, mpScene);

		RenderSpotShadowMaps(pCmd, &CBHeap, SceneShadowView);
		RenderDirectionalShadowMaps(pCmd, &CBHeap, SceneShadowView);
		RenderPointShadowMaps(pCmd, &CBHeap, SceneShadowView, 0, SceneShadowView.NumPointShadowViews);

		RenderDepthPrePass(pCmd, &CBHeap, SceneView, cbAddresses, cbPerView, cbPerFrame);

		mRenderer.RenderObjectIDPass(pCmd, pCmdCpy, &CBHeap, cbAddresses, cbPerView, SceneView, SceneShadowView, BACK_BUFFER_INDEX, mSettings.gfx, mAsyncComputeWorkSubmitted);

		if (bMSAA)
		{
			ResolveMSAA_DepthPrePass(pCmd, &CBHeap);
		}

		TransitionDepthPrePassForRead(pCmd, bMSAA);

		if (!bMSAA)
		{
			CopyDepthForCompute(pCmd); // for FFX-CACAO 
		}

		if (bDownsampleDepth)
		{
			DownsampleDepth(pCmd, &CBHeap, rsc.Tex_SceneDepth, rsc.SRV_SceneDepth);
		}

		RenderAmbientOcclusion(pCmd, SceneView);

		TransitionForSceneRendering(pCmd, ctx, PPParams);

		RenderSceneColor(pCmd, &CBHeap, SceneView, PPParams, cbAddresses, cbPerView, cbPerFrame);

		if (!bReflectionsEnabled)
		{
			RenderLightBounds(pCmd, &CBHeap, SceneView, bMSAA, bReflectionsEnabled);
			RenderBoundingBoxes(pCmd, &CBHeap, SceneView, bMSAA);
			RenderDebugVertexAxes(pCmd, &CBHeap, SceneView, bMSAA);
			
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = mRenderer.GetRTV(bMSAA ? rsc.RTV_SceneColorMSAA : rsc.RTV_SceneColor).GetCPUDescHandle();
			RenderOutline(pCmd, &CBHeap, cbPerView, SceneView, bMSAA, { rtvHandle });
		}

		ResolveMSAA(pCmd, &CBHeap, PPParams);

		TransitionForPostProcessing(pCmd, PPParams);

		if (bReflectionsEnabled)
		{
			RenderReflections(pCmd, &CBHeap, SceneView);
			
			// render scene-debugging stuff that shouldn't be in reflections: bounding volumes, etc
			RenderSceneBoundingVolumes(pCmd, &CBHeap, cbPerView, SceneView, bMSAA);

			CompositeReflections(pCmd, &CBHeap, SceneView);
		}

		pRsc = RenderPostProcess(pCmd, &CBHeap, PPParams);

		RenderUI(pCmd, &CBHeap, ctx, PPParams, pRsc);

		if (bUseHDRRenderPath)
		{
			CompositUIToHDRSwapchain(pCmd, &CBHeap, ctx, PPParams);
		}
	}

	else // RENDER_THREAD__MULTI_THREADED_COMMAND_RECORDING
	{
		#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
		ThreadPool& WorkerThreads = mWorkers_Render;
		#else
		ThreadPool& WorkerThreads = mWorkers_Simulation;
		#endif

		constexpr size_t iCmdZPrePassThread = 0;
		constexpr size_t iCmdObjIDPassThread = iCmdZPrePassThread + 1;
		constexpr size_t iCmdPointLightsThread = iCmdObjIDPassThread + 1;
		const     size_t iCmdSpots        = iCmdPointLightsThread + SceneShadowView.NumPointShadowViews;
		const     size_t iCmdDirectional  = iCmdSpots + (SceneShadowView.NumSpotShadowViews > 0 ? 1 : 0);
		const     size_t iCmdRenderThread = iCmdDirectional + (SceneShadowView.ShadowView_Directional.meshRenderParams.empty() ? 0 : 1);

		ID3D12GraphicsCommandList* pCmd_ThisThread = (ID3D12GraphicsCommandList*)ctx.GetCommandListPtr(CommandQueue::EType::GFX, iCmdRenderThread);
		DynamicBufferHeap& CBHeap_This = ctx.GetConstantBufferHeap(iCmdRenderThread);

		{
			SCOPED_CPU_MARKER_C("BUSY_WAIT_WORKER", 0xFFFF0000); // sync for SceneView
			while (WorkerThreads.GetNumActiveTasks() != 0); // busy-wait is not good
		}
		CopyPerObjectConstantBufferData(cbAddresses, &CBHeap_This, SceneView, mpScene); // TODO: threadify this, join+fork

		{
			SCOPED_CPU_MARKER("DispatchWorkers");

			// ZPrePass
			{
				ID3D12GraphicsCommandList* pCmd_ZPrePass = (ID3D12GraphicsCommandList*)ctx.GetCommandListPtr(CommandQueue::EType::GFX, iCmdZPrePassThread);
				ID3D12GraphicsCommandList* pCmd_Compute = (ID3D12GraphicsCommandList*)ctx.GetCommandListPtr(CommandQueue::EType::COMPUTE, 0);

				DynamicBufferHeap& CBHeap_WorkerZPrePass = ctx.GetConstantBufferHeap(iCmdZPrePassThread);
				WorkerThreads.AddTask([=, &CBHeap_WorkerZPrePass, &SceneView, &cbAddresses, &ctx]()
				{
					RENDER_WORKER_CPU_MARKER;

					TransitionDepthPrePassForWrite(pCmd_ZPrePass, bMSAA);

					RenderDepthPrePass(pCmd_ZPrePass, &CBHeap_WorkerZPrePass, SceneView, cbAddresses, cbPerView, cbPerFrame);

					if (bMSAA)
					{
						TransitionDepthPrePassMSAAResolve(pCmd_ZPrePass);
					}

					if (bAsyncCompute)
					{
						if (!bMSAA)
						{
							CopyDepthForCompute(pCmd_ZPrePass); // This should be eventually removed, see function body
						}

						ID3D12Resource* pRscAmbientOcclusion = mRenderer.GetTextureResource(rsc.Tex_AmbientOcclusion);
						CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(pRscAmbientOcclusion, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
						pCmd_ZPrePass->ResourceBarrier(1, &barrier);

						pCmd_ZPrePass->Close();
						{
							SCOPED_CPU_MARKER("ExecGfxCmdList");
							GFXCmdQ.pQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&pCmd_ZPrePass);
						}
						{
							SCOPED_CPU_MARKER("Signal");
							mAsyncComputeSSAOReadyFence[BACK_BUFFER_INDEX].Signal(GFXCmdQ.pQueue);
							mAsyncComputeSSAOReadyFence[BACK_BUFFER_INDEX].WaitOnGPU(CMPCmdQ.pQueue);
							mAsyncComputeWorkSubmitted.store(true);
						}

						if (bMSAA)
						{
							ResolveMSAA_DepthPrePass(pCmd_Compute, &CBHeap_WorkerZPrePass);
							TransitionDepthPrePassForReadAsyncCompute(pCmd_Compute);
						}

						if (bDownsampleDepth)
						{
							DownsampleDepth(pCmd_Compute, &CBHeap_WorkerZPrePass, rsc.Tex_SceneDepth, rsc.SRV_SceneDepth);
						}

						RenderAmbientOcclusion(pCmd_Compute, SceneView);

						{
							SCOPED_CPU_MARKER("ExecCmpCmdList");
							pCmd_Compute->Close();
							CMPCmdQ.pQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&pCmd_Compute);
						}
						mAsyncComputeSSAODoneFence[BACK_BUFFER_INDEX].Signal(CMPCmdQ.pQueue);
					}
					else
					{
						if (bMSAA)
						{
							ResolveMSAA_DepthPrePass(pCmd_ZPrePass, &CBHeap_WorkerZPrePass);
						}

						TransitionDepthPrePassForRead(pCmd_ZPrePass, bMSAA);

						if (!bMSAA)
						{
							CopyDepthForCompute(pCmd_ZPrePass); // This should be eventually removed, see function body
						}

						if (bDownsampleDepth)
						{
							DownsampleDepth(pCmd_ZPrePass, &CBHeap_WorkerZPrePass, rsc.Tex_SceneDepth, rsc.SRV_SceneDepth);
						}
					}
				});
			}

			// objectID Pass
			{
				ID3D12GraphicsCommandList* pCmd_ObjIDPass = (ID3D12GraphicsCommandList*)ctx.GetCommandListPtr(CommandQueue::EType::GFX, iCmdObjIDPassThread);

				DynamicBufferHeap& CBHeap_WorkerObjIDPass = ctx.GetConstantBufferHeap(iCmdObjIDPassThread);
				WorkerThreads.AddTask([=, &SceneView, &cbAddresses, &CBHeap_WorkerObjIDPass]()
				{
					RENDER_WORKER_CPU_MARKER;
					mRenderer.RenderObjectIDPass(pCmd_ObjIDPass, pCmdCpy, &CBHeap_WorkerObjIDPass, cbAddresses, cbPerView, SceneView, SceneShadowView, BACK_BUFFER_INDEX, mSettings.gfx, mAsyncComputeWorkSubmitted);
				});
			}

			if (SceneShadowView.NumSpotShadowViews > 0)
			{
				ID3D12GraphicsCommandList* pCmd_Spots = (ID3D12GraphicsCommandList*)ctx.GetCommandListPtr(CommandQueue::EType::GFX, iCmdSpots);
				DynamicBufferHeap& CBHeap_Spots = ctx.GetConstantBufferHeap(iCmdSpots);
				WorkerThreads.AddTask([=, &CBHeap_Spots, &SceneShadowView]()
				{
					RENDER_WORKER_CPU_MARKER;
					RenderSpotShadowMaps(pCmd_Spots, &CBHeap_Spots, SceneShadowView);
				});
			}
			if (SceneShadowView.NumPointShadowViews > 0)
			{
				for (uint iPoint = 0; iPoint < SceneShadowView.NumPointShadowViews; ++iPoint)
				{
					const size_t iPointWorker = iCmdPointLightsThread + iPoint;
					ID3D12GraphicsCommandList* pCmd_Point = (ID3D12GraphicsCommandList*)ctx.GetCommandListPtr(CommandQueue::EType::GFX, iPointWorker);
					DynamicBufferHeap& CBHeap_Point = ctx.GetConstantBufferHeap(iPointWorker);
					WorkerThreads.AddTask([=, &CBHeap_Point, &SceneShadowView]()
					{
						RENDER_WORKER_CPU_MARKER;
						RenderPointShadowMaps(pCmd_Point, &CBHeap_Point, SceneShadowView, iPoint, 1);
					});
				}
			}

			if (!SceneShadowView.ShadowView_Directional.meshRenderParams.empty())
			{
				ID3D12GraphicsCommandList* pCmd_Directional = (ID3D12GraphicsCommandList*)ctx.GetCommandListPtr(CommandQueue::EType::GFX, iCmdDirectional);
				DynamicBufferHeap& CBHeap_Directional = ctx.GetConstantBufferHeap(iCmdDirectional);
				WorkerThreads.AddTask([=, &CBHeap_Directional, &SceneShadowView]()
				{
					RENDER_WORKER_CPU_MARKER;
					RenderDirectionalShadowMaps(pCmd_Directional, &CBHeap_Directional, SceneShadowView);
				});
			}
		}

		if (bAsyncCompute)
		{
			// async compute queue cannot issue these barrier transitions. 
			// we're waiting the async compute fence before we execute this cmd list so there's no sync issues.
			ID3D12Resource* pRscAmbientOcclusion = mRenderer.GetTextureResource(rsc.Tex_AmbientOcclusion);
			std::vector<CD3DX12_RESOURCE_BARRIER> Barriers;
			if (bMSAA) 
			{
				Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthMSAA, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));
				Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscNormalsMSAA, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
			}
			Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscAmbientOcclusion, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ));
			pCmd_ThisThread->ResourceBarrier((UINT32)Barriers.size(), Barriers.data());
		}
		else
		{
			RenderAmbientOcclusion(pCmd_ThisThread, SceneView);
		}

		TransitionForSceneRendering(pCmd_ThisThread, ctx, PPParams);

		RenderSceneColor(pCmd_ThisThread, &CBHeap_This, SceneView, PPParams, cbAddresses, cbPerView, cbPerFrame);

		if (!bReflectionsEnabled)
		{
			RenderLightBounds(pCmd_ThisThread, &CBHeap_This, SceneView, bMSAA, bReflectionsEnabled);
			RenderBoundingBoxes(pCmd_ThisThread, &CBHeap_This, SceneView, bMSAA);
			RenderDebugVertexAxes(pCmd_ThisThread, &CBHeap_This, SceneView, bMSAA);

			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = mRenderer.GetRTV(bMSAA ? rsc.RTV_SceneColorMSAA : rsc.RTV_SceneColor).GetCPUDescHandle();
			RenderOutline(pCmd_ThisThread, &CBHeap_This, cbPerView, SceneView, bMSAA, { rtvHandle });
		}

		ResolveMSAA(pCmd_ThisThread, &CBHeap_This, PPParams);

		TransitionForPostProcessing(pCmd_ThisThread, PPParams);

		if (bReflectionsEnabled)
		{
			RenderReflections(pCmd_ThisThread, &CBHeap_This, SceneView);
			
			// render scene-debugging stuff that shouldn't be in reflections: bounding volumes, etc
			RenderSceneBoundingVolumes(pCmd_ThisThread, &CBHeap_This, cbPerView, SceneView, bMSAA);

			CompositeReflections(pCmd_ThisThread, &CBHeap_This, SceneView);
		}

		pRsc = RenderPostProcess(pCmd_ThisThread, &CBHeap_This, PPParams);

		RenderUI(pCmd_ThisThread, &CBHeap_This, ctx, PPParams, pRsc);

		if (bUseHDRRenderPath)
		{
			CompositUIToHDRSwapchain(pCmd_ThisThread, &CBHeap_This, ctx, PPParams);
		}

		// SYNC Render Workers
		{	
			SCOPED_CPU_MARKER_C("BUSY_WAIT_WORKERS", 0xFFFF0000);
			while (WorkerThreads.GetNumActiveTasks() != 0);
		}
	}

	// close gfx cmd lists
	std::vector<ID3D12CommandList*> vCmdLists = ctx.GetGFXCommandListPtrs();
	if constexpr (!RENDER_THREAD__MULTI_THREADED_COMMAND_RECORDING)
	{
		// TODO: async compute support for single-threaded CPU cmd recording
		static_cast<ID3D12GraphicsCommandList*>(vCmdLists[0])->Close();
		ctx.PresentQueue.pQueue->ExecuteCommandLists(1, vCmdLists.data());

		hr = PresentFrame(ctx);
		if (hr == DXGI_STATUS_OCCLUDED) { RenderThread_HandleStatusOccluded(); }
		if (hr == DXGI_ERROR_DEVICE_REMOVED) { RenderThread_HandleDeviceRemoved(); }
		{
			SCOPED_CPU_MARKER_C("GPU_BOUND", 0xFF005500);
			ctx.SwapChain.MoveToNextFrame();
		}
	}

	else
	{
		#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
		ThreadPool& WorkerThreads = mWorkers_Render;
		#else
		ThreadPool& WorkerThreads = mWorkers_Simulation;
		#endif
		const bool bAsyncCopy = mSettings.gfx.bEnableAsyncCopy;

		//  TODO remove this copy paste
		constexpr size_t iCmdZPrePassThread = 0;
		constexpr size_t iCmdObjIDPassThread = iCmdZPrePassThread + 1;
		constexpr size_t iCmdPointLightsThread = iCmdObjIDPassThread + 1;
		const     size_t iCmdSpots = iCmdPointLightsThread + SceneShadowView.NumPointShadowViews;
		const     size_t iCmdDirectional = iCmdSpots + (SceneShadowView.NumSpotShadowViews > 0 ? 1 : 0);
		const     size_t iCmdRenderThread = iCmdDirectional + (SceneShadowView.ShadowView_Directional.meshRenderParams.empty() ? 0 : 1);

		// close command lists
		const UINT NumCommandLists = ctx.GetNumCurrentlyRecordingThreads(CommandQueue::EType::GFX);
		for (UINT i = 0; i < NumCommandLists; ++i)
		{
			if (mSettings.gfx.bEnableAsyncCopy && (i == iCmdObjIDPassThread))
				continue;
			if (bAsyncCompute && (i == iCmdZPrePassThread)) 
				continue; // already closed & executed

			static_cast<ID3D12GraphicsCommandList*>(vCmdLists[i])->Close();
		}

		// execute command lists on a thread
#if EXECUTE_CMD_LISTS_ON_WORKER
		mWaitForSubmitWorker = true;
		WorkerThreads.AddTask([=, &SceneView, &cbAddresses, &ctx, &SceneShadowView]()
		{	
			RENDER_WORKER_CPU_MARKER;
			{
#endif
				if (bAsyncCompute)
				{
					SCOPED_CPU_MARKER("ExecuteCommandLists_Async");

					ID3D12CommandList* pGfxCmd = vCmdLists[iCmdRenderThread];
					ID3D12CommandList* pGfxCmdObjIDPass = vCmdLists[iCmdObjIDPassThread];
					
					std::vector<ID3D12CommandList*> vLightCommandLists;
					{
						SCOPED_CPU_MARKER("Malloc");
						if (!bAsyncCopy)
						{
							vLightCommandLists.push_back(pGfxCmdObjIDPass); // append objID to the shadow pass cmd lists if async copy isnt enabled for batching
						}
						for (size_t i = iCmdPointLightsThread; i - iCmdPointLightsThread < SceneShadowView.NumPointShadowViews; ++i)
							vLightCommandLists.push_back(vCmdLists[i]);
						if (iCmdSpots != iCmdRenderThread)
							vLightCommandLists.push_back(vCmdLists[iCmdSpots]);
						if (iCmdDirectional != iCmdRenderThread)
							vLightCommandLists.push_back(vCmdLists[iCmdDirectional]);
					}

					if (!vLightCommandLists.empty())
					{
						ctx.PresentQueue.pQueue->ExecuteCommandLists((UINT)vLightCommandLists.size(), (ID3D12CommandList**)&vLightCommandLists[0]);
					}

					mAsyncComputeSSAODoneFence[BACK_BUFFER_INDEX].WaitOnGPU(ctx.PresentQueue.pQueue, SSAODoneFenceValue + 1);
					ctx.PresentQueue.pQueue->ExecuteCommandLists(1, &pGfxCmd);
				}
				else
				{
					SCOPED_CPU_MARKER("ExecuteCommandLists");
					std::vector<ID3D12CommandList*> vCmdLists = ctx.GetGFXCommandListPtrs();
					if (mSettings.gfx.bEnableAsyncCopy)
					{
						vCmdLists.erase(vCmdLists.begin() + iCmdObjIDPassThread); // already kicked off objID pass
					}

					#if 0 // debug log
					for (int i = 0; i < vCmdLists.size(); ++i)
					{
						std::string s = "";
						if (i == iCmdZPrePassThread) s += "ZPrePass";
						if (i == iCmdObjIDPassThread) s += "ObjIDPass";
						if (i == iCmdPointLightsThread && SceneShadowView.NumPointShadowViews > 0) s += "PointLights";
						if (i == iCmdSpots && SceneShadowView.NumSpotShadowViews > 0) s += "SpotLights";
						if (i == iCmdDirectional && iCmdDirectional != iCmdRenderThread) s += "DirLight";
						if (i == iCmdRenderThread) s += "RenderThread";
						Log::Info("  GFX : pCmd[%d]: %x | %s", i, vCmdLists[i], s.c_str());
					}
					#endif

					const size_t NumCmds = iCmdRenderThread + (mSettings.gfx.bEnableAsyncCopy ? 0 : 1);
					ctx.PresentQueue.pQueue->ExecuteCommandLists((UINT)NumCmds, (ID3D12CommandList**)&vCmdLists[0]);
				}

				HRESULT hr = PresentFrame(ctx);
				#if !EXECUTE_CMD_LISTS_ON_WORKER
				if (hr == DXGI_STATUS_OCCLUDED) { RenderThread_HandleStatusOccluded(); }
				if (hr == DXGI_ERROR_DEVICE_REMOVED) { RenderThread_HandleDeviceRemoved(); }
				#endif

				ctx.SwapChain.MoveToNextFrame();

#if EXECUTE_CMD_LISTS_ON_WORKER
				mSubmitWorkerFinished.store(true);
			}
		});
#endif
	}

	return hr;
}
