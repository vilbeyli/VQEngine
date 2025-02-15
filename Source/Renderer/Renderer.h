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
#include "Resources/Texture.h"

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
#include "Libs/VQUtils/Source/SystemInfo.h" // FGPUInfo
#include "Libs/VQUtils/Source/Multithreading.h"

#include <vector>
#include <unordered_map>
#include <array>

#define THREADED_CTX_INIT 1
#define RENDER_THREAD__MULTI_THREADED_COMMAND_RECORDING 1
#define EXECUTE_CMD_LISTS_ON_WORKER 1

namespace D3D12MA { class Allocator; }
class Window;
class Device;
struct FSceneView;
struct FSceneShadowViews;
struct FShadowView;
struct FPostProcessParameters;
struct ID3D12RootSignature;
struct ID3D12PipelineState;
struct TextureCreateDesc;
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
constexpr size_t MAX_INSTANCE_COUNT__SHADOW_MESHES = 128;

enum EProceduralTextures
{
	  CHECKERBOARD = 0
	, CHECKERBOARD_GRAYSCALE
	, IBL_BRDF_INTEGRATION_LUT

	, NUM_PROCEDURAL_TEXTURES
};
struct FRenderStats
{
	uint NumDraws;
	uint NumDispatches;
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
	inline CommandQueue&         GetCommandQueue(CommandQueue::EType eType) { return mCmdQueues[(int)eType]; }
	inline FDeviceCapabilities   GetDeviceCapabilities() const { return mDevice.GetDeviceCapabilities(); }


	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// Swapchain
	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	void                         InitializeRenderContext(const Window* pWnd, int NumSwapchainBuffers, bool bVSync, bool bHDRSwapchain);
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
	TextureID                    CreateTexture(const TextureCreateDesc& desc, bool bCheckAlpha = false);
	
	const ID3D12Resource*        GetTextureResource(TextureID Id) const;
	      ID3D12Resource*        GetTextureResource(TextureID Id);
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
	PSO_ID                       CreatePSO_OnThisThread(const FPSODesc& psoLoadDesc);
	void                         EnqueueTask_ShaderLoad(TaskID PSOLoadTaskID, const FShaderStageCompileDesc&);
	std::vector<std::shared_future<FShaderStageCompileResult>> StartShaderLoadTasks(TaskID PSOLoadTaskID);
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
	void PreFilterEnvironmentMap(const Mesh& CubeMesh, HWND hwnd);

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
	
	HRESULT RenderLoadingScreen(const Window* pWindow, const FLoadingScreenData& LoadingScreenData, bool bUseHDRRenderPath); // TODO: should be generalized and removed later
	
	std::shared_ptr<IRenderPass> GetRenderPass(ERenderPass ePass) { return mRenderPasses[ePass]; }
	      FRenderingResources_MainWindow& GetRenderingResources_MainWindow() { return mResources_MainWnd; }
	const FRenderingResources_MainWindow& GetRenderingResources_MainWindow() const { return mResources_MainWnd; }
	const FRenderingResources_DebugWindow& GetRenderingResources_DebugWindow() const { return mResources_DebugWnd; }
	FSceneDrawData& GetSceneDrawData(int FRAME_INDEX);

	void ResetNumFramesRendered() { mNumFramesRendered = 0; }

	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// Sync
	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	void InitializeFences(HWND hwnd);
	void DestroyFences(HWND hwnd);
	void WaitCopyFenceOnCPU(HWND hwnd);

	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// PUBLIC MEMBERS
	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	FLightingPSOs     mLightingPSOs;
	FDepthPrePassPSOs mZPrePassPSOs;
	FShadowPassPSOs   mShadowPassPSOs;
	std::atomic<bool> mbDefaultResourcesLoaded; 

private:
	using PSOArray_t = std::array<ID3D12PipelineState*, EBuiltinPSOs::NUM_BUILTIN_PSOs>;
	friend class ScreenSpaceReflectionsPass; // device access
	
