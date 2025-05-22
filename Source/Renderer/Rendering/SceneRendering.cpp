//	VQE
//	Copyright(C) 2025  - Volkan Ilbeyli
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

#include "Renderer.h"
#include "RenderPass/AmbientOcclusion.h"
#include "RenderPass/DepthPrePass.h"
#include "RenderPass/DepthMSAAResolve.h"
#include "RenderPass/ScreenSpaceReflections.h"
#include "RenderPass/ApplyReflections.h"
#include "RenderPass/MagnifierPass.h"
#include "RenderPass/ObjectIDPass.h"
#include "RenderPass/OutlinePass.h"

#include "Shaders/LightingConstantBufferData.h"

#include "Core/Common.h"

#include "Engine/GPUMarker.h"
#include "Engine/Scene/SceneViews.h"
#include "Engine/UI/VQUI.h"
#include "Engine/Core/Window.h"

#include "Libs/VQUtils/Source/utils.h"
#include "Libs/imgui/imgui.h"

using namespace DirectX;
using namespace VQ_SHADER_DATA;
struct FFrameConstantBufferUnlit { DirectX::XMMATRIX matModelViewProj; DirectX::XMFLOAT4 color; };

void VQRenderer::AllocateCommandLists(ECommandQueueType eQueueType, size_t iBackBuffer, size_t NumRecordingThreads)
{
	ID3D12Device* pD3DDevice = mDevice.GetDevicePtr();

	D3D12_COMMAND_LIST_TYPE CMD_LIST_TYPE = GetDX12CmdListType(eQueueType);
	std::vector<ID3D12CommandAllocator*>& vCmdAllocators = mRenderingCommandAllocators[eQueueType][iBackBuffer];

	mNumCurrentlyRecordingRenderingThreads[eQueueType] = static_cast<UINT>(NumRecordingThreads);

	// we need to create new command list allocators
	const size_t NumAlreadyAllocatedCommandListAllocators = vCmdAllocators.size();
	if (NumAlreadyAllocatedCommandListAllocators < NumRecordingThreads)
	{
		// create command allocators and command lists for the new threads
		vCmdAllocators.resize(NumRecordingThreads);

		assert(NumAlreadyAllocatedCommandListAllocators >= 1);
		for (size_t iNewCmdListAlloc = NumAlreadyAllocatedCommandListAllocators; iNewCmdListAlloc < NumRecordingThreads; ++iNewCmdListAlloc)
		{
			// create the command allocator
			ID3D12CommandAllocator*& pAlloc = vCmdAllocators[iNewCmdListAlloc];
			pD3DDevice->CreateCommandAllocator(CMD_LIST_TYPE, IID_PPV_ARGS(&pAlloc));
			SetName(pAlloc, "CmdListAlloc[%d]", (int)iNewCmdListAlloc);
		}
	}

	// we need to create new command lists
	const size_t NumAlreadyAllocatedCommandLists = mpRenderingCmds[eQueueType].size();
	if (NumAlreadyAllocatedCommandLists < NumRecordingThreads)
	{
		// create command allocators and command lists for the new threads
		mpRenderingCmds[eQueueType].resize(NumRecordingThreads);
		mCmdClosed[eQueueType].resize(NumRecordingThreads);

		assert(NumAlreadyAllocatedCommandLists >= 1);
		for (size_t iNewCmdListAlloc = NumAlreadyAllocatedCommandLists; iNewCmdListAlloc < NumRecordingThreads; ++iNewCmdListAlloc)
		{
			// create the command list
			pD3DDevice->CreateCommandList(0, CMD_LIST_TYPE, vCmdAllocators[iNewCmdListAlloc], nullptr, IID_PPV_ARGS(&mpRenderingCmds[eQueueType][iNewCmdListAlloc]));
			ID3D12CommandList* pCmd = mpRenderingCmds[eQueueType][iNewCmdListAlloc];
			if (pCmd)
			{
				SetName(pCmd, "pCmd[%d]", (int)iNewCmdListAlloc);
			}

			// close if gfx command list
			//if (eQueueType == ECommandQueueType::GFX)
			{
				ID3D12GraphicsCommandList* pGfxCmdList = (ID3D12GraphicsCommandList*)pCmd;
				pGfxCmdList->Close();
				mCmdClosed[eQueueType][iNewCmdListAlloc] = true;
			}
		}
	}
}
void VQRenderer::AllocateConstantBufferMemory(uint32_t NumHeaps, uint32_t NumBackBuffers, uint32_t MemoryPerHeap)
{
	const size_t NumAlreadyAllocatedHeaps = mDynamicHeap_RenderingConstantBuffer.size();

	// we need to create new dynamic memory heaps
	if (NumAlreadyAllocatedHeaps < NumHeaps)
	{
		assert(NumAlreadyAllocatedHeaps >= 1);

		mDynamicHeap_RenderingConstantBuffer.resize(NumHeaps);

		for (uint32_t iHeap = (uint32_t)NumAlreadyAllocatedHeaps; iHeap < NumHeaps; ++iHeap)
		{
			mDynamicHeap_RenderingConstantBuffer[iHeap].Create(mDevice.GetDevicePtr(), NumBackBuffers, MemoryPerHeap);
		}
	}
}

void VQRenderer::ResetCommandLists(ECommandQueueType eQueueType, size_t iBackBuffer, size_t NumRecordingThreads)
{
	std::vector<ID3D12CommandAllocator*>& vCmdAllocators = mRenderingCommandAllocators[eQueueType][iBackBuffer];
	assert(NumRecordingThreads <= vCmdAllocators.size());

	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	for (size_t iThread = 0; iThread < NumRecordingThreads; ++iThread)
	{
		ID3D12CommandAllocator*& pAlloc = vCmdAllocators[iThread];
		ThrowIfFailed(pAlloc->Reset());

		// However, when ExecuteCommandList() is called on a particular command 
		// list, that command list can then be reset at any time and must be before 
		// re-recording.
		assert(mpRenderingCmds[eQueueType][iThread]);
		static_cast<ID3D12GraphicsCommandList*>(mpRenderingCmds[eQueueType][iThread])->Reset(pAlloc, nullptr);
		mCmdClosed[eQueueType][iThread] = false;
	}
}


bool VQRenderer::ShouldEnableAsyncCompute(const FGraphicsSettings& GFXSettings, const FSceneView& SceneView, const FSceneShadowViews& ShadowView) 
{
	if (!GFXSettings.bEnableAsyncCompute)
		return false;
	if (ShadowView.NumPointShadowViews > 0)
		return true;
	if (ShadowView.NumSpotShadowViews > 3)
		return true;
	return false;
}
FSceneDrawData& VQRenderer::GetSceneDrawData(int FRAME_INDEX)
{
	// [0] since we don't have parallel update+render, use FRAME_INDEX otherwise
	return mFrameSceneDrawData[0];
}

static uint32_t GetNumShadowViewCmdRecordingThreads(const FSceneShadowViews& ShadowView)
{
#if RENDER_THREAD__MULTI_THREADED_COMMAND_RECORDING
	return ShadowView.NumDirectionalViews
		//(ShadowView.ShadowView_Directional.meshRenderParams.size() > 0 ? 1 : 0) // 1 thread for directional light (assumes long list of mesh render cmds)
		+ ShadowView.NumPointShadowViews  // each point light view (6x frustums per point light)
		+ (ShadowView.NumSpotShadowViews > 0 ? 1 : 0); // process spot light render lists in own thread
#else
	return 0;
#endif
}

HRESULT VQRenderer::PreRenderScene(
	  ThreadPool& WorkerThreads
	, const Window* pWindow
	, const FSceneView& SceneView
	, const FSceneShadowViews& SceneShadowView
	, const FPostProcessParameters& PPParams
	, const FGraphicsSettings& GFXSettings
	, const FUIState& UIState
)
{
	SCOPED_CPU_MARKER("Renderer.PreRenderScene");
	WaitMainSwapchainReady();
	FWindowRenderContext& ctx = this->GetWindowRenderContext(pWindow->GetHWND());
	const int SWAPCHAIN_INDEX = ctx.SwapChain.GetCurrentBackBufferIndex();
	const int NUM_SWAPCHAIN_BACKBUFFERS = ctx.SwapChain.GetNumBackBuffers();
	const bool bAsyncSubmit = mWaitForSubmitWorker;
	const bool bUseAsyncCompute = ShouldEnableAsyncCompute(GFXSettings, SceneView, SceneShadowView);

#if RENDER_THREAD__MULTI_THREADED_COMMAND_RECORDING
	const uint32_t NumCmdRecordingThreads_GFX
		= 1 // worker thrd: DepthPrePass
		+ 1 // worker thrd: ObjectIDPass
		+ 1 // this thread: AO+SceneColor+PostProcess+Submit+Present
		+ GetNumShadowViewCmdRecordingThreads(SceneShadowView);
	const uint32_t NumCmdRecordingThreads_CMP = 0;
	const uint32_t NumCmdRecordingThreads_CPY = 0;
	const uint32_t NumCmdRecordingThreads = NumCmdRecordingThreads_GFX + NumCmdRecordingThreads_CPY + NumCmdRecordingThreads_CMP;
	const uint32_t ConstantBufferBytesPerThread = (128 + 256) * MEGABYTE;
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

	WaitHeapsInitialized();
	{
		SCOPED_CPU_MARKER("AllocCBMem");
		AllocateConstantBufferMemory(NumCmdRecordingThreads, NUM_SWAPCHAIN_BACKBUFFERS, ConstantBufferBytesPerThread);
	}
	{
		SCOPED_CPU_MARKER("CB.BeginFrame");
		for (size_t iThread = 0; iThread < NumCmdRecordingThreads; ++iThread)
		{
			mDynamicHeap_RenderingConstantBuffer[iThread].OnBeginFrame();
		}
	}

	if (!SceneView.FrustumRenderLists.empty())
	{
		BatchDrawCalls(WorkerThreads, SceneView, SceneShadowView, ctx, PPParams, GFXSettings);
	}

	if (mWaitForSubmitWorker) // not really used, need to offload submit to a non-worker thread (sync issues on main)
	{
		{
			SCOPED_CPU_MARKER_C("WAIT_SUBMIT_WORKER", 0xFFFF0000);
			mSubmitWorkerSignal.Wait();
		}
		mWaitForSubmitWorker = false;
		mSubmitWorkerSignal.Reset();
	}
	{
		SCOPED_CPU_MARKER("AllocCmdLists");
		AllocateCommandLists(ECommandQueueType::GFX, SWAPCHAIN_INDEX, NumCmdRecordingThreads_GFX);
		if (GFXSettings.bEnableAsyncCopy) { AllocateCommandLists(ECommandQueueType::COPY, SWAPCHAIN_INDEX, NumCopyCmdLists); }
		if (bUseAsyncCompute)             { AllocateCommandLists(ECommandQueueType::COMPUTE, SWAPCHAIN_INDEX, NumComputeCmdLists); }
	}

	{
		SCOPED_CPU_MARKER("ResetCmdLists");
		ResetCommandLists(ECommandQueueType::GFX, SWAPCHAIN_INDEX, NumCmdRecordingThreads_GFX);
		if (GFXSettings.bEnableAsyncCopy) { ResetCommandLists(ECommandQueueType::COPY, SWAPCHAIN_INDEX, NumCopyCmdLists); }
		if (bUseAsyncCompute)             { ResetCommandLists(ECommandQueueType::COMPUTE, SWAPCHAIN_INDEX, NumComputeCmdLists); }
	}

	ID3D12DescriptorHeap* ppHeaps[] = { this->GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };

	{
		SCOPED_CPU_MARKER("Cmd.SetDescHeap");
		auto& vpGFXCmds = mpRenderingCmds[ECommandQueueType::GFX];
		for (uint32_t iGFX = 0; iGFX < NumCmdRecordingThreads_GFX; ++iGFX)
		{
			static_cast<ID3D12GraphicsCommandList*>(vpGFXCmds[iGFX])->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		}
		
		if (bUseAsyncCompute)
		{
			auto& vpCMPCmds = mpRenderingCmds[ECommandQueueType::COMPUTE];
			for (uint iCMP = 0; iCMP < NumComputeCmdLists; ++iCMP)
			{
				static_cast<ID3D12GraphicsCommandList*>(vpCMPCmds[iCMP])->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
			}
		}
	}

	return S_OK;
}

