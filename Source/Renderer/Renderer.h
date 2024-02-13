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

#include "Device.h"
#include "SwapChain.h"
#include "CommandQueue.h"
#include "ResourceHeaps.h"
#include "ResourceViews.h"
#include "Buffer.h"
#include "Texture.h"
#include "Shader.h"
#include "WindowRenderContext.h"

#include "../Engine/Core/Types.h"
#include "../Engine/Core/Platform.h"
#include "../Engine/Settings.h"

#define VQUTILS_SYSTEMINFO_INCLUDE_D3D12 1
#include "../../Libs/VQUtils/Source/SystemInfo.h" // FGPUInfo
#include "../../Libs/VQUtils/Source/Image.h"
#include "../../Libs/VQUtils/Source/Multithreading.h"

#include <vector>
#include <unordered_map>
#include <array>
#include <queue>
#include <set>

namespace D3D12MA { class Allocator; }
class Window;
struct ID3D12RootSignature;
struct ID3D12PipelineState;

//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// TYPE DEFINITIONS
//
//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

struct FPSODesc
{
	std::string PSOName;
	D3D12_COMPUTE_PIPELINE_STATE_DESC  D3D12ComputeDesc;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC D3D12GraphicsDesc;
	std::vector<FShaderStageCompileDesc> ShaderStageCompileDescs;
};

struct FPSOCreationTaskParameters
{
	PSO_ID* pID = nullptr; // ID to be set by the task once the PSO loads
	FPSODesc Desc = {};
};


constexpr size_t MAX_INSTANCE_COUNT__UNLIT_SHADER = 512;
constexpr size_t MAX_INSTANCE_COUNT__SHADOW_MESHES = 128;

enum EBuiltinPSOs // TODO: remove the hardcoded PSOs when a generic Shader solution is integrated
{
	FULLSCREEN_TRIANGLE_PSO = 0,
	VIZUALIZATION_CS_PSO,
	UI_PSO,
	UI_HDR_scRGB_PSO,
	TONEMAPPER_PSO,
	HDR_FP16_SWAPCHAIN_PSO,
	SKYDOME_PSO,
	SKYDOME_PSO_MSAA_4,
	OBJECT_PSO,
	OBJECT_PSO_MSAA_4,
	DEPTH_PREPASS_PSO,
	DEPTH_PREPASS_PSO_MSAA_4,
	FORWARD_LIGHTING_PSO,
	FORWARD_LIGHTING_AND_MV_PSO,
	FORWARD_LIGHTING_AND_VIZ_PSO,
	FORWARD_LIGHTING_AND_VIZ_AND_MV_PSO,
	FORWARD_LIGHTING_PSO_MSAA_4,
	FORWARD_LIGHTING_AND_MV_PSO_MSAA_4,
	FORWARD_LIGHTING_AND_VIZ_PSO_MSAA_4,
	FORWARD_LIGHTING_AND_VIZ_AND_MV_PSO_MSAA_4,
	WIREFRAME_PSO,
	WIREFRAME_PSO_MSAA_4,
	WIREFRAME_INSTANCED_PSO,
	WIREFRAME_INSTANCED_MSAA4_PSO,
	UNLIT_PSO,
	UNLIT_PSO_MSAA_4,
	OUTLINE_PSO,
	OUTLINE_PSO_MSAA_4,
	DEPTH_PASS_PSO,
	DEPTH_PASS_LINEAR_PSO,
	DEPTH_PASS_ALPHAMASKED_PSO,
	CUBEMAP_CONVOLUTION_DIFFUSE_PSO,
	CUBEMAP_CONVOLUTION_DIFFUSE_PER_FACE_PSO,
	CUBEMAP_CONVOLUTION_SPECULAR_PSO,
	GAUSSIAN_BLUR_CS_NAIVE_X_PSO,
	GAUSSIAN_BLUR_CS_NAIVE_Y_PSO,
	BRDF_INTEGRATION_CS_PSO,
	FFX_CAS_CS_PSO,
	FFX_SPD_CS_PSO,
	FFX_FSR1_EASU_CS_PSO,
	FFX_FSR1_RCAS_CS_PSO,
	DOWNSAMPLE_DEPTH_CS_PSO,
	NUM_BUILTIN_PSOs
};

