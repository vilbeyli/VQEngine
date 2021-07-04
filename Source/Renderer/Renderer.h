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

struct FPSOLoadDesc
{
	std::string PSOName;
	union
	{
		D3D12_COMPUTE_PIPELINE_STATE_DESC  D3D12ComputeDesc;
		D3D12_GRAPHICS_PIPELINE_STATE_DESC D3D12GraphicsDesc;
	};
	std::vector<FShaderStageCompileDesc> ShaderStageCompileDescs;
};

enum EBuiltinPSOs // TODO: hardcoded PSOs until a generic Shader solution is integrated
{
	HELLO_WORLD_TRIANGLE_PSO = 0,
	FULLSCREEN_TRIANGLE_PSO,
	HELLO_WORLD_CUBE_PSO,
	HELLO_WORLD_CUBE_PSO_MSAA_4,
	TONEMAPPER_PSO,
	HDR_FP16_SWAPCHAIN_PSO,
	SKYDOME_PSO,
	SKYDOME_PSO_MSAA_4,
	OBJECT_PSO,
	OBJECT_PSO_MSAA_4,
	DEPTH_PREPASS_PSO,
	DEPTH_PREPASS_PSO_MSAA_4,
	FORWARD_LIGHTING_PSO,
	FORWARD_LIGHTING_PSO_MSAA_4,
	WIREFRAME_PSO,
	WIREFRAME_PSO_MSAA_4,
	UNLIT_PSO,
	UNLIT_PSO_MSAA_4,
	DEPTH_PASS_PSO,
	DEPTH_PASS_LINEAR_PSO,
	DEPTH_PASS_ALPHAMASKED_PSO,
	DEPTH_RESOLVE,
	CUBEMAP_CONVOLUTION_DIFFUSE_PSO,
	CUBEMAP_CONVOLUTION_DIFFUSE_PER_FACE_PSO,
	CUBEMAP_CONVOLUTION_SPECULAR_PSO,
	GAUSSIAN_BLUR_CS_NAIVE_X_PSO,
	GAUSSIAN_BLUR_CS_NAIVE_Y_PSO,
	BRDF_INTEGRATION_CS_PSO,
	FFX_CAS_CS_PSO,
	FFX_SPD_CS_PSO,
	NUM_BUILTIN_PSOs
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
	void                         Exit();
	inline void                  WaitForLoadCompletion() const { while (!mbDefaultResourcesLoaded); };

	void                         OnWindowSizeChanged(HWND hwnd, unsigned w, unsigned h);

	inline ID3D12Device* GetDevicePtr() { return mDevice.GetDevicePtr(); };

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
	SRV_ID                       CreateSRV(uint NumDescriptors = 1); // TODO: Rename to Alloc**V()
	DSV_ID                       CreateDSV(uint NumDescriptors = 1); // TODO: Rename to Alloc**V()
	RTV_ID                       CreateRTV(uint NumDescriptors = 1); // TODO: Rename to Alloc**V()
	UAV_ID                       CreateUAV(uint NumDescriptors = 1); // TODO: Rename to Alloc**V()
	SRV_ID                       CreateAndInitializeSRV(TextureID texID);
	DSV_ID                       CreateAndInitializeDSV(TextureID texID);