HRESULT VQRenderer::RenderScene(ThreadPool& WorkerThreads, const Window* pWindow, const FSceneView& SceneView, const FSceneShadowViews& ShadowView, const FPostProcessParameters& PPParams, const FGraphicsSettings& GFXSettings, const FUIState& UIState, bool bHDRDisplay)
{
	SCOPED_CPU_MARKER("Renderer.RenderScene");
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	const int FRAME_DATA_INDEX = mNumRenderLoopsExecuted % NUM_BACK_BUFFERS;
#else
	const int FRAME_DATA_INDEX = 0;
#endif

	{
		SCOPED_CPU_MARKER("WAIT_PSO_WORKER_DISPATCH");
		mLatchPSOLoaderDispatched.wait();
	}
	if (!this->mPSOCompileResults.empty())
	{
		this->WaitPSOCompilation();
		this->AssignPSOs();
	}

	{
		SCOPED_CPU_MARKER_C("WaitBatching", 0xFFFF0000);
		for(size_t i = 0; i < SceneView.NumActiveFrustumRenderLists; ++i)
			SceneView.FrustumRenderLists[i].BatchDoneSignal.Wait();
	}

	const FSceneDrawData& SceneDrawData = GetSceneDrawData(FRAME_DATA_INDEX);
	mRenderStats.NumLitMeshDrawCommands = (uint)SceneDrawData.mainViewDrawParams.size();
	mRenderStats.NumBoundingBoxDrawCommands = (uint)SceneView.NumMeshBBRenderCmds;
	mRenderStats.NumShadowMeshDrawCommands = 0;
	for (auto& vParams : SceneDrawData.spotShadowDrawParams ) mRenderStats.NumShadowMeshDrawCommands += (uint)vParams.size();
	for (auto& vParams : SceneDrawData.pointShadowDrawParams) mRenderStats.NumShadowMeshDrawCommands += (uint)vParams.size();
	mRenderStats.NumShadowMeshDrawCommands += (uint)SceneDrawData.directionalShadowDrawParams.size();
	mRenderStats.NumDispatches = 0;
	mRenderStats.NumDraws = 0;

	HWND hwnd = pWindow->GetHWND();
	const uint32 W = pWindow->GetWidth();
	const uint32 H = pWindow->GetHeight();

	FWindowRenderContext& ctx = this->GetWindowRenderContext(hwnd);

	HRESULT hr                              = S_OK;
	const int NUM_BACK_BUFFERS              = ctx.GetNumSwapchainBuffers();
	const int BACK_BUFFER_INDEX             = ctx.GetCurrentSwapchainBufferIndex();

	const bool bReflectionsEnabled = GFXSettings.Reflections != EReflections::REFLECTIONS_OFF && GFXSettings.Reflections == EReflections::SCREEN_SPACE_REFLECTIONS__FFX; // TODO: remove the && after RayTracing is added
	const bool bDownsampleDepth    = GFXSettings.Reflections == EReflections::SCREEN_SPACE_REFLECTIONS__FFX;
	const bool& bMSAA              = GFXSettings.bAntiAliasing;
	const bool bAsyncCompute       = ShouldEnableAsyncCompute(GFXSettings, SceneView, ShadowView);

	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();
	auto pRscNormals     = this->GetTextureResource(rsc.Tex_SceneNormals);
	auto pRscNormalsMSAA = this->GetTextureResource(rsc.Tex_SceneNormalsMSAA);
	auto pRscDepthResolve= this->GetTextureResource(rsc.Tex_SceneDepthResolve);
	auto pRscDepthMSAA   = this->GetTextureResource(rsc.Tex_SceneDepthMSAA);
	auto pRscDepth       = this->GetTextureResource(rsc.Tex_SceneDepth);


	ID3D12Resource* pRsc = nullptr;
	
	ID3D12CommandList* pCmdCpy = (ID3D12CommandList*)mpRenderingCmds[ECommandQueueType::COPY][0];
	CommandQueue& GFXCmdQ = this->GetCommandQueue(ECommandQueueType::GFX);
	CommandQueue& CPYCmdQ = this->GetCommandQueue(ECommandQueueType::COPY);
	CommandQueue& CMPCmdQ = this->GetCommandQueue(ECommandQueueType::COMPUTE);

	// ---------------------------------------------------------------------------------------------------
	// TODO: undo const cast and assign in a proper spot -------------------------------------------------
	FSceneView& RefSceneView = const_cast<FSceneView&>(SceneView);
	FPostProcessParameters& RefPPParams = const_cast<FPostProcessParameters&>(PPParams);

	RefSceneView.SceneRTWidth  = static_cast<int>(ctx.WindowDisplayResolutionX * (PPParams.IsFSREnabled() ? PPParams.ResolutionScale : 1.0f));
	RefSceneView.SceneRTHeight = static_cast<int>(ctx.WindowDisplayResolutionY * (PPParams.IsFSREnabled() ? PPParams.ResolutionScale : 1.0f));
	RefPPParams.SceneRTWidth  = SceneView.SceneRTWidth;
	RefPPParams.SceneRTHeight = SceneView.SceneRTHeight;
	RefPPParams.DisplayResolutionWidth  = ctx.WindowDisplayResolutionX;
	RefPPParams.DisplayResolutionHeight = ctx.WindowDisplayResolutionY;
	if (bHDRDisplay) // do some settings override for some render paths
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
			mEventQueue_WinToVQE_Renderer.AddItem(std::make_unique<WindowResizeEvent>(W, H, hwnd));
			mEventQueue_WinToVQE_Update.AddItem(std::make_unique<WindowResizeEvent>(W, H, hwnd));
#endif
		}
	}
	assert(PPParams.DisplayResolutionHeight != 0);
	assert(PPParams.DisplayResolutionWidth != 0);
	// TODO: undo const cast and assign in a proper spot -------------------------------------------------
	// ---------------------------------------------------------------------------------------------------


	const float RenderResolutionX = static_cast<float>(SceneView.SceneRTWidth);
	const float RenderResolutionY = static_cast<float>(SceneView.SceneRTHeight);

	UINT64 SSAODoneFenceValue = mAsyncComputeSSAODoneFence[BACK_BUFFER_INDEX].GetValue();
	D3D12_GPU_VIRTUAL_ADDRESS cbPerFrame = {};
	{
		SCOPED_CPU_MARKER("CopyPerFrameConstantBufferData");
		DynamicBufferHeap& CBHeap = mDynamicHeap_RenderingConstantBuffer[0];
		PerFrameData* pPerFrame = {};
		CBHeap.AllocConstantBuffer(sizeof(PerFrameData), (void**)(&pPerFrame), &cbPerFrame);

		assert(pPerFrame);
		pPerFrame->Lights = SceneView.GPULightingData;
		pPerFrame->fAmbientLightingFactor = SceneView.sceneRenderOptions.fAmbientLightingFactor;
		pPerFrame->f2PointLightShadowMapDimensions       = { 1024.0f, 1024.f  }; // TODO
		pPerFrame->f2SpotLightShadowMapDimensions        = { 1024.0f, 1024.f  }; // TODO
		pPerFrame->f2DirectionalLightShadowMapDimensions = { 2048.0f, 2048.0f }; // TODO
		pPerFrame->fHDRIOffsetInRadians = SceneView.HDRIYawOffset;

		if (bHDRDisplay)
		{
			// adjust ambient factor as the tonemapper changes the output curve for HDR displays 
			// and makes the ambient lighting too strong.
			pPerFrame->fAmbientLightingFactor *= 0.005f;
		}
	}

	D3D12_GPU_VIRTUAL_ADDRESS cbPerView = {};
	{
		SCOPED_CPU_MARKER("CopyPerViewConstantBufferData");
		DynamicBufferHeap& CBHeap = mDynamicHeap_RenderingConstantBuffer[0];
		PerViewLightingData* pPerView = {};
		CBHeap.AllocConstantBuffer(sizeof(decltype(*pPerView)), (void**)(&pPerView), &cbPerView);

		assert(pPerView);
		XMStoreFloat3(&pPerView->CameraPosition, SceneView.cameraPosition);
		pPerView->ScreenDimensions.x = RenderResolutionX;
		pPerView->ScreenDimensions.y = RenderResolutionY;
		pPerView->MaxEnvMapLODLevels = static_cast<float>(rsc.EnvironmentMap.GetNumSpecularIrradianceCubemapLODLevels(*this));
		pPerView->EnvironmentMapDiffuseOnlyIllumination = GFXSettings.Reflections == EReflections::SCREEN_SPACE_REFLECTIONS__FFX;
		const FFrustumPlaneset planes = FFrustumPlaneset::ExtractFromMatrix(SceneView.viewProj, true);
		memcpy(pPerView->WorldFrustumPlanes, planes.abcd, sizeof(planes.abcd));
	}

	if constexpr (!RENDER_THREAD__MULTI_THREADED_COMMAND_RECORDING)
	{
		constexpr size_t THREAD_INDEX = 0;

		ID3D12GraphicsCommandList* pCmd = (ID3D12GraphicsCommandList*)mpRenderingCmds[ECommandQueueType::GFX][THREAD_INDEX];
		DynamicBufferHeap& CBHeap = mDynamicHeap_RenderingConstantBuffer[THREAD_INDEX];

		RenderSpotShadowMaps(pCmd, &CBHeap, ShadowView, SceneView);
		RenderDirectionalShadowMaps(pCmd, &CBHeap, ShadowView, SceneView);
		RenderPointShadowMaps(pCmd, &CBHeap, ShadowView, SceneView, 0, ShadowView.NumPointShadowViews);

		RenderDepthPrePass(pCmd, SceneView, cbPerView, GFXSettings, bAsyncCompute);

		RenderObjectIDPass(THREAD_INDEX, pCmdCpy, &CBHeap, cbPerView, SceneView, ShadowView, BACK_BUFFER_INDEX, GFXSettings);

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

		RenderAmbientOcclusion(pCmd, SceneView, GFXSettings, bAsyncCompute);

		TransitionForSceneRendering(pCmd, ctx, PPParams, GFXSettings);

		RenderSceneColor(pCmd, &CBHeap, SceneView, PPParams, cbPerView, cbPerFrame, GFXSettings, bHDRDisplay);

		if (!bReflectionsEnabled)
		{
			RenderLightBounds(pCmd, &CBHeap, SceneView, bMSAA, bReflectionsEnabled);
			RenderBoundingBoxes(pCmd, &CBHeap, SceneView, bMSAA);
			RenderDebugVertexAxes(pCmd, &CBHeap, SceneView, bMSAA);
			
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = this->GetRTV(bMSAA ? rsc.RTV_SceneColorMSAA : rsc.RTV_SceneColor).GetCPUDescHandle();
			RenderOutline(pCmd, &CBHeap, cbPerView, SceneView, bMSAA, { rtvHandle });
		}

		ResolveMSAA(pCmd, &CBHeap, PPParams, GFXSettings);

		TransitionForPostProcessing(pCmd, PPParams, GFXSettings);

		if (bReflectionsEnabled)
		{
			RenderReflections(pCmd, &CBHeap, SceneView, GFXSettings);
			
			// render scene-debugging stuff that shouldn't be in reflections: bounding volumes, etc
			RenderSceneBoundingVolumes(pCmd, &CBHeap, cbPerView, SceneView, bMSAA);

			CompositeReflections(pCmd, &CBHeap, SceneView, GFXSettings);
		}

		pRsc = RenderPostProcess(pCmd, &CBHeap, PPParams, bHDRDisplay);

		RenderUI(pCmd, &CBHeap, ctx, PPParams, pRsc, UIState, bHDRDisplay);

		if (bHDRDisplay)
		{
			CompositUIToHDRSwapchain(pCmd, &CBHeap, ctx, PPParams, pWindow);
		}
	}

	else // RENDER_THREAD__MULTI_THREADED_COMMAND_RECORDING
	{
		constexpr size_t iCmdZPrePassThread = 0;
		constexpr size_t iCmdObjIDPassThread = iCmdZPrePassThread + 1;
		constexpr size_t iCmdPointLightsThread = iCmdObjIDPassThread + 1;
		const     size_t iCmdSpots        = iCmdPointLightsThread + ShadowView.NumPointShadowViews;
		const     size_t iCmdDirectional  = iCmdSpots + (ShadowView.NumSpotShadowViews > 0 ? 1 : 0);
		const     size_t iCmdRenderThread = iCmdDirectional + (ShadowView.NumDirectionalViews);

		ID3D12GraphicsCommandList* pCmd_ThisThread = (ID3D12GraphicsCommandList*)mpRenderingCmds[ECommandQueueType::GFX][iCmdRenderThread];
		DynamicBufferHeap& CBHeap_This = mDynamicHeap_RenderingConstantBuffer[iCmdRenderThread];

		{
			SCOPED_CPU_MARKER_C("BUSY_WAIT_WORKER", 0xFFFF0000); // sync for SceneView
			while (WorkerThreads.GetNumActiveTasks() != 0); // busy-wait is not good
		}

		{
			SCOPED_CPU_MARKER("DispatchWorkers");

			// ZPrePass
			{
				SCOPED_CPU_MARKER("Dispatch.ZPrePass");
				ID3D12GraphicsCommandList* pCmd_ZPrePass = (ID3D12GraphicsCommandList*)mpRenderingCmds[ECommandQueueType::GFX][iCmdZPrePassThread];
				ID3D12GraphicsCommandList* pCmd_Compute  = (ID3D12GraphicsCommandList*)mpRenderingCmds[ECommandQueueType::COMPUTE][0];

				DynamicBufferHeap& CBHeap_WorkerZPrePass = mDynamicHeap_RenderingConstantBuffer[iCmdZPrePassThread];
				WorkerThreads.AddTask([=, &CBHeap_WorkerZPrePass, &SceneView, &ctx]()
				{
					RENDER_WORKER_CPU_MARKER;

					TransitionDepthPrePassForWrite(pCmd_ZPrePass, bMSAA);

					RenderDepthPrePass(pCmd_ZPrePass, SceneView, cbPerView, GFXSettings, bAsyncCompute);

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

						ID3D12Resource* pRscAmbientOcclusion = this->GetTextureResource(rsc.Tex_AmbientOcclusion);
						CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(pRscAmbientOcclusion, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
						pCmd_ZPrePass->ResourceBarrier(1, &barrier);

						pCmd_ZPrePass->Close();
						mCmdClosed[GFX][iCmdZPrePassThread] = true;
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

						RenderAmbientOcclusion(pCmd_Compute, SceneView, GFXSettings, bAsyncCompute);

						{
							SCOPED_CPU_MARKER("ExecCmpCmdList");
							pCmd_Compute->Close();
							mCmdClosed[COMPUTE][0] = true;
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
				SCOPED_CPU_MARKER("Dispatch.ObjectID");
				ID3D12GraphicsCommandList* pCmd_ObjIDPass = (ID3D12GraphicsCommandList*)mpRenderingCmds[ECommandQueueType::GFX][iCmdObjIDPassThread];

				DynamicBufferHeap& CBHeap_WorkerObjIDPass = mDynamicHeap_RenderingConstantBuffer[iCmdObjIDPassThread];
				WorkerThreads.AddTask([=, &SceneView, &ShadowView, &CBHeap_WorkerObjIDPass, &GFXSettings]()
				{
					RENDER_WORKER_CPU_MARKER;
					RenderObjectIDPass(iCmdObjIDPassThread, pCmdCpy, &CBHeap_WorkerObjIDPass, cbPerView, SceneView, ShadowView, BACK_BUFFER_INDEX, GFXSettings);
				});
			}

			// shadow passes
			{
				SCOPED_CPU_MARKER("Dispatch.ShadowPasses");

				if (ShadowView.NumSpotShadowViews > 0)
				{
					ID3D12GraphicsCommandList* pCmd_Spots = (ID3D12GraphicsCommandList*)mpRenderingCmds[ECommandQueueType::GFX][iCmdSpots];
					DynamicBufferHeap& CBHeap_Spots = mDynamicHeap_RenderingConstantBuffer[iCmdSpots];
					WorkerThreads.AddTask([=, &CBHeap_Spots, &ShadowView, &SceneView]()
					{
						RENDER_WORKER_CPU_MARKER;
						RenderSpotShadowMaps(pCmd_Spots, &CBHeap_Spots, ShadowView, SceneView);
					});
				}
				if (ShadowView.NumPointShadowViews > 0)
				{
					for (uint iPoint = 0; iPoint < ShadowView.NumPointShadowViews; ++iPoint)
					{
						const size_t iPointWorker = iCmdPointLightsThread + iPoint;
						ID3D12GraphicsCommandList* pCmd_Point = (ID3D12GraphicsCommandList*)mpRenderingCmds[ECommandQueueType::GFX][iPointWorker];
						DynamicBufferHeap& CBHeap_Point = mDynamicHeap_RenderingConstantBuffer[iPointWorker];
						WorkerThreads.AddTask([=, &CBHeap_Point, &ShadowView, &SceneView]()
						{
							RENDER_WORKER_CPU_MARKER;
							RenderPointShadowMaps(pCmd_Point, &CBHeap_Point, ShadowView, SceneView, iPoint, 1);
						});
					}
				}

				if (ShadowView.NumDirectionalViews > 0)
				{
					ID3D12GraphicsCommandList* pCmd_Directional = (ID3D12GraphicsCommandList*)mpRenderingCmds[ECommandQueueType::GFX][iCmdDirectional];
					DynamicBufferHeap& CBHeap_Directional = mDynamicHeap_RenderingConstantBuffer[iCmdDirectional];
					WorkerThreads.AddTask([=, &CBHeap_Directional, &ShadowView, &SceneView]()
					{
						RENDER_WORKER_CPU_MARKER;
						RenderDirectionalShadowMaps(pCmd_Directional, &CBHeap_Directional, ShadowView, SceneView);
					});
				}
			}
		}

		if (bAsyncCompute)
		{
			// async compute queue cannot issue these barrier transitions. 
			// we're waiting the async compute fence before we execute this cmd list so there's no sync issues.
			ID3D12Resource* pRscAmbientOcclusion = this->GetTextureResource(rsc.Tex_AmbientOcclusion);
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
			RenderAmbientOcclusion(pCmd_ThisThread, SceneView, GFXSettings, bAsyncCompute);
		}

		TransitionForSceneRendering(pCmd_ThisThread, ctx, PPParams, GFXSettings);

		RenderSceneColor(pCmd_ThisThread, &CBHeap_This, SceneView, PPParams, cbPerView, cbPerFrame, GFXSettings, bHDRDisplay);

		if (!bReflectionsEnabled)
		{
			RenderLightBounds(pCmd_ThisThread, &CBHeap_This, SceneView, bMSAA, bReflectionsEnabled);
			RenderBoundingBoxes(pCmd_ThisThread, &CBHeap_This, SceneView, bMSAA);
			RenderDebugVertexAxes(pCmd_ThisThread, &CBHeap_This, SceneView, bMSAA);

			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = this->GetRTV(bMSAA ? rsc.RTV_SceneColorMSAA : rsc.RTV_SceneColor).GetCPUDescHandle();
			RenderOutline(pCmd_ThisThread, &CBHeap_This, cbPerView, SceneView, bMSAA, { rtvHandle });
		}

		ResolveMSAA(pCmd_ThisThread, &CBHeap_This, PPParams, GFXSettings);

		TransitionForPostProcessing(pCmd_ThisThread, PPParams, GFXSettings);

		if (bReflectionsEnabled)
		{
			RenderReflections(pCmd_ThisThread, &CBHeap_This, SceneView, GFXSettings);
			
			// render scene-debugging stuff that shouldn't be in reflections: bounding volumes, etc
			RenderSceneBoundingVolumes(pCmd_ThisThread, &CBHeap_This, cbPerView, SceneView, bMSAA);

			CompositeReflections(pCmd_ThisThread, &CBHeap_This, SceneView, GFXSettings);
		}

		pRsc = RenderPostProcess(pCmd_ThisThread, &CBHeap_This, PPParams, bHDRDisplay);

		RenderUI(pCmd_ThisThread, &CBHeap_This, ctx, PPParams, pRsc, UIState, bHDRDisplay);

		if (bHDRDisplay)
		{
			CompositUIToHDRSwapchain(pCmd_ThisThread, &CBHeap_This, ctx, PPParams, pWindow);
		}

		// SYNC Render Workers
		{	
			SCOPED_CPU_MARKER_C("BUSY_WAIT_WORKERS", 0xFFFF0000);
			while (WorkerThreads.GetNumActiveTasks() != 0);
		}
	}

	// close gfx cmd lists
	std::vector<ID3D12CommandList*> vCmdLists = mpRenderingCmds[ECommandQueueType::GFX];
	if constexpr (!RENDER_THREAD__MULTI_THREADED_COMMAND_RECORDING)
	{
		// TODO: async compute support for single-threaded CPU cmd recording
		static_cast<ID3D12GraphicsCommandList*>(vCmdLists[0])->Close();
		mCmdClosed[GFX][0] = true;
		ctx.PresentQueue.pQueue->ExecuteCommandLists(1, vCmdLists.data());

		hr = PresentFrame(ctx);
		if(hr == S_OK)
		{
			SCOPED_CPU_MARKER_C("GPU_BOUND", 0xFF005500);
			ctx.SwapChain.MoveToNextFrame();
		}
	}

	else
	{
		const bool bAsyncCopy = GFXSettings.bEnableAsyncCopy;

		//  TODO remove this copy paste
		constexpr size_t iCmdZPrePassThread = 0;
		constexpr size_t iCmdObjIDPassThread = iCmdZPrePassThread + 1;
		constexpr size_t iCmdPointLightsThread = iCmdObjIDPassThread + 1;
		const     size_t iCmdSpots = iCmdPointLightsThread + ShadowView.NumPointShadowViews;
		const     size_t iCmdDirectional = iCmdSpots + (ShadowView.NumSpotShadowViews > 0 ? 1 : 0);
		const     size_t iCmdRenderThread = iCmdDirectional + ShadowView.NumDirectionalViews;

		// close command lists
		const UINT NumCommandLists = mNumCurrentlyRecordingRenderingThreads[ECommandQueueType::GFX];
		for (UINT i = 0; i < NumCommandLists; ++i)
		{
			if (GFXSettings.bEnableAsyncCopy && (i == iCmdObjIDPassThread))
			{
				assert(mCmdClosed[GFX][i]);
				continue;
			}
			if (bAsyncCompute && (i == iCmdZPrePassThread))
			{
				assert(mCmdClosed[GFX][i]);
				continue; // already closed & executed
			}

			static_cast<ID3D12GraphicsCommandList*>(vCmdLists[i])->Close();
			mCmdClosed[GFX][i] = true;
		}

		// execute command lists on a thread
#if EXECUTE_CMD_LISTS_ON_WORKER
		mWaitForSubmitWorker = true;
		WorkerThreads.AddTask([=, &SceneView, &ctx, &ShadowView]()
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
						for (size_t i = iCmdPointLightsThread; i - iCmdPointLightsThread < ShadowView.NumPointShadowViews; ++i)
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
					for (int i = 0; i < mCmdClosed[GFX].size(); ++i)
						if (!mCmdClosed[GFX][i])
							Log::Warning("Cmd[GFX][%d] not closed!", i);
					ctx.PresentQueue.pQueue->ExecuteCommandLists(1, &pGfxCmd);
				}
				else
				{
					SCOPED_CPU_MARKER("ExecuteCommandLists");
					std::vector<ID3D12CommandList*> vCmdLists = mpRenderingCmds[ECommandQueueType::GFX];
					if (GFXSettings.bEnableAsyncCopy)
					{
						vCmdLists.erase(vCmdLists.begin() + iCmdObjIDPassThread); // already kicked off objID pass
					}

					#if 0 // debug log
					for (int i = 0; i < vCmdLists.size(); ++i)
					{
						std::string s = "";
						if (i == iCmdZPrePassThread) s += "ZPrePass";
						if (i == iCmdObjIDPassThread) s += "ObjIDPass";
						if (i == iCmdPointLightsThread && ShadowView.NumPointShadowViews > 0) s += "PointLights";
						if (i == iCmdSpots && ShadowView.NumSpotShadowViews > 0) s += "SpotLights";
						if (i == iCmdDirectional && iCmdDirectional != iCmdRenderThread) s += "DirLight";
						if (i == iCmdRenderThread) s += "RenderThread";
						Log::Info("  GFX : pCmd[%d]: %x | %s", i, vCmdLists[i], s.c_str());
					}
					#endif

					const size_t NumCmds = iCmdRenderThread + (GFXSettings.bEnableAsyncCopy ? 0 : 1);
					for(int i=0; i<mCmdClosed[GFX].size(); ++i)
						if(!mCmdClosed[GFX][i])
							Log::Warning("Cmd[GFX][%d] not closed!", i);
					ctx.PresentQueue.pQueue->ExecuteCommandLists((UINT)NumCmds, (ID3D12CommandList**)&vCmdLists[0]);
				}

				HRESULT hr = PresentFrame(ctx);
				#if !EXECUTE_CMD_LISTS_ON_WORKER
				if (hr == DXGI_STATUS_OCCLUDED) { RenderThread_HandleStatusOccluded(); }
				if (hr == DXGI_ERROR_DEVICE_REMOVED) { RenderThread_HandleDeviceRemoved(); }
				#endif

				if(hr == S_OK)
					ctx.SwapChain.MoveToNextFrame();

#if EXECUTE_CMD_LISTS_ON_WORKER
				mSubmitWorkerSignal.Notify();
			}
		});
#endif
	}

	++mRenderStats.mNumFramesRendered;
	return hr;
}