enum EBuiltinRootSignatures
{
	CS__SRV1_UAV1_ROOTCBV1,
	CS__SRV2_UAV1_ROOTCBV1,
	//--------------------------
	LEGACY__HelloWorldTriangle,
	LEGACY__FullScreenTriangle,
	LEGACY__HelloWorldCube,
	LEGACY__Object,
	LEGACY__ForwardLighting,
	LEGACY__WireframeUnlit,
	LEGACY__ShadowPassDepthOnlyVS,
	LEGACY__ShadowPassLinearDepthVSPS,
	LEGACY__ShadowPassMaskedDepthVSPS,
	LEGACY__ConvolutionCubemap,
	LEGACY__BRDFIntegrationCS,
	LEGACY__FFX_SPD_CS,
	LEGACY__ZPrePass,
	LEGACY__FFX_FSR1,
	LEGACY__UI_HDR_Composite,
	LEGACY__DownsampleDepthCS,

	NUM_BUILTIN_ROOT_SIGNATURES
};

enum EProceduralTextures
{
	  CHECKERBOARD = 0
	, CHECKERBOARD_GRAYSCALE
	, IBL_BRDF_INTEGRATION_LUT

	, NUM_PROCEDURAL_TEXTURES
};

//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// RENDERER
//
//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
class VQRenderer
{
public:
	void                         Initialize(const FGraphicsSettings& Settings);
	void                         Load();
	void                         Unload();
	void                         Destroy();
	inline void                  WaitForLoadCompletion() const { while (!mbDefaultResourcesLoaded); };

	void                         OnWindowSizeChanged(HWND hwnd, unsigned w, unsigned h);

	// Swapchain-interface
	void                         InitializeRenderContext(const Window* pWnd, int NumSwapchainBuffers, bool bVSync, bool bHDRSwapchain);
	inline short                 GetSwapChainBackBufferCount(std::unique_ptr<Window>& pWnd) const { return GetSwapChainBackBufferCount(pWnd.get()); };
	unsigned short               GetSwapChainBackBufferCount(Window* pWnd) const;
	unsigned short               GetSwapChainBackBufferCount(HWND hwnd) const;
	SwapChain&                   GetWindowSwapChain(HWND hwnd);
	FWindowRenderContext&        GetWindowRenderContext(HWND hwnd);

	// Resource management
	BufferID                     CreateBuffer(const FBufferDesc& desc);
	TextureID                    CreateTextureFromFile(const char* pFilePath, bool bGenerateMips = false);
	TextureID                    CreateTexture(const TextureCreateDesc& desc);
	void                         UploadVertexAndIndexBufferHeaps();

	// Allocates a ResourceView from the respective heap and returns a unique identifier.
	SRV_ID                       AllocateSRV(uint NumDescriptors = 1);
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
	void                         DestroySRV(SRV_ID& srvID);
	void                         DestroyDSV(DSV_ID& dsvID);

	// Pipeline State Object creation functions
	PSO_ID                       CreatePSO_OnThisThread(const FPSODesc& psoLoadDesc);
	void                         EnqueueTask_CreatePSO(FPSOCreationTaskParameters&& params);
	void                         StartPSOCreationTasks();
	void                         WaitForPSOCreationTaskQueueCompletion();
	//void AbortTasks(); // ?


	// Getters: PSO, RootSignature, Heap
	inline ID3D12PipelineState*  GetPSO(EBuiltinPSOs pso) const { return mPSOs.at(static_cast<PSO_ID>(pso)); }
	       ID3D12PipelineState*  GetPSO(PSO_ID psoID) const;
		   ID3D12RootSignature*  GetBuiltinRootSignature(EBuiltinRootSignatures eRootSignature) const;

	ID3D12DescriptorHeap*        GetDescHeap(EResourceHeapType HeapType);

	// Getters: Resource Views
	const VBV&                   GetVertexBufferView(BufferID Id) const;
	const IBV&                   GetIndexBufferView(BufferID Id) const;
	const CBV_SRV_UAV&           GetShaderResourceView(SRV_ID Id) const;
	const CBV_SRV_UAV&           GetUnorderedAccessView(UAV_ID Id) const;
	const CBV_SRV_UAV&           GetConstantBufferView(CBV_ID Id) const;
	const RTV&                   GetRenderTargetView(RTV_ID Id) const;
	const DSV&                   GetDepthStencilView(RTV_ID Id) const;

	const ID3D12Resource*        GetTextureResource(TextureID Id) const;
	      ID3D12Resource*        GetTextureResource(TextureID Id);
	DXGI_FORMAT                  GetTextureFormat(TextureID Id) const;

