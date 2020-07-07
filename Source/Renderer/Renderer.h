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

using BufferID = int;
using TextureID = int;
using SRV_ID = int;
using UAV_ID = int;
using CBV_ID = int;
using RTV_ID = int;
using DSV_ID = int;
#define INVALID_ID  -1


//
// TYPE DEFINITIONS
//
// Data to be updated per frame
struct FFrameData
{
	std::array<float, 4> SwapChainClearColor;
};
struct FLoadingScreenData
{
	std::array<float, 4> SwapChainClearColor;
	// TODO: loading screen background img resource
	// TODO: animation resources
};

struct FRendererInitializeParameters
{
	std::vector<FWindowRepresentation> Windows;
	FGraphicsSettings                  Settings;
};

// Encapsulates the swapchain and window association.
// Each SwapChain is:
// - associated with a Window (HWND)
// - associated with a Graphics Queue for Present()
// - created with a Device
struct FWindowRenderContext
{
	Device* pDevice = nullptr;
	SwapChain    SwapChain;
	CommandQueue PresentQueue;


	// 1x allocator per command-recording-thread, multiplied by num swapchain backbuffers
	// Source: https://gpuopen.com/performance/
	std::vector<ID3D12CommandAllocator*> mCommandAllocatorsGFX;
	std::vector<ID3D12CommandAllocator*> mCommandAllocatorsCompute;
	std::vector<ID3D12CommandAllocator*> mCommandAllocatorsCopy;


	ID3D12GraphicsCommandList* pCmdList_GFX = nullptr;

	bool bVsync = false;

	int MainRTResolutionX = -1;
	int MainRTResolutionY = -1;
};

enum EBuiltinPSOs
{
	HELLO_WORLD_TRIANGLE_PSO = 0,
	LOADING_SCREEN_PSO,

	NUM_BUILTIN_PSOs
};


//
// RENDERER
//
class VQRenderer
{
public:
	static std::vector< VQSystemInfo::FGPUInfo > EnumerateDX12Adapters(bool bEnableDebugLayer, bool bEnumerateSoftwareAdapters = false);

public:
	void                         Initialize(const FRendererInitializeParameters& RendererInitParams);
	void                         Load();
	void                         Unload();
	void                         Exit();

	void                         OnWindowSizeChanged(HWND hwnd, unsigned w, unsigned h);

	// Swapchain-interface
	inline short                 GetSwapChainBackBufferCountOfWindow(std::unique_ptr<Window>& pWnd) const { return GetSwapChainBackBufferCountOfWindow(pWnd.get()); };
	short                        GetSwapChainBackBufferCountOfWindow(Window* pWnd) const;
	short                        GetSwapChainBackBufferCountOfWindow(HWND hwnd) const;
	SwapChain&                   GetWindowSwapChain(HWND hwnd);
	FWindowRenderContext&        GetWindowRenderContext(HWND hwnd);

	// Resource management
	BufferID                     CreateBuffer(const FBufferDesc& desc);

	// Getters: PSO, RootSignature, Heap
	inline ID3D12PipelineState*  GetPSO(EBuiltinPSOs pso) const { return mpBuiltinPSOs[pso]; }
	inline ID3D12RootSignature*  GetRootSignature(EVertexBufferType vbType) const { return mpBuiltinRootSignatures[vbType]; }
	ID3D12DescriptorHeap*        GetDescHeap(EResourceHeapType HeapType);

	// Getters: Resource Views
	const VBV&                   GetVertexBufferView(BufferID Id) const;
	const IBV&                   GetIndexBufferView(BufferID Id) const;
	const CBV_SRV_UAV&           GetShaderResourceView(SRV_ID Id) const;
	const CBV_SRV_UAV&           GetUnorderedAccessView(UAV_ID Id) const;
	const CBV_SRV_UAV&           GetConstantBufferView(CBV_ID Id) const;
	const RTV&                   GetRenderTargetView(RTV_ID Id) const;

	inline const VBV&            GetVBV(BufferID Id) const { return GetVertexBufferView(Id); }
	inline const IBV&            GetIBV(BufferID Id) const { return GetIndexBufferView(Id); }
	inline const CBV_SRV_UAV&    GetSRV(SRV_ID   Id) const { return GetShaderResourceView(Id); }
	inline const CBV_SRV_UAV&    GetUAV(UAV_ID   Id) const { return GetUnorderedAccessView(Id); }
	inline const CBV_SRV_UAV&    GetCBV(CBV_ID   Id) const { return GetConstantBufferView(Id); }
	inline const RTV&            GetRTV(RTV_ID   Id) const { return GetRenderTargetView(Id); }


private:
	void InitializeD3D12MA();
	void InitializeResourceHeaps();

	void LoadPSOs();
	void LoadDefaultResources();

	BufferID CreateVertexBuffer(const FBufferDesc& desc);
	BufferID CreateIndexBuffer(const FBufferDesc& desc);
	BufferID CreateConstantBuffer(const FBufferDesc& desc);

	bool CheckContext(HWND hwnd) const;

private:
	using RootSignatureArray_t = std::array<ID3D12RootSignature*, EVertexBufferType::NUM_VERTEX_BUFFER_TYPES>;
	using PSOArray_t           = std::array<ID3D12PipelineState*, EBuiltinPSOs::NUM_BUILTIN_PSOs>;

	// GPU
	Device                   mDevice; 
	CommandQueue             mGFXQueue;
	CommandQueue             mComputeQueue;
	CommandQueue             mCopyQueue;

	// memory
	D3D12MA::Allocator*      mpAllocator;
	StaticResourceViewHeap   mHeapRTV;
	StaticResourceViewHeap   mHeapDSV;
	StaticResourceViewHeap   mHeapCBV_SRV_UAV;
	StaticResourceViewHeap   mHeapSampler;
	UploadHeap               mHeapUpload;

	// resources
	StaticBufferPool         mStaticVertexBufferPool;
	StaticBufferPool         mStaticIndexBufferPool;
	std::vector<Texture>     mTextures;
	//todo: samplers

	// resource views
	std::vector<VBV>         mVBVs;
	std::vector<IBV>         mIBVs;
	std::vector<CBV_SRV_UAV> mCBVs;
	std::vector<CBV_SRV_UAV> mSRVs;
	std::vector<CBV_SRV_UAV> mUAVs;
	std::vector<RTV>         mRTVs;
	// todo: convert std::vector<T> -> std::unordered_map<ID, T>

	// root signatures
	RootSignatureArray_t     mpBuiltinRootSignatures;

	// PSOs
	PSOArray_t               mpBuiltinPSOs;

	// data
	std::unordered_map<HWND, FWindowRenderContext> mRenderContextLookup;

};
