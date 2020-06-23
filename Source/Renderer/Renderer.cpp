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

#include "Renderer.h"
#include "Device.h"

#include "../Application/Window.h"

#include "../../Libs/VQUtils/Source/Log.h"
#include "../../Libs/VQUtils/Source/utils.h"
#include "../../Libs/D3D12MA/src/Common.h"
#include "../../Libs/D3D12MA/src/D3D12MemAlloc.h"

#include <cassert>
#include <atomic>

#include <wrl.h>
#include <D3Dcompiler.h>
#pragma comment(lib, "D3DCompiler.lib")

using namespace Microsoft::WRL;
using namespace VQSystemInfo;

#ifdef _DEBUG
	#define ENABLE_DEBUG_LAYER      1
	#define ENABLE_VALIDATION_LAYER 1
#else
	#define ENABLE_DEBUG_LAYER      0
	#define ENABLE_VALIDATION_LAYER 0
#endif


// ---------------------------------------------------------------------------------------
// D3D12MA Integration 
// ---------------------------------------------------------------------------------------
#define D3D12MA_ENABLE_CPU_ALLOCATION_CALLBACKS       1
#define D3D12MA_ENABLE_CPU_ALLOCATION_CALLBACKS_PRINT 1
static void* const         CUSTOM_ALLOCATION_USER_DATA = (void*)(uintptr_t)0xDEADC0DE;
static std::atomic<size_t> g_CpuAllocationCount{ 0 };
static void* CustomAllocate(size_t Size, size_t Alignment, void* pUserData)
{
	assert(pUserData == CUSTOM_ALLOCATION_USER_DATA);
	void* memory = _aligned_malloc(Size, Alignment);
	if (D3D12MA_ENABLE_CPU_ALLOCATION_CALLBACKS_PRINT)
	{
		wprintf(L"Allocate Size=%llu Alignment=%llu -> %p\n", Size, Alignment, memory);
	}
	++g_CpuAllocationCount;
	return memory;
}
static void CustomFree(void* pMemory, void* pUserData)
{
	assert(pUserData == CUSTOM_ALLOCATION_USER_DATA);
	if (pMemory)
	{
		--g_CpuAllocationCount;
		if (D3D12MA_ENABLE_CPU_ALLOCATION_CALLBACKS_PRINT)
		{
			wprintf(L"Free %p\n", pMemory);
		}
		_aligned_free(pMemory);
	}
}
// ---------------------------------------------------------------------------------------

#define KILOBYTE 1024
#define MEGABYTE 1024*KILOBYTE
#define GIGABYTE 1024*MEGABYTE


