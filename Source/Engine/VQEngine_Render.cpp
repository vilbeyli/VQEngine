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
	const bool bExclusiveFullscreen_MainWnd = CheckInitialSwapchainResizeRequired(mInitialSwapchainResizeRequiredWindowLookup, mSettings.WndMain, mpWinMain->GetHWND());

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	mNumRenderLoopsExecuted.store(0);
#endif

	// Initialize Renderer: Device, Queues, Heaps
	mRenderer.Initialize(mSettings.gfx);

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

	// initialize builtin meshes
	InitializeBuiltinMeshes();

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	mbRenderThreadInitialized.store(true);
#endif

	//
	// TODO: THREADED LOADING
	// 
	// load renderer resources, compile shaders, load PSOs
	mRenderer.Load();

	RenderThread_LoadResources();

	// initialize render passes
	{
		mRenderPass_AO.Initialize(mRenderer.GetDevicePtr());
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
	mRenderPass_AO.Exit();
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
			mRenderer.InitializeUAV(r.UAV_SceneDepth, 0u, r.Tex_SceneDepthResolve);
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
		}
		{ // MSAA resolve target
			TextureCreateDesc desc("SceneColor");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				MainColorRTFormat
				, RenderResolutionX
				, RenderResolutionY
				, 1 // Array Size
				, 0 // MIP levels
				, 1 // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
			);

			desc.ResourceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			r.Tex_SceneColor = mRenderer.CreateTexture(desc);
			mRenderer.InitializeRTV(r.RTV_SceneColor, 0u, r.Tex_SceneColor);
			mRenderer.InitializeSRV(r.SRV_SceneColor, 0u, r.Tex_SceneColor);
		}

		{ // Scene Normals
			TextureCreateDesc desc("SceneNormals");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				DXGI_FORMAT_R10G10B10A2_UNORM
				, RenderResolutionX
				, RenderResolutionY
				, 1 // Array Size
				, 0 // MIP levels
				, 1 // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
			);
			desc.ResourceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			r.Tex_SceneNormals = mRenderer.CreateTexture(desc);
			mRenderer.InitializeRTV(r.RTV_SceneNormals, 0u, r.Tex_SceneNormals);
			mRenderer.InitializeSRV(r.SRV_SceneNormals, 0u, r.Tex_SceneNormals);

			//mRenderPass_DepthPrePass.OnCreateWindowSizeDependentResources(Width, Height, nullptr);
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

			//mRenderPass_DepthPrePass.OnCreateWindowSizeDependentResources(Width, Height, nullptr);
		}


		{ // BlurIntermediate UAV & SRV
			TextureCreateDesc desc("BlurIntermediate");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				MainColorRTFormat
				, RenderResolutionX
				, RenderResolutionY
				, 1 // Array Size
				, 0 // MIP levels
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


			FAmbientOcclusionPass::FResourceParameters params;
			params.pRscNormalBuffer = mRenderer.GetTextureResource(r.Tex_SceneNormals);
			params.pRscDepthBuffer  = mRenderer.GetTextureResource(r.Tex_SceneDepthResolve);
			params.pRscOutput       = mRenderer.GetTextureResource(r.Tex_AmbientOcclusion);
			params.FmtNormalBuffer = mRenderer.GetTextureFormat(r.Tex_SceneNormals);
			params.FmtDepthBuffer  = DXGI_FORMAT_R32_FLOAT; //mRenderer.GetTextureFormat(r.Tex_SceneDepth); /*R32_TYPELESS*/
			params.FmtOutput       = desc.d3d12Desc.Format;
			mRenderPass_AO.OnCreateWindowSizeDependentResources(RenderResolutionX, RenderResolutionY, static_cast<const void*>(&params));
		}

	} // main window resources



	// TODO: generic implementation of other window procedures for load
}

