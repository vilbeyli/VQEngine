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

// ------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// MAIN
//
// ------------------------------------------------------------------------------------------------------------------------------------------------------------
void VQEngine::RenderThread_Main()
{
	Log::Info("RenderThread Created.");
	RenderThread_Inititalize();

	RenderThread_HandleEvents();

	bool bQuit = false;
	float dt = 0.0f;
	while (!this->mbStopAllThreads && !bQuit)
	{
		dt = mTimerRender.Tick();

		RenderThread_HandleEvents();

		if (this->mbStopAllThreads || bQuit)
			break; // HandleEvents() can set @this->mbStopAllThreads true with WindowCloseEvent;

		RenderThread_WaitForUpdateThread();

#if DEBUG_LOG_THREAD_SYNC_VERBOSE
		Log::Info(/*"RenderThread_Tick() : */"r%d (u=%llu)", mNumRenderLoopsExecuted.load(), mNumUpdateLoopsExecuted.load());
#endif

		RenderThread_PreRender();
		RenderThread_RenderFrame();

		++mNumRenderLoopsExecuted;

		RenderThread_SignalUpdateThread();

		RenderThread_HandleEvents();

		if (mEffectiveFrameRateLimit_ms != 0.0f)
		{
			const float TimeBudgetLeft_ms = mEffectiveFrameRateLimit_ms - dt;
			if (TimeBudgetLeft_ms > 0.0f)
			{
				Sleep((DWORD)TimeBudgetLeft_ms);
			}
			//Log::Info("RenderThread_Main() : dt=%.2f, Sleep=%.2f", dt, TimeBudgetLeft_ms);
		}
	}

	RenderThread_Exit();
	Log::Info("RenderThread_Main() : Exit");
}


void VQEngine::RenderThread_WaitForUpdateThread()
{
#if DEBUG_LOG_THREAD_SYNC_VERBOSE
	Log::Info("r:wait : u=%llu, r=%llu", mNumUpdateLoopsExecuted.load(), mNumRenderLoopsExecuted.load());
#endif

	mpSemRender->Wait();
}

void VQEngine::RenderThread_SignalUpdateThread()
{
	mpSemUpdate->Signal();
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

	mNumRenderLoopsExecuted.store(0);

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
	mbRenderThreadInitialized.store(true);

	//
	// TODO: THREADED LOADING
	// 
	// load renderer resources, compile shaders, load PSOs
	mRenderer.Load();

	RenderThread_LoadResources();

	// initialize render passes
	{
		ID3D12Device* pDevice = mRenderer.GetWindowRenderContext(mpWinMain->GetHWND()).pDevice->GetDevicePtr();
		mRenderPass_AO.Initialize(pDevice);
	}

	// load window resources
	const bool bFullscreen = mpWinMain->IsFullscreen();
	const int W = bFullscreen ? mpWinMain->GetFullscreenWidth() : mpWinMain->GetWidth();
	const int H = bFullscreen ? mpWinMain->GetFullscreenHeight() : mpWinMain->GetHeight();
	RenderThread_LoadWindowSizeDependentResources(mpWinMain->GetHWND(), W, H);

	mTimerRender.Reset();
	mTimerRender.Start();
}

void VQEngine::RenderThread_Exit()
{
	mpSemUpdate->Signal();
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


constexpr bool MSAA_ENABLE       = true;
constexpr uint MSAA_SAMPLE_COUNT = 4;

// ------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// LOAD
//
// ------------------------------------------------------------------------------------------------------------------------------------------------------------
void VQEngine::RenderThread_LoadWindowSizeDependentResources(HWND hwnd, int Width, int Height)
{
	assert(Width >= 1 && Height >= 1);

	if (hwnd == mpWinMain->GetHWND())
	{

		constexpr DXGI_FORMAT MainColorRTFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
		FRenderingResources_MainWindow& r = mResources_MainWnd;


		{	// Scene depth stencil view /w MSAA
			TextureCreateDesc desc("SceneDepthMSAA");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				DXGI_FORMAT_R32_TYPELESS
				, Width
				, Height
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
			TextureCreateDesc desc("SceneDepth");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				DXGI_FORMAT_R32_FLOAT
				, Width
				, Height
				, 1 // Array Size
				, 1 // MIP levels
				, 1 // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
			);
			desc.ResourceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			r.Tex_SceneDepth = mRenderer.CreateTexture(desc);
			mRenderer.InitializeUAV(r.UAV_SceneDepth, 0u, r.Tex_SceneDepth);
		}

		{ // Main render target view w/ MSAA
			TextureCreateDesc desc("SceneColorMSAA");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				MainColorRTFormat
				, Width
				, Height
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
				, Width
				, Height
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
				, Width
				, Height
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
				, Width
				, Height
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
				, Width
				, Height
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
				MainColorRTFormat
				, Width
				, Height
				, 1 // Array Size
				, 0 // MIP levels
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
				MainColorRTFormat
				, Width
				, Height
				, 1 // Array Size
				, 0 // MIP levels
				, 1 // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
			);
			desc.ResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			r.Tex_PostProcess_FFXCASOut = mRenderer.CreateTexture(desc);
			mRenderer.InitializeUAV(r.UAV_PostProcess_FFXCASOut, 0u, r.Tex_PostProcess_FFXCASOut);
			mRenderer.InitializeSRV(r.SRV_PostProcess_FFXCASOut, 0u, r.Tex_PostProcess_FFXCASOut);
		}
		
		{ // FFX-CACAO Resources
			TextureCreateDesc desc("FFXCACAO_Out");
			desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
				DXGI_FORMAT_R8_UNORM
				, Width
				, Height
				, 1 // Array Size
				, 0 // MIP levels
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
			params.pRscDepthBuffer  = mRenderer.GetTextureResource(r.Tex_SceneDepth);
			params.pRscOutput       = mRenderer.GetTextureResource(r.Tex_AmbientOcclusion);
			params.FmtNormalBuffer = mRenderer.GetTextureFormat(r.Tex_SceneNormals);
			params.FmtDepthBuffer  = DXGI_FORMAT_R32_FLOAT; //mRenderer.GetTextureFormat(r.Tex_SceneDepth); /*R32_TYPELESS*/
			params.FmtOutput       = desc.d3d12Desc.Format;
			mRenderPass_AO.OnCreateWindowSizeDependentResources(Width, Height, static_cast<const void*>(&params));
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

		rsc.SRV_PostProcess_TonemapperOut    = mRenderer.CreateSRV();
		rsc.SRV_PostProcess_BlurIntermediate = mRenderer.CreateSRV(); 
		rsc.SRV_PostProcess_BlurOutput       = mRenderer.CreateSRV();
		rsc.SRV_PostProcess_FFXCASOut        = mRenderer.CreateSRV();
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
		mRenderer.DestroyTexture(r.Tex_SceneColor);
		mRenderer.DestroyTexture(r.Tex_SceneNormals);

		mRenderer.DestroyTexture(r.Tex_AmbientOcclusion);

		mRenderer.DestroyTexture(r.Tex_PostProcess_BlurOutput);
		mRenderer.DestroyTexture(r.Tex_PostProcess_BlurIntermediate);
		mRenderer.DestroyTexture(r.Tex_PostProcess_TonemapperOut);
		mRenderer.DestroyTexture(r.Tex_PostProcess_FFXCASOut);

		mRenderPass_AO.OnDestroyWindowSizeDependentResources();
	}

	// TODO: generic implementation of other window procedures for unload
}


