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
		RenderThread_Render();

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
	// load renderer resources
	mRenderer.Load();

	// TODO: initialize scene resources
	mResources_MainWnd.DSV_MainViewDepthMSAA = mRenderer.CreateDSV();
	mResources_MainWnd.DSV_MainViewDepth     = mRenderer.CreateDSV();
	mResources_MainWnd.RTV_MainViewColorMSAA = mRenderer.CreateRTV();
	mResources_MainWnd.RTV_MainViewColor     = mRenderer.CreateRTV();
	mResources_MainWnd.SRV_MainViewColor     = mRenderer.CreateSRV();
	mResources_MainWnd.UAV_PostProcess_TonemapperOut = mRenderer.CreateUAV();
	mResources_MainWnd.SRV_PostProcess_TonemapperOut = mRenderer.CreateSRV();

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
		GeometryGenerator::GeometryData<VertexType> data = GeometryGenerator::Cylinder<VertexType>();
		mResourceNames.mBuiltinMeshNames[eMesh] = "Cylinder";
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


		{	// Main depth stencil view
			D3D12_RESOURCE_DESC d = CD3DX12_RESOURCE_DESC::Tex2D(
				DXGI_FORMAT_R32_TYPELESS
				, Width
				, Height
				, 1 // Array Size
				, 1 // MIP levels
				, 1 // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
			);
			r.Tex_MainViewDepth = mRenderer.CreateTexture("SceneDepth", d, D3D12_RESOURCE_STATE_DEPTH_WRITE);
			mRenderer.InitializeDSV(r.DSV_MainViewDepth, 0u, r.Tex_MainViewDepth);
		}
		{	// Main depth stencil view /w MSAA
			D3D12_RESOURCE_DESC d = CD3DX12_RESOURCE_DESC::Tex2D(
				DXGI_FORMAT_R32_TYPELESS
				, Width
				, Height
				, 1 // Array Size
				, 1 // MIP levels
				, MSAA_SAMPLE_COUNT // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
			);
			r.Tex_MainViewDepthMSAA = mRenderer.CreateTexture("SceneDepthMSAA", d, D3D12_RESOURCE_STATE_DEPTH_WRITE);
			mRenderer.InitializeDSV(r.DSV_MainViewDepthMSAA, 0u, r.Tex_MainViewDepthMSAA);
		}

		{ // Main render target view w/ MSAA
			D3D12_RESOURCE_DESC d = CD3DX12_RESOURCE_DESC::Tex2D(
				MainColorRTFormat
				, Width
				, Height
				, 1 // Array Size
				, 1 // MIP levels
				, MSAA_SAMPLE_COUNT // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
			);

			r.Tex_MainViewColorMSAA = mRenderer.CreateTexture("SceneColorMSAA", d, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
			mRenderer.InitializeRTV(r.RTV_MainViewColorMSAA, 0u, r.Tex_MainViewColorMSAA);
		}

		{ // MSAA resolve target
			D3D12_RESOURCE_DESC d = CD3DX12_RESOURCE_DESC::Tex2D(
				MainColorRTFormat
				, Width
				, Height
				, 1 // Array Size
				, 0 // MIP levels
				, 1 // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
			);

			r.Tex_MainViewColor = mRenderer.CreateTexture("SceneColor", d, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			mRenderer.InitializeRTV(r.RTV_MainViewColor, 0u, r.Tex_MainViewColor);
			mRenderer.InitializeSRV(r.SRV_MainViewColor, 0u, r.Tex_MainViewColor);
		}

		{ // Tonemapper UAV
			D3D12_RESOURCE_DESC d = CD3DX12_RESOURCE_DESC::Tex2D(
				MainColorRTFormat
				, Width
				, Height
				, 1 // Array Size
				, 0 // MIP levels
				, 1 // MSAA SampleCount
				, 0 // MSAA SampleQuality
				, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
			);
			r.Tex_PostProcess_TonemapperOut = mRenderer.CreateTexture("TonemapperOut", d, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			mRenderer.InitializeUAV(r.UAV_PostProcess_TonemapperOut, 0u, r.Tex_PostProcess_TonemapperOut);
			mRenderer.InitializeSRV(r.SRV_PostProcess_TonemapperOut, 0u, r.Tex_PostProcess_TonemapperOut);
		}
	}

	// TODO: generic implementation of other window procedures for load
}

void VQEngine::RenderThread_UnloadWindowSizeDependentResources(HWND hwnd)
{
	if (hwnd == mpWinMain->GetHWND())
	{
		FRenderingResources_MainWindow& r = mResources_MainWnd;

		// sync GPU
		auto& ctx = mRenderer.GetWindowRenderContext(hwnd);
		ctx.SwapChain.WaitForGPU();

		mRenderer.DestroyTexture(r.Tex_MainViewDepth);
		mRenderer.DestroyTexture(r.Tex_MainViewDepthMSAA);
		mRenderer.DestroyTexture(r.Tex_MainViewColor);
		mRenderer.DestroyTexture(r.Tex_MainViewColorMSAA);
		mRenderer.DestroyTexture(r.Tex_PostProcess_TonemapperOut);
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
}

void VQEngine::RenderThread_Render()
{
	const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCount(mpWinMain);
	const int FRAME_DATA_INDEX  = mNumRenderLoopsExecuted % NUM_BACK_BUFFERS;

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

	// TODO: remove copy paste and use encapsulation of context rendering properly
	// currently check only for hr0 for the mainWindow
	if (hr == DXGI_STATUS_OCCLUDED)
	{
		if (mpWinMain->IsFullscreen())
		{
			mpWinMain->SetFullscreen(false);
			mpWinMain->Show();
		}
	}
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
	ID3D12DescriptorHeap* ppHeaps[]         = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };
	D3D12_RECT            scissorsRect      { 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };

	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);

	pCmd->SetPipelineState(mRenderer.GetPSO(bUseHDRRenderPath ? EBuiltinPSOs::HDR_FP16_SWAPCHAIN_PSO : EBuiltinPSOs::FULLSCREEN_TRIANGLE_PSO));

	// hardcoded roog signature for now until shader reflection and rootsignature management is implemented
	pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(1));

	pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
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

