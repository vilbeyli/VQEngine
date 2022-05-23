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
#include "Geometry.h"
#include "GPUMarker.h"

#include <d3d12.h>
#include <dxgi.h>

#include "RenderPass/AmbientOcclusion.h"
#include "RenderPass/DepthPrePass.h"

#include "VQEngine_RenderCommon.h"

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
void VQEngine::RenderThread_Inititalize()
{
	mRenderPasses = // manual render pass registration for now (early in dev)
	{
		&mRenderPass_AO,
		&mRenderPass_SSR,
		&mRenderPass_ApplyReflections,
		&mRenderPass_ZPrePass,
		&mRenderPass_DepthResolve
	};

	const bool bExclusiveFullscreen_MainWnd = CheckInitialSwapchainResizeRequired(mInitialSwapchainResizeRequiredWindowLookup, mSettings.WndMain, mpWinMain->GetHWND());

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	mNumRenderLoopsExecuted.store(0);
#endif

	// Initialize swapchains for each rendering window
	// all windows use the same number of swapchains as the main window
	const int NUM_SWAPCHAIN_BUFFERS = mSettings.gfx.bUseTripleBuffering ? 3 : 2;
	{
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
		const bool bIsContainingWindowOnHDRScreen = VQSystemInfo::FMonitorInfo::CheckHDRSupport(mpWinDebug->GetHWND());
		constexpr bool bCreateHDRSwapchain = false; // only main window in HDR for now
		mRenderer.InitializeRenderContext(mpWinDebug.get(), NUM_SWAPCHAIN_BUFFERS, false, bCreateHDRSwapchain);
		mEventQueue_VQEToWin_Main.AddItem(std::make_shared<HandleWindowTransitionsEvent>(mpWinDebug->GetHWND()));
	}

	InitializeBuiltinMeshes();

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	mbRenderThreadInitialized.store(true);
#endif

	// load builtin resources, compile shaders, load PSOs
	mRenderer.Load(); // TODO: THREADED LOADING
	RenderThread_LoadResources();

	// initialize render passes
	std::vector<FPSOCreationTaskParameters> RenderPassPSOLoadDescs;
	for (IRenderPass* pPass : mRenderPasses)
	{
		pPass->Initialize(); // initialize the render pass

		// collect its PSO load descriptors so we can dispatch PSO compilation workers
		const auto vPassPSODescs = pPass->CollectPSOCreationParameters();
		RenderPassPSOLoadDescs.insert(RenderPassPSOLoadDescs.end()
			, std::make_move_iterator(vPassPSODescs.begin())
			, std::make_move_iterator(vPassPSODescs.end())
		);
	}

	// compile PSOs (single-threaded)
	for (auto& pr : RenderPassPSOLoadDescs)
	{
		*pr.pID = mRenderer.CreatePSO_OnThisThread(pr.Desc);
	}


	// load window resources
	const bool bFullscreen = mpWinMain->IsFullscreen();
	const int W = bFullscreen ? mpWinMain->GetFullscreenWidth() : mpWinMain->GetWidth();
	const int H = bFullscreen ? mpWinMain->GetFullscreenHeight() : mpWinMain->GetHeight();

	// Post process parameters are not initialized at this stage to determine the resolution scale
	const float fResolutionScale = 1.0f;
	RenderThread_LoadWindowSizeDependentResources(mpWinMain->GetHWND(), W, H, fResolutionScale);

	mTimerRender.Reset();
	mTimerRender.Start();
}

void VQEngine::RenderThread_Exit()
{
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	mpSemUpdate->Signal();
#endif
	for (IRenderPass* pPass : mRenderPasses)
	{
		pPass->Destroy();
	}
}

void VQEngine::InitializeBuiltinMeshes()
{
	using VertexType = FVertexWithNormalAndTangent;
	{
		const EBuiltInMeshes eMesh = EBuiltInMeshes::TRIANGLE;
		GeometryGenerator::GeometryData<VertexType> data = GeometryGenerator::Triangle<VertexType>(1.0f);
		mResourceNames.mBuiltinMeshNames[eMesh] = "Triangle";
		mBuiltinMeshes[eMesh] = Mesh(&mRenderer, data.Vertices, data.Indices, mResourceNames.mBuiltinMeshNames[eMesh]);
	}
	{
		const EBuiltInMeshes eMesh = EBuiltInMeshes::CUBE;
		GeometryGenerator::GeometryData<VertexType> data = GeometryGenerator::Cube<VertexType>();
		mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::CUBE] = "Cube";
		mBuiltinMeshes[eMesh] = Mesh(&mRenderer, data.Vertices, data.Indices, mResourceNames.mBuiltinMeshNames[eMesh]);
	} 
	{
		const EBuiltInMeshes eMesh = EBuiltInMeshes::CYLINDER;
		GeometryGenerator::GeometryData<VertexType> data = GeometryGenerator::Cylinder<VertexType>(3.0f, 1.0f, 1.0f, 45, 6, 1);
		mResourceNames.mBuiltinMeshNames[eMesh] = "Cylinder";
		mBuiltinMeshes[eMesh] = Mesh(&mRenderer, data.Vertices, data.Indices, mResourceNames.mBuiltinMeshNames[eMesh]);
	}
	{
		const EBuiltInMeshes eMesh = EBuiltInMeshes::SPHERE;
		GeometryGenerator::GeometryData<VertexType> data = GeometryGenerator::Sphere<VertexType>(1.0f, 30, 30, 1);
		mResourceNames.mBuiltinMeshNames[eMesh] = "Sphere";
		mBuiltinMeshes[eMesh] = Mesh(&mRenderer, data.Vertices, data.Indices, mResourceNames.mBuiltinMeshNames[eMesh]);
	}
	{
		const EBuiltInMeshes eMesh = EBuiltInMeshes::CONE;
		GeometryGenerator::GeometryData<VertexType> data = GeometryGenerator::Cone<VertexType>(1, 1, 42);
		mResourceNames.mBuiltinMeshNames[eMesh] = "Cone";
		mBuiltinMeshes[eMesh] = Mesh(&mRenderer, data.Vertices, data.Indices, mResourceNames.mBuiltinMeshNames[eMesh]);
	}
	// ...

	mRenderer.UploadVertexAndIndexBufferHeaps();
}