	// Initializes a ResourceView from given texture and the specified heap index
	void                         InitializeDSV(DSV_ID dsvID, uint heapIndex, TextureID texID, int ArraySlice = 0);
	void                         InitializeSRV(SRV_ID srvID, uint heapIndex, TextureID texID, bool bInitAsArrayView = false, bool bInitAsCubeView = false, D3D12_SHADER_RESOURCE_VIEW_DESC* pSRVDesc = nullptr, UINT ShaderComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
	void                         InitializeSRV(SRV_ID srvID, uint heapIndex, D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc);
	void                         InitializeRTV(RTV_ID rtvID, uint heapIndex, TextureID texID);
	void                         InitializeRTV(RTV_ID rtvID, uint heapIndex, TextureID texID, int arraySlice, int mipLevel);
	void                         InitializeUAV(UAV_ID uavID, uint heapIndex, TextureID texID);

	void                         DestroyTexture(TextureID& texID);
	void                         DestroySRV(SRV_ID& srvID);
	void                         DestroyDSV(DSV_ID& dsvID);

	// Getters: PSO, RootSignature, Heap
	inline ID3D12PipelineState*  GetPSO(EBuiltinPSOs pso) const { return mPSOs.at(static_cast<PSO_ID>(pso)); }
	inline ID3D12RootSignature*  GetRootSignature(int idx) const { return mpBuiltinRootSignatures[idx]; }
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

	// Texture Residency
	void QueueTextureUpload(const FTextureUploadDesc& desc);
	void ProcessTextureUpload(const FTextureUploadDesc& desc);
	void ProcessTextureUploadQueue();
	void TextureUploadThread_Main();
	inline void StartTextureUploads() { mSignal_UploadThreadWorkReady.NotifyOne(); };

private:
	using PSOArray_t = std::array<ID3D12PipelineState*, EBuiltinPSOs::NUM_BUILTIN_PSOs>;
	
	// GPU
	Device       mDevice; 
	CommandQueue mGFXQueue;
	CommandQueue mComputeQueue;
	CommandQueue mCopyQueue;

	// memory
	D3D12MA::Allocator*            mpAllocator;
	StaticResourceViewHeap         mHeapRTV;
	StaticResourceViewHeap         mHeapDSV;
	StaticResourceViewHeap         mHeapCBV_SRV_UAV;
	StaticResourceViewHeap         mHeapSampler;
	UploadHeap                     mHeapUpload;
	StaticBufferHeap               mStaticHeap_VertexBuffer;
	StaticBufferHeap               mStaticHeap_IndexBuffer;
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
	std::vector<ID3D12RootSignature*> mpBuiltinRootSignatures;
	std::unordered_map<PSO_ID, ID3D12PipelineState*> mPSOs;

	// data
	std::unordered_map<HWND, FWindowRenderContext> mRenderContextLookup;

	// bookkeeping
	std::unordered_map<TextureID, std::string>         mLookup_TextureDiskLocations;
	std::unordered_map<EProceduralTextures, SRV_ID>    mLookup_ProceduralTextureSRVs;
	std::unordered_map<EProceduralTextures, TextureID> mLookup_ProceduralTextureIDs;

	std::atomic<bool>              mbExitUploadThread;
	Signal                         mSignal_UploadThreadWorkReady;
	std::thread                    mTextureUploadThread;
	std::mutex                     mMtxTextureUploadQueue;
	std::queue<FTextureUploadDesc> mTextureUploadQueue;

	std::atomic<bool>              mbDefaultResourcesLoaded;
	
	
	// Multithreaded PSO Loading
	ThreadPool mWorkers_PSOLoad; // Loading a PSO will use one worker from shaderLoad pool for each shader stage to be compiled
	struct FPSOLoadTaskContext
	{
		std::queue<FPSOLoadDesc> LoadQueue;
		std::set<std::hash<FPSOLoadDesc>> UniquePSOHashes;
	};
	std::unordered_map <TaskID, FPSOLoadTaskContext> mLookup_PSOLoadContext;

	// Multithreaded Shader Loading
	ThreadPool mWorkers_ShaderLoad;
	struct FShaderLoadTaskContext { std::queue<FShaderStageCompileDesc> LoadQueue; };
	std::unordered_map < TaskID, FShaderLoadTaskContext> mLookup_ShaderLoadContext;
	
	void EnqueueShaderLoadTask(TaskID PSOLoadTaskID, const FShaderStageCompileDesc&);
	std::vector<std::shared_future<FShaderStageCompileResult>> StartShaderLoadTasks(TaskID PSOLoadTaskID);

private:
	void InitializeD3D12MA();
	void InitializeHeaps();
	

	void LoadRootSignatures();
	void LoadPSOs();
	void LoadDefaultResources();
	

	ID3D12PipelineState* LoadPSO(const FPSOLoadDesc& psoLoadDesc);
	FShaderStageCompileResult LoadShader(const FShaderStageCompileDesc& shaderStageDesc);

	BufferID CreateVertexBuffer(const FBufferDesc& desc);
	BufferID CreateIndexBuffer(const FBufferDesc& desc);
	BufferID CreateConstantBuffer(const FBufferDesc& desc);

	bool CheckContext(HWND hwnd) const;

	TextureID AddTexture_ThreadSafe(Texture&& tex);

//
// STATIC PUBLIC DATA/INTERFACE
//
public:
	static std::vector< VQSystemInfo::FGPUInfo > EnumerateDX12Adapters(bool bEnableDebugLayer, bool bEnumerateSoftwareAdapters = false, IDXGIFactory6* pFactory = nullptr);
	static const std::string_view&               DXGIFormatAsString(DXGI_FORMAT format);
	static EProceduralTextures                   GetProceduralTextureEnumFromName(const std::string& ProceduralTextureName);

	static std::string PSOCacheDirectory;
	static std::string ShaderCacheDirectory;
	static void InitializeShaderAndPSOCacheDirectory();
};