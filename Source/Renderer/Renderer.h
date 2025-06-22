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

#include "Core/Device.h"
#include "Core/CommandQueue.h"
#include "Core/Fence.h"

#include "Resources/ResourceHeaps.h"
#include "Resources/ResourceViews.h"
#include "Resources/Buffer.h"
#include "Resources/TextureManager.h"

#include "Pipeline/ShaderCompileUtils.h"
#include "Pipeline/PipelineStateObjects.h"

#include "Rendering/WindowRenderContext.h"
#include "Rendering/RenderResources.h"
#include "Rendering/DrawData.h"
#include "Rendering/RenderPass/RenderPass.h"

#include "Engine/Core/Types.h"
#include "Engine/Core/Platform.h"
#include "Engine/Settings.h"

#define VQUTILS_SYSTEMINFO_INCLUDE_D3D12 1
#include "Libs/VQUtils/Include/SystemInfo.h" // FGPUInfo
#include "Libs/VQUtils/Include/Multithreading/ThreadPool.h"
#include "Libs/VQUtils/Include/Multithreading/TaskSignal.h"

#include <vector>
#include <array>

#define THREADED_CTX_INIT 1
#define RENDER_THREAD__MULTI_THREADED_COMMAND_RECORDING 1
#define EXECUTE_CMD_LISTS_ON_WORKER 0 // TODO: fix cmd list submission thread + swapchain index sync
#define MARKER_COLOR  0xFF00FF00 
#define RENDER_WORKER_CPU_MARKER   SCOPED_CPU_MARKER_C("RenderWorker", MARKER_COLOR)

namespace D3D12MA { class Allocator; }
class Window;
class Device;
struct FSceneView;
struct FSceneShadowViews;
struct FPostProcessParameters;
struct ID3D12RootSignature;
struct ID3D12PipelineState;
struct FTextureRequest;
struct FEnvironmentMapRenderingResources;
struct Mesh;
struct FUIState;
struct FLoadingScreenData;

//========================================================================================================================================================================================================---
//
// TYPE DEFINITIONS
//
//========================================================================================================================================================================================================---
constexpr size_t MAX_INSTANCE_COUNT__UNLIT_SHADER = 512;

enum EProceduralTextures
{
	  CHECKERBOARD = 0
	, CHECKERBOARD_GRAYSCALE
	, IBL_BRDF_INTEGRATION_LUT

	, NUM_PROCEDURAL_TEXTURES
};
struct FRenderStats
{
	uint64 mNumFramesRendered = 0;
	uint   NumDraws = 0;
	uint   NumDispatches = 0;
	uint   NumLitMeshDrawCommands = 0;
	uint   NumShadowMeshDrawCommands = 0;
	uint   NumBoundingBoxDrawCommands = 0;
	inline void Reset() { *this = FRenderStats(); }
};
struct FCommandRecordingThreadConfig
{
	int8 iGfxCmd = -1;
	int8 iCopyCmd = -1;
	int8 iComputeCmd = -1;
};
enum ERenderThreadWorkID
{
	ZPrePassAndAsyncCompute = 0,
	ObjectIDRenderAndCopy,
	PointShadow0,
	PointShadow1,
	PointShadow2,
	PointShadow3,
	PointShadow4,
	SpotShadows,
	DirectionalShadows,
	SceneAndPostprocessing,
	UIAndPresentation,

	NUM_RENDER_THREAD_WORK_IDS,
};