// ------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// LOAD
//
// ------------------------------------------------------------------------------------------------------------------------------------------------------------
void VQEngine::RenderThread_LoadWindowSizeDependentResources(HWND hwnd, int Width, int Height, float fResolutionScale)
{
	assert(Width >= 1 && Height >= 1);

	const uint RenderResolutionX = static_cast<uint>(Width * fResolutionScale);
	const uint RenderResolutionY = static_cast<uint>(Height * fResolutionScale);

	if (hwnd == mpWinMain->GetHWND())
	{
		const bool bHDR = this->IsHDRSettingOn();
		const bool bRenderingHDR = this->ShouldRenderHDR(hwnd);

		constexpr DXGI_FORMAT MainColorRTFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
		const     DXGI_FORMAT TonemapperOutputFormat = bRenderingHDR ? VQEngine::PREFERRED_HDR_FORMAT : DXGI_FORMAT_R8G8B8A8_UNORM;

		FRenderingResources_MainWindow& r = mResources_MainWnd;


		{	// Scene depth stencil view /w MSAA
			TextureCreateDesc desc("SceneDepthMSAA");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				DXGI_FORMAT_R32_TYPELESS
				, RenderResolutionX
				, RenderResolutionY
				, 1 // Array Size
				, 1 // MIP levels
				, MSAA_SAMPLE_COUNT // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
			);
			desc.ResourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
			r.Tex_SceneDepthMSAA = mRenderer.CreateTexture(desc);
			mRenderer.InitializeDSV(r.DSV_SceneDepthMSAA, 0u, r.Tex_SceneDepthMSAA);
			mRenderer.InitializeSRV(r.SRV_SceneDepthMSAA, 0u, r.Tex_SceneDepthMSAA);
		}
		{	// Scene depth stencil resolve target
			TextureCreateDesc desc("SceneDepthResolve");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				DXGI_FORMAT_R32_FLOAT
				, RenderResolutionX
				, RenderResolutionY
				, 1 // Array Size
				, 1 // MIP levels
				, 1 // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
			);
			desc.ResourceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			r.Tex_SceneDepthResolve = mRenderer.CreateTexture(desc);
			mRenderer.InitializeUAV(r.UAV_SceneDepth, 0u, r.Tex_SceneDepthResolve, 0u, 0u);
			mRenderer.InitializeSRV(r.SRV_SceneDepth, 0u, r.Tex_SceneDepthResolve, 0u, 0u);
		}
		{	// Scene depth stencil target (for MSAA off)
			TextureCreateDesc desc("SceneDepth");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				DXGI_FORMAT_R32_TYPELESS
				, RenderResolutionX
				, RenderResolutionY
				, 1 // Array Size
				, 1 // MIP levels
				, 1 // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
			);
			desc.ResourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
			r.Tex_SceneDepth = mRenderer.CreateTexture(desc);
			mRenderer.InitializeDSV(r.DSV_SceneDepth, 0u, r.Tex_SceneDepth);
		}
		{
			TextureCreateDesc desc("DownsampledSceneDepth");
			const int NumMIPs = Image::CalculateMipLevelCount(RenderResolutionX, RenderResolutionY);
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				DXGI_FORMAT_R32_FLOAT
				, RenderResolutionX
				, RenderResolutionY
				, 1 // Array Size
				, NumMIPs
				, 1 // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
			);
			desc.ResourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			r.Tex_DownsampledSceneDepth = mRenderer.CreateTexture(desc);
			for(int mip=0; mip<13; ++mip) // 13 comes from downsampledepth.hlsl resource count, TODO: fix magic number
				mRenderer.InitializeUAV(r.UAV_DownsampledSceneDepth, mip, r.Tex_DownsampledSceneDepth, 0, min(mip, NumMIPs - 1));
		}

		{ // Main render target view w/ MSAA
			TextureCreateDesc desc("SceneColorMSAA");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				MainColorRTFormat
				, RenderResolutionX
				, RenderResolutionY
				, 1 // Array Size
				, 1 // MIP levels
				, MSAA_SAMPLE_COUNT // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
			);

			desc.ResourceState = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
			r.Tex_SceneColorMSAA = mRenderer.CreateTexture(desc);
			mRenderer.InitializeRTV(r.RTV_SceneColorMSAA, 0u, r.Tex_SceneColorMSAA);
			mRenderer.InitializeSRV(r.SRV_SceneColorMSAA, 0u, r.Tex_SceneColorMSAA);

			// scene visualization
			desc.TexName = "SceneVizMSAA";
			r.Tex_SceneVisualizationMSAA = mRenderer.CreateTexture(desc);
			mRenderer.InitializeRTV(r.RTV_SceneVisualizationMSAA, 0u, r.Tex_SceneVisualizationMSAA);
			mRenderer.InitializeSRV(r.SRV_SceneVisualizationMSAA, 0u, r.Tex_SceneVisualizationMSAA);

			// motion vectors
			desc.TexName = "SceneMotionVectorsMSAA";
			desc.d3d12Desc.Format = DXGI_FORMAT_R16G16_FLOAT;
			r.Tex_SceneMotionVectorsMSAA = mRenderer.CreateTexture(desc);
			mRenderer.InitializeRTV(r.RTV_SceneMotionVectorsMSAA, 0u, r.Tex_SceneMotionVectorsMSAA);
		}
		{ // MSAA resolve target
			TextureCreateDesc desc("SceneColor");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				MainColorRTFormat
				, RenderResolutionX
				, RenderResolutionY
				, 1 // Array Size
				, 1 // MIP levels
				, 1 // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
			);

			desc.ResourceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			r.Tex_SceneColor = mRenderer.CreateTexture(desc);
			mRenderer.InitializeRTV(r.RTV_SceneColor, 0u, r.Tex_SceneColor);
			mRenderer.InitializeSRV(r.SRV_SceneColor, 0u, r.Tex_SceneColor);
			mRenderer.InitializeUAV(r.UAV_SceneColor, 0u, r.Tex_SceneColor);
			
			// scene bounding volumes
			desc.TexName = "SceneBVs";
			desc.d3d12Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
			r.Tex_SceneColorBoundingVolumes = mRenderer.CreateTexture(desc);
			mRenderer.InitializeRTV(r.RTV_SceneColorBoundingVolumes, 0u, r.Tex_SceneColorBoundingVolumes);
			mRenderer.InitializeSRV(r.SRV_SceneColorBoundingVolumes, 0u, r.Tex_SceneColorBoundingVolumes);

			// scene visualization
			desc.TexName = "SceneViz";
			desc.d3d12Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
			r.Tex_SceneVisualization = mRenderer.CreateTexture(desc);
			mRenderer.InitializeRTV(r.RTV_SceneVisualization, 0u, r.Tex_SceneVisualization);
			mRenderer.InitializeSRV(r.SRV_SceneVisualization, 0u, r.Tex_SceneVisualization);

			// motion vectors
			desc.TexName = "SceneMotionVectors";
			desc.d3d12Desc.Format = DXGI_FORMAT_R16G16_FLOAT;
			r.Tex_SceneMotionVectors = mRenderer.CreateTexture(desc);
			mRenderer.InitializeRTV(r.RTV_SceneMotionVectors, 0u, r.Tex_SceneMotionVectors);
			mRenderer.InitializeSRV(r.SRV_SceneMotionVectors, 0u, r.Tex_SceneMotionVectors);
		}
		{ // Scene Normals
			TextureCreateDesc desc("SceneNormals");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				DXGI_FORMAT_R10G10B10A2_UNORM
				, RenderResolutionX
				, RenderResolutionY
				, 1 // Array Size
				, 1 // MIP levels
				, 1 // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
			);
			desc.ResourceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			r.Tex_SceneNormals = mRenderer.CreateTexture(desc);
			mRenderer.InitializeRTV(r.RTV_SceneNormals, 0u, r.Tex_SceneNormals);
			mRenderer.InitializeSRV(r.SRV_SceneNormals, 0u, r.Tex_SceneNormals);
			mRenderer.InitializeUAV(r.UAV_SceneNormals, 0u, r.Tex_SceneNormals);
		}
		
		{ // Scene Normals /w MSAA
			TextureCreateDesc desc("SceneNormalsMSAA");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				DXGI_FORMAT_R10G10B10A2_UNORM
				, RenderResolutionX
				, RenderResolutionY
				, 1 // Array Size
				, 1 // MIP levels
				, MSAA_SAMPLE_COUNT // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
			);
			desc.ResourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;
			r.Tex_SceneNormalsMSAA = mRenderer.CreateTexture(desc);
			mRenderer.InitializeRTV(r.RTV_SceneNormalsMSAA, 0u, r.Tex_SceneNormalsMSAA);
			mRenderer.InitializeSRV(r.SRV_SceneNormalsMSAA, 0u, r.Tex_SceneNormalsMSAA);
		}

		mRenderPass_DepthResolve.OnCreateWindowSizeDependentResources(RenderResolutionX, RenderResolutionY);


		{ // BlurIntermediate UAV & SRV
			TextureCreateDesc desc("BlurIntermediate");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				MainColorRTFormat
				, RenderResolutionX
				, RenderResolutionY
				, 1 // Array Size
				, 1 // MIP levels
				, 1 // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
			);
			desc.ResourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			r.Tex_PostProcess_BlurIntermediate = mRenderer.CreateTexture(desc);
			mRenderer.InitializeUAV(r.UAV_PostProcess_BlurIntermediate, 0u, r.Tex_PostProcess_BlurIntermediate);
			mRenderer.InitializeSRV(r.SRV_PostProcess_BlurIntermediate, 0u, r.Tex_PostProcess_BlurIntermediate);

			desc.TexName = "BlurOutput";
			desc.ResourceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			r.Tex_PostProcess_BlurOutput = mRenderer.CreateTexture(desc);
			mRenderer.InitializeUAV(r.UAV_PostProcess_BlurOutput, 0u, r.Tex_PostProcess_BlurOutput);
			mRenderer.InitializeSRV(r.SRV_PostProcess_BlurOutput, 0u, r.Tex_PostProcess_BlurOutput);
		}

		{ // Tonemapper Resources
			TextureCreateDesc desc("TonemapperOut");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				TonemapperOutputFormat
				, RenderResolutionX
				, RenderResolutionY
				, 1 // Array Size
				, 1 // MIP levels
				, 1 // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
			);

			r.Tex_PostProcess_TonemapperOut = mRenderer.CreateTexture(desc);
			mRenderer.InitializeUAV(r.UAV_PostProcess_TonemapperOut, 0u, r.Tex_PostProcess_TonemapperOut);
			mRenderer.InitializeSRV(r.SRV_PostProcess_TonemapperOut, 0u, r.Tex_PostProcess_TonemapperOut);
		}

		{ // Visualization Resources
			TextureCreateDesc desc("VizOut");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				TonemapperOutputFormat
				, RenderResolutionX
				, RenderResolutionY
				, 1 // Array Size
				, 1 // MIP levels
				, 1 // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
			);
			desc.ResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			r.Tex_PostProcess_VisualizationOut = mRenderer.CreateTexture(desc);
			mRenderer.InitializeUAV(r.UAV_PostProcess_VisualizationOut, 0u, r.Tex_PostProcess_VisualizationOut);
			mRenderer.InitializeSRV(r.SRV_PostProcess_VisualizationOut, 0u, r.Tex_PostProcess_VisualizationOut);
		}


		{ // FFX-CAS Resources
			TextureCreateDesc desc("FFXCAS_Out");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				TonemapperOutputFormat
				, RenderResolutionX
				, RenderResolutionY
				, 1 // Array Size
				, 1 // MIP levels
				, 1 // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
			);
			desc.ResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			r.Tex_PostProcess_FFXCASOut = mRenderer.CreateTexture(desc);
			mRenderer.InitializeUAV(r.UAV_PostProcess_FFXCASOut, 0u, r.Tex_PostProcess_FFXCASOut);
			mRenderer.InitializeSRV(r.SRV_PostProcess_FFXCASOut, 0u, r.Tex_PostProcess_FFXCASOut);
		}

		{ // FSR1-EASU Resources
			TextureCreateDesc desc("FSR_EASU_Out");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				TonemapperOutputFormat
				, Width
				, Height
				, 1 // Array Size
				, 1 // MIP levels
				, 1 // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
			);
			desc.ResourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			r.Tex_PostProcess_FSR_EASUOut = mRenderer.CreateTexture(desc);
			mRenderer.InitializeUAV(r.UAV_PostProcess_FSR_EASUOut, 0u, r.Tex_PostProcess_FSR_EASUOut);
			mRenderer.InitializeSRV(r.SRV_PostProcess_FSR_EASUOut, 0u, r.Tex_PostProcess_FSR_EASUOut);
		}
		{ // FSR1-RCAS Resources
			TextureCreateDesc desc("FSR_RCAS_Out");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				TonemapperOutputFormat
				, Width
				, Height
				, 1 // Array Size
				, 1 // MIP levels
				, 1 // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
			);
			desc.ResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			r.Tex_PostProcess_FSR_RCASOut = mRenderer.CreateTexture(desc);
			mRenderer.InitializeUAV(r.UAV_PostProcess_FSR_RCASOut, 0u, r.Tex_PostProcess_FSR_RCASOut);
			mRenderer.InitializeSRV(r.SRV_PostProcess_FSR_RCASOut, 0u, r.Tex_PostProcess_FSR_RCASOut);
		}

		{ // UI Resources
			TextureCreateDesc desc("UI_SDR");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				DXGI_FORMAT_R8G8B8A8_UNORM
				, Width
				, Height
				, 1 // Array Size
				, 1 // MIP levels
				, 1 // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
			);
			desc.ResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			r.Tex_UI_SDR = mRenderer.CreateTexture(desc);
			mRenderer.InitializeRTV(r.RTV_UI_SDR, 0u, r.Tex_UI_SDR);
			mRenderer.InitializeSRV(r.SRV_UI_SDR, 0u, r.Tex_UI_SDR);
		}


		{ // FFX-CACAO Resources
			TextureCreateDesc desc("FFXCACAO_Out");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				DXGI_FORMAT_R8_UNORM
				, RenderResolutionX
				, RenderResolutionY
				, 1 // Array Size
				, 1 // MIP levels
				, 1 // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
			);
			desc.ResourceState = D3D12_RESOURCE_STATE_GENERIC_READ;
			r.Tex_AmbientOcclusion = mRenderer.CreateTexture(desc);
			mRenderer.InitializeUAV(r.UAV_FFXCACAO_Out, 0u, r.Tex_AmbientOcclusion);
			mRenderer.InitializeSRV(r.SRV_FFXCACAO_Out, 0u, r.Tex_AmbientOcclusion);


			AmbientOcclusionPass::FResourceParameters params;
			params.pRscNormalBuffer = mRenderer.GetTextureResource(r.Tex_SceneNormals);
			params.pRscDepthBuffer  = mRenderer.GetTextureResource(r.Tex_SceneDepthResolve);
			params.pRscOutput       = mRenderer.GetTextureResource(r.Tex_AmbientOcclusion);
			params.FmtNormalBuffer  = mRenderer.GetTextureFormat(r.Tex_SceneNormals);
			params.FmtDepthBuffer   = DXGI_FORMAT_R32_FLOAT; //mRenderer.GetTextureFormat(r.Tex_SceneDepth); /*R32_TYPELESS*/
			params.FmtOutput        = desc.d3d12Desc.Format;
			mRenderPass_AO.OnCreateWindowSizeDependentResources(RenderResolutionX, RenderResolutionY, &params);
		}

		{ // FFX-SSSR Resources
			ScreenSpaceReflectionsPass::FResourceParameters params;
			params.NormalBufferFormat = mRenderer.GetTextureFormat(r.Tex_SceneNormals);
			params.TexNormals = r.Tex_SceneNormals;
			params.TexHierarchicalDepthBuffer = r.Tex_DownsampledSceneDepth;
			params.TexSceneColor = r.Tex_SceneColor;
			params.TexSceneColorRoughness = r.Tex_SceneColor;
			params.TexMotionVectors = r.Tex_SceneMotionVectors;
			mRenderPass_SSR.OnCreateWindowSizeDependentResources(RenderResolutionX, RenderResolutionY, &params);
		}
	} // main window resources



	// TODO: generic implementation of other window procedures for load
}