void VQRenderer::RenderObjectIDPass(int iThread, ID3D12CommandList* pCmdCopy, DynamicBufferHeap* pCBufferHeap, D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr, const FSceneView& SceneView, const FSceneShadowViews& ShadowView, const int BACK_BUFFER_INDEX, const FGraphicsSettings& GFXSettings)
{
	std::shared_ptr<ObjectIDPass> pObjectIDPass = std::static_pointer_cast<ObjectIDPass>(this->GetRenderPass(ERenderPass::ObjectID));
	ID3D12GraphicsCommandList* pCmd = (ID3D12GraphicsCommandList*)mpRenderingCmds[ECommandQueueType::GFX][iThread];
	{
		SCOPED_GPU_MARKER(pCmd, "RenderObjectIDPass");
		ObjectIDPass::FDrawParameters params;
		params.pCmd = pCmd;
		params.pCmdCopy = pCmdCopy;
		params.pSceneView = &SceneView;
		params.pSceneDrawData = &mFrameSceneDrawData[0];
		params.cbPerView = perViewCBAddr;
		params.bEnableAsyncCopy = GFXSettings.bEnableAsyncCopy;
		pObjectIDPass->RecordCommands(&params);
	}

	SCOPED_CPU_MARKER("Copy");
	D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
	srcLoc.pResource = pObjectIDPass->GetGPUTextureResource();
	srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	srcLoc.SubresourceIndex = 0; // Assuming copying from the first mip level

	D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
	dstLoc.pResource = pObjectIDPass->GetCPUTextureResource();
	dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	D3D12_RESOURCE_DESC srcDesc = srcLoc.pResource->GetDesc();
	this->GetDevicePtr()->GetCopyableFootprints(&srcDesc, 0, 1, 0, &dstLoc.PlacedFootprint, nullptr, nullptr, nullptr);

	if (GFXSettings.bEnableAsyncCopy)
	{
		CommandQueue& GFXCmdQ = this->GetCommandQueue(ECommandQueueType::GFX);
		CommandQueue& CPYCmdQ = this->GetCommandQueue(ECommandQueueType::COPY);
		CommandQueue& CMPCmdQ = this->GetCommandQueue(ECommandQueueType::COMPUTE);
		ID3D12GraphicsCommandList* pCmdCpy = static_cast<ID3D12GraphicsCommandList*>(pCmdCopy);
		Fence& CopyFence = mCopyObjIDDoneFence[BACK_BUFFER_INDEX];

		pCmd->Close();
		mCmdClosed[GFX][iThread] = true;

		if (ShouldEnableAsyncCompute(GFXSettings, SceneView, ShadowView))
		{
			SCOPED_CPU_MARKER_C("WAIT_DEPTH_PREPASS_SUBMIT", 0xFFFF0000);
			while (!mAsyncComputeWorkSubmitted.load()); // pls don't judge
			mAsyncComputeWorkSubmitted.store(false);
		}
		{
			SCOPED_CPU_MARKER("ExecuteList");
			GFXCmdQ.pQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&pCmd);
		}
		{
			SCOPED_CPU_MARKER("Fence");
			CopyFence.Signal(GFXCmdQ.pQueue);
			CopyFence.WaitOnGPU(CPYCmdQ.pQueue); // wait for render target done
		}

		D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
		dstLoc.pResource = pObjectIDPass->GetCPUTextureResource();
		dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		this->GetDevicePtr()->GetCopyableFootprints(&srcDesc, 0, 1, 0, &dstLoc.PlacedFootprint, nullptr, nullptr, nullptr);

		pCmdCpy->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

		{
			SCOPED_CPU_MARKER("ExecuteListCpy");
			pCmdCpy->Close();
			mCmdClosed[COPY][0] = true;
			CPYCmdQ.pQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&pCmdCpy);
		}
		{
			SCOPED_CPU_MARKER("Fence Signal");
			CopyFence.Signal(CPYCmdQ.pQueue);
		}
	}
	else // execute copy on graphics
	{
		pCmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(srcLoc.pResource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		pCmd->ResourceBarrier(1, &barrier);
	}
}


void VQRenderer::DrawShadowViewMeshList(ID3D12GraphicsCommandList* pCmd, const std::vector<FInstancedDrawParameters>& drawParams, size_t iDepthMode)
{
#if RENDER_INSTANCED_SHADOW_MESHES

	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();
	const CBV_SRV_UAV& NullTex2DSRV = this->GetSRV(rsc.SRV_NullTexture2D);

	//Log::Info("DrawShadowMeshList(%d)", drawParams.size());
	for (const FInstancedDrawParameters& draw : drawParams)
	{
		const bool bTessellationEnabled = draw.cbAddr_Tessellation != 0;

		size_t iAlpha = 0; size_t iRaster = 0; size_t iFaceCull = 0;
		draw.UnpackMaterialConfig(iAlpha, iRaster, iFaceCull);
		size_t iTess = 0; size_t iDomain = 0; size_t iPart = 0; size_t iOutTopo = 0; size_t iTessCull = 0;
		draw.UnpackTessellationConfig(iTess, iDomain, iPart, iOutTopo, iTessCull);

		const PSO_ID psoID = this->mShadowPassPSOs.Get(iDepthMode, iRaster, iFaceCull, iTess, iDomain, iPart, iOutTopo, iTessCull, iAlpha);
		pCmd->SetPipelineState(this->GetPSO(psoID));

		// set constant buffer data
		pCmd->SetGraphicsRootConstantBufferView(1, draw.cbAddr); // 2: per object
		if (bTessellationEnabled)
		{
			pCmd->SetGraphicsRootConstantBufferView(2, draw.cbAddr_Tessellation);
		}

		// set textures
		pCmd->SetGraphicsRootDescriptorTable(3, draw.SRVMaterialMaps == INVALID_ID 
			? NullTex2DSRV.GetGPUDescHandle()
			: this->GetSRV(draw.SRVMaterialMaps).GetGPUDescHandle(0)
		);
		pCmd->SetGraphicsRootDescriptorTable(4, draw.SRVHeightMap == INVALID_ID
			? NullTex2DSRV.GetGPUDescHandle()
			: this->GetSRV(draw.SRVHeightMap).GetGPUDescHandle(0)
		);

		VBV vb = this->GetVertexBufferView(draw.VB);
		IBV ib = this->GetIndexBufferView (draw.IB);
		pCmd->IASetPrimitiveTopology(draw.IATopology);
		pCmd->IASetVertexBuffers(0, 1, &vb);
		pCmd->IASetIndexBuffer(&ib);

		pCmd->DrawIndexedInstanced(draw.numIndices, draw.numInstances, 0, 0, 0);
	}

#else 
	struct FCBufferLightVS
	{
		XMMATRIX matWorldViewProj;
		XMMATRIX matWorld;
	};

	for (const FShadowMeshRenderData& renderCmd : shadowView.meshRenderParams)
	{
		SCOPED_CPU_MARKER("Process_ShadowMeshRenderCommand");
		// set constant buffer data
		FCBufferLightVS* pCBuffer = {};
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		pCBufferHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer)), (void**)(&pCBuffer), &cbAddr);
		pCBuffer->matWorldViewProj = renderCmd.matWorldViewProj;
		pCBuffer->matWorld = renderCmd.matWorldTransformation;
		pCmd->SetGraphicsRootConstantBufferView(0, cbAddr);

		const Mesh& mesh = mpScene->mMeshes.at(renderCmd.meshID);
		DrawMesh(pCmd, mesh);
	}
#endif
}


//
// RENDER PASSES
//
static D3D12_GPU_VIRTUAL_ADDRESS SetPerViewSceneCB(
	DynamicBufferHeap& CBHeap,
	const DirectX::XMFLOAT3& CamPosition,
	float RenderResolutionX,
	float RenderResolutionY,
	const DirectX::XMMATRIX& ViewProj
)
{
	SCOPED_CPU_MARKER("CopyPerViewSceneCB");
	D3D12_GPU_VIRTUAL_ADDRESS cbPerView = {};

	PerViewLightingData* pPerView = {};
	CBHeap.AllocConstantBuffer(sizeof(decltype(*pPerView)), (void**)(&pPerView), &cbPerView);

	assert(pPerView);
	pPerView->CameraPosition = CamPosition;
	pPerView->ScreenDimensions.x = RenderResolutionX;
	pPerView->ScreenDimensions.y = RenderResolutionY;
	const FFrustumPlaneset planes = FFrustumPlaneset::ExtractFromMatrix(ViewProj, true);
	memcpy(pPerView->WorldFrustumPlanes, planes.abcd, sizeof(planes.abcd));
	
	return cbPerView;
}
static D3D12_GPU_VIRTUAL_ADDRESS SetPerViewShadowCB(
	DynamicBufferHeap& CBHeap,
	const DirectX::XMFLOAT3& CamPosition,
	float fFarPlane,
	const DirectX::XMMATRIX& ViewProj
)
{
	SCOPED_CPU_MARKER("CopyPerViewSceneCB");
	D3D12_GPU_VIRTUAL_ADDRESS cbPerView = {};

	PerShadowViewData* pPerView = {};
	CBHeap.AllocConstantBuffer(sizeof(decltype(*pPerView)), (void**)(&pPerView), &cbPerView);

	assert(pPerView);
	pPerView->CameraPosition = CamPosition;
	pPerView->fFarPlane = fFarPlane;
	const FFrustumPlaneset planes = FFrustumPlaneset::ExtractFromMatrix(ViewProj, true);
	memcpy(pPerView->WorldFrustumPlanes, planes.abcd, sizeof(planes.abcd));

	return cbPerView;
}
void VQRenderer::RenderDirectionalShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowViews& ShadowView, const FSceneView& SceneView)
{
	SCOPED_GPU_MARKER(pCmd, "RenderDirectionalShadowMaps");
	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();
	const XMMATRIX& MatViewProj = ShadowView.ShadowView_Directional;
	const FSceneDrawData& SceneDrawData = mFrameSceneDrawData[0]; // [0] since we don't have parallel update+render
	
	const std::vector<FInstancedDrawParameters>& drawParams = SceneDrawData.directionalShadowDrawParams;
	if (!drawParams.empty())
	{
		SCOPED_GPU_MARKER(pCmd, "Directional");

		pCmd->SetGraphicsRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ShadowPass));

		const float RenderResolutionX = 2048.0f; // TODO
		const float RenderResolutionY = 2048.0f; // TODO
		D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
		D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
		pCmd->RSSetViewports(1, &viewport);
		pCmd->RSSetScissorRects(1, &scissorsRect);

		// Bind Depth / clear
		const DSV& dsv = this->GetDSV(rsc.DSV_ShadowMaps_Directional);
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsv.GetCPUDescHandle();
		pCmd->OMSetRenderTargets(0, NULL, FALSE, &dsvHandle);

		//if constexpr (!B_CLEAR_DEPTH_BUFFERS_BEFORE_DRAW)
		{
			D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
			pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, NULL);
		}

		DirectX::XMFLOAT3 f3(0, 0, 0); // TODO: set this up properly, can affect tessellated geometry rendering
		float range = 1.0f; // TODO: set this up properly, can affect tessellated geometry rendering
		D3D12_GPU_VIRTUAL_ADDRESS cbPerView = SetPerViewShadowCB(*pCBufferHeap, 
			f3,
			range,
			MatViewProj
		);
		pCmd->SetGraphicsRootConstantBufferView(0, cbPerView);

		DrawShadowViewMeshList(pCmd, drawParams, 0);
	}
}
void VQRenderer::RenderSpotShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowViews& SceneShadowViews, const FSceneView& SceneView)
{
	SCOPED_GPU_MARKER(pCmd, "RenderSpotShadowMaps");
	const bool bRenderAtLeastOneSpotShadowMap = SceneShadowViews.NumSpotShadowViews > 0;
	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();
	const FSceneDrawData& SceneDrawData = mFrameSceneDrawData[0]; // [0] since we don't have parallel update+render

	// Set Viewport & Scissors
	const float RenderResolutionX = 1024.0f; // TODO
	const float RenderResolutionY = 1024.0f; // TODO
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);
	
	if (!bRenderAtLeastOneSpotShadowMap)
		return;

	pCmd->SetGraphicsRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ShadowPass));
	
	assert(SceneDrawData.spotShadowDrawParams.size() == SceneShadowViews.NumSpotShadowViews);
	for (uint i = 0; i < SceneShadowViews.NumSpotShadowViews; ++i)
	{
		const XMMATRIX& ShadowView = SceneShadowViews.ShadowViews_Spot[i];
		const std::vector<FInstancedDrawParameters>& drawParams = SceneDrawData.spotShadowDrawParams[i];
		if (drawParams.empty())
			continue;

		const std::string marker = "Spot[" + std::to_string(i) + "]";
		SCOPED_GPU_MARKER(pCmd, marker.c_str());

		// Bind Depth / clear
		const DSV& dsv = this->GetDSV(rsc.DSV_ShadowMaps_Spot);
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsv.GetCPUDescHandle(i);
		pCmd->OMSetRenderTargets(0, NULL, FALSE, &dsvHandle);

		D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
		pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, NULL);
		
		
		D3D12_GPU_VIRTUAL_ADDRESS cbPerView = SetPerViewShadowCB(*pCBufferHeap,
			SceneView.GPULightingData.spot_casters[i].position,
			SceneView.GPULightingData.spot_casters[i].range, // far plane
			SceneShadowViews.ShadowViews_Spot[i]
		);
		pCmd->SetGraphicsRootConstantBufferView(0, cbPerView);

		DrawShadowViewMeshList(pCmd, drawParams, 0);
	}
}
void VQRenderer::RenderPointShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowViews& SceneShadowViews, const FSceneView& SceneView, size_t iBegin, size_t NumPointLights)
{
	SCOPED_GPU_MARKER(pCmd, "RenderPointShadowMaps");
	if (SceneShadowViews.NumPointShadowViews <= 0)
		return;

	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();
	const FSceneDrawData& SceneDrawData = mFrameSceneDrawData[0]; // [0] since we don't have parallel update+render

#if RENDER_THREAD__MULTI_THREADED_COMMAND_RECORDING
	// Set Viewport & Scissors
	const float RenderResolutionX = 1024.0f; // TODO
	const float RenderResolutionY = 1024.0f; // TODO
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);
#endif

	pCmd->SetGraphicsRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ShadowPass));
	for (size_t i = iBegin; i < iBegin + NumPointLights; ++i)
	{
		const std::string marker = "Point[" + std::to_string(i) + "]";
		SCOPED_GPU_MARKER(pCmd, marker.c_str());

		D3D12_GPU_VIRTUAL_ADDRESS cbPerView = SetPerViewShadowCB(*pCBufferHeap, 
			SceneView.GPULightingData.point_casters[i].position,
			SceneView.GPULightingData.point_casters[i].range, // far plane
			SceneShadowViews.ShadowViews_Point[i]
		);
		pCmd->SetGraphicsRootConstantBufferView(0, cbPerView);

		for (size_t face = 0; face < 6; ++face)
		{
			const size_t iShadowView = i * 6 + face;

			const std::vector<FInstancedDrawParameters>& drawParams = SceneDrawData.pointShadowDrawParams[iShadowView];
			const XMMATRIX& ShadowView = SceneShadowViews.ShadowViews_Point[iShadowView];

			if (drawParams.empty())
				continue;

			const std::string marker_face = "[Cubemap Face=" + std::to_string(face) + "]";
			SCOPED_GPU_MARKER(pCmd, marker_face.c_str());

			// Bind Depth / clear
			const DSV& dsv = this->GetDSV(rsc.DSV_ShadowMaps_Point);
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsv.GetCPUDescHandle((uint32_t)iShadowView);
			D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
			pCmd->OMSetRenderTargets(0, NULL, FALSE, &dsvHandle);
			pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, NULL);

			DrawShadowViewMeshList(pCmd, drawParams, 1);
		}
	}
}