//========================================================================================================================================================================================================---
//
// RENDERER
//
//========================================================================================================================================================================================================---
class VQRenderer
{
public:
	void                         Initialize(const FGraphicsSettings& Settings);
	void                         Load();
	void                         Unload();
	void                         Destroy();

	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// EVENTS
	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	void                         WaitForLoadCompletion() const;
	void                         OnWindowSizeChanged(HWND hwnd, unsigned w, unsigned h);
	void                         LoadWindowSizeDependentResources(HWND hwnd, unsigned w, unsigned h, float fResolutionScale, bool bHDR);
	void                         UnloadWindowSizeDependentResources(HWND hwnd);

	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// Device & Queues
	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	inline ID3D12Device*         GetDevicePtr() const { return mDevice.GetDevicePtr(); }
	inline CommandQueue&         GetCommandQueue(ECommandQueueType eType) { return mRenderingCmdQueues[(int)eType]; }
	inline CommandQueue&         GetPresentationCommandQueue() { return mRenderingPresentationQueue; }
	inline FDeviceCapabilities   GetDeviceCapabilities() const { return mDevice.GetDeviceCapabilities(); }


	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// Swapchain
	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	void                         InitializeRenderContext(const Window* pWnd, int NumSwapchainBuffers, bool bVSync, bool bHDRSwapchain, bool bDedicatedPresentQueue);
	inline short                 GetSwapChainBackBufferCount(std::unique_ptr<Window>& pWnd) const { return GetSwapChainBackBufferCount(pWnd.get()); };
	unsigned short               GetSwapChainBackBufferCount(Window* pWnd) const;
	unsigned short               GetSwapChainBackBufferCount(HWND hwnd) const;
	SwapChain&                   GetWindowSwapChain(HWND hwnd);
	FWindowRenderContext&        GetWindowRenderContext(HWND hwnd);
	void                         WaitMainSwapchainReady() const;


	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// Buffers
	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	BufferID                     CreateBuffer(const FBufferDesc& desc);
	void                         UploadVertexAndIndexBufferHeaps();


	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// Textures
	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	TextureID                    CreateTextureFromFile(const char* pFilePath, bool bCheckAlpha, bool bGenerateMips/* = false*/);
	TextureID                    CreateTexture(const FTextureRequest& desc, bool bCheckAlpha = false);
	inline void                  WaitForTexture(TextureID ID) const { mTextureManager.WaitForTexture(ID); }; // blocks caller until texture is loaded
	TextureManager&              GetTextureManager() { return mTextureManager; }

	ID3D12Resource*              GetTextureResource(TextureID Id) const;
	DXGI_FORMAT                  GetTextureFormat(TextureID Id) const;
	bool                         GetTextureAlphaChannelUsed(TextureID Id) const;
	inline void                  GetTextureDimensions(TextureID Id, int& SizeX, int& SizeY) const { int dummy; GetTextureDimensions(Id, SizeX, SizeY, dummy); }
	inline void                  GetTextureDimensions(TextureID Id, int& SizeX, int& SizeY, int& NumSlices) const { int dummy; GetTextureDimensions(Id, SizeX, SizeY, NumSlices, dummy); }
	void                         GetTextureDimensions(TextureID Id, int& SizeX, int& SizeY, int& NumSlices, int& NumMips) const;
	uint                         GetTextureMips(TextureID Id) const;
	uint                         GetTextureSampleCount(TextureID) const;
	
	TextureID                    GetProceduralTexture(EProceduralTextures tex) const;
	inline const SRV&            GetProceduralTextureSRV(EProceduralTextures tex) const { return GetSRV(GetProceduralTextureSRV_ID(tex)); }
	inline const SRV_ID          GetProceduralTextureSRV_ID(EProceduralTextures tex) const { return mLookup_ProceduralTextureSRVs.at(tex); }
	
	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// Resource views
	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	SRV_ID                       AllocateSRV(uint NumDescriptors = 1); // Allocates a ResourceView descriptor from the respective heap and returns a unique identifier.
	DSV_ID                       AllocateDSV(uint NumDescriptors = 1);
	RTV_ID                       AllocateRTV(uint NumDescriptors = 1);
	UAV_ID                       AllocateUAV(uint NumDescriptors = 1);
	SRV_ID                       AllocateAndInitializeSRV(TextureID texID);
	DSV_ID                       AllocateAndInitializeDSV(TextureID texID);