	inline void                  GetTextureDimensions(TextureID Id, int& SizeX, int& SizeY) const { int dummy; GetTextureDimensions(Id, SizeX, SizeY, dummy); }
	inline void                  GetTextureDimensions(TextureID Id, int& SizeX, int& SizeY, int& NumSlices) const { int dummy; GetTextureDimensions(Id, SizeX, SizeY, NumSlices, dummy); }
	void                         GetTextureDimensions(TextureID Id, int& SizeX, int& SizeY, int& NumSlices, int& NumMips) const;
	uint                         GetTextureMips(TextureID Id) const;
	uint                         GetTextureSampleCount(TextureID) const;

	inline const VBV&            GetVBV(BufferID Id) const { return GetVertexBufferView(Id);    }
	inline const IBV&            GetIBV(BufferID Id) const { return GetIndexBufferView(Id);     }
	inline const CBV_SRV_UAV&    GetSRV(SRV_ID   Id) const { return GetShaderResourceView(Id);  }
	inline const CBV_SRV_UAV&    GetUAV(UAV_ID   Id) const { return GetUnorderedAccessView(Id); }
	inline const CBV_SRV_UAV&    GetCBV(CBV_ID   Id) const { return GetConstantBufferView(Id);  }
	inline const RTV&            GetRTV(RTV_ID   Id) const { return GetRenderTargetView(Id);    }
	inline const DSV&            GetDSV(DSV_ID   Id) const { return GetDepthStencilView(Id);    }
	
	inline const SRV&            GetProceduralTextureSRV(EProceduralTextures tex) const { return GetSRV(GetProceduralTextureSRV_ID(tex)); }
	inline const SRV_ID          GetProceduralTextureSRV_ID(EProceduralTextures tex) const { return mLookup_ProceduralTextureSRVs.at(tex); }
	TextureID                    GetProceduralTexture(EProceduralTextures tex) const;
	
	inline ID3D12Device*         GetDevicePtr() { return mDevice.GetDevicePtr(); }
	inline FDeviceCapabilities   GetDeviceCapabilities() const { return mDevice.GetDeviceCapabilities(); }
	inline CommandQueue&         GetCommandQueue(CommandQueue::EType eType) { return mCmdQueues[(int)eType]; }

private:
	using PSOArray_t = std::array<ID3D12PipelineState*, EBuiltinPSOs::NUM_BUILTIN_PSOs>;
	
	// GPU
	Device       mDevice; 
	std::array<CommandQueue, CommandQueue::EType::NUM_COMMAND_QUEUE_TYPES> mCmdQueues;

	// memory
	D3D12MA::Allocator*            mpAllocator;
	// CPU-visible heaps ----------------------------------------
	StaticResourceViewHeap         mHeapRTV; 
	StaticResourceViewHeap         mHeapDSV; 
	StaticResourceViewHeap         mHeapUAV; // CPU-visible heap (TODO: fix SSAO UAV clear error msg)
	UploadHeap                     mHeapUpload;
	// CPU-visible heaps ----------------------------------------
	// GPU-visible heaps ----------------------------------------
	StaticResourceViewHeap         mHeapCBV_SRV_UAV;
	StaticResourceViewHeap         mHeapSampler;
	StaticBufferHeap               mStaticHeap_VertexBuffer;
	StaticBufferHeap               mStaticHeap_IndexBuffer;
	// GPU-visible heaps ----------------------------------------
	// constant buffers are handled in FRenderContext objects

	// resources & views
	std::unordered_map<std::string, TextureID>     mLoadedTexturePaths;
	std::unordered_map<TextureID, Texture>         mTextures;
	std::unordered_map<SamplerID, SAMPLER>         mSamplers;
	std::unordered_map<BufferID, VBV>              mVBVs;
	std::unordered_map<BufferID, IBV>              mIBVs;
	std::unordered_map<CBV_ID  , CBV_SRV_UAV>      mCBVs;
	std::unordered_map<SRV_ID  , CBV_SRV_UAV>      mSRVs;
	std::unordered_map<UAV_ID  , CBV_SRV_UAV>      mUAVs;
	std::unordered_map<RTV_ID  , RTV>              mRTVs;
	std::unordered_map<DSV_ID  , DSV>              mDSVs;
	mutable std::mutex                             mMtxStaticVBHeap;
	mutable std::mutex                             mMtxStaticIBHeap;
	mutable std::mutex                             mMtxDynamicCBHeap;
	mutable std::mutex                             mMtxTextures;
	mutable std::mutex                             mMtxSamplers;
	mutable std::mutex                             mMtxSRVs_CBVs_UAVs;
	mutable std::mutex                             mMtxRTVs;
	mutable std::mutex                             mMtxDSVs;
	mutable std::mutex                             mMtxVBVs;
	mutable std::mutex                             mMtxIBVs;