HRESULT VQEngine::RenderThread_RenderMainWindow_Scene(FWindowRenderContext& ctx)
{
	HRESULT hr = S_OK;
	const int NUM_BACK_BUFFERS  = ctx.SwapChain.GetNumBackBuffers();
	const int BACK_BUFFER_INDEX = ctx.SwapChain.GetCurrentBackBufferIndex();
	const int FRAME_DATA_INDEX  = mNumRenderLoopsExecuted % NUM_BACK_BUFFERS;
	assert(ctx.mCommandAllocatorsGFX.size() >= NUM_BACK_BUFFERS);
	const bool bUseHDRRenderPath = this->ShouldRenderHDR(mpWinMain->GetHWND());
	// ----------------------------------------------------------------------------


	//
	// PRE RENDER
	//

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

	ID3D12GraphicsCommandList*& pCmd = ctx.pCmdList_GFX;

	//
	// RENDER
	//
	TransitionForSceneRendering(ctx);

	RenderShadowMaps(ctx);

	RenderSceneColor(ctx, mpScene->GetSceneView(FRAME_DATA_INDEX));
	
	ResolveMSAA(ctx);

	TransitionForPostProcessing(ctx);

	RenderPostProcess(ctx, mpScene->GetPostProcessParameters(FRAME_DATA_INDEX));

	RenderUI(ctx);

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

void VQEngine::TransitionForSceneRendering(FWindowRenderContext& ctx)
{
	const bool& bMSAA = mSettings.gfx.bAntiAliasing;
	ID3D12GraphicsCommandList*& pCmd = ctx.pCmdList_GFX;

	auto pRscTonemapper = mRenderer.GetTextureResource(mResources_MainWnd.Tex_PostProcess_TonemapperOut);
	auto pRscColor = mRenderer.GetTextureResource(mResources_MainWnd.Tex_MainViewColor);
	auto pRscColorMSAA = mRenderer.GetTextureResource(mResources_MainWnd.Tex_MainViewColorMSAA);

	SCOPED_GPU_MARKER(pCmd, "TransitionForSceneRendering");

	if (bMSAA)
	{
		const CD3DX12_RESOURCE_BARRIER pBarriers[] =
		{
			 CD3DX12_RESOURCE_BARRIER::Transition(pRscColorMSAA , D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
		};
		pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
	}
	else
	{
		const CD3DX12_RESOURCE_BARRIER pBarriers[] =
		{
			  CD3DX12_RESOURCE_BARRIER::Transition(pRscColor , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
		};
		pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
	}
}

void VQEngine::RenderShadowMaps(FWindowRenderContext& ctx)
{
	ID3D12GraphicsCommandList*& pCmd = ctx.pCmdList_GFX;
	//PIXBeginEvent(pCmd, VQ_PIXEventColor, "TransitionForSceneRendering");
	////TODO
	//PIXEndEvent(pCmd);
}

void VQEngine::RenderSceneColor(FWindowRenderContext& ctx, const FSceneView& SceneView)
{
	using namespace DirectX;

	const bool& bMSAA = mSettings.gfx.bAntiAliasing;
	ID3D12GraphicsCommandList*& pCmd = ctx.pCmdList_GFX;

	const RTV& rtv = mRenderer.GetRTV(bMSAA ? mResources_MainWnd.RTV_MainViewColorMSAA : mResources_MainWnd.RTV_MainViewColor);
	const DSV& dsv = mRenderer.GetDSV(bMSAA ? mResources_MainWnd.DSV_MainViewDepthMSAA : mResources_MainWnd.DSV_MainViewDepth);


	SCOPED_GPU_MARKER(pCmd, "RenderSceneColor");

	// Clear Depth & Render targets
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtv.GetCPUDescHandle();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsv.GetCPUDescHandle();
	D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	pCmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, nullptr);

	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);


	// Set Viewport & Scissors
	const float RenderResolutionX = static_cast<float>(ctx.MainRTResolutionX);
	const float RenderResolutionY = static_cast<float>(ctx.MainRTResolutionY);
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);

	pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA ? EBuiltinPSOs::FORWARD_LIGHTING_MSAA_4 : EBuiltinPSOs::FORWARD_LIGHTING));
	pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(5)); // hardcoded root signature for now until shader reflection and rootsignature management is implemented

	ID3D12DescriptorHeap* ppHeaps[] = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };

	using namespace VQ_SHADER_DATA;
	// set PerFrame constants
	{
		constexpr UINT PerFrameRSBindSlot = 3;
		PerFrameData* pPerFrame = {};
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		ctx.mDynamicHeap_ConstantBuffer.AllocConstantBuffer(sizeof(PerFrameData), (void**)(&pPerFrame), &cbAddr);

		assert(pPerFrame);
		pPerFrame->Lights = SceneView.GPULightingData;
		
		//pCmd->SetGraphicsRootDescriptorTable(PerFrameRSBindSlot, );
		pCmd->SetGraphicsRootConstantBufferView(PerFrameRSBindSlot, cbAddr);
	}

	// set PerView constants
	{
		constexpr UINT PerViewRSBindSlot = 2;
		PerViewData* pPerView = {};
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		ctx.mDynamicHeap_ConstantBuffer.AllocConstantBuffer(sizeof(decltype(pPerView)), (void**)(&pPerView), &cbAddr);

		assert(pPerView);
		XMStoreFloat3(&pPerView->CameraPosition, SceneView.cameraPosition);
		
		// TODO: PreView data

		//pCmd->SetGraphicsRootDescriptorTable(PerViewRSBindSlot, D3D12_GPU_DESCRIPTOR_HANDLE{ cbAddr });
		pCmd->SetGraphicsRootConstantBufferView(PerViewRSBindSlot, cbAddr);
	}

	// Draw Objects -----------------------------------------------
	constexpr UINT PerObjRSBindSlot = 0;
	{
		SCOPED_GPU_MARKER(pCmd, "Geometry");
		pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		for (const FMeshRenderCommand& meshRenderCmd : SceneView.meshRenderCommands)
		{
			const Material& mat = mpScene->GetMaterial(meshRenderCmd.matID);

			// set constant buffer data
			PerObjectData* pPerObj = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			ctx.mDynamicHeap_ConstantBuffer.AllocConstantBuffer(sizeof(decltype(pPerObj)), (void**)(&pPerObj), &cbAddr);


			pPerObj->matWorldViewProj = meshRenderCmd.WorldTransformationMatrix * SceneView.viewProj;
			pPerObj->matWorld         = meshRenderCmd.WorldTransformationMatrix;
			XMStoreFloat3x3(&pPerObj->matNormal, meshRenderCmd.NormalTransformationMatrix);

			pCmd->SetGraphicsRootConstantBufferView(1, cbAddr);


			// set textures
			if (mat.SRVMaterialMaps != INVALID_ID)
				pCmd->SetGraphicsRootDescriptorTable(0, mRenderer.GetSRV(mat.SRVMaterialMaps).GetGPUDescHandle(0));


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

	// Draw Environment Map ---------------------------------------
	const bool bHasEnvironmentMapHDRTexture = mResources_MainWnd.EnvironmentMap.SRV_HDREnvironment != INVALID_ID;
	const bool bDrawEnvironmentMap = bHasEnvironmentMapHDRTexture && true;
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
	auto pRscColor     = mRenderer.GetTextureResource(mResources_MainWnd.Tex_MainViewColor);
	auto pRscColorMSAA = mRenderer.GetTextureResource(mResources_MainWnd.Tex_MainViewColorMSAA);

	{
		const CD3DX12_RESOURCE_BARRIER pBarriers[] =
		{
				  CD3DX12_RESOURCE_BARRIER::Transition(pRscColorMSAA , D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE)
				, CD3DX12_RESOURCE_BARRIER::Transition(pRscColor     , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST)
		};
		pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
	}

	constexpr DXGI_FORMAT ResolveFormat = DXGI_FORMAT_R16G16B16A16_FLOAT; // TODO: infer this from scene resources
	pCmd->ResolveSubresource(pRscColor, 0, pRscColorMSAA, 0, ResolveFormat);
}

void VQEngine::TransitionForPostProcessing(FWindowRenderContext& ctx)
{
	const bool& bMSAA = mSettings.gfx.bAntiAliasing;
	ID3D12GraphicsCommandList*& pCmd = ctx.pCmdList_GFX;

	auto pRscInput  = mRenderer.GetTextureResource(mResources_MainWnd.Tex_MainViewColor);
	auto pRscOutput = mRenderer.GetTextureResource(mResources_MainWnd.Tex_PostProcess_TonemapperOut);

	SCOPED_GPU_MARKER(pCmd, "TransitionForPostProcessing");
	const CD3DX12_RESOURCE_BARRIER pBarriers[] =
	{
		  CD3DX12_RESOURCE_BARRIER::Transition(pRscInput , (bMSAA ? D3D12_RESOURCE_STATE_RESOLVE_DEST : D3D12_RESOURCE_STATE_RENDER_TARGET), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscOutput, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
	};
	pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
}

void VQEngine::RenderPostProcess(FWindowRenderContext& ctx, const FPostProcessParameters& PPParams)
{
	ID3D12DescriptorHeap*       ppHeaps[] = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };
	ID3D12GraphicsCommandList*&      pCmd = ctx.pCmdList_GFX;

	// pass io
	const SRV& srv_ColorIn  = mRenderer.GetSRV(mResources_MainWnd.SRV_MainViewColor);
	const UAV& uav_ColorOut = mRenderer.GetUAV(mResources_MainWnd.UAV_PostProcess_TonemapperOut);

	FPostProcessParameters* pConstBuffer = {};
	D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
	ctx.mDynamicHeap_ConstantBuffer.AllocConstantBuffer(sizeof(FPostProcessParameters), (void**)&pConstBuffer, &cbAddr);
	*pConstBuffer = PPParams;

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
	pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::TONEMAPPER_PSO));
	pCmd->SetComputeRootSignature(mRenderer.GetRootSignature(3)); // compute RS
	pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	pCmd->SetComputeRootDescriptorTable(0, srv_ColorIn.GetGPUDescHandle());
	pCmd->SetComputeRootDescriptorTable(1, uav_ColorOut.GetGPUDescHandle());
	pCmd->SetComputeRootConstantBufferView(2, cbAddr);
	pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);
}

void VQEngine::RenderUI(FWindowRenderContext& ctx)
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


	const SRV&                srv_ColorIn = mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_TonemapperOut);
	ID3D12Resource*          pSwapChainRT = ctx.SwapChain.GetCurrentBackBufferRenderTarget();
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = ctx.SwapChain.GetCurrentBackBufferRTVHandle();

	SCOPED_GPU_MARKER(pCmd, "SwapchainPassthrough");
	// Transition Input & Output resources
	auto pRscTonemapperOut = mRenderer.GetTextureResource(mResources_MainWnd.Tex_PostProcess_TonemapperOut);
	CD3DX12_RESOURCE_BARRIER barriers[] =
	{
		    CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT      , D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)
		  , CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
	};
	pCmd->ResourceBarrier(_countof(barriers), barriers);

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