void VQRenderer::RenderDepthPrePass(
	ID3D12GraphicsCommandList* pCmd,
	const FSceneView& SceneView,
	D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr,
	const FGraphicsSettings& GFXSettings,
	bool bAsyncCompute
)
{
	SCOPED_GPU_MARKER(pCmd, "RenderDepthPrePass");

	const bool& bMSAA = GFXSettings.bAntiAliasing;
	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();
	const FSceneDrawData& SceneDrawData = mFrameSceneDrawData[0]; // [0] since we don't have parallel update+render

	ID3D12Resource* pRscNormals     = this->GetTextureResource(rsc.Tex_SceneNormals);
	ID3D12Resource* pRscNormalsMSAA = this->GetTextureResource(rsc.Tex_SceneNormalsMSAA);
	ID3D12Resource* pRscDepthResolve= this->GetTextureResource(rsc.Tex_SceneDepthResolve);
	ID3D12Resource* pRscDepthMSAA   = this->GetTextureResource(rsc.Tex_SceneDepthMSAA);
	ID3D12Resource* pRscDepth       = this->GetTextureResource(rsc.Tex_SceneDepth);

	const DSV& dsvColor   = this->GetDSV(bMSAA ? rsc.DSV_SceneDepthMSAA : rsc.DSV_SceneDepth);
	const RTV& rtvNormals = this->GetRTV(bMSAA ? rsc.RTV_SceneNormalsMSAA : rsc.RTV_SceneNormals);

	D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvNormals.GetCPUDescHandle();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvColor.GetCPUDescHandle();

	const float RenderResolutionX = static_cast<float>(SceneView.SceneRTWidth);
	const float RenderResolutionY = static_cast<float>(SceneView.SceneRTHeight);
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };

	// ------------------------------------------------------------------------------------------------------------------------

	pCmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, nullptr);

	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);

	pCmd->SetGraphicsRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ZPrePass));
	pCmd->SetGraphicsRootConstantBufferView(4, perViewCBAddr);

	const size_t iMSAA = bMSAA ? 1 : 0;
	const size_t iFaceCull = 2; // 2:back
	PSO_ID psoIDPrev = INVALID_ID;
	BufferID vbPrev = INVALID_ID;
	BufferID ibPrev = INVALID_ID;
	D3D_PRIMITIVE_TOPOLOGY topoPrev = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
	for (const FInstancedDrawParameters& meshRenderCmd : SceneDrawData.mainViewDrawParams)
	{
		size_t iAlpha = 0; size_t iRaster = 0; size_t iFaceCull = 0;
		meshRenderCmd.UnpackMaterialConfig(iAlpha, iRaster, iFaceCull);

		size_t iTess = 0; size_t iDomain = 0; size_t iPart = 0; size_t iOutTopo = 0; size_t iTessCull = 0;
		meshRenderCmd.UnpackTessellationConfig(iTess, iDomain, iPart, iOutTopo, iTessCull);

		const PSO_ID psoID = this->mZPrePassPSOs.Get(iMSAA, iRaster, iFaceCull, iTess, iDomain, iPart, iOutTopo, iTessCull, iAlpha);

		if (psoIDPrev != psoID)
		{
			pCmd->SetPipelineState(this->GetPSO(psoID));
		}

		pCmd->SetGraphicsRootConstantBufferView(1, meshRenderCmd.cbAddr);
		if (meshRenderCmd.cbAddr_Tessellation)
			pCmd->SetGraphicsRootConstantBufferView(2, meshRenderCmd.cbAddr_Tessellation);
		if (meshRenderCmd.SRVMaterialMaps != INVALID_ID) // set textures
		{
			const CBV_SRV_UAV& HeightMapSRV = this->GetSRV(meshRenderCmd.SRVHeightMap == INVALID_ID ? rsc.SRV_NullTexture2D : meshRenderCmd.SRVHeightMap);
			pCmd->SetGraphicsRootDescriptorTable(0, this->GetSRV(meshRenderCmd.SRVMaterialMaps).GetGPUDescHandle(0));
			pCmd->SetGraphicsRootDescriptorTable(3, HeightMapSRV.GetGPUDescHandle(0));
		}

		if (topoPrev != meshRenderCmd.IATopology)
		{
			pCmd->IASetPrimitiveTopology(meshRenderCmd.IATopology);
		}
		if (vbPrev != meshRenderCmd.VB)
		{
			const VBV& vb = GetVertexBufferView(meshRenderCmd.VB);
			pCmd->IASetVertexBuffers(0, 1, &vb);
		}
		if (ibPrev != meshRenderCmd.IB)
		{
			const IBV& ib = GetIndexBufferView(meshRenderCmd.IB);
			pCmd->IASetIndexBuffer(&ib);
		}

		pCmd->DrawIndexedInstanced(meshRenderCmd.numIndices, meshRenderCmd.numInstances, 0, 0, 0);

		psoIDPrev = psoID;
		ibPrev = meshRenderCmd.IB;
		vbPrev = meshRenderCmd.VB;
		topoPrev = meshRenderCmd.IATopology;
	}
}


void VQRenderer::TransitionDepthPrePassForWrite(ID3D12GraphicsCommandList* pCmd, bool bMSAA)
{
	SCOPED_GPU_MARKER(pCmd, "TransitionDepthPrePassForWrite");
	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();

	auto pRscNormals     = this->GetTextureResource(rsc.Tex_SceneNormals);
	auto pRscNormalsMSAA = this->GetTextureResource(rsc.Tex_SceneNormalsMSAA);
	auto pRscDepthResolve= this->GetTextureResource(rsc.Tex_SceneDepthResolve);
	auto pRscDepthMSAA   = this->GetTextureResource(rsc.Tex_SceneDepthMSAA);
	auto pRscDepth       = this->GetTextureResource(rsc.Tex_SceneDepth);

	std::vector<CD3DX12_RESOURCE_BARRIER> Barriers;
	if (!bMSAA)
	{
		Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscNormals, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
		pCmd->ResourceBarrier((UINT32)Barriers.size(), Barriers.data());
	}
}

void VQRenderer::TransitionDepthPrePassForRead(ID3D12GraphicsCommandList* pCmd, bool bMSAA)
{
	SCOPED_GPU_MARKER(pCmd, "TransitionDepthPrePassForRead");
	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();

	auto pRscNormals     = this->GetTextureResource(rsc.Tex_SceneNormals);
	auto pRscNormalsMSAA = this->GetTextureResource(rsc.Tex_SceneNormalsMSAA);
	auto pRscDepthResolve= this->GetTextureResource(rsc.Tex_SceneDepthResolve);
	auto pRscDepthMSAA   = this->GetTextureResource(rsc.Tex_SceneDepthMSAA);
	auto pRscDepth       = this->GetTextureResource(rsc.Tex_SceneDepth);

	std::vector<CD3DX12_RESOURCE_BARRIER> Barriers;
	if (bMSAA)
	{
		Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthMSAA, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));
		Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscNormalsMSAA, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
		
		Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthResolve, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
		Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscNormals, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	}
	else
	{
		Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE));
		Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthResolve, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST));
		Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscNormals, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	}

	pCmd->ResourceBarrier((UINT32)Barriers.size(), Barriers.data());
}

void VQRenderer::TransitionDepthPrePassForReadAsyncCompute(ID3D12GraphicsCommandList* pCmd)
{
	SCOPED_GPU_MARKER(pCmd, "TransitionDepthPrePassForReadAsyncCompute");
	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();

	auto pRscNormals = this->GetTextureResource(rsc.Tex_SceneNormals);
	auto pRscNormalsMSAA = this->GetTextureResource(rsc.Tex_SceneNormalsMSAA);
	auto pRscDepthResolve = this->GetTextureResource(rsc.Tex_SceneDepthResolve);
	auto pRscDepthMSAA = this->GetTextureResource(rsc.Tex_SceneDepthMSAA);
	auto pRscDepth = this->GetTextureResource(rsc.Tex_SceneDepth);

	std::vector<CD3DX12_RESOURCE_BARRIER> Barriers;
	Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthResolve, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscNormals, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	pCmd->ResourceBarrier((UINT32)Barriers.size(), Barriers.data());
}

void VQRenderer::TransitionDepthPrePassMSAAResolve(ID3D12GraphicsCommandList* pCmd)
{
	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();

	auto pRscNormals     = this->GetTextureResource(rsc.Tex_SceneNormals);
	auto pRscNormalsMSAA = this->GetTextureResource(rsc.Tex_SceneNormalsMSAA);
	auto pRscDepthResolve= this->GetTextureResource(rsc.Tex_SceneDepthResolve);
	auto pRscDepthMSAA   = this->GetTextureResource(rsc.Tex_SceneDepthMSAA);
	auto pRscDepth       = this->GetTextureResource(rsc.Tex_SceneDepth);
	
	const CD3DX12_RESOURCE_BARRIER pBarriers[] =
	{
		// write->read
		  CD3DX12_RESOURCE_BARRIER::Transition(pRscNormalsMSAA , D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthMSAA   , D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		// read->write
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscNormals     , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthResolve, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
	};
	pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
}

void VQRenderer::ResolveMSAA_DepthPrePass(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap)
{
	SCOPED_GPU_MARKER(pCmd, "MSAAResolve<Depth=%d, Normals=%d, Roughness=%d>"); // TODO: string formatting

	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();

	DepthMSAAResolvePass::FDrawParameters params;
	params.pCmd = pCmd;
	params.pCBufferHeap = pCBufferHeap;
	params.SRV_MSAADepth = rsc.SRV_SceneDepthMSAA;
	params.SRV_MSAANormals = rsc.SRV_SceneNormalsMSAA;
	params.SRV_MSAARoughness = rsc.SRV_SceneColorMSAA;
	params.UAV_ResolvedDepth = rsc.UAV_SceneDepth;
	params.UAV_ResolvedNormals = rsc.UAV_SceneNormals;
	params.UAV_ResolvedRoughness = INVALID_ID;
	this->GetRenderPass(ERenderPass::DepthMSAAResolve)->RecordCommands(&params);
}

// ------------------------------------------------------------------------------------------------------------
// This CopyDepthForCompute pass is, in theory, not necessary.
// However, since CACAO pass initializes resource views OnCreateWindowSizeDependentResources
// and uses the Tex_SceneDepthResolve resource as input, assuming MSAA=on, we have two choices:
//   (1) re-initialize CACAO depth input resource view on MSAA change
//   (2) copy depth
// 
// Opting in for (2) for a quick fix for the MSAA=off case.
// (1) is the correct solution -- it involves syncing w/ GPU to update the view so the correct resource is used.
// 
// Note that we have 3 depth resources
// - Tex_SceneDepthMSAA    : DSV + SRV        | MSAA target
// - Tex_SceneDepth        : DSV + SRV        | non-MSAA target
// - Tex_SceneDepthResolve : SRV + UAV Write  | MSAA resolve target
void VQRenderer::CopyDepthForCompute(ID3D12GraphicsCommandList* pCmd)
{
	SCOPED_GPU_MARKER(pCmd, "CopyDepthForCompute");
	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();

	auto pRscDepthResolve= this->GetTextureResource(rsc.Tex_SceneDepthResolve);
	auto pRscDepth       = this->GetTextureResource(rsc.Tex_SceneDepth);

	pCmd->CopyResource(pRscDepthResolve, pRscDepth);
	
	std::vector<CD3DX12_RESOURCE_BARRIER> Barriers;
	Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthResolve, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepth, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));
	pCmd->ResourceBarrier((UINT32)Barriers.size(), Barriers.data());
}
// ------------------------------------------------------------------------------------------------------------