//
// PUBLIC
//
void VQRenderer::Initialize(const FRendererInitializeParameters& params)
{
	Device* pVQDevice = &mDevice;
	const FGraphicsSettings& Settings = params.Settings;
	const int NUM_SWAPCHAIN_BUFFERS   = Settings.bUseTripleBuffering ? 3 : 2;


	// Create the device
	FDeviceCreateDesc deviceDesc = {};
	deviceDesc.bEnableDebugLayer = ENABLE_DEBUG_LAYER;
	deviceDesc.bEnableValidationLayer = ENABLE_VALIDATION_LAYER;
	mDevice.Create(deviceDesc);
	ID3D12Device* pDevice = pVQDevice->GetDevicePtr();


	// Create Command Queues of different types
	mGFXQueue.Create(pVQDevice, CommandQueue::ECommandQueueType::GFX);
	mComputeQueue.Create(pVQDevice, CommandQueue::ECommandQueueType::COMPUTE);
	mCopyQueue.Create(pVQDevice, CommandQueue::ECommandQueueType::COPY);


	// Create the present queues & swapchains associated with each window passed into the VQRenderer
	// Swapchains contain their own render targets 
	const size_t NumWindows = params.Windows.size();
	for(size_t i = 0; i< NumWindows; ++i)
	{
		const FWindowRepresentation& wnd = params.Windows[i];
		

		FRenderWindowContext ctx = {};

		ctx.pDevice = pVQDevice;
		ctx.PresentQueue.Create(ctx.pDevice, CommandQueue::ECommandQueueType::GFX); // Create the GFX queue for presenting the SwapChain
		ctx.bVsync = wnd.bVSync; // we store bVSync in ctx instead of SwapChain and provide it through SwapChain.Present() param : either way should be fine

		// Create the SwapChain
		FSwapChainCreateDesc swapChainDesc = {};
		swapChainDesc.numBackBuffers = NUM_SWAPCHAIN_BUFFERS;
		swapChainDesc.pDevice = ctx.pDevice->GetDevicePtr();
		swapChainDesc.pWindow = &wnd;
		swapChainDesc.pCmdQueue = &ctx.PresentQueue;
		swapChainDesc.bVSync = ctx.bVsync;
		swapChainDesc.bFullscreen = false;
		ctx.SwapChain.Create(swapChainDesc);

		// Create command allocators
		ctx.mCommandAllocatorsGFX.resize(NUM_SWAPCHAIN_BUFFERS);
		ctx.mCommandAllocatorsCompute.resize(NUM_SWAPCHAIN_BUFFERS);
		ctx.mCommandAllocatorsCopy.resize(NUM_SWAPCHAIN_BUFFERS);
		for (int f = 0; f < NUM_SWAPCHAIN_BUFFERS; ++f)
		{
			ID3D12CommandAllocator* pCmdAlloc = {};
			pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT , IID_PPV_ARGS(&ctx.mCommandAllocatorsGFX[f]));
			pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&ctx.mCommandAllocatorsCompute[f]));
			pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY   , IID_PPV_ARGS(&ctx.mCommandAllocatorsCopy[f]));
			SetName(ctx.mCommandAllocatorsGFX[f], "RenderContext::CmdAllocGFX[%d]", f);
			SetName(ctx.mCommandAllocatorsCompute[f], "RenderContext::CmdAllocCompute[%d]", f);
			SetName(ctx.mCommandAllocatorsCopy[f], "RenderContext::CmdAllocCopy[%d]", f);
		}

		// Create command lists
		pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ctx.mCommandAllocatorsGFX[0], nullptr, IID_PPV_ARGS(&ctx.pCmdList_GFX));
		ctx.pCmdList_GFX->SetName(L"RenderContext::CmdListGFX");
		ctx.pCmdList_GFX->Close();

		// Save other context data
		ctx.MainRTResolutionX = wnd.width;
		ctx.MainRTResolutionY = wnd.height;

		// save the render context
		this->mRenderContextLookup.emplace(wnd.hwnd, std::move(ctx));
	}

	// Initialize memory
	InitializeD3D12MA();
	InitializeResourceHeaps();
	{
		constexpr uint32 STATIC_GEOMETRY_MEMORY_SIZE = 64 * MEGABYTE;
		constexpr bool USE_GPU_MEMORY = true;
		mStaticVertexBufferPool.Create(pDevice, EBufferType::VERTEX_BUFFER, STATIC_GEOMETRY_MEMORY_SIZE, USE_GPU_MEMORY, "VQRenderer::mStaticVertexBufferPool");
		mStaticIndexBufferPool .Create(pDevice, EBufferType::INDEX_BUFFER , STATIC_GEOMETRY_MEMORY_SIZE, USE_GPU_MEMORY, "VQRenderer::mStaticIndexBufferPool");
	}

	Log::Info("[Renderer] Initialized.");
	// TODO: Log system info
}

void VQRenderer::Load()
{
	LoadPSOs();
	LoadDefaultResources();
	mHeapUpload.UploadToGPUAndWait(mGFXQueue.pQueue);
}

void VQRenderer::Unload()
{
}


void VQRenderer::Exit()
{
	for (auto it_hwnd_ctx : mRenderContextLookup)
	{
		auto& ctx = it_hwnd_ctx.second;
		for (ID3D12CommandAllocator* pCmdAlloc : ctx.mCommandAllocatorsGFX)     if (pCmdAlloc) pCmdAlloc->Release();
		for (ID3D12CommandAllocator* pCmdAlloc : ctx.mCommandAllocatorsCompute) if (pCmdAlloc) pCmdAlloc->Release();
		for (ID3D12CommandAllocator* pCmdAlloc : ctx.mCommandAllocatorsCopy)    if (pCmdAlloc) pCmdAlloc->Release();

		ctx.SwapChain.Destroy(); // syncs GPU before releasing resources

		ctx.PresentQueue.Destroy();
		if (ctx.pCmdList_GFX) ctx.pCmdList_GFX->Release();
	}

	mHeapUpload.Destroy();
	mStaticVertexBufferPool.Destroy();
	mStaticIndexBufferPool.Destroy();

	mpAllocator->Release();

	mpRootSignature->Release();
	mpPSO->Release();

	mGFXQueue.Destroy();
	mComputeQueue.Destroy();
	mCopyQueue.Destroy();

	mDevice.Destroy();
}