void VQEngine::RenderThread_LoadResources()

{
	FRenderingResources_MainWindow& rsc = mResources_MainWnd;

	// null cubemap SRV
	{
		mResources_MainWnd.SRV_NullCubemap = mRenderer.AllocateSRV();
		mResources_MainWnd.SRV_NullTexture2D = mRenderer.AllocateSRV();

		D3D12_SHADER_RESOURCE_VIEW_DESC nullSRVDesc = {};
		nullSRVDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		nullSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		nullSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		nullSRVDesc.TextureCube.MipLevels = 1;
		mRenderer.InitializeSRV(mResources_MainWnd.SRV_NullCubemap, 0, nullSRVDesc);
		
		nullSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		mRenderer.InitializeSRV(mResources_MainWnd.SRV_NullTexture2D, 0, nullSRVDesc);
	}

	// depth pre pass
	{
		rsc.DSV_SceneDepthMSAA = mRenderer.AllocateDSV();
		rsc.DSV_SceneDepth = mRenderer.AllocateDSV();
		rsc.UAV_SceneDepth = mRenderer.AllocateUAV();
		rsc.UAV_SceneColor = mRenderer.AllocateUAV();
		rsc.UAV_SceneNormals = mRenderer.AllocateUAV();
		rsc.RTV_SceneNormals = mRenderer.AllocateRTV();
		rsc.RTV_SceneNormalsMSAA = mRenderer.AllocateRTV();
		rsc.SRV_SceneNormals = mRenderer.AllocateSRV();
		rsc.SRV_SceneNormalsMSAA = mRenderer.AllocateSRV();
		rsc.SRV_SceneDepthMSAA = mRenderer.AllocateSRV();
		rsc.SRV_SceneDepth = mRenderer.AllocateSRV();
	}

	// scene color pass
	{
		rsc.RTV_SceneColorMSAA = mRenderer.AllocateRTV();
		rsc.RTV_SceneColor = mRenderer.AllocateRTV();
		rsc.RTV_SceneColorBoundingVolumes = mRenderer.AllocateRTV();
		rsc.RTV_SceneVisualization = mRenderer.AllocateRTV();
		rsc.RTV_SceneVisualizationMSAA = mRenderer.AllocateRTV();
		rsc.RTV_SceneMotionVectors = mRenderer.AllocateRTV();
		rsc.RTV_SceneMotionVectorsMSAA = mRenderer.AllocateRTV();
		rsc.SRV_SceneColor = mRenderer.AllocateSRV();
		rsc.SRV_SceneColorMSAA = mRenderer.AllocateSRV();
		rsc.SRV_SceneColorBoundingVolumes = mRenderer.AllocateSRV();
		rsc.SRV_SceneVisualization = mRenderer.AllocateSRV();
		rsc.SRV_SceneMotionVectors = mRenderer.AllocateSRV();
		rsc.SRV_SceneVisualizationMSAA = mRenderer.AllocateSRV();
	}

	// reflection passes
	{
		rsc.UAV_DownsampledSceneDepth = mRenderer.AllocateUAV(13);
		rsc.UAV_DownsampledSceneDepthAtomicCounter = mRenderer.AllocateUAV(1);

		TextureCreateDesc desc("DownsampledSceneDepthAtomicCounter");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Buffer(4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		desc.ResourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		rsc.Tex_DownsampledSceneDepthAtomicCounter = mRenderer.CreateTexture(desc);
		mRenderer.InitializeUAVForBuffer(rsc.UAV_DownsampledSceneDepthAtomicCounter, 0u, rsc.Tex_DownsampledSceneDepthAtomicCounter, DXGI_FORMAT_R32_UINT);
	}

	// ambient occlusion pass
	{
		rsc.UAV_FFXCACAO_Out = mRenderer.AllocateUAV();
		rsc.SRV_FFXCACAO_Out = mRenderer.AllocateSRV();
	}

	// post process pass
	{
		rsc.UAV_PostProcess_TonemapperOut    = mRenderer.AllocateUAV();
		rsc.UAV_PostProcess_VisualizationOut = mRenderer.AllocateUAV();
		rsc.UAV_PostProcess_BlurIntermediate = mRenderer.AllocateUAV(); 
		rsc.UAV_PostProcess_BlurOutput       = mRenderer.AllocateUAV();
		rsc.UAV_PostProcess_FFXCASOut        = mRenderer.AllocateUAV();
		rsc.UAV_PostProcess_FSR_EASUOut      = mRenderer.AllocateUAV();
		rsc.UAV_PostProcess_FSR_RCASOut      = mRenderer.AllocateUAV();

		rsc.SRV_PostProcess_TonemapperOut    = mRenderer.AllocateSRV();
		rsc.SRV_PostProcess_VisualizationOut = mRenderer.AllocateSRV();
		rsc.SRV_PostProcess_BlurIntermediate = mRenderer.AllocateSRV(); 
		rsc.SRV_PostProcess_BlurOutput       = mRenderer.AllocateSRV();
		rsc.SRV_PostProcess_FFXCASOut        = mRenderer.AllocateSRV();
		rsc.SRV_PostProcess_FSR_EASUOut      = mRenderer.AllocateSRV();
		rsc.SRV_PostProcess_FSR_RCASOut      = mRenderer.AllocateSRV();
	}

	// UI HDR pass
	{
		rsc.RTV_UI_SDR = mRenderer.AllocateRTV();
		rsc.SRV_UI_SDR = mRenderer.AllocateSRV();
	}

	// shadow map passes
	{
		TextureCreateDesc desc("ShadowMaps_Spot");
		// initialize texture memory
		constexpr UINT SHADOW_MAP_DIMENSION_SPOT = 1024;
		constexpr UINT SHADOW_MAP_DIMENSION_POINT = 1024;
		constexpr UINT SHADOW_MAP_DIMENSION_DIRECTIONAL = 2048;

		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R32_TYPELESS
			, SHADOW_MAP_DIMENSION_SPOT
			, SHADOW_MAP_DIMENSION_SPOT
			, NUM_SHADOWING_LIGHTS__SPOT // Array Size
			, 1 // MIP levels
			, 1 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
		);
		desc.ResourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		rsc.Tex_ShadowMaps_Spot = mRenderer.CreateTexture(desc);

		desc.d3d12Desc.DepthOrArraySize = NUM_SHADOWING_LIGHTS__POINT * 6;
		desc.d3d12Desc.Width  = SHADOW_MAP_DIMENSION_POINT;
		desc.d3d12Desc.Height = SHADOW_MAP_DIMENSION_POINT;
		desc.TexName = "ShadowMaps_Point";
		desc.bCubemap = true;
		rsc.Tex_ShadowMaps_Point = mRenderer.CreateTexture(desc);


		desc.d3d12Desc.Width  = SHADOW_MAP_DIMENSION_DIRECTIONAL;
		desc.d3d12Desc.Height = SHADOW_MAP_DIMENSION_DIRECTIONAL;
		desc.d3d12Desc.DepthOrArraySize = 1;
		desc.bCubemap = false;
		desc.TexName = "ShadowMap_Directional";
		rsc.Tex_ShadowMaps_Directional = mRenderer.CreateTexture(desc);
		
		// initialize DSVs
		rsc.DSV_ShadowMaps_Spot        = mRenderer.AllocateDSV(NUM_SHADOWING_LIGHTS__SPOT);
		rsc.DSV_ShadowMaps_Point       = mRenderer.AllocateDSV(NUM_SHADOWING_LIGHTS__POINT * 6);
		rsc.DSV_ShadowMaps_Directional = mRenderer.AllocateDSV();

		for (int i = 0; i < NUM_SHADOWING_LIGHTS__SPOT; ++i)      mRenderer.InitializeDSV(rsc.DSV_ShadowMaps_Spot , i, rsc.Tex_ShadowMaps_Spot , i);
		for (int i = 0; i < NUM_SHADOWING_LIGHTS__POINT * 6; ++i) mRenderer.InitializeDSV(rsc.DSV_ShadowMaps_Point, i, rsc.Tex_ShadowMaps_Point, i);
		mRenderer.InitializeDSV(rsc.DSV_ShadowMaps_Directional, 0, rsc.Tex_ShadowMaps_Directional);


		// initialize SRVs
		rsc.SRV_ShadowMaps_Spot        = mRenderer.AllocateSRV();
		rsc.SRV_ShadowMaps_Point       = mRenderer.AllocateSRV();
		rsc.SRV_ShadowMaps_Directional = mRenderer.AllocateSRV();
		mRenderer.InitializeSRV(rsc.SRV_ShadowMaps_Spot , 0, rsc.Tex_ShadowMaps_Spot, true);
		mRenderer.InitializeSRV(rsc.SRV_ShadowMaps_Point, 0, rsc.Tex_ShadowMaps_Point, true, true);
		mRenderer.InitializeSRV(rsc.SRV_ShadowMaps_Directional, 0, rsc.Tex_ShadowMaps_Directional);
	}
}