void VQRenderer::RenderAmbientOcclusion(ID3D12GraphicsCommandList* pCmd, const FSceneView& SceneView, const FGraphicsSettings& GFXSettings, bool bAsyncCompute)
{
	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();

	const bool& bMSAA = GFXSettings.bAntiAliasing;
	
	std::shared_ptr<AmbientOcclusionPass> pAOPass = std::static_pointer_cast<AmbientOcclusionPass>(this->GetRenderPass(ERenderPass::AmbientOcclusion));
	ID3D12Resource* pRscAmbientOcclusion = this->GetTextureResource(rsc.Tex_AmbientOcclusion);
	const UAV& uav = this->GetUAV(rsc.UAV_FFXCACAO_Out);

	const char* pStrPass[AmbientOcclusionPass::EMethod::NUM_AMBIENT_OCCLUSION_METHODS] =
	{
		"Ambient Occlusion (FidelityFX CACAO)"
	};
	SCOPED_GPU_MARKER(pCmd, pStrPass[pAOPass->GetMethod()]);
	
	static bool sbScreenSpaceAO_Previous = false;
	const bool bSSAOToggledOff = sbScreenSpaceAO_Previous && !SceneView.sceneRenderOptions.bScreenSpaceAO;

	if (SceneView.sceneRenderOptions.bScreenSpaceAO)
	{
		AmbientOcclusionPass::FDrawParameters drawParams = {};
		DirectX::XMStoreFloat4x4(&drawParams.matNormalToView, SceneView.view);
		DirectX::XMStoreFloat4x4(&drawParams.matProj, SceneView.proj);
		drawParams.pCmd = pCmd;
		drawParams.bAsyncCompute = bAsyncCompute;

		if (!bAsyncCompute)
		{
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(pRscAmbientOcclusion, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			pCmd->ResourceBarrier(1, &barrier);
		}
		pAOPass->RecordCommands(&drawParams);

		// set back the descriptor heap for the VQEngine
		ID3D12DescriptorHeap* ppHeaps[] = { this->GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };
		pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	}
	
	// clear UAV only once
	if(bSSAOToggledOff)
	{
		CD3DX12_RESOURCE_BARRIER barrierRW = CD3DX12_RESOURCE_BARRIER::Transition(pRscAmbientOcclusion, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		CD3DX12_RESOURCE_BARRIER barrierWR = CD3DX12_RESOURCE_BARRIER::Transition(pRscAmbientOcclusion, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
		const FLOAT clearValue[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

		pCmd->ResourceBarrier(1, &barrierRW);
		pCmd->ClearUnorderedAccessViewFloat(uav.GetGPUDescHandle(), uav.GetCPUDescHandle(), pRscAmbientOcclusion, clearValue, 0, NULL);
		pCmd->ResourceBarrier(1, &barrierWR);
	}

	sbScreenSpaceAO_Previous = SceneView.sceneRenderOptions.bScreenSpaceAO;
}

static bool IsFFX_SSSREnabled(const FGraphicsSettings& GFXSettings)
{
	return GFXSettings.Reflections == EReflections::SCREEN_SPACE_REFLECTIONS__FFX;
}
bool VQRenderer::ShouldUseMotionVectorsTarget(const FGraphicsSettings& GFXSettings)
{
	return IsFFX_SSSREnabled(GFXSettings);
}
bool VQRenderer::ShouldUseVisualizationTarget(const FPostProcessParameters& PPParams)
{
	return PPParams.DrawModeEnum == EDrawMode::ALBEDO
		|| PPParams.DrawModeEnum == EDrawMode::ROUGHNESS
		|| PPParams.DrawModeEnum == EDrawMode::METALLIC;
}
void VQRenderer::TransitionForSceneRendering(ID3D12GraphicsCommandList* pCmd, FWindowRenderContext& ctx, const FPostProcessParameters& PPParams, const FGraphicsSettings& GFXSettings)
{
	const bool& bMSAA = GFXSettings.bAntiAliasing;

	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();

	auto pRscColor           = this->GetTextureResource(rsc.Tex_SceneColor);
	auto pRscColorMSAA       = this->GetTextureResource(rsc.Tex_SceneColorMSAA);
	auto pRscViz             = this->GetTextureResource(rsc.Tex_SceneVisualization);
	auto pRscVizMSAA         = this->GetTextureResource(rsc.Tex_SceneVisualizationMSAA);
	auto pRscMoVecMSAA       = this->GetTextureResource(rsc.Tex_SceneMotionVectorsMSAA);
	auto pRscMoVec           = this->GetTextureResource(rsc.Tex_SceneMotionVectors);
	auto pRscNormals         = this->GetTextureResource(rsc.Tex_SceneNormals);
	auto pRscNormalsMSAA     = this->GetTextureResource(rsc.Tex_SceneNormalsMSAA);
	auto pRscShadowMaps_Spot = this->GetTextureResource(rsc.Tex_ShadowMaps_Spot);
	auto pRscShadowMaps_Point= this->GetTextureResource(rsc.Tex_ShadowMaps_Point);
	auto pRscShadowMaps_Directional = this->GetTextureResource(rsc.Tex_ShadowMaps_Directional);

	SCOPED_GPU_MARKER(pCmd, "TransitionForSceneRendering");

	CD3DX12_RESOURCE_BARRIER ColorTransition = bMSAA
		? CD3DX12_RESOURCE_BARRIER::Transition(pRscColorMSAA, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
		: CD3DX12_RESOURCE_BARRIER::Transition(pRscColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	std::vector<CD3DX12_RESOURCE_BARRIER> vBarriers;
	vBarriers.push_back(ColorTransition);
	vBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Spot, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	vBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Point, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	vBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Directional, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

	if (ShouldUseVisualizationTarget(PPParams))
	{
		vBarriers.push_back(bMSAA
			? CD3DX12_RESOURCE_BARRIER::Transition(pRscVizMSAA, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
			: CD3DX12_RESOURCE_BARRIER::Transition(pRscViz, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
		);
	}
	if (ShouldUseMotionVectorsTarget(GFXSettings))
	{
		vBarriers.push_back(bMSAA
			? CD3DX12_RESOURCE_BARRIER::Transition(pRscMoVecMSAA, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
			: CD3DX12_RESOURCE_BARRIER::Transition(pRscMoVec, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
		);
	}

	pCmd->ResourceBarrier((UINT)vBarriers.size(), vBarriers.data());
}

void VQRenderer::RenderSceneColor(
	ID3D12GraphicsCommandList* pCmd,
	DynamicBufferHeap* pCBufferHeap,
	const FSceneView& SceneView, 
	const FPostProcessParameters& PPParams,
	D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr,
	D3D12_GPU_VIRTUAL_ADDRESS perFrameCBAddr, 
	const FGraphicsSettings& GFXSettings,
	bool bHDR
)
{
	SCOPED_GPU_MARKER(pCmd, "RenderSceneColor");

	struct FFrameConstantBuffer { DirectX::XMMATRIX matModelViewProj; };
	struct FFrameConstantBuffer2 { DirectX::XMMATRIX matModelViewProj; int iTextureConfig; int iTextureOutput; };

	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();

	const bool bHDRDisplay = bHDR; // TODO: this->ShouldRenderHDR(pWindow->GetHWND());
	const bool bHasEnvironmentMapHDRTexture = rsc.EnvironmentMap.SRV_HDREnvironment != INVALID_ID;
	const bool bDrawEnvironmentMap = bHasEnvironmentMapHDRTexture && rsc.SRV_BRDFIntegrationLUT != INVALID_ID;
	const bool bUseVisualizationRenderTarget = ShouldUseVisualizationTarget(PPParams);
	const bool bRenderMotionVectors = ShouldUseMotionVectorsTarget(GFXSettings);
	const bool bRenderScreenSpaceReflections = IsFFX_SSSREnabled(GFXSettings);

	const bool& bMSAA = GFXSettings.bAntiAliasing;

	const RTV& rtvColor = this->GetRTV(bMSAA ? rsc.RTV_SceneColorMSAA : rsc.RTV_SceneColor);
	const RTV& rtvColorViz = this->GetRTV(bMSAA ? rsc.RTV_SceneVisualizationMSAA : rsc.RTV_SceneVisualization);
	const RTV& rtvMoVec = this->GetRTV(bMSAA ? rsc.RTV_SceneMotionVectorsMSAA : rsc.RTV_SceneMotionVectors);

	const DSV& dsvColor = this->GetDSV(bMSAA ? rsc.DSV_SceneDepthMSAA : rsc.DSV_SceneDepth);
	auto pRscDepth = this->GetTextureResource(rsc.Tex_SceneDepth);

	const CBV_SRV_UAV& NullCubemapSRV = this->GetSRV(rsc.SRV_NullCubemap);
	const CBV_SRV_UAV& NullTex2DSRV = this->GetSRV(rsc.SRV_NullTexture2D);

	const FSceneDrawData& SceneDrawData = mFrameSceneDrawData[0]; // [0] since we don't have parallel update+render

	// Clear Depth & Render targets
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvColor.GetCPUDescHandle();
	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles(1, rtvColor.GetCPUDescHandle());
	if (bUseVisualizationRenderTarget) rtvHandles.push_back(rtvColorViz.GetCPUDescHandle());
	if (bRenderMotionVectors)          rtvHandles.push_back(rtvMoVec.GetCPUDescHandle());
	
	{
		SCOPED_GPU_MARKER(pCmd, "Clear");
		for (D3D12_CPU_DESCRIPTOR_HANDLE& rtv : rtvHandles)
			pCmd->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
	}
	
	pCmd->OMSetRenderTargets((UINT)rtvHandles.size(), rtvHandles.data(), FALSE, &dsvHandle);

	// Set Viewport & Scissors
	const float RenderResolutionX = static_cast<float>(SceneView.SceneRTWidth);
	const float RenderResolutionY = static_cast<float>(SceneView.SceneRTHeight);
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);

	const size_t iMSAA = bMSAA ? 1 : 0;
	const size_t iFaceCull = 2; // 2:back
	const size_t iOutMoVec = bRenderMotionVectors ? 1 : 0;
	const size_t iOutRough = bUseVisualizationRenderTarget ? 1 : 0;
	
	pCmd->SetGraphicsRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ForwardLighting));

	// set PerFrame constants
	constexpr UINT PerFrameRSBindSlot = 3;
	constexpr UINT PerViewRSBindSlot = 2;
	{
		SCOPED_GPU_MARKER(pCmd, "CBPerFrame");
		pCmd->SetGraphicsRootConstantBufferView(PerFrameRSBindSlot, perFrameCBAddr);
		pCmd->SetGraphicsRootDescriptorTable(4, this->GetSRV(rsc.SRV_ShadowMaps_Spot).GetGPUDescHandle(0));
		pCmd->SetGraphicsRootDescriptorTable(5, this->GetSRV(rsc.SRV_ShadowMaps_Point).GetGPUDescHandle(0));
		pCmd->SetGraphicsRootDescriptorTable(6, this->GetSRV(rsc.SRV_ShadowMaps_Directional).GetGPUDescHandle(0));
		pCmd->SetGraphicsRootDescriptorTable(7, bDrawEnvironmentMap
			? this->GetSRV(rsc.EnvironmentMap.SRV_IrradianceDiffBlurred).GetGPUDescHandle(0)
			: NullCubemapSRV.GetGPUDescHandle()
		);
		pCmd->SetGraphicsRootDescriptorTable(8, bDrawEnvironmentMap
			? this->GetSRV(rsc.EnvironmentMap.SRV_IrradianceSpec).GetGPUDescHandle(0)
			: NullCubemapSRV.GetGPUDescHandle()
		);
		pCmd->SetGraphicsRootDescriptorTable(9, bDrawEnvironmentMap
			? this->GetSRV(rsc.SRV_BRDFIntegrationLUT).GetGPUDescHandle()
			: NullTex2DSRV.GetGPUDescHandle()
		);
		pCmd->SetGraphicsRootDescriptorTable(10, this->GetSRV(rsc.SRV_FFXCACAO_Out).GetGPUDescHandle());
	}

	// set PerView constants
	{
		SCOPED_GPU_MARKER(pCmd, "CBPerView");
		pCmd->SetGraphicsRootConstantBufferView(PerViewRSBindSlot, perViewCBAddr);
	}

	// Draw Objects -----------------------------------------------
	if(!SceneDrawData.mainViewDrawParams.empty())
	{
		constexpr UINT PerObjRSBindSlot = 1;
		SCOPED_GPU_MARKER(pCmd, "Geometry");

		int iCB = 0;
		PSO_ID psoID_Prev = INVALID_ID;
		BufferID vbPrev = INVALID_ID;
		BufferID ibPrev = INVALID_ID;
		D3D_PRIMITIVE_TOPOLOGY topoPrev = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		for (const FInstancedDrawParameters& meshRenderCmd : SceneDrawData.mainViewDrawParams)
		{
			size_t iAlpha = 0; size_t iRaster = 0; size_t iFaceCull = 0;
			meshRenderCmd.UnpackMaterialConfig(iAlpha, iRaster, iFaceCull);
			size_t iTess = 0; size_t iDomain = 0; size_t iPart = 0; size_t iOutTopo = 0; size_t iTessCull = 0;
			meshRenderCmd.UnpackTessellationConfig(iTess, iDomain, iPart, iOutTopo, iTessCull);

			PSO_ID psoID = this->mLightingPSOs.Get(iMSAA, iRaster, iFaceCull, iOutMoVec, iOutRough, iTess, iDomain, iPart, iOutTopo, iTessCull, iAlpha);
			ID3D12PipelineState* pPipelineState = this->GetPSO(psoID);
			
			if(psoID_Prev != psoID) // TODO: profile PSO
			{
				pCmd->SetPipelineState(pPipelineState);
			}

			pCmd->SetGraphicsRootConstantBufferView(PerObjRSBindSlot, meshRenderCmd.cbAddr);

			if (meshRenderCmd.SRVMaterialMaps != INVALID_ID) // set textures
			{
				pCmd->SetGraphicsRootDescriptorTable(0, this->GetSRV(meshRenderCmd.SRVMaterialMaps).GetGPUDescHandle(0));
				//pCmd->SetGraphicsRootDescriptorTable(4, this->GetSRV(mat.SRVMaterialMaps).GetGPUDescHandle(0));
			}
			if (meshRenderCmd.cbAddr_Tessellation)
			{
				pCmd->SetGraphicsRootConstantBufferView(12, meshRenderCmd.cbAddr_Tessellation);
			}

			pCmd->SetGraphicsRootDescriptorTable(11, meshRenderCmd.SRVHeightMap == INVALID_ID
				? NullTex2DSRV.GetGPUDescHandle()
				: this->GetSRV(meshRenderCmd.SRVHeightMap).GetGPUDescHandle(0)
			);
			
			if (topoPrev != meshRenderCmd.IATopology)
			{
				topoPrev = meshRenderCmd.IATopology;
				pCmd->IASetPrimitiveTopology(meshRenderCmd.IATopology);
			}
			if (vbPrev != meshRenderCmd.VB)
			{
				const VBV& vb = GetVertexBufferView(meshRenderCmd.VB);
				pCmd->IASetVertexBuffers(0, 1, &vb);
			}
			if (ibPrev != meshRenderCmd.IB)
			{
				const IBV& ib = GetIndexBufferView(meshRenderCmd.IB);
				pCmd->IASetIndexBuffer(&ib);
			}

			pCmd->DrawIndexedInstanced(meshRenderCmd.numIndices, meshRenderCmd.numInstances, 0, 0, 0);

			psoID_Prev = psoID;
			ibPrev = meshRenderCmd.IB;
			vbPrev = meshRenderCmd.VB;
			topoPrev = meshRenderCmd.IATopology;
		}
	}

	// Draw Light Meshes ------------------------------------------
	if(!SceneDrawData.lightRenderParams.empty())
	{
		SCOPED_GPU_MARKER(pCmd, "Lights");
		pCmd->SetPipelineState(this->GetPSO(bMSAA ? EBuiltinPSOs::UNLIT_PSO_MSAA_4 : EBuiltinPSOs::UNLIT_PSO));
		
		pCmd->SetGraphicsRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__WireframeUnlit));

		for (const FLightRenderData& lightRenderCmd : SceneDrawData.lightRenderParams)
		{
			FFrameConstantBufferUnlit* pCBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			pCBufferHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer)), (void**)(&pCBuffer), &cbAddr);
			pCBuffer->color            = lightRenderCmd.color;
			pCBuffer->matModelViewProj = lightRenderCmd.matWorldTransformation * SceneView.viewProj;
			pCmd->SetGraphicsRootConstantBufferView(0, cbAddr);

			const Mesh& mesh = *lightRenderCmd.pMesh;
			const int LastLOD = mesh.GetNumLODs() - 1;
			const auto VBIBIDs = mesh.GetIABufferIDs(LastLOD);
			const uint32 NumIndices = mesh.GetNumIndices(LastLOD);
			const uint32 NumInstances = 1;
			const BufferID& VB_ID = VBIBIDs.first;
			const BufferID& IB_ID = VBIBIDs.second;
			const VBV& vb = this->GetVertexBufferView(VB_ID);
			const IBV& ib = this->GetIndexBufferView(IB_ID);

			pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmd->IASetVertexBuffers(0, 1, &vb);
			pCmd->IASetIndexBuffer(&ib);
			pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
		}
	}

	// Draw Environment Map ---------------------------------------
	if (bDrawEnvironmentMap)
	{
		SCOPED_GPU_MARKER(pCmd, "EnvironmentMap");

		ID3D12DescriptorHeap* ppHeaps[] = { this->GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };

		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		FFrameConstantBuffer * pConstBuffer = {};
		pCBufferHeap->AllocConstantBuffer(sizeof(FFrameConstantBuffer ), (void**)(&pConstBuffer), &cbAddr);
		pConstBuffer->matModelViewProj = SceneView.EnvironmentMapViewProj;

		pCmd->SetPipelineState(this->GetPSO(bMSAA ? EBuiltinPSOs::SKYDOME_PSO_MSAA_4 : EBuiltinPSOs::SKYDOME_PSO));
		pCmd->SetGraphicsRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__HelloWorldCube));

		pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		pCmd->SetGraphicsRootDescriptorTable(0, this->GetSRV(rsc.EnvironmentMap.SRV_HDREnvironment).GetGPUDescHandle());
		pCmd->SetGraphicsRootConstantBufferView(1, cbAddr);

		const UINT NumIndices = SceneView.pEnvironmentMapMesh->GetNumIndices();
		auto VBIB = SceneView.pEnvironmentMapMesh->GetIABufferIDs();
		auto vb = this->GetVertexBufferView(VBIB.first);
		auto ib = this->GetIndexBufferView(VBIB.second);

		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmd->IASetVertexBuffers(0, 1, &vb);
		pCmd->IASetIndexBuffer(&ib);

		pCmd->DrawIndexedInstanced(NumIndices, 1, 0, 0, 0);
	}
}

void VQRenderer::RenderBoundingBoxes(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, bool bMSAA)
{
	struct FFrameConstantBufferUnlitInstanced { DirectX::XMMATRIX matModelViewProj[MAX_INSTANCE_COUNT__UNLIT_SHADER]; DirectX::XMFLOAT4 color; };
	const FSceneDrawData& SceneDrawData = mFrameSceneDrawData[0]; // [0] since we don't have parallel update+render

	if (!SceneDrawData.boundingBoxRenderParams.empty())
	{
		SCOPED_GPU_MARKER(pCmd, "BoundingBoxes");


		pCmd->SetGraphicsRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__WireframeUnlit));

		// set IA
		const FInstancedBoundingBoxRenderData& params = *SceneDrawData.boundingBoxRenderParams.begin();
		const auto VBIBIDs = params.vertexIndexBuffer;
		const uint32 NumIndices = params.numIndices;
		const BufferID& VB_ID = VBIBIDs.first;
		const BufferID& IB_ID = VBIBIDs.second;
		const VBV& vb = this->GetVertexBufferView(VB_ID);
		const IBV& ib = this->GetIndexBufferView(IB_ID);

		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmd->IASetVertexBuffers(0, 1, &vb);
		pCmd->IASetIndexBuffer(&ib);

#if RENDER_INSTANCED_BOUNDING_BOXES
		pCmd->SetPipelineState(this->GetPSO(bMSAA
			? EBuiltinPSOs::WIREFRAME_INSTANCED_MSAA4_PSO
			: EBuiltinPSOs::WIREFRAME_INSTANCED_PSO)
		);

		for (const FInstancedBoundingBoxRenderData& BBRenderCmd : SceneDrawData.boundingBoxRenderParams)
		{
			const int NumInstances = (int)BBRenderCmd.matWorldViewProj.size();
			if (NumInstances == 0)
				continue; // shouldnt happen

			assert(NumInstances <= MAX_INSTANCE_COUNT__UNLIT_SHADER);

			FFrameConstantBufferUnlitInstanced* pCBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			pCBufferHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer)), (void**)(&pCBuffer), &cbAddr);
			pCBuffer->color = BBRenderCmd.color;
			memcpy(pCBuffer->matModelViewProj, BBRenderCmd.matWorldViewProj.data(), sizeof(DirectX::XMMATRIX)* NumInstances);

			pCmd->SetGraphicsRootConstantBufferView(0, cbAddr);
			pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
		}

#else	
		pCmd->SetPipelineState(this->GetPSO(bMSAA
			? EBuiltinPSOs::WIREFRAME_PSO_MSAA_4
			: EBuiltinPSOs::WIREFRAME_PSO)
		);

		const uint32 NumInstances = 1;
		for (const FBoundingBoxRenderData& BBRenderCmd : SceneView.boundingBoxRenderParams)
		{
			// set constant buffer data
			FFrameConstantBufferUnlit* pCBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			pCBufferHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer)), (void**)(&pCBuffer), &cbAddr);
			pCBuffer->color = BBRenderCmd.color;
			pCBuffer->matModelViewProj = BBRenderCmd.matWorldTransformation * SceneView.viewProj;
			pCmd->SetGraphicsRootConstantBufferView(0, cbAddr);
			pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
		}
#endif
	}
}

void VQRenderer::RenderOutline(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr, const FSceneView& SceneView, bool bMSAA, const std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>& rtvHandles)
{
	SCOPED_GPU_MARKER(pCmd, "RenderOutlinePass");
	const FSceneDrawData& SceneDrawData = mFrameSceneDrawData[0]; // [0] since we don't have parallel update+render

	OutlinePass::FDrawParameters params;
	params.pCmd = pCmd;
	params.pCBufferHeap = pCBufferHeap;
	params.cbPerView = perViewCBAddr;
	params.pSceneView = &SceneView;
	params.bMSAA = bMSAA;
	params.pRTVHandles = &rtvHandles;
	params.pSceneDrawData = &SceneDrawData;
	this->GetRenderPass(ERenderPass::Outline)->RecordCommands(&params);
}

void VQRenderer::RenderLightBounds(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, bool bMSAA, bool bReflectionsEnabled)
{
	const FSceneDrawData& SceneDrawData = mFrameSceneDrawData[0]; // [0] since we don't have parallel update+render

	if (!SceneDrawData.lightBoundsRenderParams.empty())
	{
		SCOPED_GPU_MARKER(pCmd, "LightBounds");
		const uint32 NumInstances = 1;
		if (bReflectionsEnabled)
		{
			// With reflections, we will be writing light bounds into a separate render target and blend at the
			// compositing pass at the end. We do this to avoid having the light bounds and other debugging effects
			// appear in the reflections.
			pCmd->SetPipelineState(this->GetPSO(bMSAA ? EBuiltinPSOs::UNLIT_PSO_MSAA_4 : EBuiltinPSOs::UNLIT_PSO));
		}
		else
		{
			// Without reflections we use an unlit PSO w/ blending enabled so we directly write into the color buffer.
			pCmd->SetPipelineState(this->GetPSO(bMSAA ? EBuiltinPSOs::UNLIT_BLEND_PSO_MSAA_4 : EBuiltinPSOs::UNLIT_BLEND_PSO));
		}
		pCmd->SetGraphicsRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__WireframeUnlit));

		std::vector< D3D12_GPU_VIRTUAL_ADDRESS> cbAddresses(SceneDrawData.lightBoundsRenderParams.size());
		int iCbAddress = 0;
		for (const FLightRenderData& lightBoundRenderCmd : SceneDrawData.lightBoundsRenderParams)
		{
			// set constant buffer data
			FFrameConstantBufferUnlit* pCBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS& cbAddr = cbAddresses[iCbAddress++];
			pCBufferHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer)), (void**)(&pCBuffer), &cbAddr);
			pCBuffer->color = lightBoundRenderCmd.color;
			pCBuffer->color.w *= 0.01f;
			pCBuffer->matModelViewProj = lightBoundRenderCmd.matWorldTransformation * SceneView.viewProj;

			// set IA
			const Mesh& mesh = *lightBoundRenderCmd.pMesh;

			const auto VBIBIDs = mesh.GetIABufferIDs();
			const uint32 NumIndices = mesh.GetNumIndices();
			const BufferID& VB_ID = VBIBIDs.first;
			const BufferID& IB_ID = VBIBIDs.second;
			const VBV& vb = this->GetVertexBufferView(VB_ID);
			const IBV& ib = this->GetIndexBufferView(IB_ID);

			pCmd->SetGraphicsRootConstantBufferView(0, cbAddr);
			pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmd->IASetVertexBuffers(0, 1, &vb);
			pCmd->IASetIndexBuffer(&ib);
			pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
		}

		pCmd->SetPipelineState(this->GetPSO(bMSAA ? EBuiltinPSOs::WIREFRAME_PSO_MSAA_4 : EBuiltinPSOs::WIREFRAME_PSO));
		for (const FLightRenderData& lightBoundRenderCmd : SceneDrawData.lightBoundsRenderParams)
		{
			FFrameConstantBufferUnlit* pCBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			pCBufferHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer)), (void**)(&pCBuffer), &cbAddr);
			pCBuffer->color = lightBoundRenderCmd.color;
			pCBuffer->matModelViewProj = lightBoundRenderCmd.matWorldTransformation * SceneView.viewProj;

			const Mesh& mesh = *lightBoundRenderCmd.pMesh;

			const auto VBIBIDs = mesh.GetIABufferIDs();
			const uint32 NumIndices = mesh.GetNumIndices();
			const BufferID& VB_ID = VBIBIDs.first;
			const BufferID& IB_ID = VBIBIDs.second;
			const VBV& vb = this->GetVertexBufferView(VB_ID);
			const IBV& ib = this->GetIndexBufferView(IB_ID);

			pCmd->SetGraphicsRootConstantBufferView(0, cbAddr);
			pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmd->IASetVertexBuffers(0, 1, &vb);
			pCmd->IASetIndexBuffer(&ib);
			pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
		}
	}
}

