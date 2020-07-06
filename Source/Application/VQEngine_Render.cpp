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

#include <d3d12.h>
#include <dxgi.h>


// ------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// MAIN
//
// ------------------------------------------------------------------------------------------------------------------------------------------------------------
void VQEngine::RenderThread_Main()
{
	Log::Info("RenderThread_Main()");
	RenderThread_Inititalize();


	bool bQuit = false;
	while (!this->mbStopAllThreads && !bQuit)
	{
		RenderThread_HandleEvents();

		RenderThread_WaitForUpdateThread();

#if DEBUG_LOG_THREAD_SYNC_VERBOSE
		Log::Info(/*"RenderThread_Tick() : */"r%d (u=%llu)", mNumRenderLoopsExecuted.load(), mNumUpdateLoopsExecuted.load());
#endif

		RenderThread_PreRender();
		RenderThread_Render();

		++mNumRenderLoopsExecuted;

		RenderThread_SignalUpdateThread();

		RenderThread_HandleEvents();
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
void VQEngine::RenderThread_Inititalize()
{
	FRendererInitializeParameters params = {};
	params.Settings = mSettings.gfx;
	params.Windows.push_back(FWindowRepresentation(mpWinMain , mSettings.gfx.bVsync, mSettings.WndMain.DisplayMode == EDisplayMode::EXCLUSIVE_FULLSCREEN));
	if(mpWinDebug) params.Windows.push_back(FWindowRepresentation(mpWinDebug, false, false));

	mRenderer.Initialize(params);

	auto fnHandleWindowTransitions = [&](std::unique_ptr<Window>& pWin, const FWindowSettings& settings)
	{
		if (!pWin) return;

		// TODO: generic solution to multi window/display settings. 
		//       For now, simply prevent debug wnd occupying main wnd's display.
		if ( mpWinMain->IsFullscreen()
			&& (mSettings.WndMain.PreferredDisplay == mSettings.WndDebug.PreferredDisplay)
			&& settings.IsDisplayModeFullscreen()
			&& pWin != mpWinMain)
		{
			Log::Warning("Debug window and Main window cannot be Fullscreen on the same display!");
			pWin->SetFullscreen(false);
			// TODO: as a more graceful fallback, move it to the next monitor and keep fullscreen
			return;
		}

		// Borderless fullscreen transitions are handled through Window object
		// Exclusive  fullscreen transitions are handled through the Swapchain
		if (settings.DisplayMode == EDisplayMode::BORDERLESS_FULLSCREEN)
		{
			// TODO: preferred screen impl
			if(pWin) pWin->ToggleWindowedFullscreen();
		}
	};

	fnHandleWindowTransitions(mpWinMain , mSettings.WndMain);
	fnHandleWindowTransitions(mpWinDebug, mSettings.WndDebug);

	mbRenderThreadInitialized.store(true);
	mNumRenderLoopsExecuted.store(0);

	InitializeBuiltinMeshes();
	mRenderer.Load();
}

void VQEngine::RenderThread_Exit()
{
	mRenderer.Unload();
	mRenderer.Exit();
}

void VQEngine::InitializeBuiltinMeshes()
{
	{
		GeometryGenerator::GeometryData<FVertexWithColorAndAlpha> data = GeometryGenerator::Triangle<FVertexWithColorAndAlpha>(1.0f);
		mBuiltinMeshNames[EBuiltInMeshes::TRIANGLE] = "Triangle";
		mBuiltinMeshes[EBuiltInMeshes::TRIANGLE] = Mesh(&mRenderer, data.Vertices, data.Indices, mBuiltinMeshNames[EBuiltInMeshes::TRIANGLE]);
	}

#if 0
	{
		GeometryGenerator::GeometryData<FVertexWithColorAndAlpha> data = GeometryGenerator::Cube<FVertexWithColorAndAlpha>();
		mBuiltinMeshNames[EBuiltInMeshes::CUBE] = "Cube";
		mBuiltinMeshes[EBuiltInMeshes::CUBE] = Mesh(&mRenderer, data.Vertices, data.Indices, mBuiltinMeshNames[EBuiltInMeshes::CUBE]);
	}
	// ...
#endif
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
	const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCountOfWindow(mpWinMain);
	const int FRAME_DATA_INDEX  = mNumRenderLoopsExecuted % NUM_BACK_BUFFERS;

	RenderThread_RenderMainWindow();
	RenderThread_RenderDebugWindow();
}


void VQEngine::RenderThread_RenderMainWindow()
{
	const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCountOfWindow(mpWinMain);
	const int FRAME_DATA_INDEX = mNumRenderLoopsExecuted % NUM_BACK_BUFFERS;

	HRESULT hr = S_OK; 

	FWindowRenderContext& ctx = mRenderer.GetWindowRenderContext(mpWinMain->GetHWND());
	hr = mbLoadingLevel
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
	ID3D12GraphicsCommandList* pCmd = ctx.pCmdList_GFX;

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

	// Draw Triangle

	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);

	pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::HELLO_WORLD_TRIANGLE_PSO));
	pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(EVertexBufferType::COLOR_AND_ALPHA));