	// GPU
	Device mDevice;
	std::array<CommandQueue, CommandQueue::EType::NUM_COMMAND_QUEUE_TYPES> mCmdQueues;

	// memory
	D3D12MA::Allocator*    mpAllocator;
	
	StaticResourceViewHeap mHeapRTV;   // CPU-visible heap
	StaticResourceViewHeap mHeapDSV;   // CPU-visible heap
	StaticResourceViewHeap mHeapUAV;   // CPU-visible heap
	UploadHeap             mHeapUpload;
	StaticResourceViewHeap mHeapCBV_SRV_UAV;         // GPU-visible heap
	StaticResourceViewHeap mHeapSampler;             // GPU-visible heap
	StaticBufferHeap       mStaticHeap_VertexBuffer; // GPU-visible heap
	StaticBufferHeap       mStaticHeap_IndexBuffer;  // GPU-visible heap
	
	// resources & views
	std::unordered_map<TextureID, Texture>    mTextures;
	std::unordered_map<SamplerID, SAMPLER>    mSamplers;
	std::unordered_map<BufferID, VBV>         mVBVs;
	std::unordered_map<BufferID, IBV>         mIBVs;
	std::unordered_map<CBV_ID  , CBV_SRV_UAV> mCBVs;
	std::unordered_map<SRV_ID  , CBV_SRV_UAV> mSRVs;
	std::unordered_map<UAV_ID  , CBV_SRV_UAV> mUAVs;
	std::unordered_map<RTV_ID  , RTV>         mRTVs;
	std::unordered_map<DSV_ID  , DSV>         mDSVs;
	mutable std::mutex                        mMtxStaticVBHeap;
	mutable std::mutex                        mMtxStaticIBHeap;
	mutable std::mutex                        mMtxDynamicCBHeap;
	mutable std::mutex                        mMtxTextures;
	mutable std::mutex                        mMtxSamplers;
	mutable std::mutex                        mMtxSRVs_CBVs_UAVs;
	mutable std::mutex                        mMtxRTVs;
	mutable std::mutex                        mMtxDSVs;
	mutable std::mutex                        mMtxVBVs;
	mutable std::mutex                        mMtxIBVs;
	mutable std::mutex                        mMtxLoadedTexturePaths;

	// PSOs & Root Signatures
	std::unordered_map<RS_ID , ID3D12RootSignature*> mRootSignatureLookup;
	std::unordered_map<PSO_ID, ID3D12PipelineState*> mPSOs;
	ThreadPool mWorkers_PSOLoad;
	ThreadPool mWorkers_ShaderLoad;
	struct FPSOCompileResult { ID3D12PipelineState* pPSO; PSO_ID id; };
	struct FShaderLoadTaskContext { std::queue<FShaderStageCompileDesc> TaskQueue; };
	std::vector<std::shared_future<VQRenderer::FPSOCompileResult>> mPSOCompileResults;
	std::vector<std::shared_future<FShaderStageCompileResult>>     mShaderCompileResults;
	std::unordered_map < TaskID, FShaderLoadTaskContext>           mLookup_ShaderLoadContext;

	// rendering
	std::vector<std::shared_ptr<IRenderPass>>      mRenderPasses; // WIP design
	std::unordered_map<HWND, FWindowRenderContext> mRenderContextLookup;
	FRenderingResources_MainWindow  mResources_MainWnd;
	FRenderingResources_DebugWindow mResources_DebugWnd;
	uint64                          mNumFramesRendered;
	std::vector<FSceneDrawData>     mFrameSceneDrawData; // per-frame if pipelined update+render threads

	// sync
	std::vector<Fence>              mAsyncComputeSSAOReadyFence;
	std::vector<Fence>              mAsyncComputeSSAODoneFence;
	std::vector<Fence>              mCopyObjIDDoneFence; // GPU->CPU
	std::atomic<bool>               mAsyncComputeWorkSubmitted = false;
	std::atomic<bool>               mSubmitWorkerFinished = true;
	bool                            mWaitForSubmitWorker = false;