	// root signatures & PSOs
	std::unordered_map<RS_ID , ID3D12RootSignature*> mRootSignatureLookup;
	std::unordered_map<PSO_ID, ID3D12PipelineState*> mPSOs;

	// data
	std::unordered_map<HWND, FWindowRenderContext> mRenderContextLookup;

	// bookkeeping
	std::unordered_map<TextureID, std::string>         mLookup_TextureDiskLocations;
	std::unordered_map<EProceduralTextures, SRV_ID>    mLookup_ProceduralTextureSRVs;
	std::unordered_map<EProceduralTextures, TextureID> mLookup_ProceduralTextureIDs;

	// texture uploading
	std::atomic<bool>              mbExitUploadThread;
	Signal                         mSignal_UploadThreadWorkReady;
	std::thread                    mTextureUploadThread;
	std::mutex                     mMtxTextureUploadQueue;
	std::queue<FTextureUploadDesc> mTextureUploadQueue;

	std::atomic<bool>              mbDefaultResourcesLoaded;
	
	
	// Multithreaded PSO Loading
	ThreadPool mWorkers_PSOLoad; // Loading a PSO will use one worker from shaderLoad pool for each shader stage to be compiled
	struct FPSOCreationTaskExecutionContext{ std::queue<FPSOCreationTaskParameters> TaskQueue; };
	std::unordered_map <TaskID, FPSOCreationTaskExecutionContext> mLookup_PSOCreationContext;

	// Multithreaded Shader Loading
	ThreadPool mWorkers_ShaderLoad;
	struct FShaderLoadTaskContext { std::queue<FShaderStageCompileDesc> TaskQueue; };
	std::unordered_map < TaskID, FShaderLoadTaskContext> mLookup_ShaderLoadContext;
	
	void EnqueueTask_ShaderLoad(TaskID PSOLoadTaskID, const FShaderStageCompileDesc&);
	std::vector<std::shared_future<FShaderStageCompileResult>> StartShaderLoadTasks(TaskID PSOLoadTaskID);

private:
	void InitializeD3D12MA();
	void InitializeHeaps();
	

	void LoadBuiltinRootSignatures();
	void LoadBuiltinPSOs();
	void LoadDefaultResources();
	

	ID3D12PipelineState* LoadPSO(const FPSODesc& psoLoadDesc);
	FShaderStageCompileResult LoadShader(const FShaderStageCompileDesc& shaderStageDesc);

	BufferID CreateVertexBuffer(const FBufferDesc& desc);
	BufferID CreateIndexBuffer(const FBufferDesc& desc);
	BufferID CreateConstantBuffer(const FBufferDesc& desc);

	bool CheckContext(HWND hwnd) const;

	TextureID AddTexture_ThreadSafe(Texture&& tex);

	// Texture Residency
	void QueueTextureUpload(const FTextureUploadDesc& desc);
	void ProcessTextureUpload(const FTextureUploadDesc& desc);
	void ProcessTextureUploadQueue();
	void TextureUploadThread_Main();
	inline void StartTextureUploads() { mSignal_UploadThreadWorkReady.NotifyOne(); };

//
// STATIC PUBLIC DATA/INTERFACE
//
public:
	static std::vector< VQSystemInfo::FGPUInfo > EnumerateDX12Adapters(bool bEnableDebugLayer, bool bEnumerateSoftwareAdapters = false, IDXGIFactory6* pFactory = nullptr);
	static const std::string_view&               DXGIFormatAsString(DXGI_FORMAT format);
	static EProceduralTextures                   GetProceduralTextureEnumFromName(const std::string& ProceduralTextureName);

	static std::string ShaderSourceFileDirectory;
	static std::wstring GetFullPathOfShader(LPCWSTR shaderFileName);
	static std::wstring GetFullPathOfShader(const std::string& shaderFileName);
	static std::string PSOCacheDirectory;
	static std::string ShaderCacheDirectory;
	static void InitializeShaderAndPSOCacheDirectory();
};


namespace VQ_DXGI_UTILS
{
	size_t BitsPerPixel(DXGI_FORMAT fmt);

	//--------------------------------------------------------------------------------------
	// return the byte size of a pixel (or block if block compressed)
	//--------------------------------------------------------------------------------------
	size_t GetPixelByteSize(DXGI_FORMAT fmt);

	void MipImage(void* pData, uint width, uint height, uint bytesPerPixel);
	void CopyPixels(const void* pData, void* pDest, uint32_t stride, uint32_t bytesWidth, uint32_t height);
}