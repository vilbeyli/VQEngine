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

#include "Shaders/LightingConstantBufferData.h"

#include "Engine/Scene/Scene.h"
#include "Engine/Scene/MeshGenerator.h"
#include "Engine/Core/Window.h"

#include "Libs/VQUtils/Include/utils.h"

#include "Renderer/Renderer.h"
#include "Renderer/Resources/DXGIUtils.h"


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

	{
		SCOPED_CPU_MARKER("RenderThread_RenderFrame()");

		// Render window contexts
		RenderThread_RenderMainWindow();

		if (mpWinDebug && !mpWinDebug->IsClosed())
		{
			RenderThread_RenderDebugWindow();
		}
	}

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
	SCOPED_CPU_MARKER_C("RenderThread_Inititalize()", 0xFF007700);
	{
		SCOPED_CPU_MARKER_C("WAIT_MAIN_WINDOW_CREATE", 0xFF0000FF);
		mSignalMainWindowCreated.Wait();
	}
	HWND hwndMain = mpWinMain->GetHWND();
	const bool bExclusiveFullscreen_MainWnd = CheckInitialSwapchainResizeRequired(mInitialSwapchainResizeRequiredWindowLookup, mSettings.WndMain, hwndMain);

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	mNumRenderLoopsExecuted.store(0);
#endif

#if THREADED_CTX_INIT
	mWorkers_Simulation.AddTask([=]() 
	{
		RENDER_WORKER_CPU_MARKER;
#endif
		// Initialize swapchains for each rendering window
		// all windows use the same number of swapchains as the main window
		const int NUM_SWAPCHAIN_BUFFERS = mSettings.gfx.Display.bUseTripleBuffering ? 3 : 2;
		{
			SCOPED_CPU_MARKER("mpWinMainInitContext");
			const bool bIsContainingWindowOnHDRScreen = VQSystemInfo::FMonitorInfo::CheckHDRSupport(hwndMain);
			const bool bCreateHDRSwapchain = mSettings.WndMain.bEnableHDR && bIsContainingWindowOnHDRScreen;
			if (mSettings.WndMain.bEnableHDR && !bIsContainingWindowOnHDRScreen)
			{
				Log::Warning("RenderThread_Initialize(): HDR Swapchain requested, but the containing monitor does not support HDR. Falling back to SDR Swapchain, and will enable HDR swapchain when the window is moved to a HDR-capable display");
			}

			mpRenderer->InitializeRenderContext(mpWinMain.get(), NUM_SWAPCHAIN_BUFFERS, mSettings.gfx.Display.bVsync, bCreateHDRSwapchain, mSettings.gfx.bUseSeparateSubmissionQueue);
			mEventQueue_VQEToWin_Main.AddItem(std::make_shared<HandleWindowTransitionsEvent>(hwndMain));
		}
		if(mpWinDebug)
		{
			SCOPED_CPU_MARKER("mpWinDebugInitContext");
			const bool bIsContainingWindowOnHDRScreen = VQSystemInfo::FMonitorInfo::CheckHDRSupport(mpWinDebug->GetHWND());
			constexpr bool bCreateHDRSwapchain = false; // only main window in HDR for now
			mpRenderer->InitializeRenderContext(mpWinDebug.get(), NUM_SWAPCHAIN_BUFFERS, false, bCreateHDRSwapchain, mSettings.gfx.bUseSeparateSubmissionQueue);
			mEventQueue_VQEToWin_Main.AddItem(std::make_shared<HandleWindowTransitionsEvent>(mpWinDebug->GetHWND()));
		}
#if THREADED_CTX_INIT
	});
#endif

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	mbRenderThreadInitialized.store(true);
#endif
	 
	// load builtin resources, compile shaders, load PSOs
	mWorkers_Simulation.AddTask([=]() { RENDER_WORKER_CPU_MARKER; mpRenderer->Load(); });
	mWorkers_Simulation.AddTask([=]() { RENDER_WORKER_CPU_MARKER; GenerateBuiltinMeshes(); });
	mWorkers_Simulation.AddTask([=]() { RENDER_WORKER_CPU_MARKER; LoadLoadingScreenData(); });
	mWorkers_Simulation.AddTask([=]() 
	{
		RENDER_WORKER_CPU_MARKER;
		{
			SCOPED_CPU_MARKER_C("InitFences", 0xFF007700);
#if THREADED_CTX_INIT
			mpRenderer->WaitMainSwapchainReady();
#endif
			mpRenderer->InitializeFences(mpWinMain->GetHWND());
		}
		// load window resources
		const bool bFullscreen = mpWinMain->IsFullscreen();
		const int W = bFullscreen ? mpWinMain->GetFullscreenWidth() : mpWinMain->GetWidth();
		const int H = bFullscreen ? mpWinMain->GetFullscreenHeight() : mpWinMain->GetHeight();
		const float fResolutionScale = 1.0f; // Post process parameters are not initialized at this stage to determine the resolution scale

		mpRenderer->LoadWindowSizeDependentResources(hwndMain, W, H, fResolutionScale, this->ShouldRenderHDR(hwndMain));
	});
}