// ------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// RENDER
//
// ------------------------------------------------------------------------------------------------------------------------------------------------------------
void VQEngine::RenderThread_PreRender()
{
	FWindowRenderContext& ctx = mRenderer.GetWindowRenderContext(mpWinMain->GetHWND());

	const int NUM_BACK_BUFFERS  = ctx.SwapChain.GetNumBackBuffers();
	const int BACK_BUFFER_INDEX = ctx.SwapChain.GetCurrentBackBufferIndex();
	const int FRAME_DATA_INDEX  = mNumRenderLoopsExecuted % NUM_BACK_BUFFERS;

	// Dynamic constant buffer maintenance
	ctx.mDynamicHeap_ConstantBuffer.OnBeginFrame();

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


	ID3D12DescriptorHeap* ppHeaps[] = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };
	ctx.pCmdList_GFX->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
}

void VQEngine::RenderThread_RenderFrame()
{
	//
	// Handle one-time & infrequent events
	//
	if (mbEnvironmentMapPreFilter.load())
	{
		PreFilterEnvironmentMap(mResources_MainWnd.EnvironmentMap);
		mbEnvironmentMapPreFilter.store(false);
	}


	//
	// Render window contexts
	//
	RenderThread_RenderMainWindow();

	if(mpWinDebug && !mpWinDebug->IsClosed())
		RenderThread_RenderDebugWindow();
}


void VQEngine::RenderThread_RenderMainWindow()
{
	const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCount(mpWinMain);
	const int FRAME_DATA_INDEX = mNumRenderLoopsExecuted % NUM_BACK_BUFFERS;

	HRESULT hr = S_OK; 

	FWindowRenderContext& ctx = mRenderer.GetWindowRenderContext(mpWinMain->GetHWND());
	hr = mbLoadingLevel || mbLoadingEnvironmentMap
		? RenderThread_RenderMainWindow_LoadingScreen(ctx)
		: RenderThread_RenderMainWindow_Scene(ctx);

	if (hr == DXGI_STATUS_OCCLUDED)     { RenderThread_HandleStatusOccluded(); }
	if (hr == DXGI_ERROR_DEVICE_REMOVED){ RenderThread_HandleDeviceRemoved();  }
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
	MessageBox(NULL, "Device Removed.\n\nVQEngine will shutdown.", "VQEngine Renderer Error", MB_OK);
	this->mbStopAllThreads.store(true);
	RenderThread_SignalUpdateThread();
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
	const float RenderResolutionX = static_cast<float>(ctx.MainRTResolutionX);
	const float RenderResolutionY = static_cast<float>(ctx.MainRTResolutionY);
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
	const int NUM_BACK_BUFFERS = ctx.SwapChain.GetNumBackBuffers();
	const int BACK_BUFFER_INDEX = ctx.SwapChain.GetCurrentBackBufferIndex();
	const int FRAME_DATA_INDEX = mNumRenderLoopsExecuted % NUM_BACK_BUFFERS;
	assert(ctx.mCommandAllocatorsGFX.size() >= NUM_BACK_BUFFERS);
	const bool bUseHDRRenderPath = this->ShouldRenderHDR(mpWinMain->GetHWND());
	// ----------------------------------------------------------------------------

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
		mLoadingScreenData.SwapChainClearColor[0],
		mLoadingScreenData.SwapChainClearColor[1],
		mLoadingScreenData.SwapChainClearColor[2],
		mLoadingScreenData.SwapChainClearColor[3]
	};
	pCmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

	// Draw Triangle
	const float           RenderResolutionX = static_cast<float>(ctx.MainRTResolutionX);
	const float           RenderResolutionY = static_cast<float>(ctx.MainRTResolutionY);
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

	pCmd->Close();

	ID3D12CommandList* ppCommandLists[] = { ctx.pCmdList_GFX };
	ctx.PresentQueue.pQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);


	//
	// PRESENT
	//
	hr = ctx.SwapChain.Present();
	ctx.SwapChain.MoveToNextFrame();
	return hr;
}


//====================================================================================================
//
//
//
//====================================================================================================
HRESULT VQEngine::RenderThread_RenderMainWindow_Scene(FWindowRenderContext& ctx)
{
	HRESULT hr = S_OK;
	const int NUM_BACK_BUFFERS  = ctx.SwapChain.GetNumBackBuffers();
	const int BACK_BUFFER_INDEX = ctx.SwapChain.GetCurrentBackBufferIndex();
	const int FRAME_DATA_INDEX  = mNumRenderLoopsExecuted % NUM_BACK_BUFFERS;
	assert(ctx.mCommandAllocatorsGFX.size() >= NUM_BACK_BUFFERS);
	const bool bUseHDRRenderPath = this->ShouldRenderHDR(mpWinMain->GetHWND());
	const FSceneView&       SceneView       = mpScene->GetSceneView(FRAME_DATA_INDEX);
	const FSceneShadowView& SceneShadowView = mpScene->GetShadowView(FRAME_DATA_INDEX);
	const FPostProcessParameters& PPParams  = mpScene->GetPostProcessParameters(FRAME_DATA_INDEX);
	// ----------------------------------------------------------------------------

	ID3D12GraphicsCommandList*& pCmd = ctx.pCmdList_GFX;

	ID3D12DescriptorHeap* ppHeaps[] = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };
	pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	//
	// RENDER
	//
	RenderShadowMaps(ctx, SceneShadowView);

	RenderDepthPrePass(ctx, SceneView);

	TransitionForSceneRendering(ctx);
	
	RenderAmbientOcclusion(ctx, SceneView);

	RenderSceneColor(ctx, SceneView);
	
	ResolveMSAA(ctx);

	TransitionForPostProcessing(ctx, PPParams);

	RenderPostProcess(ctx, PPParams);

	RenderUI(ctx, PPParams);

	if (bUseHDRRenderPath)
	{
		CompositUIToHDRSwapchain(ctx);
	}

	hr = PresentFrame(ctx);

	return hr;
}


//
// Command Recording
//

void VQEngine::DrawMesh(ID3D12GraphicsCommandList* pCmd, const Mesh& mesh)
{
	using namespace DirectX;
	const bool& bMSAA = mSettings.gfx.bAntiAliasing;

	// Draw Object -----------------------------------------------
	const auto VBIBIDs = mesh.GetIABufferIDs();
	const uint32 NumIndices = mesh.GetNumIndices();
	const uint32 NumInstances = 1;
	const BufferID& VB_ID = VBIBIDs.first;
	const BufferID& IB_ID = VBIBIDs.second;
	const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
	const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

	pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmd->IASetVertexBuffers(0, 1, &vb);
	pCmd->IASetIndexBuffer(&ib);

	pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
}