void VQEngine::RenderThread_UnloadWindowSizeDependentResources(HWND hwnd)
{
	if (hwnd == mpWinMain->GetHWND())
	{
		FRenderingResources_MainWindow& r = mResources_MainWnd;

		// sync GPU
		auto& ctx = mRenderer.GetWindowRenderContext(hwnd);
		ctx.SwapChain.WaitForGPU();

		mRenderer.DestroyTexture(r.Tex_SceneDepthMSAA);
		mRenderer.DestroyTexture(r.Tex_SceneColorMSAA);
		mRenderer.DestroyTexture(r.Tex_SceneNormalsMSAA);

		mRenderer.DestroyTexture(r.Tex_SceneDepth);
		mRenderer.DestroyTexture(r.Tex_SceneDepthResolve);
		mRenderer.DestroyTexture(r.Tex_SceneColor);
		mRenderer.DestroyTexture(r.Tex_SceneColorBoundingVolumes);
		mRenderer.DestroyTexture(r.Tex_SceneNormals);
		mRenderer.DestroyTexture(r.Tex_SceneVisualization);
		mRenderer.DestroyTexture(r.Tex_SceneVisualizationMSAA);

		mRenderer.DestroyTexture(r.Tex_AmbientOcclusion);
		// TODO: destroy SSR resources?

		mRenderer.DestroyTexture(r.Tex_PostProcess_BlurOutput);
		mRenderer.DestroyTexture(r.Tex_PostProcess_BlurIntermediate);
		mRenderer.DestroyTexture(r.Tex_PostProcess_TonemapperOut);
		mRenderer.DestroyTexture(r.Tex_PostProcess_VisualizationOut);
		mRenderer.DestroyTexture(r.Tex_PostProcess_FFXCASOut);
		mRenderer.DestroyTexture(r.Tex_PostProcess_FSR_EASUOut);
		mRenderer.DestroyTexture(r.Tex_PostProcess_FSR_RCASOut);

		for(auto* pRenderPass : mRenderPasses)
			pRenderPass->OnDestroyWindowSizeDependentResources();
	}

	// TODO: generic implementation of other window procedures for unload
}