void VQEngine::RenderThread_Exit()
{
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	mpSemUpdate->Signal();
#endif

	const int NumBackBuffers = mpRenderer->GetSwapChainBackBufferCount(mpWinMain);
	mpRenderer->DestroyFences(mpWinMain->GetHWND());
}

constexpr int NUM_TESSELLATION_GEOMETRY = 5;
void VQEngine::GenerateBuiltinMeshes()
{
	SCOPED_CPU_MARKER("GenerateBuiltinMeshes");
	{
		SCOPED_CPU_MARKER("RegisterNames");
		mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::TRIANGLE] = "Triangle";
		mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::CUBE] = "Cube";
		mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::CYLINDER] = "Cylinder";
		mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::SPHERE] = "Sphere";
		mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::CONE] = "Cone";
		mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::GRID_SIMPLE_QUAD] = "SimpleGrid";
		mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::GRID_DETAILED_QUAD0] = "DetaildGrid0";
		mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::GRID_DETAILED_QUAD1] = "DetaildGrid1";
		mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::GRID_DETAILED_QUAD2] = "DetaildGrid2";
		mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::TESSELLATION_CONTROL_POINTS__QUAD1] = "TessellationGrid_Quad1";
		mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::TESSELLATION_CONTROL_POINTS__QUAD4] = "TessellationGrid_Quad4";
		mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::TESSELLATION_CONTROL_POINTS__QUAD9] = "TessellationGrid_Quad9";
		mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::TESSELLATION_CONTROL_POINTS__QUAD16] = "TessellationGrid_Quad16";
		mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::TESSELLATION_CONTROL_POINTS__QUAD25] = "TessellationGrid_Quad25";
	}


	using VertexType = FVertexWithNormalAndTangent;
	using PatchVertexType = FVertexDefault;

	GeometryData<VertexType> dataTri = GeometryGenerator::Triangle<VertexType>(1.0f);
	GeometryData<VertexType> dataCube = GeometryGenerator::Cube<VertexType>();
	GeometryData<VertexType> dataCylinder = GeometryGenerator::Cylinder<VertexType>(3.0f, 1.0f, 1.0f, 45, 6, 4);
	GeometryData<VertexType> dataSphere = GeometryGenerator::Sphere<VertexType>(1.0f, 30, 30, 5);

	const float GridLength = 1.0f;
	GeometryData<VertexType> dataCone = GeometryGenerator::Cone<VertexType>(1, 1, 42, 4);
	GeometryData<VertexType> dataSimpleGrid = GeometryGenerator::Grid<VertexType>(GridLength, GridLength, 2, 2, 1);
	GeometryData<VertexType> dataDetailGrid0 = GeometryGenerator::Grid<VertexType>(GridLength, GridLength, 3, 3, 1);
	GeometryData<VertexType> dataDetailGrid1 = GeometryGenerator::Grid<VertexType>(GridLength, GridLength, 12, 12, 4);
	GeometryData<VertexType> dataDetailGrid2 = GeometryGenerator::Grid<VertexType>(GridLength, GridLength, 1200, 1200, 6);

	{
		mBuiltinMeshes[EBuiltInMeshes::TRIANGLE] = Mesh(nullptr, std::move(dataTri     ), mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::TRIANGLE]);
		mBuiltinMeshes[EBuiltInMeshes::CUBE    ] = Mesh(nullptr, std::move(dataCube    ), mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::CUBE]);
		mBuiltinMeshes[EBuiltInMeshes::CYLINDER] = Mesh(nullptr, std::move(dataCylinder), mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::CYLINDER]);
		mBuiltinMeshes[EBuiltInMeshes::SPHERE  ] = Mesh(nullptr, std::move(dataSphere  ), mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::SPHERE]);
		mBuiltinMeshes[EBuiltInMeshes::CONE    ] = Mesh(nullptr, std::move(dataCone    ), mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::CONE]);

		mBuiltinMeshes[EBuiltInMeshes::GRID_SIMPLE_QUAD   ] = Mesh(nullptr, std::move(dataSimpleGrid), mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::GRID_SIMPLE_QUAD]);
		mBuiltinMeshes[EBuiltInMeshes::GRID_DETAILED_QUAD0] = Mesh(nullptr, std::move(dataDetailGrid0), mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::GRID_DETAILED_QUAD0]);
		mBuiltinMeshes[EBuiltInMeshes::GRID_DETAILED_QUAD1] = Mesh(nullptr, std::move(dataDetailGrid1), mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::GRID_DETAILED_QUAD1]);
		mBuiltinMeshes[EBuiltInMeshes::GRID_DETAILED_QUAD2] = Mesh(nullptr, std::move(dataDetailGrid2), mResourceNames.mBuiltinMeshNames[EBuiltInMeshes::GRID_DETAILED_QUAD2]);

		for (int i = 0; i < NUM_TESSELLATION_GEOMETRY; ++i)
		{
			mBuiltinMeshes[TESSELLATION_CONTROL_POINTS__QUAD1 + i] = Mesh(nullptr, std::move(GeometryGenerator::TessellationPatch_Quad<PatchVertexType>(1+i)), mResourceNames.mBuiltinMeshNames[TESSELLATION_CONTROL_POINTS__QUAD1 + i]);
		}
	}

	mbBuiltinMeshGenFinished.store(true);
	mBuiltinMeshGenSignal.NotifyAll();
}