#if 0
	//pCmd->SetDescriptorHeaps(_countof(), NULL);
	//pCmd->SetGraphicsRootDescriptorTable(0, g_MainDescriptorHeap[g_FrameIndex]->GetGPUDescriptorHandleForHeapStart()))
	//pCmd->SetGraphicsRootDescriptorTable(2, g_MainDescriptorHeap[g_FrameIndex]->GetGPUDescriptorHandleForHeapStart()))
	//pCmd->SetGraphicsRootConstantBufferView(1, )
#endif

	const float RenderResolutionX = static_cast<float>(ctx.MainRTResolutionX);
	const float RenderResolutionY = static_cast<float>(ctx.MainRTResolutionY);
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	pCmd->RSSetViewports(1, &viewport);

	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
	pCmd->RSSetScissorRects(1, &scissorsRect);

	const auto VBIBIDs = mBuiltinMeshes[EBuiltInMeshes::TRIANGLE].GetIABuffers();
	const BufferID& VB_ID = VBIBIDs.first;
	const BufferID& IB_ID = VBIBIDs.second;
	const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
	const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

	pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmd->IASetVertexBuffers(0, 1, &vb);
	pCmd->IASetIndexBuffer(&ib);

	pCmd->DrawIndexedInstanced(3, 1, 0, 0, 0);


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
	hr = ctx.SwapChain.Present(ctx.bVsync);
	ctx.SwapChain.MoveToNextFrame();
	//return hr;
}

HRESULT VQEngine::RenderThread_RenderMainWindow_LoadingScreen(FWindowRenderContext& ctx)
{
	HRESULT hr = S_OK;
	const int NUM_BACK_BUFFERS = ctx.SwapChain.GetNumBackBuffers();
	const int BACK_BUFFER_INDEX = ctx.SwapChain.GetCurrentBackBufferIndex();
	const int FRAME_DATA_INDEX = mNumRenderLoopsExecuted % NUM_BACK_BUFFERS;
	const FFrameData& FrameData = mScene_MainWnd.mFrameData[FRAME_DATA_INDEX];
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
	ID3D12GraphicsCommandList* pCmd = ctx.pCmdList_GFX;

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

	// Draw Triangle
	const float           RenderResolutionX = static_cast<float>(ctx.MainRTResolutionX);
	const float           RenderResolutionY = static_cast<float>(ctx.MainRTResolutionY);
	D3D12_VIEWPORT        viewport          { 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	const auto            VBIBIDs           = mBuiltinMeshes[EBuiltInMeshes::TRIANGLE].GetIABuffers();
	const BufferID&       IB_ID             = VBIBIDs.second;
	const IBV&            ib                = mRenderer.GetIndexBufferView(IB_ID);
	ID3D12DescriptorHeap* ppHeaps[]         = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };
	D3D12_RECT            scissorsRect      { 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };

	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);

	pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::LOADING_SCREEN_PSO));
	pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(EVertexBufferType::DEFAULT));

	pCmd->SetDescriptorHeaps(1, ppHeaps);
	pCmd->SetGraphicsRootDescriptorTable(0, mRenderer.GetShaderResourceView(0).GetGPUDescHandle(0));