void VQEngine::RenderShadowMaps(FWindowRenderContext& ctx, const FSceneShadowView& SceneShadowView)
{
	using namespace DirectX;
	struct FCBufferLightPS
	{
		XMFLOAT3 vLightPos;
		float fFarPlane;
	};
	struct FCBufferLightVS
	{
		XMMATRIX matWorldViewProj;
		XMMATRIX matWorld;
	};

	ID3D12GraphicsCommandList*& pCmd = ctx.pCmdList_GFX;
	auto fnDrawRenderList = [&](const FSceneShadowView::FShadowView& shadowView)
	{
		for (const FShadowMeshRenderCommand& renderCmd : shadowView.meshRenderCommands)
		{
			// set constant buffer data
			FCBufferLightVS* pCBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			ctx.mDynamicHeap_ConstantBuffer.AllocConstantBuffer(sizeof(decltype(pCBuffer)), (void**)(&pCBuffer), &cbAddr);
			pCBuffer->matWorldViewProj = renderCmd.WorldTransformationMatrix * shadowView.matViewProj;
			pCBuffer->matWorld = renderCmd.WorldTransformationMatrix;
			pCmd->SetGraphicsRootConstantBufferView(0, cbAddr);

			// set IA
			const Mesh& mesh = mpScene->mMeshes.at(renderCmd.meshID);

			const auto VBIBIDs = mesh.GetIABufferIDs();
			const uint32 NumIndices = mesh.GetNumIndices();
			const uint32 NumInstances = 1;
			const BufferID& VB_ID = VBIBIDs.first;
			const BufferID& IB_ID = VBIBIDs.second;
			const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
			const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

			pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmd->IASetVertexBuffers(0, 1, &vb);
			pCmd->IASetIndexBuffer(&ib);

			// draw
			pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
		}
	};


	constexpr bool B_CLEAR_DEPTH_BUFFERS_BEFORE_DRAW = false;
	SCOPED_GPU_MARKER(pCmd, "RenderShadowMaps");


	// CLEAR DEPTH BUFFERS
	if constexpr (B_CLEAR_DEPTH_BUFFERS_BEFORE_DRAW)
	{
		SCOPED_GPU_MARKER(pCmd, "ClearShadowDepths");
		D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;

		for (int i = 0; i < SceneShadowView.NumSpotShadowViews; ++i)
		{
			const std::string marker = "Spot[" + std::to_string(i) + "]";
			SCOPED_GPU_MARKER(pCmd, marker.c_str());

			const DSV& dsv = mRenderer.GetDSV(mResources_MainWnd.DSV_ShadowMaps_Spot);
			pCmd->ClearDepthStencilView(dsv.GetCPUDescHandle(i), DSVClearFlags, 1.0f, 0, 0, NULL);
		}
		for (int i = 0; i < SceneShadowView.NumPointShadowViews; ++i)
		{
			const std::string marker = "Point[" + std::to_string(i) + "]";
			SCOPED_GPU_MARKER(pCmd, marker.c_str());
			for (int face = 0; face < 6; ++face)
			{
				const int iShadowView = i * 6 + face;
				const DSV& dsv = mRenderer.GetDSV(mResources_MainWnd.DSV_ShadowMaps_Point);
				pCmd->ClearDepthStencilView(dsv.GetCPUDescHandle(iShadowView), DSVClearFlags, 1.0f, 0, 0, NULL);
			}
		}
		if (!SceneShadowView.ShadowView_Directional.meshRenderCommands.empty())
		{
			SCOPED_GPU_MARKER(pCmd, "Directional");
			const DSV& dsv = mRenderer.GetDSV(mResources_MainWnd.DSV_ShadowMaps_Directional);
			pCmd->ClearDepthStencilView(dsv.GetCPUDescHandle(), DSVClearFlags, 1.0f, 0, 0, NULL);
		}
	}


	//
	// DIRECTIONAL LIGHT
	//
	if (!SceneShadowView.ShadowView_Directional.meshRenderCommands.empty())
	{
		const std::string marker = "Directional";
		SCOPED_GPU_MARKER(pCmd, marker.c_str());

		pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::DEPTH_PASS_PSO));
		pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(7));

		const float RenderResolutionX = 2048.0f; // TODO
		const float RenderResolutionY = 2048.0f; // TODO
		D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
		D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
		pCmd->RSSetViewports(1, &viewport);
		pCmd->RSSetScissorRects(1, &scissorsRect);

		// Bind Depth / clear
		const DSV& dsv = mRenderer.GetDSV(mResources_MainWnd.DSV_ShadowMaps_Directional);
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsv.GetCPUDescHandle();
		pCmd->OMSetRenderTargets(0, NULL, FALSE, &dsvHandle);

		if constexpr (!B_CLEAR_DEPTH_BUFFERS_BEFORE_DRAW)
		{
			D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
			pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, NULL);
		}

		fnDrawRenderList(SceneShadowView.ShadowView_Directional);
	}

	// Set Viewport & Scissors
	{
		const float RenderResolutionX = 1024.0f; // TODO
		const float RenderResolutionY = 1024.0f; // TODO
		D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
		D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
		pCmd->RSSetViewports(1, &viewport);
		pCmd->RSSetScissorRects(1, &scissorsRect);
	}

	//
	// SPOT LIGHTS
	//
	if (SceneShadowView.NumSpotShadowViews > 0)
	{
		// set PSO & RS only when we're going to render Spot views AND directional light shadows hasn't rendered
		// to save a context roll as the PSO and RS will already be what we want
		if (SceneShadowView.ShadowView_Directional.meshRenderCommands.empty())
		{
			pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::DEPTH_PASS_PSO));
			pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(7));
		}
	}
	for (int i = 0; i < SceneShadowView.NumSpotShadowViews; ++i)
	{
		const FSceneShadowView::FShadowView& ShadowView = SceneShadowView.ShadowViews_Spot[i];

		if (ShadowView.meshRenderCommands.empty())
			continue;

		const std::string marker = "Spot[" + std::to_string(i) + "]";
		SCOPED_GPU_MARKER(pCmd, marker.c_str());

		// Bind Depth / clear
		const DSV& dsv = mRenderer.GetDSV(mResources_MainWnd.DSV_ShadowMaps_Spot);
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsv.GetCPUDescHandle(i);
		pCmd->OMSetRenderTargets(0, NULL, FALSE, &dsvHandle);

		if constexpr (!B_CLEAR_DEPTH_BUFFERS_BEFORE_DRAW)
		{
			D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
			pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, NULL);
		}

		fnDrawRenderList(ShadowView);
	}


	//
	// POINT LIGHTS
	//
	if (SceneShadowView.NumPointShadowViews > 0)
	{
		pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::DEPTH_PASS_LINEAR_PSO));
		pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(8));
	}
	for (int i = 0; i < SceneShadowView.NumPointShadowViews; ++i)
	{
		const std::string marker = "Point[" + std::to_string(i) + "]";
		SCOPED_GPU_MARKER(pCmd, marker.c_str());

		FCBufferLightPS* pCBuffer = {};
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		ctx.mDynamicHeap_ConstantBuffer.AllocConstantBuffer(sizeof(decltype(pCBuffer)), (void**)(&pCBuffer), &cbAddr);

		pCBuffer->vLightPos = SceneShadowView.PointLightLinearDepthParams[i].vWorldPos;
		pCBuffer->fFarPlane = SceneShadowView.PointLightLinearDepthParams[i].fFarPlane;
		pCmd->SetGraphicsRootConstantBufferView(1, cbAddr);

		for (int face = 0; face < 6; ++face)
		{
			const int iShadowView = i * 6 + face;
			const FSceneShadowView::FShadowView& ShadowView = SceneShadowView.ShadowViews_Point[iShadowView];

			if (ShadowView.meshRenderCommands.empty())
				continue;

			const std::string marker_face = "[Cubemap Face=" + std::to_string(face) + "]";
			SCOPED_GPU_MARKER(pCmd, marker_face.c_str());
		
			// Bind Depth / clear
			const DSV& dsv = mRenderer.GetDSV(mResources_MainWnd.DSV_ShadowMaps_Point);
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsv.GetCPUDescHandle(iShadowView);
			pCmd->OMSetRenderTargets(0, NULL, FALSE, &dsvHandle);
			
			if constexpr (!B_CLEAR_DEPTH_BUFFERS_BEFORE_DRAW)
			{
				D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
				pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, NULL);
			}

			fnDrawRenderList(ShadowView);
		}
	}

}