	// Initializes a ResourceView from given texture and the specified heap index
	void                         InitializeDSV(DSV_ID dsvID, uint heapIndex, TextureID texID, int ArraySlice = 0);
	void                         InitializeSRV(SRV_ID srvID, uint heapIndex, TextureID texID, bool bInitAsArrayView = false, bool bInitAsCubeView = false, D3D12_SHADER_RESOURCE_VIEW_DESC* pSRVDesc = nullptr, UINT ShaderComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
	void                         InitializeSRV(SRV_ID srvID, uint heapIndex, D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc);
	void                         InitializeRTV(RTV_ID rtvID, uint heapIndex, TextureID texID);
	void                         InitializeRTV(RTV_ID rtvID, uint heapIndex, TextureID texID, int arraySlice, int mipLevel);
	void                         InitializeUAV(UAV_ID uavID, uint heapIndex, TextureID texID, uint arraySlice = 0, uint mipSlice = 0);
	void                         InitializeUAVForBuffer(UAV_ID uavID, uint heapIndex, TextureID texID, DXGI_FORMAT bufferViewFormatOverride);
	void                         InitializeSRVForBuffer(SRV_ID uavID, uint heapIndex, TextureID texID, DXGI_FORMAT bufferViewFormatOverride);
	void                         InitializeNullSRV(SRV_ID srvID, uint heapIndex, UINT ShaderComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);

	void                         DestroyTexture(TextureID& texID);
	void                         DestroySRV(SRV_ID srvID);
	void                         DestroyDSV(DSV_ID dsvID);

	const VBV&                   GetVertexBufferView(BufferID Id) const;
	const IBV&                   GetIndexBufferView(BufferID Id) const;
	const CBV_SRV_UAV&           GetShaderResourceView(SRV_ID Id) const;
	const CBV_SRV_UAV&           GetUnorderedAccessView(UAV_ID Id) const;
	const CBV_SRV_UAV&           GetConstantBufferView(CBV_ID Id) const;
	const RTV&                   GetRenderTargetView(RTV_ID Id) const;
	const DSV&                   GetDepthStencilView(RTV_ID Id) const;
	inline const VBV&            GetVBV(BufferID Id) const { return GetVertexBufferView(Id);    }
	inline const IBV&            GetIBV(BufferID Id) const { return GetIndexBufferView(Id);     }
	inline const CBV_SRV_UAV&    GetSRV(SRV_ID   Id) const { return GetShaderResourceView(Id);  }
	inline const CBV_SRV_UAV&    GetUAV(UAV_ID   Id) const { return GetUnorderedAccessView(Id); }
	inline const CBV_SRV_UAV&    GetCBV(CBV_ID   Id) const { return GetConstantBufferView(Id);  }
	inline const RTV&            GetRTV(RTV_ID   Id) const { return GetRenderTargetView(Id);    }
	inline const DSV&            GetDSV(DSV_ID   Id) const { return GetDepthStencilView(Id);    }


	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// PSO & Shader management
	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	void                         StartPSOCompilation_MT();
	void                         WaitPSOCompilation();
	void                         AssignPSOs();
	std::vector<FPSODesc>        LoadBuiltinPSODescs_Legacy();
	std::vector<FPSODesc>        LoadBuiltinPSODescs();
	void                         ReservePSOMap(size_t NumPSOs);

	inline ID3D12PipelineState*  GetPSO(EBuiltinPSOs pso) const { return mPSOs.at(static_cast<PSO_ID>(pso)); }
	       ID3D12PipelineState*  GetPSO(PSO_ID psoID) const;
	       ID3D12RootSignature*  GetBuiltinRootSignature(EBuiltinRootSignatures eRootSignature) const;

	ID3D12DescriptorHeap*        GetDescHeap(EResourceHeapType HeapType);


	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// Render (Public)
	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	void PreFilterEnvironmentMap(const Mesh& CubeMesh);

	HRESULT PreRenderScene(ThreadPool& WorkerThreads,
		const Window* pWindow,
		const FSceneView& SceneView,
		const FSceneShadowViews& ShadowView,
		const FPostProcessParameters& PPParams,
		const FGraphicsSettings& GFXSettings,
		const FUIState& UIState
	);

	HRESULT RenderScene(ThreadPool& WorkerThreads,
		const Window* pWindow,
		const FSceneView& SceneView,
		const FSceneShadowViews& ShadowView,
		const FPostProcessParameters& PPParams,
		const FGraphicsSettings& GFXSettings,
		const FUIState& UIState,
		bool bHDRDisplay
	);
	
	HRESULT PreRenderLoadingScreen(ThreadPool& WorkerThreads,
		const Window* pWindow,
		const FGraphicsSettings& GFXSettings,
		const FUIState& UIState
	);
	HRESULT RenderLoadingScreen(const Window* pWindow, 
		const FLoadingScreenData& LoadingScreenData, 
		bool bUseHDRRenderPath
	);
	
	void ClearRenderPassHistories();

	std::shared_ptr<IRenderPass> GetRenderPass(ERenderPass ePass) { return mRenderPasses[ePass]; }
	      FRenderingResources_MainWindow& GetRenderingResources_MainWindow() { return mResources_MainWnd; }
	const FRenderingResources_MainWindow& GetRenderingResources_MainWindow() const { return mResources_MainWnd; }
	const FRenderingResources_DebugWindow& GetRenderingResources_DebugWindow() const { return mResources_DebugWnd; }
	FSceneDrawData& GetSceneDrawData(int FRAME_INDEX);

	void ResetNumFramesRendered() { mRenderStats.mNumFramesRendered = 0; }
	const FRenderStats& GetRenderStats() const { return mRenderStats; }

	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// Frame Sync
	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	void InitializeFences(HWND hwnd);
	void DestroyFences(HWND hwnd);
	void WaitCopyFenceOnCPU(HWND hwnd);

	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// Init Sync
	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	void WaitHeapsInitialized();
	void WaitMemoryAllocatorInitialized();
	void WaitLoadingScreenReady() const;
	void SignalLoadingScreenReady();

	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// PUBLIC MEMBERS
	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	FLightingPSOs     mLightingPSOs;
	FDepthPrePassPSOs mZPrePassPSOs;
	FShadowPassPSOs   mShadowPassPSOs;

private:
	using PSOArray_t = std::array<ID3D12PipelineState*, EBuiltinPSOs::NUM_BUILTIN_PSOs>;
	friend class ScreenSpaceReflectionsPass; // device access
	
	// GPU
	Device mDevice;

	// render command execution context
	CommandQueue mRenderingCmdQueues[NUM_COMMAND_QUEUE_TYPES];
	std::vector<std::vector<ID3D12CommandAllocator*>> mRenderingCommandAllocators[NUM_COMMAND_QUEUE_TYPES]; // pre queue, per back buffer, per recording thread
	std::vector<std::vector<ID3D12CommandList*>> mpRenderingCmds[NUM_COMMAND_QUEUE_TYPES]; // per queue, per back buffer, per recording thread
	std::vector<std::vector<bool>> mCmdClosed[NUM_COMMAND_QUEUE_TYPES]; // per queue, per back buffer, per recording thread
	std::vector<DynamicBufferHeap> mDynamicHeap_RenderingConstantBuffer; // per recording thread
	UINT mNumCurrentlyRecordingRenderingThreads[NUM_COMMAND_QUEUE_TYPES];
	std::vector<Fence> mAsyncComputeSSAOReadyFence;
	std::vector<Fence> mAsyncComputeSSAODoneFence;
	std::vector<Fence> mCopyObjIDDoneFence; // GPU->CPU
	std::atomic<bool>  mAsyncComputeWorkSubmitted = false;
	Fence mFrameRenderDoneFence;
	
	FCommandRecordingThreadConfig mRenderWorkerConfig[NUM_RENDER_THREAD_WORK_IDS];

	// frame presentation context
	CommandQueue      mRenderingPresentationQueue;
	bool              mWaitForSubmitWorker = false;
	TaskSignal<void>  mSubmitWorkerSignal;
	std::thread       mFrameSubmitThread; // currenly unused, main thread submits the frame
	std::atomic<bool> mStopTrheads = false;

	// background gpu task execution context
	enum EBackgroungTaskThread
	{
		EnvironmentMap_Prefiltering = 0,
		GPU_Generated_Textures,

		NUM_BACKGROUND_TASK_THREADS,
	};
	CommandQueue            mBackgroundTaskCmdQueues[NUM_COMMAND_QUEUE_TYPES];
	ID3D12CommandAllocator* mBackgroundTaskCommandAllocators[NUM_COMMAND_QUEUE_TYPES][NUM_BACKGROUND_TASK_THREADS];
	ID3D12CommandList*      mpBackgroundTaskCmds[NUM_COMMAND_QUEUE_TYPES][NUM_BACKGROUND_TASK_THREADS];
	DynamicBufferHeap       mDynamicHeap_BackgroundTaskConstantBuffer[NUM_BACKGROUND_TASK_THREADS];
	UINT                    mNumCurrentlyRecordingBackgroundTaskThreads[NUM_COMMAND_QUEUE_TYPES];
	Fence                   mBackgroundTaskFencesPerQueue[NUM_COMMAND_QUEUE_TYPES];

	// memory allocator
	D3D12MA::Allocator*    mpAllocator = nullptr;
	
	// heaps
	StaticResourceViewHeap mHeapRTV;   // CPU-visible heap
	StaticResourceViewHeap mHeapDSV;   // CPU-visible heap
	StaticResourceViewHeap mHeapUAV;   // CPU-visible heap
	StaticResourceViewHeap mHeapCBV_SRV_UAV;         // GPU-visible heap
	StaticResourceViewHeap mHeapSampler;             // GPU-visible heap
	UploadHeap             mHeapUpload;
	StaticBufferHeap       mStaticHeap_VertexBuffer; // GPU-visible heap
	StaticBufferHeap       mStaticHeap_IndexBuffer;  // GPU-visible heap
	mutable std::mutex     mMtxSamplers;
	mutable std::mutex     mMtxUploadHeap;
	mutable std::mutex     mMtxStaticVBHeap;
	mutable std::mutex     mMtxStaticIBHeap;
	
	// resources & views
	std::unordered_map<SamplerID, SAMPLER>    mSamplers;
	std::unordered_map<BufferID, VBV>         mVBVs;
	std::unordered_map<BufferID, IBV>         mIBVs;
	std::unordered_map<CBV_ID  , CBV_SRV_UAV> mCBVs;
	std::unordered_map<SRV_ID  , CBV_SRV_UAV> mSRVs;
	std::unordered_map<UAV_ID  , CBV_SRV_UAV> mUAVs;
	std::unordered_map<RTV_ID  , RTV>         mRTVs;
	std::unordered_map<DSV_ID  , DSV>         mDSVs;
	mutable std::mutex                        mMtxDynamicCBHeap;
	mutable std::mutex                        mMtxSRVs_CBVs_UAVs; // TODO: separate mutexes for SRV/CBV/UAV
	mutable std::mutex                        mMtxRTVs;
	mutable std::mutex                        mMtxDSVs;
	mutable std::mutex                        mMtxVBVs;
	mutable std::mutex                        mMtxIBVs;
	TextureManager                            mTextureManager;

	// PSOs & Root Signatures
	std::unordered_map<RS_ID , ID3D12RootSignature*> mRootSignatureLookup;
	std::unordered_map<PSO_ID, ID3D12PipelineState*> mPSOs;
	ThreadPool mWorkers_PSOLoad;
	ThreadPool mWorkers_ShaderLoad;
	struct FPSOCompileResult { ID3D12PipelineState* pPSO; PSO_ID id; };
	struct FShaderLoadTaskContext { std::queue<FShaderStageCompileDesc> TaskQueue; };
	std::vector<std::shared_future<FPSOCompileResult>        > mPSOCompileResults;
	std::vector<std::shared_future<FShaderStageCompileResult>> mShaderCompileResults;
	std::unordered_map < TaskID, FShaderLoadTaskContext>       mLookup_ShaderLoadContext;

	// rendering
	std::vector<std::shared_ptr<IRenderPass>>      mRenderPasses; // WIP design
	std::unordered_map<HWND, FWindowRenderContext> mRenderContextLookup;
	FRenderingResources_MainWindow  mResources_MainWnd;
	FRenderingResources_DebugWindow mResources_DebugWnd;
	std::vector<FSceneDrawData>     mFrameSceneDrawData; // per-frame if pipelined update+render threads

	// init sync
	std::latch mLatchDeviceInitialized{ 1 };
	std::latch mLatchCmdQueuesInitialized{ 1 };
	std::latch mLatchMemoryAllocatorInitialized{ 1 };
	std::latch mLatchHeapsInitialized{ 1 };
	std::latch mLatchRootSignaturesInitialized{ 1 };
	std::latch mLatchSignalLoadingScreenReady{ 1 };
	std::latch mLatchPSOLoaderDispatched{ 1 };
	std::latch mLatchRenderPassesInitialized{ 1 };
	std::latch mLatchSwapchainInitialized{ 1 };
	std::latch mLatchDefaultResourcesLoaded{ 1 };
	std::latch mLatchWindowSizeDependentResourcesInitialized{ 1 };
	bool mbWindowSizeDependentResourcesFirstInitiazliationDone = false;

	// bookkeeping
	std::unordered_map<EProceduralTextures, SRV_ID>    mLookup_ProceduralTextureSRVs;
	std::unordered_map<EProceduralTextures, TextureID> mLookup_ProceduralTextureIDs;
	std::unordered_map<std::string, bool>              mShaderCacheDirtyMap;

	FRenderStats mRenderStats;

private:
	void LoadBuiltinRootSignatures();
	void LoadDefaultResources();
	void CreateProceduralTextures();
	void CreateProceduralTextureViews();

	ID3D12PipelineState* CompileGraphicsPSO(FPSODesc& Desc, std::vector<std::shared_future<FShaderStageCompileResult>>& ShaderCompileResults);
	ID3D12PipelineState* CompileComputePSO (FPSODesc& Desc, std::vector<std::shared_future<FShaderStageCompileResult>>& ShaderCompileResults);
	const FPSOCompileResult WaitPSOReady(PSO_ID psoID);

	FShaderStageCompileResult LoadShader(const FShaderStageCompileDesc& shaderStageDesc, const std::unordered_map<std::string, bool>& ShaderCacheDirtyMap);

	BufferID CreateVertexBuffer(const FBufferDesc& desc);
	BufferID CreateIndexBuffer(const FBufferDesc& desc);
	BufferID CreateConstantBuffer(const FBufferDesc& desc);

	bool CheckContext(HWND hwnd) const;

	void AllocateCommandLists(ECommandQueueType eQueueType, size_t iBackBuffer, size_t NumRecordingThreads);
	void ResetCommandLists(ECommandQueueType eQueueType, size_t iBackBuffer, size_t NumRecordingThreads);

	void AllocateConstantBufferMemory(uint32_t NumHeaps, uint32_t NumBackBuffers, uint32_t MemoryPerHeap);

	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// Render (Private)
	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	void            RenderObjectIDPass(int iThread, ID3D12CommandList* pCmdCopy, DynamicBufferHeap* pCBufferHeap, D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr, const FSceneView& SceneView, const FSceneShadowViews& ShadowView, const int BACK_BUFFER_INDEX, const FGraphicsSettings& GFXSettings);
	void            TransitionForSceneRendering(ID3D12GraphicsCommandList* pCmd, FWindowRenderContext& ctx, const FPostProcessParameters& PPParams, const FGraphicsSettings& GFXSettings);
	void            RenderDirectionalShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowViews& ShadowView, const FSceneView& SceneView);
	void            RenderSpotShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowViews& ShadowView, const FSceneView& SceneView);
	void            RenderPointShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowViews& ShadowView, const FSceneView& SceneView, size_t iBegin, size_t NumPointLights);
	void            RenderDepthPrePass(ID3D12GraphicsCommandList* pCmd, const FSceneView& SceneView, D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr, const FGraphicsSettings& GFXSettings, bool bAsyncCompute);
	void            TransitionDepthPrePassForWrite(ID3D12GraphicsCommandList* pCmd, bool bMSAA);
	void            TransitionDepthPrePassForRead(ID3D12GraphicsCommandList* pCmd, bool bMSAA);
	void            TransitionDepthPrePassForReadAsyncCompute(ID3D12GraphicsCommandList* pCmd);
	void            TransitionDepthPrePassMSAAResolve(ID3D12GraphicsCommandList* pCmd);
	void            ResolveMSAA_DepthPrePass(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap);
	void            CopyDepthForCompute(ID3D12GraphicsCommandList* pCmd);
	void            RenderAmbientOcclusion(ID3D12GraphicsCommandList* pCmd, const FSceneView& SceneView, const FGraphicsSettings& GFXSettings, bool bAsyncCompute);
	void            RenderSceneColor(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, const FPostProcessParameters& PPParams, D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr, D3D12_GPU_VIRTUAL_ADDRESS perFrameCBAddr, const FGraphicsSettings& GFXSettings, bool bHDR);
	void            RenderBoundingBoxes(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, bool bMSAA);
	void            RenderOutline(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr, const FSceneView& SceneView, bool bMSAA, const std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>& rtvHandles);
	void            RenderLightBounds(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, bool bMSAA, bool bReflectionsEnabled);
	void            RenderDebugVertexAxes(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, bool bMSAA);
	void            ResolveMSAA(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FPostProcessParameters& PPParams, const FGraphicsSettings& GFXSettings);
	void            DownsampleDepth(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, TextureID DepthTextureID, SRV_ID SRVDepth);
	void            RenderReflections(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, const FGraphicsSettings& GFXSettings);
	void            RenderSceneBoundingVolumes(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr, const FSceneView& SceneView, const FGraphicsSettings& GFXSettings);
	void            CompositeReflections(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, const FGraphicsSettings& GFXSettings);
	void            TransitionForPostProcessing(ID3D12GraphicsCommandList* pCmd, const FPostProcessParameters& PPParams, const FGraphicsSettings& GFXSettings);
	void            TransitionForUI(ID3D12GraphicsCommandList* pCmd, const FPostProcessParameters& PPParams, const FGraphicsSettings& GFXSettings, bool bHDRDisplay, ID3D12Resource* pRsc, ID3D12Resource* pSwapChainRT);
	ID3D12Resource* RenderPostProcess(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FPostProcessParameters& PPParams, const FGraphicsSettings& GFXSettings, bool bHDR);
	void            RenderUI(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, FWindowRenderContext& ctx, const FPostProcessParameters& PPParams, ID3D12Resource* pRscIn, const SRV& srv_ColorIn, const FUIState& UIState, const FGraphicsSettings& GFXSettings, bool bHDR);
	void            CompositUIToHDRSwapchain(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, FWindowRenderContext& ctx, const FPostProcessParameters& PPParams, const FGraphicsSettings& GFXSettings);
	HRESULT         PresentFrame(FWindowRenderContext& ctx);
	void DrawShadowViewMeshList(ID3D12GraphicsCommandList* pCmd, const std::vector<FInstancedDrawParameters>& drawParams, size_t iDepthMode);

	void BatchDrawCalls(ThreadPool& WorkerThreads, const FSceneView& SceneView, const FSceneShadowViews& SceneShadowView, FWindowRenderContext& ctx, const FPostProcessParameters& PPParams, const FGraphicsSettings& GFXSettings);
	
	void FrameSubmitThread_Main();