// ------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// RENDER
//
// ------------------------------------------------------------------------------------------------------------------------------------------------------------

static uint32_t GetNumShadowViewCmdRecordingThreads(const FSceneShadowView& ShadowView)
{
#if RENDER_THREAD__MULTI_THREADED_COMMAND_RECORDING
	return (ShadowView.ShadowView_Directional.meshRenderCommands.size() > 0 ? 1 : 0) // 1 thread for directional light (assumes long list of mesh render cmds)
		+ ShadowView.NumPointShadowViews  // each point light view (6x frustums per point light
		+ (ShadowView.NumSpotShadowViews > 0 ? 1 : 0); // process spot light render lists in own thread
#else
	return 0;
#endif
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
	
	const FSceneView&       SceneView       = mpScene->GetSceneView(FRAME_DATA_INDEX);
	const FSceneShadowView& SceneShadowView = mpScene->GetShadowView(FRAME_DATA_INDEX);

#if RENDER_THREAD__MULTI_THREADED_COMMAND_RECORDING
	const uint32_t NumCmdRecordingThreads_GFX
		= 1 // worker thrd: DepthPrePass
		+ 1 // this thread: AO+SceneColor+PostProcess+Submit+Present
		+ GetNumShadowViewCmdRecordingThreads(SceneShadowView);
	const uint32_t NumCmdRecordingThreads_CMP = 0;
	const uint32_t NumCmdRecordingThreads_CPY = 0;
	const uint32_t NumCmdRecordingThreads = NumCmdRecordingThreads_GFX + NumCmdRecordingThreads_CPY + NumCmdRecordingThreads_CMP;
	const uint32_t ConstantBufferBytesPerThread = 128 * MEGABYTE;
#else
	const uint32_t NumCmdRecordingThreads_GFX = 1;
	const uint32_t NumCmdRecordingThreads = NumCmdRecordingThreads_GFX;
	const uint32_t ConstantBufferBytesPerThread = 36 * MEGABYTE;
#endif

	ctx.AllocateCommandLists(CommandQueue::EType::GFX, NumCmdRecordingThreads_GFX);
	ctx.ResetCommandLists(CommandQueue::EType::GFX, NumCmdRecordingThreads_GFX);
	ctx.AllocateConstantBufferMemory(NumCmdRecordingThreads, ConstantBufferBytesPerThread);

	for (size_t iThread = 0; iThread < NumCmdRecordingThreads; ++iThread)
		ctx.GetConstantBufferHeap(iThread).OnBeginFrame();

	ID3D12DescriptorHeap* ppHeaps[] = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };

	auto& vpGFXCmds = ctx.GetGFXCommandListPtrs();
	for (uint32_t iGFX = 0; iGFX < NumCmdRecordingThreads_GFX; ++iGFX)
	{
		vpGFXCmds[iGFX]->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
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
		PreFilterEnvironmentMap(pCmd, mResources_MainWnd.EnvironmentMap);
		mbEnvironmentMapPreFilter.store(false);
	}


	HRESULT hr = S_OK; 
	hr = mbLoadingLevel || mbLoadingEnvironmentMap
		? RenderThread_RenderMainWindow_LoadingScreen(ctx)
		: RenderThread_RenderMainWindow_Scene(ctx);

	if (hr == DXGI_STATUS_OCCLUDED)     { RenderThread_HandleStatusOccluded(); }
	if (hr == DXGI_ERROR_DEVICE_REMOVED){ RenderThread_HandleDeviceRemoved();  }

	{
		SCOPED_CPU_MARKER("SwapchainMoveToNextFrame");
		ctx.SwapChain.MoveToNextFrame();
	}
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
	pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT
		, D3D12_RESOURCE_STATE_PRESENT
		, D3D12_RESOURCE_STATE_RENDER_TARGET)
	);

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

	pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT
		, D3D12_RESOURCE_STATE_RENDER_TARGET
		, D3D12_RESOURCE_STATE_PRESENT)
	); // Transition SwapChain for Present


	hr = PresentFrame(ctx);

	return hr;
}