void VQEngine::RenderDepthPrePass(FWindowRenderContext& ctx, const FSceneView& SceneView)
{
	using namespace VQ_SHADER_DATA;

	const bool& bMSAA = mSettings.gfx.bAntiAliasing;
	ID3D12GraphicsCommandList*& pCmd = ctx.pCmdList_GFX;

	const DSV& dsvColor = mRenderer.GetDSV(mResources_MainWnd.DSV_SceneDepthMSAA); //mRenderer.GetDSV(bMSAA ? mResources_MainWnd.DSV_SceneDepthMSAA : mResources_MainWnd.DSV_SceneDepth); // TODO: do we want non-msaa target?
	const RTV& rtvNormals = mRenderer.GetRTV(bMSAA ? mResources_MainWnd.RTV_SceneNormalsMSAA : mResources_MainWnd.RTV_SceneNormals);

	D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvNormals.GetCPUDescHandle();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvColor.GetCPUDescHandle();


	const float RenderResolutionX = static_cast<float>(ctx.MainRTResolutionX);
	const float RenderResolutionY = static_cast<float>(ctx.MainRTResolutionY);
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };


	SCOPED_GPU_MARKER(pCmd, "RenderDepthPrePass");

	pCmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, nullptr);

	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);

	pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA ? EBuiltinPSOs::DEPTH_PREPASS_PSO_MSAA_4 : EBuiltinPSOs::DEPTH_PREPASS_PSO));
	pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(14)); // hardcoded root signature for now until shader reflection and rootsignature management is implemented

	// draw meshes
	for (const FMeshRenderCommand& meshRenderCmd : SceneView.meshRenderCommands)
	{
		if (mpScene->mMeshes.find(meshRenderCmd.meshID) == mpScene->mMeshes.end())
		{
			Log::Warning("MeshID=%d couldn't be found", meshRenderCmd.meshID);
			continue; // skip drawing this mesh
		}

		const Material& mat = mpScene->GetMaterial(meshRenderCmd.matID);
		const Mesh& mesh = mpScene->mMeshes.at(meshRenderCmd.meshID);
		const auto VBIBIDs = mesh.GetIABufferIDs();
		const uint32 NumIndices = mesh.GetNumIndices();
		const uint32 NumInstances = 1;
		const BufferID& VB_ID = VBIBIDs.first;
		const BufferID& IB_ID = VBIBIDs.second;
		const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
		const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);


		// set constant buffer data
		PerObjectData* pPerObj = {};
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		ctx.mDynamicHeap_ConstantBuffer.AllocConstantBuffer(sizeof(decltype(pPerObj)), (void**)(&pPerObj), &cbAddr);

		pPerObj->matWorldViewProj = meshRenderCmd.WorldTransformationMatrix * SceneView.viewProj;
		pPerObj->matWorld = meshRenderCmd.WorldTransformationMatrix;
		pPerObj->matNormal = meshRenderCmd.NormalTransformationMatrix;
		pPerObj->materialData = std::move(mat.GetCBufferData());

		pCmd->SetGraphicsRootConstantBufferView(1, cbAddr);

		// set textures
		if (mat.SRVMaterialMaps != INVALID_ID)
		{
			pCmd->SetGraphicsRootDescriptorTable(0, mRenderer.GetSRV(mat.SRVMaterialMaps).GetGPUDescHandle(0));
			//pCmd->SetGraphicsRootDescriptorTable(4, mRenderer.GetSRV(mat.SRVMaterialMaps).GetGPUDescHandle(0));
		}

		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmd->IASetVertexBuffers(0, 1, &vb);
		pCmd->IASetIndexBuffer(&ib);

		pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
	}

	// resolve if MSAA
	const auto& rsc = mResources_MainWnd;
	auto pRscNormals     = mRenderer.GetTextureResource(rsc.Tex_SceneNormals);
	auto pRscNormalsMSAA = mRenderer.GetTextureResource(rsc.Tex_SceneNormalsMSAA);
	auto pRscDepth       = mRenderer.GetTextureResource(rsc.Tex_SceneDepth);
	auto pRscDepthMSAA   = mRenderer.GetTextureResource(rsc.Tex_SceneDepthMSAA);

	if (bMSAA)
	{
		DXGI_FORMAT fmtDepth = DXGI_FORMAT_D32_FLOAT; // mRenderer.GetTextureFormat(mResources_MainWnd.Tex_SceneNormals);

		// transition for resolve
		const CD3DX12_RESOURCE_BARRIER pBarriers[] =
		{
				  CD3DX12_RESOURCE_BARRIER::Transition(pRscNormalsMSAA , D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE)
				, CD3DX12_RESOURCE_BARRIER::Transition(pRscNormals     , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST)
				
				, CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthMSAA   , D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
				, CD3DX12_RESOURCE_BARRIER::Transition(pRscDepth       , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
		};
		pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);

		// resolve
		{
			ResolveDepth(ctx, rsc.Tex_SceneDepthMSAA, rsc.SRV_SceneDepthMSAA, rsc.UAV_SceneDepth);
		}
		{
			SCOPED_GPU_MARKER(pCmd, "ResolveSceneNormalsMSAA");
			pCmd->ResolveSubresource(pRscNormals, 0, pRscNormalsMSAA, 0, mRenderer.GetTextureFormat(mResources_MainWnd.Tex_SceneNormalsMSAA));
		}
	}

	// transition for shader read
	{
		std::vector<CD3DX12_RESOURCE_BARRIER> Barriers;

		if (bMSAA)
		{
			Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscNormalsMSAA, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
			Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthMSAA  , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));
			Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepth      , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
		}
		else
		{
			// TODO: need proper pRscDepth for Non-MSAA path (current one is UAV and not a DSV)
			assert(false);
			Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
		}

		Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscNormals, 
			bMSAA ? D3D12_RESOURCE_STATE_RESOLVE_DEST : D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
		));

		pCmd->ResourceBarrier((UINT32)Barriers.size(), Barriers.data());
	}
}

void VQEngine::RenderAmbientOcclusion(FWindowRenderContext& ctx, const FSceneView& SceneView)
{
	const auto& rsc = mResources_MainWnd;
	const bool& bMSAA = mSettings.gfx.bAntiAliasing;
	ID3D12GraphicsCommandList*& pCmd = ctx.pCmdList_GFX;

	ID3D12Resource* pRscAmbientOcclusion = mRenderer.GetTextureResource(rsc.Tex_AmbientOcclusion);

	const char* pStrPass[FAmbientOcclusionPass::EMethod::NUM_AMBIENT_OCCLUSION_METHODS] =
	{
		"Ambient Occlusion (FidelityFX CACAO)"
	};
	SCOPED_GPU_MARKER(pCmd, pStrPass[mRenderPass_AO.Method]);
	

	if (SceneView.sceneParameters.bScreenSpaceAO)
	{

		FAmbientOcclusionPass::FDrawParameters drawParams = {};
		DirectX::XMStoreFloat4x4(&drawParams.matNormalToView, SceneView.view);
		DirectX::XMStoreFloat4x4(&drawParams.matProj, SceneView.proj);
		drawParams.pCmd = pCmd;

		// TODO: move into RecordCommands?
		pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pRscAmbientOcclusion, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
		mRenderPass_AO.RecordCommands(&drawParams);

		// set back the descriptor heap for the VQEngine
		ID3D12DescriptorHeap* ppHeaps[] = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };
		pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	}
	else
	{
		pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pRscAmbientOcclusion, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
		// TODO: clear to 1
		pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pRscAmbientOcclusion, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ));
	}
}