void VQRenderer::OnWindowSizeChanged(HWND hwnd, unsigned w, unsigned h)
{
	if (!CheckContext(hwnd)) return;
	FRenderWindowContext& ctx = mRenderContextLookup.at(hwnd);

	ctx.MainRTResolutionX = w; // TODO: RenderScale
	ctx.MainRTResolutionY = h; // TODO: RenderScale
}

SwapChain& VQRenderer::GetWindowSwapChain(HWND hwnd) { return mRenderContextLookup.at(hwnd).SwapChain; }

void VQRenderer::RenderWindowContext(HWND hwnd, const FFrameData& FrameData)
{
	if (!CheckContext(hwnd)) return;

	FRenderWindowContext& ctx = mRenderContextLookup.at(hwnd);

	const int NUM_BACK_BUFFERS  = ctx.SwapChain.GetNumBackBuffers();
	const int BACK_BUFFER_INDEX = ctx.SwapChain.GetCurrentBackBufferIndex();
	assert(ctx.mCommandAllocatorsGFX.size() >= NUM_BACK_BUFFERS);
	// ----------------------------------------------------------------------------

	HRESULT hr = {};

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

	pCmd->SetPipelineState(mpPSO);

	pCmd->SetGraphicsRootSignature(mpRootSignature);

#if 0
	//pCmd->SetDescriptorHeaps(_countof(), NULL);
	//pCmd->SetGraphicsRootDescriptorTable(0, g_MainDescriptorHeap[g_FrameIndex]->GetGPUDescriptorHandleForHeapStart()))
	//pCmd->SetGraphicsRootDescriptorTable(2, g_MainDescriptorHeap[g_FrameIndex]->GetGPUDescriptorHandleForHeapStart()))
	//pCmd->SetGraphicsRootConstantBufferView(1, )
#endif

	const float RenderResolutionX = static_cast<float>(ctx.MainRTResolutionX);
	const float RenderResolutionY = static_cast<float>(ctx.MainRTResolutionY);
	D3D12_VIEWPORT viewport { 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	pCmd->RSSetViewports(1, &viewport);

	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
	pCmd->RSSetScissorRects(1, &scissorsRect);

	pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmd->IASetVertexBuffers(0, 1, &mVertexBufferViews[0]);
	pCmd->IASetIndexBuffer(&mIndexBufferViews[0]);

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
	ctx.SwapChain.Present(ctx.bVsync);

	ctx.SwapChain.MoveToNextFrame();

}

short VQRenderer::GetSwapChainBackBufferCountOfWindow(Window* pWnd) const { return pWnd ? this->GetSwapChainBackBufferCountOfWindow(pWnd->GetHWND()) : 0; }
short VQRenderer::GetSwapChainBackBufferCountOfWindow(HWND hwnd) const
{
	if (!CheckContext(hwnd)) return 0;

	const FRenderWindowContext& ctx = mRenderContextLookup.at(hwnd);
	return ctx.SwapChain.GetNumBackBuffers();
	
}


bool VQRenderer::CheckContext(HWND hwnd) const
{
	if (mRenderContextLookup.find(hwnd) == mRenderContextLookup.end())
	{
		Log::Warning("Render Context not found for <hwnd=0x%x>", hwnd);
		return false;
	}
	return true;
}

// ================================================================================================================================================

//
// PRIVATE
//
void VQRenderer::InitializeD3D12MA()
{
	// Initialize D3D12MA
	const D3D12MA::ALLOCATOR_FLAGS FlagAlloc = D3D12MA::ALLOCATOR_FLAG_NONE;

	D3D12MA::ALLOCATOR_DESC desc = {};
	desc.Flags = FlagAlloc;
	desc.pDevice = mDevice.GetDevicePtr();
	desc.pAdapter = mDevice.GetAdapterPtr();

	D3D12MA::ALLOCATION_CALLBACKS allocationCallbacks = {};
	if (D3D12MA_ENABLE_CPU_ALLOCATION_CALLBACKS)
	{
		allocationCallbacks.pAllocate = &CustomAllocate;
		allocationCallbacks.pFree = &CustomFree;
		allocationCallbacks.pUserData = CUSTOM_ALLOCATION_USER_DATA;
		desc.pAllocationCallbacks = &allocationCallbacks;
	}

	CHECK_HR(D3D12MA::CreateAllocator(&desc, &mpAllocator));

	switch (mpAllocator->GetD3D12Options().ResourceHeapTier)
	{
	case D3D12_RESOURCE_HEAP_TIER_1:
		wprintf(L"ResourceHeapTier = D3D12_RESOURCE_HEAP_TIER_1\n");
		break;
	case D3D12_RESOURCE_HEAP_TIER_2:
		wprintf(L"ResourceHeapTier = D3D12_RESOURCE_HEAP_TIER_2\n");
		break;
	default:
		assert(0);
	}
}

void VQRenderer::InitializeResourceHeaps()
{
	ID3D12Device* pDevice = mDevice.GetDevicePtr();

	const uint32 UPLOAD_HEAP_SIZE = 1 * GIGABYTE; // TODO: from RendererSettings.ini
	mHeapUpload.Create(pDevice, UPLOAD_HEAP_SIZE);
}

static std::wstring GetAssetFullPath(LPCWSTR assetName)
{
	std::wstring fullPath = L"Shaders/";
	return fullPath + assetName;
}

void VQRenderer::LoadPSOs()
{
	ID3D12Device* pDevice = mDevice.GetDevicePtr();

	// Create an empty root signature.
	{
		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&mpRootSignature)));
	}


	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
	// Enable better shader debugging with the graphics debugging tools.
	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	UINT compileFlags = 0;
