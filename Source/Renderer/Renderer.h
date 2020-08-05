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

#include "../Application/Platform.h"
#include "../Application/Settings.h"
#include "../Application/Types.h"

#define VQUTILS_SYSTEMINFO_INCLUDE_D3D12 1
#include "../../Libs/VQUtils/Source/SystemInfo.h" // FGPUInfo

#include <vector>
#include <unordered_map>
#include <array>

namespace D3D12MA { class Allocator; }
class Window;
struct ID3D12RootSignature;
struct ID3D12PipelineState;



//
// TYPE DEFINITIONS
//
// Encapsulates the swapchain and window association.
// Each SwapChain is:
// - associated with a Window (HWND)
// - associated with a Graphics Queue for Present()
// - created with a Device
struct FWindowRenderContext
{
	Device*      pDevice = nullptr;
	SwapChain    SwapChain;
	CommandQueue PresentQueue;


	// 1x allocator per command-recording-thread, multiplied by num swapchain backbuffers
	// Source: https://gpuopen.com/performance/
	std::vector<ID3D12CommandAllocator*> mCommandAllocatorsGFX;
	std::vector<ID3D12CommandAllocator*> mCommandAllocatorsCompute;
	std::vector<ID3D12CommandAllocator*> mCommandAllocatorsCopy;

	DynamicBufferHeap mDynamicHeap_ConstantBuffer;

	ID3D12GraphicsCommandList* pCmdList_GFX = nullptr;

	int MainRTResolutionX = -1;
	int MainRTResolutionY = -1;
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

	NUM_BUILTIN_PSOs
};


//
// RENDERER
//
class VQRenderer
{
public:
	void                         Initialize(const FGraphicsSettings& Settings);
	void                         Load();
	void                         Unload();
	void                         Exit();

	void                         OnWindowSizeChanged(HWND hwnd, unsigned w, unsigned h);

	// Swapchain-interface
	void                         InitializeRenderContext(const Window* pWnd, int NumSwapchainBuffers, bool bVSync, bool bHDRSwapchain);
	inline short                 GetSwapChainBackBufferCount(std::unique_ptr<Window>& pWnd) const { return GetSwapChainBackBufferCount(pWnd.get()); };
	short                        GetSwapChainBackBufferCount(Window* pWnd) const;
	short                        GetSwapChainBackBufferCount(HWND hwnd) const;
	SwapChain&                   GetWindowSwapChain(HWND hwnd);
	FWindowRenderContext&        GetWindowRenderContext(HWND hwnd);

	// Resource management
	BufferID                     CreateBuffer(const FBufferDesc& desc);
	TextureID                    CreateTextureFromFile(const char* pFilePath);
	TextureID                    CreateTexture(const std::string& name, const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES ResourceState, const void* pData = nullptr);

	// Allocates a ResourceView from the respective heap and returns a unique identifier.
	SRV_ID                       CreateSRV(uint NumDescriptors = 1);
	DSV_ID                       CreateDSV(uint NumDescriptors = 1);
	RTV_ID                       CreateRTV(uint NumDescriptors = 1);
	UAV_ID                       CreateUAV(uint NumDescriptors = 1);
	SRV_ID                       CreateAndInitializeSRV(TextureID texID);
	DSV_ID                       CreateAndInitializeDSV(TextureID texID);

	// Initializes a ResourceView from given texture and the specified heap index
	void                         InitializeDSV(DSV_ID dsvID, uint heapIndex, TextureID texID);
	void                         InitializeSRV(SRV_ID srvID, uint heapIndex, TextureID texID);
	void                         InitializeRTV(RTV_ID rtvID, uint heapIndex, TextureID texID);
	void                         InitializeUAV(UAV_ID uavID, uint heapIndex, TextureID texID);

	void                         DestroyTexture(TextureID texID);
	void                         DestroySRV(SRV_ID srvID);
	void                         DestroyDSV(DSV_ID dsvID);

	// Getters: PSO, RootSignature, Heap
	inline ID3D12PipelineState*  GetPSO(EBuiltinPSOs pso) const { return mpBuiltinPSOs[pso]; }
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

	inline const VBV&            GetVBV(BufferID Id) const { return GetVertexBufferView(Id);    }
	inline const IBV&            GetIBV(BufferID Id) const { return GetIndexBufferView(Id);     }
	inline const CBV_SRV_UAV&    GetSRV(SRV_ID   Id) const { return GetShaderResourceView(Id);  }
	inline const CBV_SRV_UAV&    GetUAV(UAV_ID   Id) const { return GetUnorderedAccessView(Id); }
	inline const CBV_SRV_UAV&    GetCBV(CBV_ID   Id) const { return GetConstantBufferView(Id);  }
	inline const RTV&            GetRTV(RTV_ID   Id) const { return GetRenderTargetView(Id);    }
	inline const DSV&            GetDSV(DSV_ID   Id) const { return GetDepthStencilView(Id);    }
	
private:
	using PSOArray_t = std::array<ID3D12PipelineState*, EBuiltinPSOs::NUM_BUILTIN_PSOs>;
	
	// GPU
	Device                                         mDevice; 
	CommandQueue                                   mGFXQueue;
	CommandQueue                                   mComputeQueue;
	CommandQueue                                   mCopyQueue;

	// memory
	D3D12MA::Allocator*                            mpAllocator;
	StaticResourceViewHeap                         mHeapRTV;
	StaticResourceViewHeap                         mHeapDSV;
	StaticResourceViewHeap                         mHeapCBV_SRV_UAV;
	StaticResourceViewHeap                         mHeapSampler;
	UploadHeap                                     mHeapUpload;
	StaticBufferHeap                               mStaticHeap_VertexBuffer;
	StaticBufferHeap                               mStaticHeap_IndexBuffer;

	// resources & views
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

	// root signatures
	std::vector<ID3D12RootSignature*>              mpBuiltinRootSignatures;

	// PSOs
	PSOArray_t                                     mpBuiltinPSOs;

	// data
	std::unordered_map<HWND, FWindowRenderContext> mRenderContextLookup;

	// bookkeeping
	std::unordered_map<TextureID, std::string>     mLookup_TextureDiskLocations;



private:
	void InitializeD3D12MA();
	void InitializeHeaps();

	void LoadPSOs();
	void LoadDefaultResources();

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
	static const std::string_view& DXGIFormatAsString(DXGI_FORMAT format);
};