void VQEngine::FinalizeBuiltinMeshes()
{
	SCOPED_CPU_MARKER("FinalizeBuiltinMeshes");
	mpRenderer->WaitHeapsInitialized();
	WaitForBuiltinMeshGeneration();

	for (Mesh& mesh : mBuiltinMeshes)
	{
		SCOPED_CPU_MARKER("CreateBuffers");
		mesh.CreateBuffers(mpRenderer.get());
	}

	{
		SCOPED_CPU_MARKER("Upload");
		mpRenderer->UploadVertexAndIndexBufferHeaps();
	}

	if (!mbBuiltinMeshUploadFinished)
	{
		mBuiltinMeshUploadedLatch.count_down();
		mbBuiltinMeshUploadFinished = true;
	}
}

void VQEngine::WaitForBuiltinMeshGeneration()
{
	SCOPED_CPU_MARKER_C("WaitForBuiltinMeshGeneration", 0xFFAA0000);
	mBuiltinMeshGenSignal.Wait([&]() { return mbBuiltinMeshGenFinished.load(); });
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// RENDER
//
// ------------------------------------------------------------------------------------------------------------------------------------------------------------

void VQEngine::RenderThread_RenderMainWindow()
{
	SCOPED_CPU_MARKER("RenderThread_RenderMainWindow()");

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	const int FRAME_DATA_INDEX = mNumRenderLoopsExecuted % NUM_BACK_BUFFERS;
	ThreadPool& WorkerThreads = mWorkers_Render;
#else
	const int FRAME_DATA_INDEX = 0;
	ThreadPool& WorkerThreads = mWorkers_Simulation;
#endif

	// TODO: remove this hack, properly sync
	if (!mpScene)
		return;

	const FSceneView& SceneView = mpScene->GetSceneView(FRAME_DATA_INDEX);
	const FSceneShadowViews& SceneShadowView = mpScene->GetShadowView(FRAME_DATA_INDEX);
	const FPostProcessParameters& PPParams = mpScene->GetPostProcessParameters(FRAME_DATA_INDEX);
	const HWND hwndMain = mpWinMain->GetHWND();
	const bool bHDR = this->ShouldRenderHDR(hwndMain);
	const Window* pWindow = mpWinMain.get();

	HRESULT hr = S_OK;
	if (mbLoadingLevel || mbLoadingEnvironmentMap)
	{
		hr = mpRenderer->PreRenderLoadingScreen(WorkerThreads, pWindow, mSettings.gfx, mUIState);
		hr = mpRenderer->RenderLoadingScreen(pWindow, mLoadingScreenData, bHDR);
	}
	else
	{
		hr = mpRenderer->PreRenderScene(WorkerThreads, pWindow, SceneView, SceneShadowView, PPParams, mSettings.gfx, mUIState);
		hr = mpRenderer->RenderScene(WorkerThreads, pWindow, SceneView, SceneShadowView, PPParams, mSettings.gfx, mUIState, bHDR);
	}

	if (hr == DXGI_STATUS_OCCLUDED) { RenderThread_HandleStatusOccluded(); }
	if (hr == DXGI_ERROR_DEVICE_REMOVED) { RenderThread_HandleDeviceRemoved(); }
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
	HRESULT hr = this->mpRenderer->GetDevicePtr()->GetDeviceRemovedReason();

	this->mbStopAllThreads.store(true);

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	RenderThread_SignalUpdateThread();
#endif

	Log::Warning("Device Removed Reason: %s", VQ_DXGI_UTILS::GetDXGIError(hr));

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
	FWindowRenderContext& ctx   = mpRenderer->GetWindowRenderContext(mpWinDebug->GetHWND());
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
	const VBV& vbv = mpRenderer->GetVertexBufferView(VB_ID);
	const IBV& ibv = mpRenderer->GetIndexBufferView(IB_ID);
	const float RenderResolutionX = static_cast<float>(ctx.WindowDisplayResolutionX);
	const float RenderResolutionY = static_cast<float>(ctx.WindowDisplayResolutionY);
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };


	pCmd->SetPipelineState(mpRenderer->GetPSO(EBuiltinPSOs::HELLO_WORLD_TRIANGLE_PSO));
	pCmd->SetGraphicsRootSignature(mpRenderer->GetBuiltinRootSignature(0));

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

