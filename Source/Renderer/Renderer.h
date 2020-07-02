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
#define INVALID_BUFFER_ID  -1

using VBV = D3D12_VERTEX_BUFFER_VIEW;
using IBV = D3D12_INDEX_BUFFER_VIEW;

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

	HRESULT(*pfnRenderFrame)(FWindowRenderContext&) = nullptr; // each window can assign their own Render function
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

	// Getters
	const VBV&                   GetVertexBufferView(BufferID Id) const;
	const IBV&                   GetIndexBufferView(BufferID Id) const;
	inline ID3D12PipelineState*  GetPSO(EBuiltinPSOs pso) const { return mpBuiltinPSOs[pso]; }
	inline ID3D12RootSignature*  GetRootSignature(EVertexBufferType vbType) const { return mpBuiltinRootSignatures[vbType]; }


private:
	void InitializeD3D12MA();
	void InitializeResourceHeaps();

	void LoadPSOs();
	void LoadDefaultResources();

	bool CheckContext(HWND hwnd) const;



private:
	// GPU
	Device mDevice; 
	CommandQueue mGFXQueue;
	CommandQueue mComputeQueue;
	CommandQueue mCopyQueue;

	// memory
	D3D12MA::Allocator* mpAllocator;
	StaticResourceViewHeap mHeapRTV;
	StaticResourceViewHeap mHeapDSV;
	StaticResourceViewHeap mHeapCBV_SRV_UAV;
	StaticResourceViewHeap mHeapSampler;
	UploadHeap             mHeapUpload;

	StaticBufferPool mStaticVertexBufferPool;
	StaticBufferPool mStaticIndexBufferPool;

	// buffers
	std::vector<VBV> mVertexBufferViews;
	std::vector<IBV> mIndexBufferViews;

	// textures
	// todo

	// render targets
	// todo

	// root signatures
	std::array<ID3D12RootSignature*, EVertexBufferType::NUM_VERTEX_BUFFER_TYPES> mpBuiltinRootSignatures;

	// PSOs
	std::array<ID3D12PipelineState*, EBuiltinPSOs::NUM_BUILTIN_PSOs> mpBuiltinPSOs;



	// data
	std::unordered_map<HWND, FWindowRenderContext> mRenderContextLookup;

};