#if 0
	//pCmd->SetGraphicsRootDescriptorTable(2, g_MainDescriptorHeap[g_FrameIndex]->GetGPUDescriptorHandleForHeapStart()))
	//pCmd->SetGraphicsRootConstantBufferView(1, )
#endif

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
	hr = ctx.SwapChain.Present(ctx.bVsync);
	ctx.SwapChain.MoveToNextFrame();
	return hr;
}

HRESULT VQEngine::RenderThread_RenderMainWindow_Scene(FWindowRenderContext& ctx)
{
	HRESULT hr = S_OK;
	const int NUM_BACK_BUFFERS  = ctx.SwapChain.GetNumBackBuffers();
	const int BACK_BUFFER_INDEX = ctx.SwapChain.GetCurrentBackBufferIndex();
	const int FRAME_DATA_INDEX  = mNumRenderLoopsExecuted % NUM_BACK_BUFFERS;
	const FFrameData& FrameData = mScene_MainWnd.mFrameData[FRAME_DATA_INDEX];
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
	ID3D12GraphicsCommandList* pCmd = ctx.pCmdList_GFX;

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

	// Draw Triangle

	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);

	pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::HELLO_WORLD_TRIANGLE_PSO));
	pCmd->SetGraphicsRootSignature(mRenderer.GetRootSignature(EVertexBufferType::COLOR_AND_ALPHA));

#if 0
	//pCmd->SetDescriptorHeaps(_countof(), NULL);
	//pCmd->SetGraphicsRootDescriptorTable(0, g_MainDescriptorHeap[g_FrameIndex]->GetGPUDescriptorHandleForHeapStart()))
	//pCmd->SetGraphicsRootDescriptorTable(2, g_MainDescriptorHeap[g_FrameIndex]->GetGPUDescriptorHandleForHeapStart()))
	//pCmd->SetGraphicsRootConstantBufferView(1, )
#endif

	const float RenderResolutionX = static_cast<float>(ctx.MainRTResolutionX);
	const float RenderResolutionY = static_cast<float>(ctx.MainRTResolutionY);
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	pCmd->RSSetViewports(1, &viewport);

	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
	pCmd->RSSetScissorRects(1, &scissorsRect);

	const auto VBIBIDs = mBuiltinMeshes[EBuiltInMeshes::TRIANGLE].GetIABuffers();
	const BufferID& VB_ID = VBIBIDs.first;
	const BufferID& IB_ID = VBIBIDs.second;
	const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
	const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

	pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmd->IASetVertexBuffers(0, 1, &vb);
	pCmd->IASetIndexBuffer(&ib);

	pCmd->DrawIndexedInstanced(3, 1, 0, 0, 0);


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
	hr = ctx.SwapChain.Present(ctx.bVsync);
	ctx.SwapChain.MoveToNextFrame();
	return hr;
}



// ------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// EVENT HANDLING
//
// ------------------------------------------------------------------------------------------------------------------------------------------------------------
void VQEngine::RenderThread_HandleEvents()
{
	// Swap event recording buffers so we can read & process a limited number of events safely.
	//   Otherwise, theoretically the producer (Main) thread could keep adding new events 
	//   while we're spinning on the queue items below, and cause render thread to stall while, say, resizing.
	mWinEventQueue.SwapBuffers();
	std::queue<std::unique_ptr<IEvent>>& q = mWinEventQueue.GetBackContainer();
	if (q.empty())
		return;

	// process the events
	std::shared_ptr<IEvent> pEvent = nullptr;
	std::shared_ptr<WindowResizeEvent> pResizeEvent = nullptr;
	while (!q.empty())
	{
		pEvent = std::move(q.front());
		q.pop();

		switch (pEvent->mType)
		{
		case EEventType::WINDOW_RESIZE_EVENT: 
			// noop, we only care about the last RESIZE event to avoid calling SwapchainResize() unneccessarily
			pResizeEvent = std::static_pointer_cast<WindowResizeEvent>(pEvent);

			break;
		case EEventType::TOGGLE_FULLSCREEN_EVENT:
			// handle every fullscreen event
			RenderThread_HandleToggleFullscreenEvent(pEvent.get());
			break;
		}
	}
	// Process Window Resize
	if (pResizeEvent)
	{
		RenderThread_HandleResizeWindowEvent(pResizeEvent.get());
	}
	
}