void VQRenderer::RenderDebugVertexAxes(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, bool bMSAA)
{
	struct FObjectConstantBufferDebugVertexVectors
	{
		DirectX::XMMATRIX matWorld;
		DirectX::XMMATRIX matNormal;
		DirectX::XMMATRIX matViewProj;
		float LocalAxisSize;
	};

	const FSceneDrawData& SceneDrawData = mFrameSceneDrawData[0]; // [0] since we don't have parallel update+render

	if (!SceneView.sceneRenderOptions.bDrawVertexLocalAxes || SceneDrawData.debugVertexAxesRenderParams.empty())
	{
		return;
	}

	pCmd->SetGraphicsRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ZPrePass));
	pCmd->SetPipelineState(this->GetPSO(bMSAA ? EBuiltinPSOs::DEBUGVERTEX_LOCALSPACEVECTORS_PSO_MSAA_4 : EBuiltinPSOs::DEBUGVERTEX_LOCALSPACEVECTORS_PSO));
	for (const MeshRenderData_t& cmd : SceneDrawData.debugVertexAxesRenderParams)
	{
		FObjectConstantBufferDebugVertexVectors* pCBuffer = {};
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		pCBufferHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer)), (void**)(&pCBuffer), &cbAddr);
		pCBuffer->matWorld    = cmd.matWorld [0];
		pCBuffer->matNormal   = cmd.matNormal[0];
		pCBuffer->matViewProj = SceneView.viewProj;
		pCBuffer->LocalAxisSize = SceneView.sceneRenderOptions.fVertexLocalAxixSize;
		
		const uint32 NumInstances = 1;
		const uint32 NumIndices = cmd.numIndices;
		const VBV& vb = this->GetVertexBufferView(cmd.vertexIndexBuffer.first);
		const IBV& ib = this->GetIndexBufferView(cmd.vertexIndexBuffer.second);

		pCmd->SetGraphicsRootConstantBufferView(1, cbAddr);
		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
		pCmd->IASetVertexBuffers(0, 1, &vb);
		pCmd->IASetIndexBuffer(&ib);
		pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
	}
}

void VQRenderer::ResolveMSAA(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FPostProcessParameters& PPParams, const FGraphicsSettings& GFXSettings)
{
	const bool& bMSAA = GFXSettings.bAntiAliasing;

	if (!bMSAA)
		return;

	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow(); // shorthand

	SCOPED_GPU_MARKER(pCmd, "ResolveMSAA");
	auto pRscColor     = this->GetTextureResource(rsc.Tex_SceneColor);
	auto pRscColorMSAA = this->GetTextureResource(rsc.Tex_SceneColorMSAA);
	auto pRscViz       = this->GetTextureResource(rsc.Tex_SceneVisualization);
	auto pRscVizMSAA   = this->GetTextureResource(rsc.Tex_SceneVisualizationMSAA);
	auto pRscMoVec     = this->GetTextureResource(rsc.Tex_SceneMotionVectors);
	auto pRscMoVecMSAA = this->GetTextureResource(rsc.Tex_SceneMotionVectorsMSAA);
	auto pRscDepthMSAA = this->GetTextureResource(rsc.Tex_SceneDepthMSAA);

	const bool bUseVizualization = ShouldUseVisualizationTarget(PPParams);
	const bool bRenderMotionVectors = ShouldUseMotionVectorsTarget(GFXSettings);
	const bool bResolveRoughness = IsFFX_SSSREnabled(GFXSettings) && false;

	// transition barriers
	{
		SCOPED_GPU_MARKER(pCmd, "TransitionBarriers");
		std::vector< CD3DX12_RESOURCE_BARRIER> barriers;
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColorMSAA, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE));
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST));
		if (bUseVizualization)
		{
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscVizMSAA, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE));
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscViz, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST));
		}
		if (bRenderMotionVectors)
		{
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscMoVecMSAA, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE));
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscMoVec, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST));
		}
		pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
	}

	// resolve MSAA
	{
		SCOPED_GPU_MARKER(pCmd, "Resolve");
		pCmd->ResolveSubresource(pRscColor, 0, pRscColorMSAA, 0, this->GetTextureFormat(rsc.Tex_SceneColor));
		if (bUseVizualization)
		{
			pCmd->ResolveSubresource(pRscViz, 0, pRscVizMSAA, 0, this->GetTextureFormat(rsc.Tex_SceneVisualization));
		}
		if (bRenderMotionVectors)
		{
			pCmd->ResolveSubresource(pRscMoVec, 0, pRscMoVecMSAA, 0, this->GetTextureFormat(rsc.Tex_SceneMotionVectors));
		}


		if (bResolveRoughness)
		{
			// TODO: remove redundant resource transitions
			{
				std::vector< CD3DX12_RESOURCE_BARRIER> barriers;
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthMSAA, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColorMSAA, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColor, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
				pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
			}

			// since roughness is stored in A channel of the SceneColor texture, and is wrongly resolved by pCmd->ResolveSubresource(),
			// we will run a custom resolve pass on SceneColorMSAA and overwrite the incorrect resolved roughness.
			DepthMSAAResolvePass::FDrawParameters params;
			params.pCmd = pCmd;
			params.pCBufferHeap          = pCBufferHeap;
			params.SRV_MSAADepth         = rsc.SRV_SceneDepthMSAA;
			params.SRV_MSAANormals       = INVALID_ID;
			params.SRV_MSAARoughness     = rsc.SRV_SceneColorMSAA;
			params.UAV_ResolvedDepth     = INVALID_ID;
			params.UAV_ResolvedNormals   = INVALID_ID;
			params.UAV_ResolvedRoughness = rsc.UAV_SceneColor;
			this->GetRenderPass(ERenderPass::DepthMSAAResolve)->RecordCommands(&params);

			// TODO: remove redundant resource transitions
			{
				std::vector< CD3DX12_RESOURCE_BARRIER> barriers;
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColorMSAA, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_SOURCE));
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RESOLVE_DEST));
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthMSAA, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));
				pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
			}
		}
	}
}

void VQRenderer::DownsampleDepth(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, TextureID DepthTextureID, SRV_ID SRVDepth)
{
	int W, H;
	this->GetTextureDimensions(DepthTextureID, W, H);

	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();

	struct FParams { uint params[4]; } CBuffer;
	CBuffer.params[0] = W;
	CBuffer.params[1] = H;
	CBuffer.params[2] = 0xDEADBEEF;
	CBuffer.params[3] = 0xDEADC0DE;

	FParams* pConstBuffer = {};
	D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
	pCBufferHeap->AllocConstantBuffer(sizeof(FParams), (void**)&pConstBuffer, &cbAddr);
	*pConstBuffer = CBuffer;

	constexpr int DispatchGroupDimensionX = 64; // even though the threadgroup dims are 32x8, they work on 64x64 region
	constexpr int DispatchGroupDimensionY = 64; // even though the threadgroup dims are 32x8, they work on 64x64 region
	const     int DispatchX = (W + (DispatchGroupDimensionX - 1)) / DispatchGroupDimensionX; // DIV_AND_ROUND_UP()
	const     int DispatchY = (H + (DispatchGroupDimensionY - 1)) / DispatchGroupDimensionY; // DIV_AND_ROUND_UP()
	constexpr int DispatchZ = 1;

	SCOPED_GPU_MARKER(pCmd, "DownsampleDepth");
	pCmd->SetPipelineState(this->GetPSO(EBuiltinPSOs::DOWNSAMPLE_DEPTH_CS_PSO));
	pCmd->SetComputeRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__DownsampleDepthCS));
	pCmd->SetComputeRootDescriptorTable(0, this->GetSRV(SRVDepth).GetGPUDescHandle());
	pCmd->SetComputeRootDescriptorTable(1, this->GetUAV(rsc.UAV_DownsampledSceneDepth).GetGPUDescHandle());
	pCmd->SetComputeRootDescriptorTable(2, this->GetUAV(rsc.UAV_DownsampledSceneDepthAtomicCounter).GetGPUDescHandle());
	pCmd->SetComputeRootConstantBufferView(3, cbAddr);
	pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);
}

static DirectX::XMMATRIX GetHDRIRotationMatrix(float fHDIROffsetInRadians)
{
	const float cosB = cos(-fHDIROffsetInRadians);
	const float sinB = sin(-fHDIROffsetInRadians);
	DirectX::XMMATRIX m;
	m.r[0] = { cosB, 0, sinB, 0 };
	m.r[1] = { 0, 1, 0, 0 };
	m.r[2] = { -sinB, 0, cosB, 0 };
	m.r[3] = { 0, 0, 0, 0 };
	return m;
}
void VQRenderer::RenderReflections(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, const FGraphicsSettings& GFXSettings)
{
	const FSceneRenderOptions::FFFX_SSSR_UIOptions& UIParams = SceneView.sceneRenderOptions.FFX_SSSRParameters;
	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();

	const FEnvironmentMapRenderingResources& EnvMapRsc = rsc.EnvironmentMap;
	const bool bHasEnvironmentMap = EnvMapRsc.SRV_IrradianceSpec != INVALID_ID;
	ID3D12Resource* pRscEnvIrradianceSpecular = bHasEnvironmentMap ? pRscEnvIrradianceSpecular = this->GetTextureResource(EnvMapRsc.Tex_IrradianceSpec)  : nullptr;
	ID3D12Resource* pRscBRDFIntegrationLUT    = pRscBRDFIntegrationLUT = this->GetTextureResource(this->GetProceduralTexture(EProceduralTextures::IBL_BRDF_INTEGRATION_LUT));
	int EnvMapSpecIrrCubemapDimX = 0;
	int EnvMapSpecIrrCubemapDimY = 0;
	if (bHasEnvironmentMap)
	{
		this->GetTextureDimensions(EnvMapRsc.Tex_IrradianceSpec, EnvMapSpecIrrCubemapDimX, EnvMapSpecIrrCubemapDimY);
	}
	
	switch (GFXSettings.Reflections)
	{
	case EReflections::SCREEN_SPACE_REFLECTIONS__FFX:
	{
		SCOPED_GPU_MARKER(pCmd, "RenderReflections FidelityFX-SSSR");
		ScreenSpaceReflectionsPass::FDrawParameters params = {};
		// ---- cmd recording ----
		params.pCmd = pCmd;
		params.pCBufferHeap = pCBufferHeap;
		// ---- cbuffer ----
		params.ffxCBuffer.invViewProjection                    = DirectX::XMMatrixInverse(nullptr, SceneView.view * SceneView.proj);
		params.ffxCBuffer.projection                           = SceneView.proj;
		params.ffxCBuffer.invProjection                        = SceneView.projInverse;
		params.ffxCBuffer.view                                 = SceneView.view;
		params.ffxCBuffer.invView                              = SceneView.viewInverse;
		params.ffxCBuffer.bufferDimensions[0]                  = SceneView.SceneRTWidth;
		params.ffxCBuffer.bufferDimensions[1]                  = SceneView.SceneRTHeight;
		params.ffxCBuffer.envMapRotation                       = GetHDRIRotationMatrix(SceneView.HDRIYawOffset);
		params.ffxCBuffer.inverseBufferDimensions[0]           = 1.0f / params.ffxCBuffer.bufferDimensions[0];
		params.ffxCBuffer.inverseBufferDimensions[1]           = 1.0f / params.ffxCBuffer.bufferDimensions[1];
		params.ffxCBuffer.frameIndex                           = static_cast<uint32>(mRenderStats.mNumFramesRendered);
		params.ffxCBuffer.temporalStabilityFactor              = UIParams.temporalStability;
		params.ffxCBuffer.depthBufferThickness                 = UIParams.depthBufferThickness;
		params.ffxCBuffer.roughnessThreshold                   = UIParams.roughnessThreshold;
		params.ffxCBuffer.varianceThreshold                    = UIParams.temporalVarianceThreshold;
		params.ffxCBuffer.maxTraversalIntersections            = UIParams.maxTraversalIterations;
		params.ffxCBuffer.minTraversalOccupancy                = UIParams.minTraversalOccupancy;
		params.ffxCBuffer.mostDetailedMip                      = UIParams.mostDetailedDepthHierarchyMipLevel;
		params.ffxCBuffer.samplesPerQuad                       = UIParams.samplesPerQuad;
		params.ffxCBuffer.temporalVarianceGuidedTracingEnabled = UIParams.bEnableTemporalVarianceGuidedTracing;
		params.ffxCBuffer.envMapSpecularIrradianceCubemapMipLevelCount = EnvMapRsc.GetNumSpecularIrradianceCubemapLODLevels(*this);
		params.TexDepthHierarchy                               = rsc.Tex_DownsampledSceneDepth;
		params.TexNormals                                      = rsc.Tex_SceneNormals;
		params.SRVEnvironmentSpecularIrradianceCubemap         = bHasEnvironmentMap ? EnvMapRsc.SRV_IrradianceSpec : rsc.SRV_NullCubemap;
		params.SRVBRDFIntegrationLUT                           = rsc.SRV_BRDFIntegrationLUT;

		{
			D3D12_RESOURCE_BARRIER barriers[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(pRscBRDFIntegrationLUT   , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(pRscEnvIrradianceSpecular, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			};
			pCmd->ResourceBarrier(bHasEnvironmentMap ? _countof(barriers) : 1, barriers);
		}

		this->GetRenderPass(ERenderPass::ScreenSpaceReflections)->RecordCommands(&params);

		{
			D3D12_RESOURCE_BARRIER barriers[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(pRscBRDFIntegrationLUT   , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(pRscEnvIrradianceSpecular, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
			};
			pCmd->ResourceBarrier(bHasEnvironmentMap ? _countof(barriers) : 1, barriers);
		}
		
	} break;

	case EReflections::RAY_TRACED_REFLECTIONS:
	default:
		Log::Warning("RenderReflections(): unrecognized setting or missing implementation for reflection enum: %d", GFXSettings.Reflections);
		return;

	case EReflections::REFLECTIONS_OFF:
		Log::Error("RenderReflections(): called with REFLECTIONS_OFF, why?");
		return;
	}
}

static bool ShouldSkipBoundsPass(const FSceneDrawData& SceneDrawData)
{
	return SceneDrawData.boundingBoxRenderParams.empty()
		&& SceneDrawData.lightBoundsRenderParams.empty()
		&& SceneDrawData.outlineRenderParams.empty()
		&& SceneDrawData.debugVertexAxesRenderParams.empty();
}
void VQRenderer::RenderSceneBoundingVolumes(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr, const FSceneView& SceneView, bool bMSAA)
{
	SCOPED_GPU_MARKER(pCmd, "RenderSceneBoundingVolumes");
	const FSceneDrawData& SceneDrawData = mFrameSceneDrawData[0]; // [0] since we don't have parallel update+render
	const bool bNoBoundingVolumeToRender = ShouldSkipBoundsPass(SceneDrawData);

	if (bNoBoundingVolumeToRender)
		return;

	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();
	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	ID3D12Resource* pRscColorMSAA = this->GetTextureResource(rsc.Tex_SceneColorMSAA);
	ID3D12Resource* pRscColor = this->GetTextureResource(rsc.Tex_SceneColor);
	ID3D12Resource* pRscColorBV = this->GetTextureResource(rsc.Tex_SceneColorBoundingVolumes);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = this->GetRTV(bMSAA ? rsc.RTV_SceneColorMSAA : rsc.RTV_SceneColor).GetCPUDescHandle();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = this->GetDSV(bMSAA ? rsc.DSV_SceneDepthMSAA : rsc.DSV_SceneDepth).GetCPUDescHandle();
	D3D12_CPU_DESCRIPTOR_HANDLE rtvBVHandle = this->GetRTV(rsc.RTV_SceneColorBoundingVolumes).GetCPUDescHandle();

	const float RenderResolutionX = static_cast<float>(SceneView.SceneRTWidth);
	const float RenderResolutionY = static_cast<float>(SceneView.SceneRTHeight);
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };

	// -----------------------------------------------------------------------------------------------------------

	{
		std::vector< CD3DX12_RESOURCE_BARRIER> barriers;
		if (bMSAA)
		{
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColorMSAA, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColorBV, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST));
		}
		else
		{
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColorBV, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
		}
		pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
	}

	// if MSAA is enabled, we're re-using the color MSAA buffer to render the bounding volumes.
	// otherwise, we'll utilize a separatre color buffer for bounding volumes, which we'll later
	// composite with reflections. Assumes we're calling this function when SSR is enabled.
	pCmd->ClearRenderTargetView(bMSAA ? rtvHandle : rtvBVHandle, clearColor, 0, nullptr);
	pCmd->OMSetRenderTargets(1, bMSAA ? &rtvHandle: &rtvBVHandle, FALSE, &dsvHandle);
	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);

	RenderBoundingBoxes(pCmd, pCBufferHeap, SceneView, bMSAA);
	RenderLightBounds(pCmd, pCBufferHeap, SceneView, bMSAA, true); // RenderSceneBoundingVolumes() called only when reflections are enabled, so just pass true for the last param
	RenderDebugVertexAxes(pCmd, pCBufferHeap, SceneView, bMSAA);
	RenderOutline(pCmd, pCBufferHeap, perViewCBAddr, SceneView, bMSAA, { rtvHandle });
	if (bMSAA) // resolve if MSAA
	{
		std::vector< CD3DX12_RESOURCE_BARRIER> barriers;
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColorMSAA, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE));
		pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
		pCmd->ResolveSubresource(pRscColorBV, 0, pRscColorMSAA, 0, this->GetTextureFormat(rsc.Tex_SceneColor));
	}

	{
		std::vector< CD3DX12_RESOURCE_BARRIER> barriers;
		if (bMSAA)
		{
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColorBV, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
		}
		else
		{
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColor, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColorBV, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
		}
		pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
	}
}