void VQEngine::TransitionForSceneRendering(FWindowRenderContext& ctx)
{
	const bool& bMSAA = mSettings.gfx.bAntiAliasing;
	ID3D12GraphicsCommandList*& pCmd = ctx.pCmdList_GFX;

	const auto& rsc = mResources_MainWnd;
	auto pRscColor           = mRenderer.GetTextureResource(rsc.Tex_SceneColor);
	auto pRscColorMSAA       = mRenderer.GetTextureResource(rsc.Tex_SceneColorMSAA);
	auto pRscNormals         = mRenderer.GetTextureResource(rsc.Tex_SceneNormals);
	auto pRscNormalsMSAA     = mRenderer.GetTextureResource(rsc.Tex_SceneNormalsMSAA);
	auto pRscShadowMaps_Spot = mRenderer.GetTextureResource(rsc.Tex_ShadowMaps_Spot);
	auto pRscShadowMaps_Point= mRenderer.GetTextureResource(rsc.Tex_ShadowMaps_Point);
	auto pRscShadowMaps_Directional = mRenderer.GetTextureResource(rsc.Tex_ShadowMaps_Directional);

	SCOPED_GPU_MARKER(pCmd, "TransitionForSceneRendering");

	CD3DX12_RESOURCE_BARRIER ColorTransition = bMSAA
		? CD3DX12_RESOURCE_BARRIER::Transition(pRscColorMSAA, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
		: CD3DX12_RESOURCE_BARRIER::Transition(pRscColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const CD3DX12_RESOURCE_BARRIER pBarriers[] =
	{
		  ColorTransition
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Spot       , D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Point      , D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Directional, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
	};

	pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
}

void VQEngine::RenderSceneColor(FWindowRenderContext& ctx, const FSceneView& SceneView)
{
	using namespace VQ_SHADER_DATA;

	const bool bUseHDRRenderPath = this->ShouldRenderHDR(mpWinMain->GetHWND());
	const bool bHasEnvironmentMapHDRTexture = mResources_MainWnd.EnvironmentMap.SRV_HDREnvironment != INVALID_ID;
	const bool bDrawEnvironmentMap = bHasEnvironmentMapHDRTexture && true;

	using namespace DirectX;

	const bool& bMSAA = mSettings.gfx.bAntiAliasing;
	ID3D12GraphicsCommandList*& pCmd = ctx.pCmdList_GFX;

	const RTV& rtvColor = mRenderer.GetRTV(bMSAA ? mResources_MainWnd.RTV_SceneColorMSAA : mResources_MainWnd.RTV_SceneColor);
	const DSV& dsvColor = mRenderer.GetDSV(/*bMSAA ? */mResources_MainWnd.DSV_SceneDepthMSAA /*: mResources_MainWnd.DSV_SceneDepth*/);

	SCOPED_GPU_MARKER(pCmd, "RenderSceneColor");

	// Clear Depth & Render targets
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvColor.GetCPUDescHandle();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvColor.GetCPUDescHandle();
	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	pCmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);


	// Set Viewport & Scissors
	const float RenderResolutionX = static_cast<float>(ctx.MainRTResolutionX);
	const float RenderResolutionY = static_cast<float>(ctx.MainRTResolutionY);
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);

	pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA ? EBuiltinPSOs::FORWARD_LIGHTING_PSO_MSAA_4 : EBuiltinPSOs::FORWARD_LIGHTING_PSO));
	pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(5)); // hardcoded root signature for now until shader reflection and rootsignature management is implemented

	// set PerFrame constants
	{
		const CBV_SRV_UAV& NullCubemapSRV = mRenderer.GetSRV(mResources_MainWnd.SRV_NullCubemap);
		const CBV_SRV_UAV& NullTex2DSRV   = mRenderer.GetSRV(mResources_MainWnd.SRV_NullTexture2D);

		constexpr UINT PerFrameRSBindSlot = 3;
		PerFrameData* pPerFrame = {};
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		ctx.mDynamicHeap_ConstantBuffer.AllocConstantBuffer(sizeof(PerFrameData), (void**)(&pPerFrame), &cbAddr);

		assert(pPerFrame);
		pPerFrame->Lights = SceneView.GPULightingData;
		pPerFrame->fAmbientLightingFactor = SceneView.sceneParameters.fAmbientLightingFactor;
		pPerFrame->f2PointLightShadowMapDimensions = { 1024.0f, 1024.f }; // TODO
		pPerFrame->f2SpotLightShadowMapDimensions  = { 1024.0f, 1024.f }; // TODO
		pPerFrame->f2DirectionalLightShadowMapDimensions  = { 2048.0f, 2048.0f }; // TODO
		
		if (bUseHDRRenderPath)
		{
			// adjust ambient factor as the tonemapper changes the output curve for HDR displays 
			// and makes the ambient lighting too strong.
			pPerFrame->fAmbientLightingFactor *= 0.005f;
		}

		//pCmd->SetGraphicsRootDescriptorTable(PerFrameRSBindSlot, );
		pCmd->SetGraphicsRootConstantBufferView(PerFrameRSBindSlot, cbAddr);
		pCmd->SetGraphicsRootDescriptorTable(4, mRenderer.GetSRV(mResources_MainWnd.SRV_ShadowMaps_Spot).GetGPUDescHandle(0));
		pCmd->SetGraphicsRootDescriptorTable(5, mRenderer.GetSRV(mResources_MainWnd.SRV_ShadowMaps_Point).GetGPUDescHandle(0));
		pCmd->SetGraphicsRootDescriptorTable(6, mRenderer.GetSRV(mResources_MainWnd.SRV_ShadowMaps_Directional).GetGPUDescHandle(0));
		pCmd->SetGraphicsRootDescriptorTable(7, bDrawEnvironmentMap
			? mRenderer.GetSRV(mResources_MainWnd.EnvironmentMap.SRV_IrradianceDiffBlurred).GetGPUDescHandle(0)
			: NullCubemapSRV.GetGPUDescHandle()
		);
		pCmd->SetGraphicsRootDescriptorTable(8, bDrawEnvironmentMap
			? mRenderer.GetSRV(mResources_MainWnd.EnvironmentMap.SRV_IrradianceSpec).GetGPUDescHandle(0)
			: NullCubemapSRV.GetGPUDescHandle()
		);
		pCmd->SetGraphicsRootDescriptorTable(9, bDrawEnvironmentMap
			? mRenderer.GetSRV(mResources_MainWnd.EnvironmentMap.SRV_BRDFIntegrationLUT).GetGPUDescHandle()
			: NullTex2DSRV.GetGPUDescHandle()
		);
		pCmd->SetGraphicsRootDescriptorTable(10, mRenderer.GetSRV(mResources_MainWnd.SRV_FFXCACAO_Out).GetGPUDescHandle());
	}

	// set PerView constants
	{
		constexpr UINT PerViewRSBindSlot = 2;
		PerViewData* pPerView = {};
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		ctx.mDynamicHeap_ConstantBuffer.AllocConstantBuffer(sizeof(decltype(pPerView)), (void**)(&pPerView), &cbAddr);

		assert(pPerView);
		XMStoreFloat3(&pPerView->CameraPosition, SceneView.cameraPosition);
		pPerView->ScreenDimensions.x = RenderResolutionX;
		pPerView->ScreenDimensions.y = RenderResolutionY;

		// TODO: PreView data

		//pCmd->SetGraphicsRootDescriptorTable(PerViewRSBindSlot, D3D12_GPU_DESCRIPTOR_HANDLE{ cbAddr });
		pCmd->SetGraphicsRootConstantBufferView(PerViewRSBindSlot, cbAddr);
	}


	// Draw Objects -----------------------------------------------
	//if(!SceneView.meshRenderCommands.empty())
	{
		constexpr UINT PerObjRSBindSlot = 1;
		SCOPED_GPU_MARKER(pCmd, "Geometry");

		for (const FMeshRenderCommand& meshRenderCmd : SceneView.meshRenderCommands)
		{
			const Material& mat = mpScene->GetMaterial(meshRenderCmd.matID);

			// set constant buffer data
			PerObjectData* pPerObj = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			ctx.mDynamicHeap_ConstantBuffer.AllocConstantBuffer(sizeof(decltype(pPerObj)), (void**)(&pPerObj), &cbAddr);


			pPerObj->matWorldViewProj = meshRenderCmd.WorldTransformationMatrix * SceneView.viewProj;
			pPerObj->matWorld         = meshRenderCmd.WorldTransformationMatrix;
			pPerObj->matNormal        = meshRenderCmd.NormalTransformationMatrix;
			pPerObj->materialData = std::move(mat.GetCBufferData());

			pCmd->SetGraphicsRootConstantBufferView(PerObjRSBindSlot, cbAddr);

			// set textures
			if (mat.SRVMaterialMaps != INVALID_ID)
			{
				pCmd->SetGraphicsRootDescriptorTable(0, mRenderer.GetSRV(mat.SRVMaterialMaps).GetGPUDescHandle(0));
				//pCmd->SetGraphicsRootDescriptorTable(4, mRenderer.GetSRV(mat.SRVMaterialMaps).GetGPUDescHandle(0));

			}

			// draw mesh
			if (mpScene->mMeshes.find(meshRenderCmd.meshID) == mpScene->mMeshes.end())
			{
				Log::Warning("MeshID=%d couldn't be found", meshRenderCmd.meshID);
				continue; // skip drawing this mesh
			}

			const Mesh& mesh = mpScene->mMeshes.at(meshRenderCmd.meshID);

			const auto VBIBIDs = mesh.GetIABufferIDs();
			const uint32 NumIndices = mesh.GetNumIndices();
			const uint32 NumInstances = 1;
			const BufferID& VB_ID = VBIBIDs.first;
			const BufferID& IB_ID = VBIBIDs.second;
			const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
			const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

			pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmd->IASetVertexBuffers(0, 1, &vb);
			pCmd->IASetIndexBuffer(&ib);

			pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
		}
	}

	// Draw Light bounds ------------------------------------------
	if(!SceneView.lightBoundsRenderCommands.empty())
	{
		SCOPED_GPU_MARKER(pCmd, "LightBounds");
		pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA 
			? EBuiltinPSOs::WIREFRAME_PSO_MSAA_4 
			: EBuiltinPSOs::WIREFRAME_PSO)
		);

		pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(6)); // hardcoded root signature for now until shader reflection and rootsignature management is implemented
		for (const FLightRenderCommand& lightBoundRenderCmd : SceneView.lightBoundsRenderCommands)
		{
			// set constant buffer data
			FFrameConstantBufferUnlit* pCBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			ctx.mDynamicHeap_ConstantBuffer.AllocConstantBuffer(sizeof(decltype(pCBuffer)), (void**)(&pCBuffer), &cbAddr);
			pCBuffer->color            = lightBoundRenderCmd.color;
			pCBuffer->matModelViewProj = lightBoundRenderCmd.WorldTransformationMatrix * SceneView.viewProj;
			pCmd->SetGraphicsRootConstantBufferView(0, cbAddr);

			// set IA
			const Mesh& mesh = mpScene->mMeshes.at(lightBoundRenderCmd.meshID);

			const auto VBIBIDs = mesh.GetIABufferIDs();
			const uint32 NumIndices = mesh.GetNumIndices();
			const uint32 NumInstances = 1;
			const BufferID& VB_ID = VBIBIDs.first;
			const BufferID& IB_ID = VBIBIDs.second;
			const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
			const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

			pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmd->IASetVertexBuffers(0, 1, &vb);
			pCmd->IASetIndexBuffer(&ib);

			// draw
			pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
		}
	}

	// Draw Light Meshes ------------------------------------------
	if(!SceneView.lightRenderCommands.empty())
	{
		SCOPED_GPU_MARKER(pCmd, "Lights");
		pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA
			? EBuiltinPSOs::UNLIT_PSO_MSAA_4
			: EBuiltinPSOs::UNLIT_PSO)
		);
		if(SceneView.lightBoundsRenderCommands.empty())
			pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(6)); // hardcoded root signature for now until shader reflection and rootsignature management is implemented
		for (const FLightRenderCommand& lightRenderCmd : SceneView.lightRenderCommands)
		{
			// set constant buffer data
			FFrameConstantBufferUnlit* pCBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			ctx.mDynamicHeap_ConstantBuffer.AllocConstantBuffer(sizeof(decltype(pCBuffer)), (void**)(&pCBuffer), &cbAddr);
			pCBuffer->color            = lightRenderCmd.color;
			pCBuffer->matModelViewProj = lightRenderCmd.WorldTransformationMatrix * SceneView.viewProj;
			pCmd->SetGraphicsRootConstantBufferView(0, cbAddr);

			// set IA
			const Mesh& mesh = mpScene->mMeshes.at(lightRenderCmd.meshID);

			const auto VBIBIDs = mesh.GetIABufferIDs();
			const uint32 NumIndices = mesh.GetNumIndices();
			const uint32 NumInstances = 1;
			const BufferID& VB_ID = VBIBIDs.first;
			const BufferID& IB_ID = VBIBIDs.second;
			const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
			const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

			pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmd->IASetVertexBuffers(0, 1, &vb);
			pCmd->IASetIndexBuffer(&ib);

			// draw
			pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
		}
	}

	// Draw Environment Map ---------------------------------------
	if (bDrawEnvironmentMap)
	{
		SCOPED_GPU_MARKER(pCmd, "EnvironmentMap");

		ID3D12DescriptorHeap* ppHeaps[] = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };

		Camera skyCam = mpScene->GetActiveCamera().Clone();
		FCameraParameters p = {};
		p.bInitializeCameraController = false;
		p.ProjectionParams = skyCam.GetProjectionParameters();
		p.ProjectionParams.bPerspectiveProjection = true;
		p.ProjectionParams.FieldOfView = p.ProjectionParams.FieldOfView * RAD2DEG; // TODO: remove the need for this conversion
		p.x = p.y = p.z = 0;
		p.Yaw   = SceneView.MainViewCameraYaw   * RAD2DEG;
		p.Pitch = SceneView.MainViewCameraPitch * RAD2DEG;
		skyCam.InitializeCamera(p);

		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		FFrameConstantBuffer * pConstBuffer = {};
		ctx.mDynamicHeap_ConstantBuffer.AllocConstantBuffer(sizeof(FFrameConstantBuffer ), (void**)(&pConstBuffer), &cbAddr);
		pConstBuffer->matModelViewProj = skyCam.GetViewMatrix() * skyCam.GetProjectionMatrix();

		pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA ? EBuiltinPSOs::SKYDOME_PSO_MSAA_4 : EBuiltinPSOs::SKYDOME_PSO));

		// hardcoded root signature for now until shader reflection and rootsignature management is implemented
		pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(2));

		pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		pCmd->SetGraphicsRootDescriptorTable(0, mRenderer.GetSRV(mResources_MainWnd.EnvironmentMap.SRV_HDREnvironment).GetGPUDescHandle());
		pCmd->SetGraphicsRootConstantBufferView(1, cbAddr);

		DrawMesh(pCmd, mBuiltinMeshes[EBuiltInMeshes::CUBE]);
	}
}