void VQEngine::RenderThread_HandleResizeWindowEvent(const IEvent* pEvent)
{
	const WindowResizeEvent* pResizeEvent = static_cast<const WindowResizeEvent*>(pEvent);
	const HWND&                      hwnd = pResizeEvent->hwnd;
	const int                       WIDTH = pResizeEvent->width;
	const int                      HEIGHT = pResizeEvent->height;
	SwapChain&                  Swapchain = mRenderer.GetWindowSwapChain(hwnd);
	std::unique_ptr<Window>&         pWnd = GetWindow(hwnd);

	Log::Info("RenderThread: Handle Resize event, set resolution to %dx%d", WIDTH , HEIGHT);

	Swapchain.WaitForGPU();
	Swapchain.Resize(WIDTH, HEIGHT);
	pWnd->OnResize(WIDTH, HEIGHT);
	mRenderer.OnWindowSizeChanged(hwnd, WIDTH, HEIGHT);
}

void VQEngine::RenderThread_HandleToggleFullscreenEvent(const IEvent* pEvent)
{
	const ToggleFullscreenEvent* pToggleFSEvent = static_cast<const ToggleFullscreenEvent*>(pEvent);
	HWND                                   hwnd = pToggleFSEvent->hwnd;
	SwapChain&                        Swapchain = mRenderer.GetWindowSwapChain(pToggleFSEvent->hwnd);
	const FWindowSettings&          WndSettings = GetWindowSettings(hwnd);
	const bool   bExclusiveFullscreenTransition = WndSettings.DisplayMode == EDisplayMode::EXCLUSIVE_FULLSCREEN;
	const bool            bFullscreenStateToSet = !Swapchain.IsFullscreen();
	std::unique_ptr<Window>&               pWnd = GetWindow(hwnd);

	Log::Info("RenderThread: Handle Fullscreen(exclusiveFS=%s) transition to %dx%d"
		, (bFullscreenStateToSet ? "true" : "false")
		, WndSettings.Width
		, WndSettings.Height
	);

	// if we're transitioning into Fullscreen, save the current window dimensions
	if (bFullscreenStateToSet)
	{
		FWindowSettings& WndSettings_ = GetWindowSettings(hwnd);
		WndSettings_.Width  = pWnd->GetWidth();
		WndSettings_.Height = pWnd->GetHeight();
	}

	Swapchain.WaitForGPU(); // make sure GPU is finished


	//
	// EXCLUSIVE FULLSCREEN
	//
	if (bExclusiveFullscreenTransition)
	{
		// Swapchain handles resizing the window through SetFullscreenState() call
		Swapchain.SetFullscreen(bFullscreenStateToSet, WndSettings.Width, WndSettings.Height);

		// TODO: capture/release mouse
		
		if (!bFullscreenStateToSet)
		{
			// If the Swapchain is created in fullscreen mode, the WM_PAINT message will not be 
			// received upon switching to windowed mode (ALT+TAB or ALT+ENTER) and the window
			// will be visible, but not interactable and also not visible in taskbar.
			// Explicitly calling Show() here fixes the situation.
			pWnd->Show();
		}
	}

	//
	// BORDERLESS FULLSCREEN
	//
	else 
	{
		GetWindow(hwnd)->ToggleWindowedFullscreen(&Swapchain);
	}
}