void VQEngine::RenderThread_LoadResources()
{
	FRenderingResources_MainWindow& rsc = mResources_MainWnd;

	// null cubemap SRV
	{
		mResources_MainWnd.SRV_NullCubemap = mRenderer.CreateSRV();
		mResources_MainWnd.SRV_NullTexture2D = mRenderer.CreateSRV();

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
		rsc.DSV_SceneDepthMSAA = mRenderer.CreateDSV();
		rsc.DSV_SceneDepth = mRenderer.CreateDSV();
		rsc.UAV_SceneDepth = mRenderer.CreateUAV();
		rsc.RTV_SceneNormals = mRenderer.CreateRTV();
		rsc.RTV_SceneNormalsMSAA = mRenderer.CreateRTV();
		rsc.SRV_SceneNormals = mRenderer.CreateSRV();
		rsc.SRV_SceneDepthMSAA = mRenderer.CreateSRV();
	}

	// scene color pass
	{
		rsc.RTV_SceneColorMSAA = mRenderer.CreateRTV();
		rsc.RTV_SceneColor = mRenderer.CreateRTV();
		rsc.SRV_SceneColor = mRenderer.CreateSRV();
	}

	// ambient occlusion pass
	{
		rsc.UAV_FFXCACAO_Out = mRenderer.CreateUAV();
		rsc.SRV_FFXCACAO_Out = mRenderer.CreateSRV();
	}

	// post process pass
	{
		rsc.UAV_PostProcess_TonemapperOut    = mRenderer.CreateUAV();
		rsc.UAV_PostProcess_BlurIntermediate = mRenderer.CreateUAV(); 
		rsc.UAV_PostProcess_BlurOutput       = mRenderer.CreateUAV();
		rsc.UAV_PostProcess_FFXCASOut        = mRenderer.CreateUAV();
		rsc.UAV_PostProcess_FSR_EASUOut      = mRenderer.CreateUAV();
		rsc.UAV_PostProcess_FSR_RCASOut      = mRenderer.CreateUAV();

		rsc.SRV_PostProcess_TonemapperOut    = mRenderer.CreateSRV();
		rsc.SRV_PostProcess_BlurIntermediate = mRenderer.CreateSRV(); 
		rsc.SRV_PostProcess_BlurOutput       = mRenderer.CreateSRV();
		rsc.SRV_PostProcess_FFXCASOut        = mRenderer.CreateSRV();
		rsc.SRV_PostProcess_FSR_EASUOut      = mRenderer.CreateSRV();
		rsc.SRV_PostProcess_FSR_RCASOut      = mRenderer.CreateSRV();
	}

	// UI HDR pass
	{
		rsc.RTV_UI_SDR = mRenderer.CreateRTV();
		rsc.SRV_UI_SDR = mRenderer.CreateSRV();
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
		rsc.DSV_ShadowMaps_Spot        = mRenderer.CreateDSV(NUM_SHADOWING_LIGHTS__SPOT);
		rsc.DSV_ShadowMaps_Point       = mRenderer.CreateDSV(NUM_SHADOWING_LIGHTS__POINT * 6);
		rsc.DSV_ShadowMaps_Directional = mRenderer.CreateDSV();

		for (int i = 0; i < NUM_SHADOWING_LIGHTS__SPOT; ++i)      mRenderer.InitializeDSV(rsc.DSV_ShadowMaps_Spot , i, rsc.Tex_ShadowMaps_Spot , i);
		for (int i = 0; i < NUM_SHADOWING_LIGHTS__POINT * 6; ++i) mRenderer.InitializeDSV(rsc.DSV_ShadowMaps_Point, i, rsc.Tex_ShadowMaps_Point, i);
		mRenderer.InitializeDSV(rsc.DSV_ShadowMaps_Directional, 0, rsc.Tex_ShadowMaps_Directional);


		// initialize SRVs
		rsc.SRV_ShadowMaps_Spot        = mRenderer.CreateSRV();
		rsc.SRV_ShadowMaps_Point       = mRenderer.CreateSRV();
		rsc.SRV_ShadowMaps_Directional = mRenderer.CreateSRV();
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
		mRenderer.DestroyTexture(r.Tex_SceneNormals);

		mRenderer.DestroyTexture(r.Tex_AmbientOcclusion);

		mRenderer.DestroyTexture(r.Tex_PostProcess_BlurOutput);
		mRenderer.DestroyTexture(r.Tex_PostProcess_BlurIntermediate);
		mRenderer.DestroyTexture(r.Tex_PostProcess_TonemapperOut);
		mRenderer.DestroyTexture(r.Tex_PostProcess_FFXCASOut);
		mRenderer.DestroyTexture(r.Tex_PostProcess_FSR_EASUOut);
		mRenderer.DestroyTexture(r.Tex_PostProcess_FSR_RCASOut);

		mRenderPass_AO.OnDestroyWindowSizeDependentResources();
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
	
	const int NUM_BACK_BUFFERS  = ctx.GetNumSwapchainBuffers();
	const int BACK_BUFFER_INDEX = ctx.GetCurrentSwapchainBufferIndex();
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
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
	const uint32_t ConstantBufferBytesPerThread = 12 * MEGABYTE;
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
	this->mbStopAllThreads.store(true);
	MessageBox(NULL, "Device Removed.\n\nVQEngine will shutdown.", "VQEngine Renderer Error", MB_OK);
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	RenderThread_SignalUpdateThread();
#endif
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
	pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(0));

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

	// hardcoded roog signature for now until shader reflection and rootsignature management is implemented
	pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(1));

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
	const bool bUseHDRRenderPath            = this->ShouldRenderHDR(mpWinMain->GetHWND());
	const FSceneView& SceneView             = mpScene->GetSceneView(FRAME_DATA_INDEX);
	const FSceneShadowView& SceneShadowView = mpScene->GetShadowView(FRAME_DATA_INDEX);
	const FPostProcessParameters& PPParams  = mpScene->GetPostProcessParameters(FRAME_DATA_INDEX);
	const uint32 W = mpWinMain->GetWidth();
	const uint32 H = mpWinMain->GetHeight();
	mRenderStats = {};


	// TODO: undo const cast and assign in a proper spot -------------------------------------------------
	FSceneView& RefSceneView = const_cast<FSceneView&>(SceneView);
	FPostProcessParameters& RefPPParams = const_cast<FPostProcessParameters&>(PPParams);

	RefSceneView.SceneRTWidth  = static_cast<int>(ctx.WindowDisplayResolutionX * (PPParams.IsFSREnabled() ? PPParams.FFSR_EASUParams.GetScreenPercentage() : 1.0f));
	RefSceneView.SceneRTHeight = static_cast<int>(ctx.WindowDisplayResolutionY * (PPParams.IsFSREnabled() ? PPParams.FFSR_EASUParams.GetScreenPercentage() : 1.0f));
	RefPPParams.SceneRTWidth  = SceneView.SceneRTWidth;
	RefPPParams.SceneRTHeight = SceneView.SceneRTHeight;
	RefPPParams.DisplayResolutionWidth  = ctx.WindowDisplayResolutionX;
	RefPPParams.DisplayResolutionHeight = ctx.WindowDisplayResolutionY;


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
	
	if constexpr (!RENDER_THREAD__MULTI_THREADED_COMMAND_RECORDING)
	{
		constexpr size_t THREAD_INDEX = 0;

		ID3D12GraphicsCommandList* pCmd = (ID3D12GraphicsCommandList*)ctx.GetCommandListPtr(CommandQueue::EType::GFX, THREAD_INDEX);
		DynamicBufferHeap& CBHeap = ctx.GetConstantBufferHeap(THREAD_INDEX);

		RenderSpotShadowMaps(pCmd, &CBHeap, SceneShadowView);
		RenderDirectionalShadowMaps(pCmd, &CBHeap, SceneShadowView);
		RenderPointShadowMaps(pCmd, &CBHeap, SceneShadowView, 0, SceneShadowView.NumPointShadowViews);

		RenderDepthPrePass(pCmd, &CBHeap, SceneView);

		RenderAmbientOcclusion(pCmd, SceneView);

		TransitionForSceneRendering(pCmd, ctx);

		RenderSceneColor(pCmd, &CBHeap, SceneView);

		ResolveMSAA(pCmd);

		TransitionForPostProcessing(pCmd, PPParams);

		RenderPostProcess(pCmd, &CBHeap, PPParams);

		RenderUI(pCmd, &CBHeap, ctx, PPParams);

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

		TransitionForSceneRendering(pCmd_ThisThread, ctx);

		RenderSceneColor(pCmd_ThisThread, &CBHeap_This, SceneView);

		ResolveMSAA(pCmd_ThisThread);

		TransitionForPostProcessing(pCmd_ThisThread, PPParams);

		RenderPostProcess(pCmd_ThisThread, &CBHeap_This, PPParams);

		RenderUI(pCmd_ThisThread, &CBHeap_This, ctx, PPParams);

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