//
// STATIC PUBLIC DATA/INTERFACE
//
public:
	static std::vector< VQSystemInfo::FGPUInfo > EnumerateDX12Adapters(bool bEnableDebugLayer, bool bEnumerateSoftwareAdapters = false, IDXGIFactory6* pFactory = nullptr);
	static const std::string_view&               DXGIFormatAsString(DXGI_FORMAT format);
	static EProceduralTextures                   GetProceduralTextureEnumFromName(const std::string& ProceduralTextureName);
	static D3D12_COMMAND_LIST_TYPE               GetDX12CmdListType(ECommandQueueType type);

	static bool ShouldEnableAsyncCompute(const FGraphicsSettings& GFXSettings, const FSceneView& SceneView, const FSceneShadowViews& ShadowView);
	static bool ShouldUseMotionVectorsTarget(const FGraphicsSettings& GFXSettings);
	static bool ShouldUseVisualizationTarget(const FPostProcessParameters& PPParams);

	static std::wstring GetFullPathOfShader(LPCWSTR shaderFileName);
	static std::wstring GetFullPathOfShader(const std::string& shaderFileName);
	static std::string  GetCachedShaderBinaryPath(const FShaderStageCompileDesc& ShaderStageCompileDesc);

	static std::string ShaderSourceFileDirectory;
	static std::string PSOCacheDirectory;
	static std::string ShaderCacheDirectory;

	// Supported HDR Formats { DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT  }
	// Supported SDR Formats { DXGI_FORMAT_R8G8B8A8_UNORM   , DXGI_FORMAT_R8G8B8A8_UNORM_SRGB }
	static const DXGI_FORMAT PREFERRED_HDR_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
	static const DXGI_FORMAT PREFERRED_SDR_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
};