	// bookkeeping
	std::unordered_map<TextureID, std::string>         mLookup_TextureDiskLocations;
	std::unordered_map<EProceduralTextures, SRV_ID>    mLookup_ProceduralTextureSRVs;
	std::unordered_map<EProceduralTextures, TextureID> mLookup_ProceduralTextureIDs;
	std::unordered_map<std::string, TextureID>         mLoadedTexturePaths;

	// texture uploading
	std::atomic<bool>              mbExitUploadThread;
	Signal                         mSignal_UploadThreadWorkReady;
	std::thread                    mTextureUploadThread;
	std::mutex                     mMtxTextureUploadQueue;
	std::queue<FTextureUploadDesc> mTextureUploadQueue;
	
	// state
	std::atomic<bool> mbRendererInitialized;
	std::atomic<bool> mbMainSwapchainInitialized;

	FRenderStats mRenderStats;

private:
	void LoadBuiltinRootSignatures();
	void LoadDefaultResources();

	ID3D12PipelineState* CompileGraphicsPSO(const FPSODesc& Desc, std::vector<std::shared_future<FShaderStageCompileResult>>& ShaderCompileResults);
	ID3D12PipelineState* CompileComputePSO (const FPSODesc& Desc, std::vector<std::shared_future<FShaderStageCompileResult>>& ShaderCompileResults);

	ID3D12PipelineState* LoadPSO(const FPSODesc& psoLoadDesc);
	FShaderStageCompileResult LoadShader(const FShaderStageCompileDesc& shaderStageDesc);

	BufferID CreateVertexBuffer(const FBufferDesc& desc);
	BufferID CreateIndexBuffer(const FBufferDesc& desc);
	BufferID CreateConstantBuffer(const FBufferDesc& desc);

	bool CheckContext(HWND hwnd) const;