void VQEngine::ResolveMSAA(FWindowRenderContext& ctx)
{
	ID3D12GraphicsCommandList*& pCmd = ctx.pCmdList_GFX;
	const bool& bMSAA = mSettings.gfx.bAntiAliasing;

	if (!bMSAA)
		return;

	SCOPED_GPU_MARKER(pCmd, "ResolveMSAA");
	auto pRscColor     = mRenderer.GetTextureResource(mResources_MainWnd.Tex_SceneColor);
	auto pRscColorMSAA = mRenderer.GetTextureResource(mResources_MainWnd.Tex_SceneColorMSAA);

	{
		const CD3DX12_RESOURCE_BARRIER pBarriers[] =
		{
				  CD3DX12_RESOURCE_BARRIER::Transition(pRscColorMSAA , D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE)
				, CD3DX12_RESOURCE_BARRIER::Transition(pRscColor     , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST)
		};
		pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
	}

	pCmd->ResolveSubresource(pRscColor, 0, pRscColorMSAA, 0, mRenderer.GetTextureFormat(mResources_MainWnd.Tex_SceneColor));
}

void VQEngine::ResolveDepth(FWindowRenderContext& ctx, TextureID DepthTexture, SRV_ID SRVDepthTexture, UAV_ID UAVDepthResolveTexture)
{
	ID3D12GraphicsCommandList*& pCmd = ctx.pCmdList_GFX;

	const SRV& srvMSAADepthSource = mRenderer.GetSRV(SRVDepthTexture);
	const UAV& uavDepthResolveTarget = mRenderer.GetUAV(UAVDepthResolveTexture);
	
	int W, H;
	int NumSamples = 4; // TODO: generic?
	mRenderer.GetTextureDimensions(DepthTexture, W, H);

	constexpr int DispatchGroupDimensionX = 8;
	constexpr int DispatchGroupDimensionY = 8;
	const     int DispatchX = (W + (DispatchGroupDimensionX - 1)) / DispatchGroupDimensionX;
	const     int DispatchY = (H + (DispatchGroupDimensionY - 1)) / DispatchGroupDimensionY;
	constexpr int DispatchZ = 1;

	struct FParams { uint params[4]; } CBuffer;
	CBuffer.params[0] = W;
	CBuffer.params[1] = H;
	CBuffer.params[2] = NumSamples; // MSAA Sample Count
	CBuffer.params[3] = 0xDEADC0DE;
	FParams* pConstBuffer = {};
	D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
	ctx.mDynamicHeap_ConstantBuffer.AllocConstantBuffer(sizeof(FParams), (void**)&pConstBuffer, &cbAddr);
	*pConstBuffer = CBuffer;

	SCOPED_GPU_MARKER(pCmd, "ResolveDepth");
	pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::DEPTH_RESOLVE));
	pCmd->SetComputeRootSignature(mRenderer.GetRootSignature(3));
	pCmd->SetComputeRootDescriptorTable(0, srvMSAADepthSource.GetGPUDescHandle());
	pCmd->SetComputeRootDescriptorTable(1, uavDepthResolveTarget.GetGPUDescHandle());
	pCmd->SetComputeRootConstantBufferView(2, cbAddr);
	pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);
}