void VQRenderer::CompositeReflections(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, const FGraphicsSettings& GFXSettings)
{
	const FSceneDrawData& SceneDrawData = mFrameSceneDrawData[0]; // [0] since we don't have parallel update+render
	const bool bNoBoundingVolumeToRender = ShouldSkipBoundsPass(SceneDrawData);

	const bool& bMSAA = GFXSettings.bAntiAliasing;

	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();
	std::shared_ptr<ScreenSpaceReflectionsPass> pReflectionsPass = std::static_pointer_cast<ScreenSpaceReflectionsPass>(this->GetRenderPass(ERenderPass::ScreenSpaceReflections));
	std::shared_ptr<ApplyReflectionsPass> pApplyReflectionsPass = std::static_pointer_cast<ApplyReflectionsPass>(this->GetRenderPass(ERenderPass::ApplyReflections));


	SCOPED_GPU_MARKER(pCmd, "CompositeReflections");
	
	ApplyReflectionsPass::FDrawParameters params;
	params.pCmd = pCmd;
	params.pCBufferHeap = pCBufferHeap;
	params.SRVReflectionRadiance = pReflectionsPass->GetPassOutputSRV();
	params.SRVBoundingVolumes = bNoBoundingVolumeToRender ? INVALID_ID : rsc.SRV_SceneColorBoundingVolumes;
	params.UAVSceneRadiance = rsc.UAV_SceneColor;
	params.iSceneRTWidth  = SceneView.SceneRTWidth;
	params.iSceneRTHeight = SceneView.SceneRTHeight;

	{			
		D3D12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(this->GetTextureResource(rsc.Tex_SceneColor),
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
			),
		};
		pCmd->ResourceBarrier(_countof(barriers), barriers);
	}
	pApplyReflectionsPass->RecordCommands(&params);
	{
		D3D12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(this->GetTextureResource(rsc.Tex_SceneColor),
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
			),
		};
		pCmd->ResourceBarrier(_countof(barriers), barriers);
	}
}

void VQRenderer::TransitionForPostProcessing(ID3D12GraphicsCommandList* pCmd, const FPostProcessParameters& PPParams, const FGraphicsSettings& GFXSettings)
{
	const bool& bMSAA = GFXSettings.bAntiAliasing;
	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();

	const bool bCASEnabled = PPParams.IsFFXCASEnabled() && PPParams.Sharpness > 0.0f;
	const bool bFSREnabled = PPParams.IsFSREnabled();
	const bool bVizualizationEnabled = PPParams.DrawModeEnum != EDrawMode::LIT_AND_POSTPROCESSED;
	const bool bVizualizationSceneTargetUsed = ShouldUseVisualizationTarget(PPParams);
	const bool bMotionVectorsEnabled = ShouldUseMotionVectorsTarget(GFXSettings);

	ID3D12Resource* pRscPostProcessInput = this->GetTextureResource(rsc.Tex_SceneColor);
	ID3D12Resource* pRscTonemapperOut    = this->GetTextureResource(rsc.Tex_PostProcess_TonemapperOut);
	ID3D12Resource* pRscFFXCASOut        = this->GetTextureResource(rsc.Tex_PostProcess_FFXCASOut);
	ID3D12Resource* pRscFSROut           = this->GetTextureResource(rsc.Tex_PostProcess_FSR_RCASOut); // TODO: handle RCAS=off
	ID3D12Resource* pRscVizMSAA          = this->GetTextureResource(rsc.Tex_SceneVisualizationMSAA);
	ID3D12Resource* pRscViz              = this->GetTextureResource(rsc.Tex_SceneVisualization);
	ID3D12Resource* pRscMoVecMSAA        = this->GetTextureResource(rsc.Tex_SceneMotionVectorsMSAA);
	ID3D12Resource* pRscMoVec            = this->GetTextureResource(rsc.Tex_SceneMotionVectors);
	ID3D12Resource* pRscVizOut           = this->GetTextureResource(rsc.Tex_PostProcess_VisualizationOut);
	ID3D12Resource* pRscPostProcessOut   = bVizualizationEnabled ? pRscVizOut
		: (bFSREnabled
			? pRscFSROut 
			: (bCASEnabled
				? pRscFFXCASOut 
				: pRscTonemapperOut)
	);

	ID3D12Resource* pRscShadowMaps_Spot = this->GetTextureResource(rsc.Tex_ShadowMaps_Spot);
	ID3D12Resource* pRscShadowMaps_Point = this->GetTextureResource(rsc.Tex_ShadowMaps_Point);
	ID3D12Resource* pRscShadowMaps_Directional = this->GetTextureResource(rsc.Tex_ShadowMaps_Directional);

	SCOPED_GPU_MARKER(pCmd, "TransitionForPostProcessing");
	std::vector<CD3DX12_RESOURCE_BARRIER> barriers =
	{
		  CD3DX12_RESOURCE_BARRIER::Transition(pRscPostProcessInput , (bMSAA ? D3D12_RESOURCE_STATE_RESOLVE_DEST : D3D12_RESOURCE_STATE_RENDER_TARGET), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscPostProcessOut   , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)

		, CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Spot       , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Point      , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Directional, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE)
	};
	if ((bFSREnabled || bCASEnabled) && !bVizualizationEnabled)
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
	
	if (bVizualizationSceneTargetUsed)
	{
		barriers.push_back(bMSAA
			? CD3DX12_RESOURCE_BARRIER::Transition(pRscViz, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
			: CD3DX12_RESOURCE_BARRIER::Transition(pRscViz, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		);
	}

	if (bMotionVectorsEnabled)
	{
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscMoVec, (bMSAA ? D3D12_RESOURCE_STATE_RESOLVE_DEST : D3D12_RESOURCE_STATE_RENDER_TARGET), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	}
	pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
}

ID3D12Resource* VQRenderer::RenderPostProcess(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FPostProcessParameters& PPParams, bool bHDR)
{
	ID3D12DescriptorHeap*       ppHeaps[] = { this->GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };

	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();

	// pass io
	const SRV& srv_ColorIn          = this->GetSRV(rsc.SRV_SceneColor);
	const UAV& uav_TonemapperOut    = this->GetUAV(rsc.UAV_PostProcess_TonemapperOut);
	const UAV& uav_VisualizationOut = this->GetUAV(rsc.UAV_PostProcess_VisualizationOut);
	const SRV& srv_TonemapperOut    = this->GetSRV(rsc.SRV_PostProcess_TonemapperOut);
	const UAV& uav_FFXCASOut        = this->GetUAV(rsc.UAV_PostProcess_FFXCASOut);
	const UAV& uav_FSR_EASUOut      = this->GetUAV(rsc.UAV_PostProcess_FSR_EASUOut);
	const SRV& srv_FSR_EASUOut      = this->GetSRV(rsc.SRV_PostProcess_FSR_EASUOut);
	const UAV& uav_FSR_RCASOut      = this->GetUAV(rsc.UAV_PostProcess_FSR_RCASOut);
	const SRV& srv_FSR_RCASOut      = this->GetSRV(rsc.SRV_PostProcess_FSR_RCASOut);
	ID3D12Resource* pRscTonemapperOut = this->GetTextureResource(rsc.Tex_PostProcess_TonemapperOut);


	constexpr bool PP_ENABLE_BLUR_PASS = false;

	// compute dispatch dimensions
	const int& InputImageWidth  = PPParams.SceneRTWidth;
	const int& InputImageHeight = PPParams.SceneRTHeight;
	assert(PPParams.SceneRTWidth != 0);
	assert(PPParams.SceneRTHeight != 0);
	constexpr int DispatchGroupDimensionX = 8;
	constexpr int DispatchGroupDimensionY = 8;
	const     int DispatchRenderX = (InputImageWidth  + (DispatchGroupDimensionX - 1)) / DispatchGroupDimensionX;
	const     int DispatchRenderY = (InputImageHeight + (DispatchGroupDimensionY - 1)) / DispatchGroupDimensionY;
	constexpr int DispatchZ = 1;

	// cmds
	ID3D12Resource* pRscOutput = nullptr;
	if (PPParams.DrawModeEnum != EDrawMode::LIT_AND_POSTPROCESSED)
	{
		SCOPED_GPU_MARKER(pCmd, "RenderPostProcess_DebugViz");
		std::shared_ptr<ScreenSpaceReflectionsPass> pReflectionsPass = std::static_pointer_cast<ScreenSpaceReflectionsPass>(this->GetRenderPass(ERenderPass::ScreenSpaceReflections));

		// cbuffer
		using cbuffer_t = FPostProcessParameters::FVizualizationParams;
		cbuffer_t* pConstBuffer = {};
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		pCBufferHeap->AllocConstantBuffer(sizeof(cbuffer_t), (void**)&pConstBuffer, &cbAddr);
		memcpy(pConstBuffer, &PPParams.VizParams, sizeof(PPParams.VizParams));
		pConstBuffer->iDrawMode = static_cast<int>(PPParams.DrawModeEnum); // iDrawMode is not connected to the UI

		SRV SRVIn = srv_ColorIn;
		switch (PPParams.DrawModeEnum)
		{
		case EDrawMode::DEPTH         : SRVIn = this->GetSRV(rsc.SRV_SceneDepth); break;
		case EDrawMode::NORMALS       : SRVIn = this->GetSRV(rsc.SRV_SceneNormals); break;
		case EDrawMode::AO            : SRVIn = this->GetSRV(rsc.SRV_FFXCACAO_Out); break;
		case EDrawMode::ALBEDO        : // same as below
		case EDrawMode::METALLIC      : SRVIn = this->GetSRV(rsc.SRV_SceneVisualization); break;
		case EDrawMode::ROUGHNESS     : srv_ColorIn; break;
		case EDrawMode::REFLECTIONS   : SRVIn = this->GetSRV(pReflectionsPass->GetPassOutputSRV()); break;
		case EDrawMode::MOTION_VECTORS: SRVIn = this->GetSRV(rsc.SRV_SceneMotionVectors); break;
		}

		pCmd->SetPipelineState(this->GetPSO(EBuiltinPSOs::VIZUALIZATION_CS_PSO));
		pCmd->SetComputeRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1));
		pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		pCmd->SetComputeRootDescriptorTable(0, SRVIn.GetGPUDescHandle());
		pCmd->SetComputeRootDescriptorTable(1, uav_VisualizationOut.GetGPUDescHandle());
		pCmd->SetComputeRootConstantBufferView(2, cbAddr);
		pCmd->Dispatch(DispatchRenderX, DispatchRenderY, DispatchZ);

		pRscOutput = this->GetTextureResource(rsc.Tex_PostProcess_VisualizationOut);
	}
	else
	{
		SCOPED_GPU_MARKER(pCmd, "RenderPostProcess");
		const SRV& srv_blurOutput         = this->GetSRV(rsc.SRV_PostProcess_BlurOutput);

		if constexpr (PP_ENABLE_BLUR_PASS && PPParams.bEnableGaussianBlur)
		{
			SCOPED_GPU_MARKER(pCmd, "BlurCS");
			const UAV& uav_BlurIntermediate = this->GetUAV(rsc.UAV_PostProcess_BlurIntermediate);
			const UAV& uav_BlurOutput       = this->GetUAV(rsc.UAV_PostProcess_BlurOutput);
			const SRV& srv_blurIntermediate = this->GetSRV(rsc.SRV_PostProcess_BlurIntermediate);
			auto pRscBlurIntermediate = this->GetTextureResource(rsc.Tex_PostProcess_BlurIntermediate);
			auto pRscBlurOutput       = this->GetTextureResource(rsc.Tex_PostProcess_BlurOutput);


			FPostProcessParameters::FBlurParams* pBlurParams = nullptr;

			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			pCBufferHeap->AllocConstantBuffer(sizeof(FPostProcessParameters::FBlurParams), (void**)&pBlurParams, &cbAddr);
			pBlurParams->iImageSizeX = PPParams.SceneRTWidth;
			pBlurParams->iImageSizeY = PPParams.SceneRTHeight;

			{
				SCOPED_GPU_MARKER(pCmd, "BlurX");
				pCmd->SetPipelineState(this->GetPSO(EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_X_PSO));
				pCmd->SetComputeRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1));

				const int FFXDispatchGroupDimension = 16;
				const     int FFXDispatchX = (InputImageWidth  + (FFXDispatchGroupDimension - 1)) / FFXDispatchGroupDimension;
				const     int FFXDispatchY = (InputImageHeight + (FFXDispatchGroupDimension - 1)) / FFXDispatchGroupDimension;

				pCmd->SetComputeRootDescriptorTable(0, srv_ColorIn.GetGPUDescHandle());
				pCmd->SetComputeRootDescriptorTable(1, uav_BlurIntermediate.GetGPUDescHandle());
				pCmd->SetComputeRootConstantBufferView(2, cbAddr);
				pCmd->Dispatch(DispatchRenderX, DispatchRenderY, DispatchZ);

				const CD3DX12_RESOURCE_BARRIER pBarriers[] =
				{
					  CD3DX12_RESOURCE_BARRIER::Transition(pRscBlurIntermediate, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
					, CD3DX12_RESOURCE_BARRIER::Transition(pRscBlurOutput      , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)

				};
				pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
			}


			{
				SCOPED_GPU_MARKER(pCmd, "BlurY");
				pCmd->SetPipelineState(this->GetPSO(EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_Y_PSO));
				pCmd->SetComputeRootDescriptorTable(0, srv_blurIntermediate.GetGPUDescHandle());
				pCmd->SetComputeRootDescriptorTable(1, uav_BlurOutput.GetGPUDescHandle());
				pCmd->SetComputeRootConstantBufferView(2, cbAddr);
				pCmd->Dispatch(DispatchRenderX, DispatchRenderY, DispatchZ);

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
			pCBufferHeap->AllocConstantBuffer(sizeof(FPostProcessParameters::FTonemapper), (void**)&pConstBuffer, &cbAddr);
			*pConstBuffer = PPParams.TonemapperParams;

			pCmd->SetPipelineState(this->GetPSO(EBuiltinPSOs::TONEMAPPER_PSO));
			pCmd->SetComputeRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1));
			pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
			pCmd->SetComputeRootDescriptorTable(0, PP_ENABLE_BLUR_PASS ? srv_blurOutput.GetGPUDescHandle() : srv_ColorIn.GetGPUDescHandle());
			pCmd->SetComputeRootDescriptorTable(1, uav_TonemapperOut.GetGPUDescHandle());
			pCmd->SetComputeRootConstantBufferView(2, cbAddr);
			pCmd->Dispatch(DispatchRenderX, DispatchRenderY, DispatchZ);
			pRscOutput = pRscTonemapperOut;
		}

#if !DISABLE_FIDELITYFX_CAS
		if(PPParams.IsFFXCASEnabled() && PPParams.Sharpness > 0.0f)
		{
			ID3D12Resource* pRscFFXCASOut = this->GetTextureResource(rsc.Tex_PostProcess_FFXCASOut);
			const CD3DX12_RESOURCE_BARRIER pBarriers[] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
			};
			pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);

			SCOPED_GPU_MARKER(pCmd, "FFX-CAS CS");

			unsigned* pConstBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			const size_t cbSize = sizeof(unsigned) * 8;
			pCBufferHeap->AllocConstantBuffer(cbSize, (void**)&pConstBuffer, &cbAddr);
			memcpy(pConstBuffer, PPParams.FFXCASParams.CASConstantBlock, cbSize);
			

			ID3D12PipelineState* pPSO = this->GetPSO(EBuiltinPSOs::FFX_CAS_CS_PSO);
			assert(pPSO);
			pCmd->SetPipelineState(pPSO);
			pCmd->SetComputeRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1));
			pCmd->SetComputeRootDescriptorTable(0, srv_TonemapperOut.GetGPUDescHandle());
			pCmd->SetComputeRootDescriptorTable(1, uav_FFXCASOut.GetGPUDescHandle());
			pCmd->SetComputeRootConstantBufferView(2, cbAddr);
			
			// each FFX-CAS CS thread processes 4 pixels.
			// workgroup is 64 threads, hence 256 (16x16) pixels are processed per thread group that is dispatched
 			constexpr int CAS_WORKGROUP_WORK_DIMENSION = 16;
			const int CASDispatchX = (InputImageWidth  + (CAS_WORKGROUP_WORK_DIMENSION - 1)) / CAS_WORKGROUP_WORK_DIMENSION;
			const int CASDispatchY = (InputImageHeight + (CAS_WORKGROUP_WORK_DIMENSION - 1)) / CAS_WORKGROUP_WORK_DIMENSION;
			pCmd->Dispatch(CASDispatchX, CASDispatchY, DispatchZ);
			pRscOutput = pRscFFXCASOut;
		}
