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


#ifdef _DEBUG
	#define ENABLE_DEBUG_LAYER      1
	#define ENABLE_VALIDATION_LAYER 1
#else
	#define ENABLE_DEBUG_LAYER      0
	#define ENABLE_VALIDATION_LAYER 0
#endif

void VQRenderer::Initialize(const FRendererInitializeParameters& params)
{
	Device* pDevice = &mDevice;
	const FGraphicsSettings& Settings = params.Settings;
	const int NUM_SWAPCHAIN_BUFFERS   = Settings.bUseTripleBuffering ? 3 : 2;


	// Create the device
	FDeviceCreateDesc deviceDesc = {};
	deviceDesc.bEnableDebugLayer = ENABLE_DEBUG_LAYER;
	deviceDesc.bEnableValidationLayer = ENABLE_VALIDATION_LAYER;
	mDevice.Create(deviceDesc);


	// Create Command Queues of different types
	mGFXQueue.Create(pDevice, CommandQueue::ECommandQueueType::GFX);
	mComputeQueue.Create(pDevice, CommandQueue::ECommandQueueType::COMPUTE);
	mCopyQueue.Create(pDevice, CommandQueue::ECommandQueueType::COPY);


	// Create the present queues & swapchains associated with each window passed into the VQRenderer
	// Swapchains contain their own render targets 
	const size_t NumWindows = params.Windows.size();
	for(size_t i = 0; i< NumWindows; ++i)
	{
		const FWindowRepresentation& wnd = params.Windows[i];
		

		FRenderWindowContext ctx = {};

		ctx.pDevice = pDevice;
		ctx.PresentQueue.Create(ctx.pDevice, CommandQueue::ECommandQueueType::GFX); // Create the GFX queue for presenting the SwapChain
		ctx.bVsync = wnd.bVSync; // we store bVSync in ctx instead of SwapChain and provide it through SwapChain.Present() param : either way should be fine

		// Create the SwapChain
		FSwapChainCreateDesc swapChainDesc = {};
		swapChainDesc.numBackBuffers = NUM_SWAPCHAIN_BUFFERS;
		swapChainDesc.pDevice = ctx.pDevice->GetDevicePtr();
		swapChainDesc.pWindow = &wnd;
		swapChainDesc.pCmdQueue = &ctx.PresentQueue;
		swapChainDesc.bVSync = ctx.bVsync;
		ctx.SwapChain.Create(swapChainDesc);
		Log::Info("[Renderer] Created swapchain<hwnd=0x%x> w/ %d back buffers.", wnd.hwnd, NUM_SWAPCHAIN_BUFFERS);

		// Create command allocators
		ctx.mCommandAllocatorsGFX.resize(NUM_SWAPCHAIN_BUFFERS);
		ctx.mCommandAllocatorsCompute.resize(NUM_SWAPCHAIN_BUFFERS);
		ctx.mCommandAllocatorsCopy.resize(NUM_SWAPCHAIN_BUFFERS);
		for (int f = 0; f < NUM_SWAPCHAIN_BUFFERS; ++f)
		{
			ID3D12CommandAllocator* pCmdAlloc = {};
			pDevice->GetDevicePtr()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT , IID_PPV_ARGS(&ctx.mCommandAllocatorsGFX[f]));
			pDevice->GetDevicePtr()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&ctx.mCommandAllocatorsCompute[f]));
			pDevice->GetDevicePtr()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY   , IID_PPV_ARGS(&ctx.mCommandAllocatorsCopy[f]));
		}

		// Create command lists
		pDevice->GetDevicePtr()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ctx.mCommandAllocatorsGFX[0], nullptr, IID_PPV_ARGS(&ctx.pCmdList_GFX));
		ctx.pCmdList_GFX->Close();

		// save the render context
		this->mRenderContextLookup.emplace(wnd.hwnd, std::move(ctx));
	}

	Log::Info("[Renderer] Initialized.");
	// TODO: Log system info
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



	mGFXQueue.Destroy();
	mComputeQueue.Destroy();
	mCopyQueue.Destroy();

	mDevice.Destroy();
}


void VQRenderer::RenderWindowContext(HWND hwnd, const FFrameData& FrameData)
{
	if (mRenderContextLookup.find(hwnd) == mRenderContextLookup.end())
	{
		Log::Warning("Render Context not found for <hwnd=0x%x>", hwnd);
		return;
	}

	FRenderWindowContext& ctx = mRenderContextLookup.at(hwnd);

	const int NUM_BACK_BUFFERS  = ctx.SwapChain.GetNumBackBuffers();
	const int BACK_BUFFER_INDEX = ctx.SwapChain.GetCurrentBackBufferIndex();
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
	// Indicate that the back buffer will be used as a render target.
	ID3D12Resource* pSwapChainRT = ctx.SwapChain.GetCurrentBackBufferRenderTarget();
	ctx.pCmdList_GFX->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT
		, D3D12_RESOURCE_STATE_PRESENT
		, D3D12_RESOURCE_STATE_RENDER_TARGET)
	);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle = ctx.SwapChain.GetCurrentBackBufferRTVHandle(); 
	const float clearColor[] = 
	{ 
		FrameData.SwapChainClearColor[0],
		FrameData.SwapChainClearColor[1],
		FrameData.SwapChainClearColor[2],
		FrameData.SwapChainClearColor[3]
	};
	ctx.pCmdList_GFX->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

	// Indicate that the back buffer will now be used to present.
	ctx.pCmdList_GFX->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT
		, D3D12_RESOURCE_STATE_RENDER_TARGET
		, D3D12_RESOURCE_STATE_PRESENT)
	);

	ctx.pCmdList_GFX->Close();

	ID3D12CommandList* ppCommandLists[] = { ctx.pCmdList_GFX };
	ctx.PresentQueue.pQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);


	//
	// PRESENT
	//
	ctx.SwapChain.Present(ctx.bVsync);

	ctx.SwapChain.MoveToNextFrame();
}

short VQRenderer::GetSwapChainBackBufferCountOfWindow(Window* pWnd) const { return this->GetSwapChainBackBufferCountOfWindow(pWnd->GetHWND()); }
short VQRenderer::GetSwapChainBackBufferCountOfWindow(HWND hwnd) const
{
	if (mRenderContextLookup.find(hwnd) == mRenderContextLookup.end())
	{
		Log::Warning("Render Context not found for <hwnd=0x%x>", hwnd);
		return 0;
	}

	const FRenderWindowContext& ctx = mRenderContextLookup.at(hwnd);
	return ctx.SwapChain.GetNumBackBuffers();
	
}



// ================================================================================================================================================

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