	TextureID AddTexture_ThreadSafe(Texture&& tex);
	const Texture& GetTexture_ThreadSafe(TextureID Id) const;
	Texture& GetTexture_ThreadSafe(TextureID Id);

	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// Texture Residency
	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	void QueueTextureUpload(const FTextureUploadDesc& desc);
	void ProcessTextureUpload(const FTextureUploadDesc& desc);
	void ProcessTextureUploadQueue();
	void TextureUploadThread_Main();
	inline void StartTextureUploads() { mSignal_UploadThreadWorkReady.NotifyOne(); };


	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// Render (Private)
	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	void ComputeBRDFIntegrationLUT(ID3D12GraphicsCommandList* pCmd, SRV_ID& outSRV_ID);
	bool ShouldEnableAsyncCompute(const FGraphicsSettings& GFXSettings, const FSceneView& SceneView, const FSceneShadowViews& ShadowView) const;
	void            RenderObjectIDPass(ID3D12GraphicsCommandList* pCmd, ID3D12CommandList* pCmdCopy, DynamicBufferHeap* pCBufferHeap, const std::vector< D3D12_GPU_VIRTUAL_ADDRESS>& CBAddresses, D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr, const FSceneView& SceneView, const FSceneShadowViews& ShadowView, const int BACK_BUFFER_INDEX, const FGraphicsSettings& GFXSettings);
	void            TransitionForSceneRendering(ID3D12GraphicsCommandList* pCmd, FWindowRenderContext& ctx, const FPostProcessParameters& PPParams, const FGraphicsSettings& GFXSettings);
	void            RenderDirectionalShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowViews& ShadowView);
	void            RenderSpotShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowViews& ShadowView);
	void            RenderPointShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowViews& ShadowView, size_t iBegin, size_t NumPointLights);
	void            RenderDepthPrePass(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, const std::vector< D3D12_GPU_VIRTUAL_ADDRESS>& CBAddresses, D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr, D3D12_GPU_VIRTUAL_ADDRESS perFrameCBAddr, const FGraphicsSettings& GFXSettings, bool bAsyncCompute);
	void            TransitionDepthPrePassForWrite(ID3D12GraphicsCommandList* pCmd, bool bMSAA);
	void            TransitionDepthPrePassForRead(ID3D12GraphicsCommandList* pCmd, bool bMSAA);
	void            TransitionDepthPrePassForReadAsyncCompute(ID3D12GraphicsCommandList* pCmd);
	void            TransitionDepthPrePassMSAAResolve(ID3D12GraphicsCommandList* pCmd);
	void            ResolveMSAA_DepthPrePass(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap);
	void            CopyDepthForCompute(ID3D12GraphicsCommandList* pCmd);
	void            RenderAmbientOcclusion(ID3D12GraphicsCommandList* pCmd, const FSceneView& SceneView, const FGraphicsSettings& GFXSettings, bool bAsyncCompute);
	void            RenderSceneColor(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, const FPostProcessParameters& PPParams, const std::vector< D3D12_GPU_VIRTUAL_ADDRESS>& CBAddresses, D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr, D3D12_GPU_VIRTUAL_ADDRESS perFrameCBAddr, const FGraphicsSettings& GFXSettings, bool bHDR);
	void            RenderBoundingBoxes(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, bool bMSAA);
	void            RenderOutline(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr, const FSceneView& SceneView, bool bMSAA, const std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>& rtvHandles);
	void            RenderLightBounds(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, bool bMSAA, bool bReflectionsEnabled);
	void            RenderDebugVertexAxes(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, bool bMSAA);
	void            ResolveMSAA(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FPostProcessParameters& PPParams, const FGraphicsSettings& GFXSettings);
	void            DownsampleDepth(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, TextureID DepthTextureID, SRV_ID SRVDepth);
	void            RenderReflections(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, const FGraphicsSettings& GFXSettings);
	void            RenderSceneBoundingVolumes(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr, const FSceneView& SceneView, bool bMSAA);
	void            CompositeReflections(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, const FGraphicsSettings& GFXSettings);
	void            TransitionForPostProcessing(ID3D12GraphicsCommandList* pCmd, const FPostProcessParameters& PPParams, const FGraphicsSettings& GFXSettings);
	ID3D12Resource* RenderPostProcess(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FPostProcessParameters& PPParams, bool bHDR);
	void            RenderUI(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, FWindowRenderContext& ctx, const FPostProcessParameters& PPParams, ID3D12Resource* pRscIn, const FUIState& UIState, bool bHDR);
	void            CompositUIToHDRSwapchain(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, FWindowRenderContext& ctx, const FPostProcessParameters& PPParams, const Window* pWindow);
	HRESULT         PresentFrame(FWindowRenderContext& ctx);
	void DrawShadowViewMeshList(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FShadowView& shadowView, size_t iDepthMode);

	void BatchDrawCalls();

//
// STATIC PUBLIC DATA/INTERFACE
//
public:
	static std::vector< VQSystemInfo::FGPUInfo > EnumerateDX12Adapters(bool bEnableDebugLayer, bool bEnumerateSoftwareAdapters = false, IDXGIFactory6* pFactory = nullptr);
	static const std::string_view&               DXGIFormatAsString(DXGI_FORMAT format);
	static EProceduralTextures                   GetProceduralTextureEnumFromName(const std::string& ProceduralTextureName);

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


namespace VQ_DXGI_UTILS
{
	size_t BitsPerPixel(DXGI_FORMAT fmt);

	//=====================================================================================-
	// return the byte size of a pixel (or block if block compressed)
	//=====================================================================================-
	size_t GetPixelByteSize(DXGI_FORMAT fmt);

	void MipImage(void* pData, uint width, uint height, uint bytesPerPixel);
	void CopyPixels(const void* pData, void* pDest, uint32_t stride, uint32_t bytesWidth, uint32_t height);
}