//====================================================================================================
//
// RENDER FRAME
//
//====================================================================================================
HRESULT VQEngine::RenderThread_RenderMainWindow_Scene(FWindowRenderContext& ctx)
{
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	ThreadPool& WorkerThreads = mWorkers_Render;
#else
	ThreadPool& WorkerThreads = mWorkers_Simulation;
#endif

	SCOPED_CPU_MARKER("RenderThread_RenderMainWindow_Scene()");
	HRESULT hr = S_OK;
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
	const bool& bMSAA = mSettings.gfx.bAntiAliasing;

	const FSceneView& SceneView             = mpScene->GetSceneView(FRAME_DATA_INDEX);
	const FSceneShadowView& SceneShadowView = mpScene->GetShadowView(FRAME_DATA_INDEX);
	const FPostProcessParameters& PPParams  = mpScene->GetPostProcessParameters(FRAME_DATA_INDEX);
	const uint32 W = mpWinMain->GetWidth();
	const uint32 H = mpWinMain->GetHeight();
	mRenderStats = {};

	const auto& rsc = mResources_MainWnd;

	// TODO: undo const cast and assign in a proper spot -------------------------------------------------
	FSceneView& RefSceneView = const_cast<FSceneView&>(SceneView);
	FPostProcessParameters& RefPPParams = const_cast<FPostProcessParameters&>(PPParams);

	RefSceneView.SceneRTWidth  = static_cast<int>(ctx.WindowDisplayResolutionX * (PPParams.IsFSREnabled() ? PPParams.FFSR_EASUParams.GetScreenPercentage() : 1.0f));
	RefSceneView.SceneRTHeight = static_cast<int>(ctx.WindowDisplayResolutionY * (PPParams.IsFSREnabled() ? PPParams.FFSR_EASUParams.GetScreenPercentage() : 1.0f));
	RefPPParams.SceneRTWidth  = SceneView.SceneRTWidth;
	RefPPParams.SceneRTHeight = SceneView.SceneRTHeight;
	RefPPParams.DisplayResolutionWidth  = ctx.WindowDisplayResolutionX;
	RefPPParams.DisplayResolutionHeight = ctx.WindowDisplayResolutionY;

	// do some settings override for some render paths
	if (bUseHDRRenderPath)
	{
		if (RefPPParams.IsFFXCASEnabled())
		{
			Log::Warning("FidelityFX CAS HDR not implemented, turning CAS off");
			RefPPParams.bEnableCAS = false;
		}
		if (RefPPParams.IsFSREnabled())
		{
			// TODO: HDR conversion pass to handle color range and precision/packing, shader variants etc.
			Log::Warning("FidelityFX Super Resolution HDR not implemented yet, turning FSR off"); 
			RefPPParams.bEnableFSR = false;
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

	ID3D12Resource* pRsc = nullptr;
	if constexpr (!RENDER_THREAD__MULTI_THREADED_COMMAND_RECORDING)
	{
		constexpr size_t THREAD_INDEX = 0;

		ID3D12GraphicsCommandList* pCmd = (ID3D12GraphicsCommandList*)ctx.GetCommandListPtr(CommandQueue::EType::GFX, THREAD_INDEX);
		DynamicBufferHeap& CBHeap = ctx.GetConstantBufferHeap(THREAD_INDEX);

		RenderSpotShadowMaps(pCmd, &CBHeap, SceneShadowView);
		RenderDirectionalShadowMaps(pCmd, &CBHeap, SceneShadowView);
		RenderPointShadowMaps(pCmd, &CBHeap, SceneShadowView, 0, SceneShadowView.NumPointShadowViews);

		RenderDepthPrePass(pCmd, &CBHeap, SceneView);

		if (bDownsampleDepth)
		{
			DownsampleDepth(pCmd, &CBHeap, rsc.Tex_SceneDepth, rsc.SRV_SceneDepth);
		}

		RenderAmbientOcclusion(pCmd, SceneView);

		TransitionForSceneRendering(pCmd, ctx, PPParams);

		RenderSceneColor(pCmd, &CBHeap, SceneView, PPParams);

		ResolveMSAA(pCmd, &CBHeap, PPParams);

		TransitionForPostProcessing(pCmd, PPParams);

		if (bReflectionsEnabled)
		{
			RenderReflections(pCmd, &CBHeap, SceneView);
			
			// render scene-debugging stuff that shouldn't be in reflections: bounding volumes, etc
			RenderSceneBoundingVolumes(pCmd, &CBHeap, SceneView, bMSAA);

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
		constexpr size_t iCmdZPrePassThread = 0;
		constexpr size_t iCmdPointLightsThread = 1;
		const     size_t iCmdSpots        = iCmdPointLightsThread + SceneShadowView.NumPointShadowViews;
		const     size_t iCmdDirectional  = iCmdSpots + (SceneShadowView.NumSpotShadowViews > 0 ? 1 : 0);
		const     size_t iCmdRenderThread = iCmdDirectional + (SceneShadowView.ShadowView_Directional.meshRenderCommands.empty() ? 0 : 1);

		ID3D12GraphicsCommandList* pCmd_ThisThread = (ID3D12GraphicsCommandList*)ctx.GetCommandListPtr(CommandQueue::EType::GFX, iCmdRenderThread);
		DynamicBufferHeap& CBHeap_This = ctx.GetConstantBufferHeap(iCmdRenderThread);
		
		{
			SCOPED_CPU_MARKER("DispatchWorkers");

			// ZPrePass
			{
				ID3D12GraphicsCommandList* pCmd_ZPrePass = (ID3D12GraphicsCommandList*)ctx.GetCommandListPtr(CommandQueue::EType::GFX, iCmdZPrePassThread);
				DynamicBufferHeap& CBHeap_WorkerZPrePass = ctx.GetConstantBufferHeap(iCmdZPrePassThread);
				WorkerThreads.AddTask([=, &CBHeap_WorkerZPrePass, &SceneView]()
				{
					RENDER_WORKER_CPU_MARKER;
					RenderDepthPrePass(pCmd_ZPrePass, &CBHeap_WorkerZPrePass, SceneView);

					if (bDownsampleDepth)
					{
						DownsampleDepth(pCmd_ZPrePass, &CBHeap_WorkerZPrePass, rsc.Tex_SceneDepth, rsc.SRV_SceneDepth);
					}
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

			if (!SceneShadowView.ShadowView_Directional.meshRenderCommands.empty())
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

		RenderAmbientOcclusion(pCmd_ThisThread, SceneView);

		TransitionForSceneRendering(pCmd_ThisThread, ctx, PPParams);

		RenderSceneColor(pCmd_ThisThread, &CBHeap_This, SceneView, PPParams);

		ResolveMSAA(pCmd_ThisThread, &CBHeap_This, PPParams);

		TransitionForPostProcessing(pCmd_ThisThread, PPParams);

		if (bReflectionsEnabled)
		{
			RenderReflections(pCmd_ThisThread, &CBHeap_This, SceneView);
			
			// render scene-debugging stuff that shouldn't be in reflections: bounding volumes, etc
			RenderSceneBoundingVolumes(pCmd_ThisThread, &CBHeap_This, SceneView, bMSAA);

			CompositeReflections(pCmd_ThisThread, &CBHeap_This, SceneView);
		}

		pRsc = RenderPostProcess(pCmd_ThisThread, &CBHeap_This, PPParams);

		RenderUI(pCmd_ThisThread, &CBHeap_This, ctx, PPParams, pRsc);

		if (bUseHDRRenderPath)
		{
			CompositUIToHDRSwapchain(pCmd_ThisThread, &CBHeap_This, ctx, PPParams);
		}

		{
			SCOPED_CPU_MARKER_C("BUSY_WAIT_WORKERS", 0xFFFF0000);
			while (WorkerThreads.GetNumActiveTasks() != 0);
		}
	}

	hr = PresentFrame(ctx);

	return hr;
}