#endif


	ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"hello-triangle.hlsl").c_str(), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr));
	ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"hello-triangle.hlsl").c_str(), nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr));

	// Define the vertex input layout.
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT   , 0, 0 , D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR"   , 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	// Describe and create the graphics pipeline state object (PSO).
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout    = { inputElementDescs, _countof(inputElementDescs) };
	psoDesc.pRootSignature = mpRootSignature;
	psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
	psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;
	ThrowIfFailed(pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mpPSO)));


}

void VQRenderer::LoadDefaultResources()
{
	ID3D12Device* pDevice = mDevice.GetDevicePtr();

	// Create the vertex buffer.
	{
		struct Vertex
		{
			float position[3];
			float color[4];
		};
		constexpr float ASPECT_RATIO = 16.f / 9.f;
		// Define the geometry for a triangle.
		Vertex triangleVertices[] =
		{
			{ {  0.00f,  0.25f * ASPECT_RATIO, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
			{ {  0.25f, -0.25f * ASPECT_RATIO, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
			{ { -0.25f, -0.25f * ASPECT_RATIO, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
		};

		const UINT vertexBufferSize = sizeof(triangleVertices);
		const UINT numVerts = vertexBufferSize / sizeof(Vertex);

		// allocate mem
		VBV vertBufView;
		bool bAllocSuccess = mStaticVertexBufferPool.AllocVertexBuffer(numVerts, sizeof(Vertex), triangleVertices, &vertBufView);
		if (bAllocSuccess)
		{
			mVertexBufferViews.push_back(vertBufView);
		}
		else
		{
			Log::Error("Couldn't allocate vertex buffer");
		}
	}

	// Create the index buffer
	{
		UINT indices[] = { 0, 1, 2 };

		IBV indexBufView;
		bool bAllocSuccess = mStaticIndexBufferPool.AllocIndexBuffer(_countof(indices), sizeof(UINT), indices, &indexBufView);
		if (bAllocSuccess)
		{
			mIndexBufferViews.push_back(indexBufView);
		}
		else
		{
			Log::Error("Couldn't allocate vertex buffer");
}
	}

	mStaticVertexBufferPool.UploadData(mHeapUpload.GetCommandList());
	mStaticIndexBufferPool.UploadData(mHeapUpload.GetCommandList());
}









// ================================================================================================================================================

//
// STATIC
//
/*
@bEnumerateSoftwareAdapters : Basic Render Driver adapter.
*/
std::vector< FGPUInfo > VQRenderer::EnumerateDX12Adapters(bool bEnableDebugLayer, bool bEnumerateSoftwareAdapters /*= false*/)
{
	std::vector< FGPUInfo > GPUs;
	HRESULT hr = {};

	IDXGIAdapter1* pAdapter = nullptr; // adapters are the graphics card (this includes the embedded graphics on the motherboard)
	int iAdapter = 0;                  // we'll start looking for DX12  compatible graphics devices starting at index 0
	bool bAdapterFound = false;        // set this to true when a good one was found

	// https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/d3d10-graphics-programming-guide-dxgi
	// https://stackoverflow.com/questions/42354369/idxgifactory-versions
	// Chuck Walbourn: For DIrect3D 12, you can assume CreateDXGIFactory2 and IDXGIFactory4 or later is supported. 
	IDXGIFactory6* pDxgiFactory; // DXGIFactory6 supports preferences when querying devices: DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE
	UINT DXGIFlags = 0;
	if (bEnableDebugLayer)
	{
		DXGIFlags |= DXGI_CREATE_FACTORY_DEBUG;
	}
	hr = CreateDXGIFactory2(DXGIFlags, IID_PPV_ARGS(&pDxgiFactory));

	auto fnAddAdapter = [&bAdapterFound, &GPUs](IDXGIAdapter1*& pAdapter, const DXGI_ADAPTER_DESC1& desc, D3D_FEATURE_LEVEL FEATURE_LEVEL)
	{
		bAdapterFound = true;

		FGPUInfo GPUInfo = {};
		GPUInfo.DedicatedGPUMemory = desc.DedicatedVideoMemory;
		GPUInfo.DeviceID = desc.DeviceId;
		GPUInfo.DeviceName = StrUtil::UnicodeToASCII<_countof(desc.Description)>(desc.Description);
		GPUInfo.VendorID = desc.VendorId;
		GPUInfo.MaxSupportedFeatureLevel = FEATURE_LEVEL;
		pAdapter->QueryInterface(IID_PPV_ARGS(&GPUInfo.pAdapter));
		GPUs.push_back(GPUInfo);
	};

	// Find GPU with highest perf: https://stackoverflow.com/questions/49702059/dxgi-integred-pAdapter
	// https://docs.microsoft.com/en-us/windows/win32/api/dxgi1_6/nf-dxgi1_6-idxgifactory6-enumadapterbygpupreference
	while (pDxgiFactory->EnumAdapterByGpuPreference(iAdapter, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&pAdapter)) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC1 desc;
		pAdapter->GetDesc1(&desc);

		const bool bSoftwareAdapter = desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE;
		if ((bEnumerateSoftwareAdapters && !bSoftwareAdapter) // We want software adapters, but we got a hardware adapter
			|| (!bEnumerateSoftwareAdapters && bSoftwareAdapter) // We want hardware adapters, but we got a software adapter
			)
		{
			++iAdapter;
			pAdapter->Release();
			continue;
		}

		hr = D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_1, _uuidof(ID3D12Device), nullptr);
		if (SUCCEEDED(hr))
		{
			fnAddAdapter(pAdapter, desc, D3D_FEATURE_LEVEL_12_1);
		}
		else
		{
			const std::string AdapterDesc = StrUtil::UnicodeToASCII(desc.Description);
			Log::Warning("Device::Create(): D3D12CreateDevice() with Feature Level 12_1 failed with adapter=%s, retrying with Feature Level 12_0", AdapterDesc.c_str());
			hr = D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr);
			if (SUCCEEDED(hr))
			{
				fnAddAdapter(pAdapter, desc, D3D_FEATURE_LEVEL_12_0);
			}
			else
			{
				Log::Error("Device::Create(): D3D12CreateDevice() with Feature Level 12_0 failed ith adapter=%s", AdapterDesc.c_str());
			}
		}

		pAdapter->Release();
		++iAdapter;
	}
	pDxgiFactory->Release();
	assert(bAdapterFound);

	return GPUs;
}