void VQEngine::TransitionForPostProcessing(FWindowRenderContext& ctx, const FPostProcessParameters& PPParams)
{
	const bool& bMSAA = mSettings.gfx.bAntiAliasing;
	ID3D12GraphicsCommandList*& pCmd = ctx.pCmdList_GFX;
	const auto& rsc = mResources_MainWnd;

	auto pRscPostProcessInput = mRenderer.GetTextureResource(rsc.Tex_SceneColor);
	auto pRscTonemapperOut    = mRenderer.GetTextureResource(rsc.Tex_PostProcess_TonemapperOut);
	auto pRscFFXCASOut        = mRenderer.GetTextureResource(rsc.Tex_PostProcess_FFXCASOut);
	auto pRscPostProcessOut   = PPParams.IsFFXCASEnabled() ? pRscFFXCASOut : pRscTonemapperOut;

	auto pRscShadowMaps_Spot = mRenderer.GetTextureResource(rsc.Tex_ShadowMaps_Spot);
	auto pRscShadowMaps_Point = mRenderer.GetTextureResource(rsc.Tex_ShadowMaps_Point);
	auto pRscShadowMaps_Directional = mRenderer.GetTextureResource(rsc.Tex_ShadowMaps_Directional);

	SCOPED_GPU_MARKER(pCmd, "TransitionForPostProcessing");
	const CD3DX12_RESOURCE_BARRIER pBarriers[] =
	{
		  CD3DX12_RESOURCE_BARRIER::Transition(pRscPostProcessInput , (bMSAA ? D3D12_RESOURCE_STATE_RESOLVE_DEST : D3D12_RESOURCE_STATE_RENDER_TARGET), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscFFXCASOut        , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut    , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)

		, CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Spot       , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Point      , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Directional, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE)
	};
	pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
}

void VQEngine::RenderPostProcess(FWindowRenderContext& ctx, const FPostProcessParameters& PPParams)
{
	ID3D12DescriptorHeap*       ppHeaps[] = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };
	ID3D12GraphicsCommandList*&      pCmd = ctx.pCmdList_GFX;

	// pass io
	const SRV& srv_ColorIn       = mRenderer.GetSRV(mResources_MainWnd.SRV_SceneColor);
	const UAV& uav_TonemapperOut = mRenderer.GetUAV(mResources_MainWnd.UAV_PostProcess_TonemapperOut);
	const SRV& srv_TonemapperOut = mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_TonemapperOut);
	const UAV& uav_FFXCASOut     = mRenderer.GetUAV(mResources_MainWnd.UAV_PostProcess_FFXCASOut);

	constexpr bool PP_ENABLE_BLUR_PASS = false;

	// compute dispatch dimensions
	const int& InputImageWidth  = ctx.MainRTResolutionX;
	const int& InputImageHeight = ctx.MainRTResolutionY;
	constexpr int DispatchGroupDimensionX = 8;
	constexpr int DispatchGroupDimensionY = 8;
	const     int DispatchX = (InputImageWidth  + (DispatchGroupDimensionX - 1)) / DispatchGroupDimensionX;
	const     int DispatchY = (InputImageHeight + (DispatchGroupDimensionY - 1)) / DispatchGroupDimensionY;
	constexpr int DispatchZ = 1;

	// cmds
	SCOPED_GPU_MARKER(pCmd, "RenderPostProcess");
	{
		const SRV& srv_blurOutput         = mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_BlurOutput);
		ID3D12Resource* pRscTonemapperOut = mRenderer.GetTextureResource(mResources_MainWnd.Tex_PostProcess_TonemapperOut);

		if constexpr (PP_ENABLE_BLUR_PASS && PPParams.bEnableGaussianBlur)
		{
			SCOPED_GPU_MARKER(pCmd, "BlurCS");
			const UAV& uav_BlurIntermediate = mRenderer.GetUAV(mResources_MainWnd.UAV_PostProcess_BlurIntermediate);
			const UAV& uav_BlurOutput       = mRenderer.GetUAV(mResources_MainWnd.UAV_PostProcess_BlurOutput);
			const SRV& srv_blurIntermediate = mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_BlurIntermediate);
			auto pRscBlurIntermediate = mRenderer.GetTextureResource(mResources_MainWnd.Tex_PostProcess_BlurIntermediate);
			auto pRscBlurOutput       = mRenderer.GetTextureResource(mResources_MainWnd.Tex_PostProcess_BlurOutput);


			FPostProcessParameters::FBlurParams* pBlurParams = nullptr;

			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			ctx.mDynamicHeap_ConstantBuffer.AllocConstantBuffer(sizeof(FPostProcessParameters::FBlurParams), (void**)&pBlurParams, &cbAddr);
			pBlurParams->iImageSizeX = ctx.MainRTResolutionX;
			pBlurParams->iImageSizeY = ctx.MainRTResolutionY;

			{

				SCOPED_GPU_MARKER(pCmd, "BlurX");
				pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_X_PSO));
				pCmd->SetComputeRootSignature(mRenderer.GetRootSignature(11));

				const int FFXDispatchGroupDimension = 16;
				const     int FFXDispatchX = (InputImageWidth  + (FFXDispatchGroupDimension - 1)) / FFXDispatchGroupDimension;
				const     int FFXDispatchY = (InputImageHeight + (FFXDispatchGroupDimension - 1)) / FFXDispatchGroupDimension;

				pCmd->SetComputeRootDescriptorTable(0, srv_ColorIn.GetGPUDescHandle());
				pCmd->SetComputeRootDescriptorTable(1, uav_BlurIntermediate.GetGPUDescHandle());
				pCmd->SetComputeRootConstantBufferView(2, cbAddr);
				pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);

				const CD3DX12_RESOURCE_BARRIER pBarriers[] =
				{
					  CD3DX12_RESOURCE_BARRIER::Transition(pRscBlurIntermediate, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
					, CD3DX12_RESOURCE_BARRIER::Transition(pRscBlurOutput      , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)

				};
				pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
			}


			{
				SCOPED_GPU_MARKER(pCmd, "BlurY");
				pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_Y_PSO));
				pCmd->SetComputeRootDescriptorTable(0, srv_blurIntermediate.GetGPUDescHandle());
				pCmd->SetComputeRootDescriptorTable(1, uav_BlurOutput.GetGPUDescHandle());
				pCmd->SetComputeRootConstantBufferView(2, cbAddr);
				pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);

				const CD3DX12_RESOURCE_BARRIER pBarriers[] =
				{
					  CD3DX12_RESOURCE_BARRIER::Transition(pRscBlurOutput      , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
					, CD3DX12_RESOURCE_BARRIER::Transition(pRscBlurIntermediate, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
				};
				pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
			}
		}

		{
			SCOPED_GPU_MARKER(pCmd, "TonemapperCS");

			FPostProcessParameters::FTonemapper* pConstBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			ctx.mDynamicHeap_ConstantBuffer.AllocConstantBuffer(sizeof(FPostProcessParameters::FTonemapper), (void**)&pConstBuffer, &cbAddr);
			*pConstBuffer = PPParams.TonemapperParams;

			pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::TONEMAPPER_PSO));
			pCmd->SetComputeRootSignature(mRenderer.GetRootSignature(3)); // compute RS
			pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
			pCmd->SetComputeRootDescriptorTable(0, PP_ENABLE_BLUR_PASS ? srv_blurOutput.GetGPUDescHandle() : srv_ColorIn.GetGPUDescHandle());
			pCmd->SetComputeRootDescriptorTable(1, uav_TonemapperOut.GetGPUDescHandle());
			pCmd->SetComputeRootConstantBufferView(2, cbAddr);
			pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);
		}

		const bool bFFXCASEnabled = PPParams.IsFFXCASEnabled();
		if(bFFXCASEnabled)
		{
			const CD3DX12_RESOURCE_BARRIER pBarriers[] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
			};
			pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);

			SCOPED_GPU_MARKER(pCmd, "FFX-CAS_CS");

			FPostProcessParameters::FFFXCAS* pConstBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			ctx.mDynamicHeap_ConstantBuffer.AllocConstantBuffer(sizeof(FPostProcessParameters::FFFXCAS), (void**)&pConstBuffer, &cbAddr);
			*pConstBuffer = PPParams.FFXCASParams;

			ID3D12PipelineState* pPSO = mRenderer.GetPSO(EBuiltinPSOs::FFX_CAS_CS_PSO);
			assert(pPSO);
			pCmd->SetPipelineState(pPSO);
			pCmd->SetComputeRootSignature(mRenderer.GetRootSignature(3)); // same root signature as tonemapper
			pCmd->SetComputeRootDescriptorTable(0, srv_TonemapperOut.GetGPUDescHandle());
			pCmd->SetComputeRootDescriptorTable(1, uav_FFXCASOut.GetGPUDescHandle());
			pCmd->SetComputeRootConstantBufferView(2, cbAddr);
			
			// each FFX-CAS_CS thread processes 4 pixels.
			// workgroup is 64 threads, hence 256 (16x16) pixels are processed per thread group that is dispatched
 			constexpr int CAS_WORKGROUP_WORK_DIMENSION = 16;
			const int CASDispatchX = (InputImageWidth  + (CAS_WORKGROUP_WORK_DIMENSION - 1)) / CAS_WORKGROUP_WORK_DIMENSION;
			const int CASDispatchY = (InputImageHeight + (CAS_WORKGROUP_WORK_DIMENSION - 1)) / CAS_WORKGROUP_WORK_DIMENSION;
			pCmd->Dispatch(CASDispatchX, CASDispatchY, DispatchZ);
		}
	}
	
}