#endif

		if (PPParams.IsFSREnabled()) // FSR & CAS are mutually exclusive
		{
			if (bHDR)
			{
				// TODO: color conversion pass, barriers etc.
			}

			std::vector<CD3DX12_RESOURCE_BARRIER> pBarriers =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut
				, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
					, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
				)
			};
			pCmd->ResourceBarrier((UINT)pBarriers.size(), pBarriers.data());

			{
				SCOPED_GPU_MARKER(pCmd, "FSR-EASU CS");

				unsigned* pConstBuffer = {};
				D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
				const size_t cbSize = sizeof(unsigned) * 16;
				pCBufferHeap->AllocConstantBuffer(cbSize, (void**)&pConstBuffer, &cbAddr);
				memcpy(pConstBuffer, PPParams.FSR_EASUParams.EASUConstantBlock, cbSize);

				ID3D12PipelineState* pPSO = this->GetPSO(EBuiltinPSOs::FFX_FSR1_EASU_CS_PSO);
				assert(pPSO);
				pCmd->SetPipelineState(pPSO);
				pCmd->SetComputeRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__FFX_FSR1));
				pCmd->SetComputeRootDescriptorTable(0, srv_TonemapperOut.GetGPUDescHandle());
				pCmd->SetComputeRootDescriptorTable(1, uav_FSR_EASUOut.GetGPUDescHandle());
				pCmd->SetComputeRootConstantBufferView(2, cbAddr);

				// each FSR-EASU CS thread processes 4 pixels.
				// workgroup is 64 threads, hence 256 (16x16) pixels are processed per thread group that is dispatched
				constexpr int WORKGROUP_WORK_DIMENSION = 16;
				const int DispatchX = (PPParams.DisplayResolutionWidth  + (WORKGROUP_WORK_DIMENSION - 1)) / WORKGROUP_WORK_DIMENSION;
				const int DispatchY = (PPParams.DisplayResolutionHeight + (WORKGROUP_WORK_DIMENSION - 1)) / WORKGROUP_WORK_DIMENSION;
				pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);
			}
			const bool bFFX_RCAS_Enabled = true; // TODO: drive with UI ?
			ID3D12Resource* pRscFSR1Out = this->GetTextureResource(rsc.Tex_PostProcess_FSR_RCASOut);
			if (bFFX_RCAS_Enabled)
			{
				ID3D12Resource* pRscEASUOut = this->GetTextureResource(rsc.Tex_PostProcess_FSR_EASUOut);
				ID3D12Resource* pRscRCASOut = this->GetTextureResource(rsc.Tex_PostProcess_FSR_RCASOut);

				SCOPED_GPU_MARKER(pCmd, "FSR-RCAS CS");
				{
					std::vector<CD3DX12_RESOURCE_BARRIER> barriers;
					barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscEASUOut
						, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
						, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
					));
					pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
				}

				ID3D12PipelineState* pPSO = this->GetPSO(EBuiltinPSOs::FFX_FSR1_RCAS_CS_PSO);

				FPostProcessParameters::FFSR1_RCAS* pConstBuffer = {};
				D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
				pCBufferHeap->AllocConstantBuffer(sizeof(FPostProcessParameters::FFSR1_RCAS), (void**)&pConstBuffer, &cbAddr);
				*pConstBuffer = PPParams.FSR_RCASParams;

				pCmd->SetPipelineState(pPSO);
				pCmd->SetComputeRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__FFX_FSR1));
				pCmd->SetComputeRootDescriptorTable(0, srv_FSR_EASUOut.GetGPUDescHandle());
				pCmd->SetComputeRootDescriptorTable(1, uav_FSR_RCASOut.GetGPUDescHandle());
				pCmd->SetComputeRootConstantBufferView(2, cbAddr);

				// each FSR-RCAS CS thread processes 4 pixels.
				// workgroup is 64 threads, hence 256 (16x16) pixels are processed per thread group that is dispatched
				constexpr int WORKGROUP_WORK_DIMENSION = 16;
				const int DispatchX = (PPParams.DisplayResolutionWidth + (WORKGROUP_WORK_DIMENSION - 1)) / WORKGROUP_WORK_DIMENSION;
				const int DispatchY = (PPParams.DisplayResolutionHeight + (WORKGROUP_WORK_DIMENSION - 1)) / WORKGROUP_WORK_DIMENSION;
				pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);

				{
					const CD3DX12_RESOURCE_BARRIER pBarriers[] =
					{
						CD3DX12_RESOURCE_BARRIER::Transition(pRscEASUOut, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
						CD3DX12_RESOURCE_BARRIER::Transition(pRscRCASOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
					};
					pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
				}

			}

			pRscOutput = pRscFSR1Out;
		}
	}

	return pRscOutput;
}

void VQRenderer::RenderUI(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, FWindowRenderContext& ctx, const FPostProcessParameters& PPParams, ID3D12Resource* pRscInput, const FUIState& UIState, bool bHDR)
{
	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();
	const float             RenderResolutionX = static_cast<float>(PPParams.DisplayResolutionWidth);
	const float             RenderResolutionY = static_cast<float>(PPParams.DisplayResolutionHeight);
	D3D12_VIEWPORT                   viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	ID3D12DescriptorHeap*           ppHeaps[] = { this->GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };
	D3D12_RECT                   scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
	SwapChain&                      swapchain = ctx.SwapChain;

	D3D12_INDEX_BUFFER_VIEW nullIBV = {};
	nullIBV.Format = DXGI_FORMAT_R32_UINT;
	nullIBV.SizeInBytes = 0;
	nullIBV.BufferLocation = 0;
	
	const bool bVizualizationEnabled = PPParams.DrawModeEnum != EDrawMode::LIT_AND_POSTPROCESSED;
	const bool bFFXCASEnabled = PPParams.IsFFXCASEnabled() && PPParams.Sharpness > 0.0f;
	const bool bFSREnabled = PPParams.IsFSREnabled();
	const SRV& srv_ColorIn = bVizualizationEnabled ?
		this->GetSRV(rsc.SRV_PostProcess_VisualizationOut) : (
		bFFXCASEnabled 
		? this->GetSRV(rsc.SRV_PostProcess_FFXCASOut)
		: (bFSREnabled 
			? this->GetSRV(rsc.SRV_PostProcess_FSR_RCASOut) 
			: this->GetSRV(rsc.SRV_PostProcess_TonemapperOut)));

	ID3D12Resource* pRscFSR1Out       = this->GetTextureResource(rsc.Tex_PostProcess_FSR_RCASOut);
	ID3D12Resource* pRscTonemapperOut = this->GetTextureResource(rsc.Tex_PostProcess_TonemapperOut);
	ID3D12Resource* pRscFFXCASOut     = this->GetTextureResource(rsc.Tex_PostProcess_FFXCASOut);
	ID3D12Resource* pRscUI            = this->GetTextureResource(rsc.Tex_UI_SDR);

	ID3D12Resource*          pSwapChainRT = swapchain.GetCurrentBackBufferRenderTarget();
	
	//Log::Info("RenderUI: Backbuffer[%d]: 0x%08x | pCmd = %p", swapchain.GetCurrentBackBufferIndex(), pSwapChainRT, pCmd);
	
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = bHDR 
		? this->GetRTV(rsc.RTV_UI_SDR).GetCPUDescHandle()
		: swapchain.GetCurrentBackBufferRTVHandle();

	// barriers
	{	
		// Transition Input & Output resources
		// ignore the tonemapper barrier if CAS is enabeld as it'll already be issued.
		CD3DX12_RESOURCE_BARRIER SwapChainTransition = CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		CD3DX12_RESOURCE_BARRIER UITransition = CD3DX12_RESOURCE_BARRIER::Transition(pRscUI, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		std::vector< CD3DX12_RESOURCE_BARRIER> barriers;
		barriers.push_back(bHDR ? UITransition : SwapChainTransition);
		if (bVizualizationEnabled)
		{
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscInput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
		}
		else
		{
			if (bFFXCASEnabled)
			{
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscFFXCASOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
			}
			else
			{
				if (bFSREnabled)
				{
					barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
				}
				else
				{
					barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
				}
			}
		}
		pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
	}

	if (!bHDR)
	{
		if (UIState.mpMagnifierState->bUseMagnifier)
		{
			SCOPED_GPU_MARKER(pCmd, "MagnifierPass");
			MagnifierPass::FDrawParameters MagnifierDrawParams;
			MagnifierDrawParams.pCmd = pCmd;
			MagnifierDrawParams.pCBufferHeap = pCBufferHeap;
			MagnifierDrawParams.IndexBufferView = nullIBV;
			MagnifierDrawParams.RTV = rtvHandle;
			MagnifierDrawParams.SRVColorInput = srv_ColorIn;
			MagnifierDrawParams.pCBufferParams = UIState.mpMagnifierState->pMagnifierParams;
			this->GetRenderPass(ERenderPass::Magnifier)->RecordCommands(&MagnifierDrawParams);
		}
		else
		{
			SCOPED_GPU_MARKER(pCmd, "SwapchainPassthrough");
			pCmd->SetPipelineState(this->GetPSO(bHDR ? EBuiltinPSOs::HDR_FP16_SWAPCHAIN_PSO : EBuiltinPSOs::FULLSCREEN_TRIANGLE_PSO));
			pCmd->SetGraphicsRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__FullScreenTriangle));
			pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
			pCmd->SetGraphicsRootDescriptorTable(0, srv_ColorIn.GetGPUDescHandle());

			pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmd->IASetVertexBuffers(0, 1, NULL);
			pCmd->IASetIndexBuffer(&nullIBV);

			pCmd->RSSetViewports(1, &viewport);
			pCmd->RSSetScissorRects(1, &scissorsRect);

			pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);

			pCmd->DrawInstanced(3, 1, 0, 0);
		}
	}
	else
	{
		if (UIState.mpMagnifierState->bUseMagnifier)
		{
			// TODO: make magnifier work w/ HDR when the new HDR monitor arrives
		}
		else
		{
			pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);
			const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
			pCmd->ClearRenderTargetView(rtvHandle, clearColor, 0, NULL);
		}
	}

#if !VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	{
		SCOPED_GPU_MARKER(pCmd, "UI");

		struct cb
		{
			float matTransformation[4][4];
		};

		ImGuiIO& io = ImGui::GetIO();

		ImGui::Render();

		ImDrawData* draw_data = ImGui::GetDrawData();

		// Create and grow vertex/index buffers if needed
		char* pVertices = NULL;
		D3D12_VERTEX_BUFFER_VIEW VerticesView;

		pCBufferHeap->AllocVertexBuffer(draw_data->TotalVtxCount, sizeof(ImDrawVert), (void**)&pVertices, &VerticesView);

		char* pIndices = NULL;
		D3D12_INDEX_BUFFER_VIEW IndicesView;
		pCBufferHeap->AllocIndexBuffer(draw_data->TotalIdxCount, sizeof(ImDrawIdx), (void**)&pIndices, &IndicesView);

		ImDrawVert* vtx_dst = (ImDrawVert*)pVertices;
		ImDrawIdx* idx_dst = (ImDrawIdx*)pIndices;
		for (int n = 0; n < draw_data->CmdListsCount; n++)
		{
			const ImDrawList* cmd_list = draw_data->CmdLists[n];
			memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
			memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
			vtx_dst += cmd_list->VtxBuffer.Size;
			idx_dst += cmd_list->IdxBuffer.Size;
		}

		// Setup orthographic projection matrix into our constant buffer
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		{
			cb* constant_buffer;
			pCBufferHeap->AllocConstantBuffer(sizeof(cb), (void**)&constant_buffer, &cbAddr);

			float L = 0.0f;
			float R = io.DisplaySize.x;
			float B = io.DisplaySize.y;
			float T = 0.0f;
			float proj[4][4] =
			{
				{ 2.0f / (R - L)   , 0.0f             , 0.0f  ,  0.0f },
				{ 0.0f             , 2.0f / (T - B)   , 0.0f  ,  0.0f },
				{ 0.0f             , 0.0f             , 0.5f  ,  0.0f },
				{ (R + L) / (L - R), (T + B) / (B - T), 0.5f  ,  1.0f },
			};
			memcpy(constant_buffer->matTransformation, proj, sizeof(proj));
		}

		// Setup viewport
		D3D12_VIEWPORT vp;
		memset(&vp, 0, sizeof(D3D12_VIEWPORT));
		vp.Width = io.DisplaySize.x;
		vp.Height = io.DisplaySize.y;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		vp.TopLeftX = vp.TopLeftY = 0.0f;
		pCmd->RSSetViewports(1, &vp);

		// set pipeline & render state
		pCmd->SetPipelineState(this->GetPSO(EBuiltinPSOs::UI_PSO));
		pCmd->SetGraphicsRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__HelloWorldCube));
		pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);

		pCmd->IASetIndexBuffer(&IndicesView);
		pCmd->IASetVertexBuffers(0, 1, &VerticesView);
		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		pCmd->SetGraphicsRootConstantBufferView(1, cbAddr);

		// Render command lists
		int vtx_offset = 0;
		int idx_offset = 0;
		for (int n = 0; n < draw_data->CmdListsCount; n++)
		{
			const ImDrawList* drawList = draw_data->CmdLists[n];
			for (int cmd_i = 0; cmd_i < drawList->CmdBuffer.Size; cmd_i++)
			{
				const ImDrawCmd* pcmd = &drawList->CmdBuffer[cmd_i];
				if (pcmd->UserCallback)
				{
					pcmd->UserCallback(drawList, pcmd);
				}
				else
				{
					const D3D12_RECT r =
					{
						(LONG)pcmd->ClipRect.x,
						(LONG)pcmd->ClipRect.y,
						(LONG)pcmd->ClipRect.z,
						(LONG)pcmd->ClipRect.w
					};
					pCmd->RSSetScissorRects(1, &r);
					D3D12_GPU_DESCRIPTOR_HANDLE h = { (UINT64)(pcmd->TextureId) };
					pCmd->SetGraphicsRootDescriptorTable(0, h);

					pCmd->DrawIndexedInstanced(pcmd->ElemCount, 1, idx_offset, vtx_offset, 0);
				}
				idx_offset += pcmd->ElemCount;
			}
			vtx_offset += drawList->VtxBuffer.Size;
		}
	}
#endif

	if(!bHDR)
	{
		SCOPED_GPU_MARKER(pCmd, "SwapchainTransitionToPresent");
		// Transition SwapChain for Present
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		pCmd->ResourceBarrier(1, &barrier);
	}
}

void VQRenderer::CompositUIToHDRSwapchain(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, FWindowRenderContext& ctx, const FPostProcessParameters& PPParams, const Window* pWindow)
{
	SCOPED_GPU_MARKER(pCmd, "CompositUIToHDRSwapchain");

	// handles
	const FRenderingResources_MainWindow& rsc = this->GetRenderingResources_MainWindow();
	
	D3D12_INDEX_BUFFER_VIEW nullIBV = {};
	nullIBV.Format = DXGI_FORMAT_R32_UINT;
	nullIBV.SizeInBytes = 0;
	nullIBV.BufferLocation = 0;

	const bool bFFXCASEnabled = PPParams.IsFFXCASEnabled() && PPParams.Sharpness > 0.0f;
	const bool bFSREnabled = PPParams.IsFSREnabled();

	SwapChain& swapchain = ctx.SwapChain;
	ID3D12Resource* pSwapChainRT = swapchain.GetCurrentBackBufferRenderTarget();
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = swapchain.GetCurrentBackBufferRTVHandle();
	ID3D12Resource* pRscUI = this->GetTextureResource(rsc.Tex_UI_SDR);
	const SRV& srv_UI_SDR = this->GetSRV(rsc.SRV_UI_SDR);
	const SRV& srv_SceneColor = bFFXCASEnabled
		? this->GetSRV(rsc.SRV_PostProcess_FFXCASOut)
		: (bFSREnabled
			? this->GetSRV(rsc.SRV_PostProcess_FSR_RCASOut)
			: this->GetSRV(rsc.SRV_PostProcess_TonemapperOut));

	const int W = pWindow->GetWidth();
	const int H = pWindow->GetHeight();

	// transition barriers
	std::vector< CD3DX12_RESOURCE_BARRIER> barriers;
	CD3DX12_RESOURCE_BARRIER SwapChainTransition = CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	CD3DX12_RESOURCE_BARRIER UITransition = CD3DX12_RESOURCE_BARRIER::Transition(pRscUI, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	barriers.push_back(UITransition);
	barriers.push_back(SwapChainTransition);
	pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());

	// states
	D3D12_VIEWPORT vp = {};
	vp.Width  = static_cast<FLOAT>(W);
	vp.Height = static_cast<FLOAT>(H);
	vp.MaxDepth = 1.0f;

	D3D12_RECT rect = {0, 0, W, H};

	// cbuffer
	float* pConstBuffer = {};
	D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
	const size_t cbSize = sizeof(float) * 1;
	pCBufferHeap->AllocConstantBuffer(cbSize, (void**)&pConstBuffer, &cbAddr);
	*pConstBuffer = PPParams.TonemapperParams.UIHDRBrightness;


	// set states
	pCmd->SetPipelineState(this->GetPSO(EBuiltinPSOs::UI_HDR_scRGB_PSO)); // TODO: HDR10/PQ PSO?
	pCmd->SetGraphicsRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__UI_HDR_Composite));
	pCmd->SetGraphicsRootDescriptorTable(0, srv_SceneColor.GetGPUDescHandle());
	pCmd->SetGraphicsRootDescriptorTable(1, srv_UI_SDR.GetGPUDescHandle());
	//pCmd->SetGraphicsRootConstantBufferView(1, cbAddr);
	pCmd->SetGraphicsRoot32BitConstant(2, *((UINT*)pConstBuffer), 0);
	pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmd->IASetVertexBuffers(0, 1, NULL);
	pCmd->IASetIndexBuffer(&nullIBV);
	pCmd->RSSetScissorRects(1, &rect);
	pCmd->RSSetViewports(1, &vp);
	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);

	// draw fullscreen triangle
	pCmd->DrawInstanced(3, 1, 0, 0);

	{
		//SCOPED_GPU_MARKER(pCmd, "SwapchainTransitionToPresent");
		// Transition SwapChain for Present
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		pCmd->ResourceBarrier(1, &barrier);
	}
}

HRESULT VQRenderer::PresentFrame(FWindowRenderContext& ctx)
{
	SCOPED_CPU_MARKER("Present");
	HRESULT hr = ctx.SwapChain.Present();
	return hr;
}

void VQRenderer::ClearRenderPassHistories()
{
	{
		SCOPED_CPU_MARKER_C("WaitRenderPassesInitialized", 0xFF0000AA);
		mLatchRenderPassesInitialized.wait();
	}

	std::shared_ptr<ScreenSpaceReflectionsPass> pReclectionsPass = std::static_pointer_cast<ScreenSpaceReflectionsPass>(GetRenderPass(ERenderPass::ScreenSpaceReflections));
	pReclectionsPass->SetClearHistoryBuffers();
}