void VQEngine::RenderUI(FWindowRenderContext& ctx, const FPostProcessParameters& PPParams)
{
	const bool bUseHDRRenderPath = this->ShouldRenderHDR(mpWinMain->GetHWND());

	const float           RenderResolutionX = static_cast<float>(ctx.MainRTResolutionX);
	const float           RenderResolutionY = static_cast<float>(ctx.MainRTResolutionY);
	D3D12_VIEWPORT                  viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	const auto                      VBIBIDs = mBuiltinMeshes[EBuiltInMeshes::TRIANGLE].GetIABufferIDs();
	const BufferID&                   IB_ID = VBIBIDs.second;
	const IBV&                        ib    = mRenderer.GetIndexBufferView(IB_ID);
	ID3D12DescriptorHeap*         ppHeaps[] = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };
	D3D12_RECT                 scissorsRect { 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
	ID3D12GraphicsCommandList*&        pCmd = ctx.pCmdList_GFX;


	const bool bFFXCASEnabled = PPParams.IsFFXCASEnabled();
	const SRV& srv_ColorIn = bFFXCASEnabled 
		? mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_FFXCASOut)
		: mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_TonemapperOut);

	ID3D12Resource* pRscTonemapperOut = mRenderer.GetTextureResource(mResources_MainWnd.Tex_PostProcess_TonemapperOut);
	ID3D12Resource* pRscFFXCASOut     = mRenderer.GetTextureResource(mResources_MainWnd.Tex_PostProcess_FFXCASOut);
	ID3D12Resource* pRscInput = bFFXCASEnabled ? pRscFFXCASOut : pRscTonemapperOut;

	ID3D12Resource*          pSwapChainRT = ctx.SwapChain.GetCurrentBackBufferRenderTarget();
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = ctx.SwapChain.GetCurrentBackBufferRTVHandle();

	SCOPED_GPU_MARKER(pCmd, "SwapchainPassthrough");
	
	// Transition Input & Output resources
	// ignore the tonemapper barrier if CAS is enabeld as it'll already be issued.
	CD3DX12_RESOURCE_BARRIER SwapChainTransition = CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	if (bFFXCASEnabled)
	{
		CD3DX12_RESOURCE_BARRIER barriers[] =
		{
			  SwapChainTransition
			, CD3DX12_RESOURCE_BARRIER::Transition(pRscFFXCASOut    , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
			, CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		};
		pCmd->ResourceBarrier(_countof(barriers), barriers);
	}
	else
	{
		CD3DX12_RESOURCE_BARRIER barriers[] =
		{
			  SwapChainTransition
			, CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
			, CD3DX12_RESOURCE_BARRIER::Transition(pRscFFXCASOut    , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		};
		pCmd->ResourceBarrier(_countof(barriers), barriers);
	}
	

	// TODO: UI rendering
	//       Currently a passthrough pass fullscreen triangle pso
	pCmd->SetPipelineState(mRenderer.GetPSO(bUseHDRRenderPath ? EBuiltinPSOs::HDR_FP16_SWAPCHAIN_PSO : EBuiltinPSOs::FULLSCREEN_TRIANGLE_PSO));
	pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(1)); // hardcoded root signature for now until shader reflection and rootsignature management is implemented
	pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	pCmd->SetGraphicsRootDescriptorTable(0, srv_ColorIn.GetGPUDescHandle());

	pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmd->IASetVertexBuffers(0, 1, NULL);
	pCmd->IASetIndexBuffer(&ib);

	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);

	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);

	pCmd->DrawIndexedInstanced(3, 1, 0, 0, 0);
}

HRESULT VQEngine::PresentFrame(FWindowRenderContext& ctx)
{
	HRESULT hr = {};

	ID3D12GraphicsCommandList*& pCmd = ctx.pCmdList_GFX;
	ID3D12Resource* pSwapChainRT = ctx.SwapChain.GetCurrentBackBufferRenderTarget();

	{
		SCOPED_GPU_MARKER(pCmd, "PresentFrame");
		// Transition SwapChain for Present
		pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT
			, D3D12_RESOURCE_STATE_RENDER_TARGET
			, D3D12_RESOURCE_STATE_PRESENT)
		);
	}
	pCmd->Close();

	ID3D12CommandList* ppCommandLists[] = { pCmd };
	ctx.PresentQueue.pQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
	hr = ctx.SwapChain.Present();
	ctx.SwapChain.MoveToNextFrame();
	return hr;
}

void VQEngine::CompositUIToHDRSwapchain(FWindowRenderContext& ctx)
{
}
