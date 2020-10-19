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


// Resources for reading
// - http://diligentgraphics.com/diligent-engine/architecture/d3d12/managing-descriptor-heaps/
// - https://asawicki.info/articles/memory_management_vulkan_direct3d_12.php5
// - https://simonstechblog.blogspot.com/2019/06/d3d12-descriptor-heap-management.html

#include "Renderer.h"
#include "Device.h"
#include "Texture.h"
#include "Shader.h"

#include "../Application/Window.h"
#include "../../Shaders/LightingConstantBufferData.h"

#include "../../Libs/VQUtils/Source/Log.h"
#include "../../Libs/VQUtils/Source/utils.h"
#include "../../Libs/VQUtils/Source/Timer.h"
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

const std::string_view& VQRenderer::DXGIFormatAsString(DXGI_FORMAT format)
{
	static std::unordered_map<DXGI_FORMAT, std::string_view> DXGI_FORMAT_STRING_TRANSLATION =
	{
			  { DXGI_FORMAT_R8G8B8A8_UNORM	   , "R8G8B8A8_UNORM"    }
			, { DXGI_FORMAT_R10G10B10A2_UNORM  , "R10G10B10A2_UNORM" }
			, { DXGI_FORMAT_R16G16B16A16_FLOAT , "R16G16B16A16_FLOAT"}
	};
	return DXGI_FORMAT_STRING_TRANSLATION.at(format);
}

EProceduralTextures VQRenderer::GetProceduralTextureEnumFromName(const std::string& ProceduralTextureName)
{
	static std::unordered_map<std::string, EProceduralTextures> MAP =
	{
		  { "Checkerboard", EProceduralTextures::CHECKERBOARD }
		, { "Checkerboard_Grayscale", EProceduralTextures::CHECKERBOARD_GRAYSCALE }
	};
	return MAP.at(ProceduralTextureName);
}



// ---------------------------------------------------------------------------------------
// D3D12MA Integration 
// ---------------------------------------------------------------------------------------
#define D3D12MA_ENABLE_CPU_ALLOCATION_CALLBACKS       1
#define D3D12MA_ENABLE_CPU_ALLOCATION_CALLBACKS_PRINT 0
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

//
// PUBLIC
//
void VQRenderer::Initialize(const FGraphicsSettings& Settings)
{
	Device* pVQDevice = &mDevice;

	InitializeShaderAndPSOCacheDirectory();

	// Create the device
	FDeviceCreateDesc deviceDesc = {};
	deviceDesc.bEnableDebugLayer = ENABLE_DEBUG_LAYER;
	deviceDesc.bEnableValidationLayer = ENABLE_VALIDATION_LAYER;
	const bool bDeviceCreateSucceeded = mDevice.Create(deviceDesc);
	ID3D12Device* pDevice = pVQDevice->GetDevicePtr();

	assert(bDeviceCreateSucceeded);

	// Create Command Queues of different types
	mGFXQueue.Create(pVQDevice, CommandQueue::ECommandQueueType::GFX);
	mComputeQueue.Create(pVQDevice, CommandQueue::ECommandQueueType::COMPUTE);
	mCopyQueue.Create(pVQDevice, CommandQueue::ECommandQueueType::COPY);

	// Initialize memory
	InitializeD3D12MA();
	InitializeHeaps();

	// initialize thread
	mbExitUploadThread.store(false);
	mbDefaultResourcesLoaded.store(false);
	mTextureUploadThread = std::thread(&VQRenderer::TextureUploadThread_Main, this);

	const size_t HWThreads = ThreadPool::sHardwareThreadCount;
	const size_t HWCores   = HWThreads >> 1;
	mWorkers_ShaderLoad.Initialize(HWThreads, "ShaderLoadWorkers");
	mWorkers_PSOLoad.Initialize(HWThreads, "PSOLoadWorkers");

	Log::Info("[Renderer] Initialized.");
}

void VQRenderer::Load()
{
	Timer timer; timer.Start();
	Log::Info("[Renderer] Loading...");
	
	LoadRootSignatures();
	float tRS = timer.Tick();
	Log::Info("[Renderer]    RootSignatures=%.2fs", tRS);

	LoadPSOs();
	float tPSOs = timer.Tick();
	Log::Info("[Renderer]    PSOs=%.2fs", tPSOs);

	LoadDefaultResources();
	float tDefaultRscs = timer.Tick();
	Log::Info("[Renderer]    DefaultRscs=%.2fs", tDefaultRscs);

	float total = tRS + tPSOs + tDefaultRscs;
	Log::Info("[Renderer] Loaded in %.2fs.", total);
	mbDefaultResourcesLoaded.store(true);
}

void VQRenderer::Unload()
{
	// todo: mirror Load() functions?
}


void VQRenderer::Exit()
{
	mWorkers_PSOLoad.Exit();
	mWorkers_ShaderLoad.Exit();

	mbExitUploadThread.store(true);
	mSignal_UploadThreadWorkReady.NotifyAll();

	mHeapUpload.Destroy();
	mHeapCBV_SRV_UAV.Destroy();
	mHeapDSV.Destroy();
	mHeapRTV.Destroy();
	mStaticHeap_VertexBuffer.Destroy();
	mStaticHeap_IndexBuffer.Destroy();

	for (std::unordered_map<TextureID, Texture>::iterator it = mTextures.begin(); it != mTextures.end(); ++it)
	{
		it->second.Destroy();
	}

	mTextures.clear();
	
	mpAllocator->Release();
	
	for (auto* p : mpBuiltinRootSignatures)
	{
		if (p) p->Release();
	}

	for (std::pair<PSO_ID, ID3D12PipelineState*> pPSO : mPSOs)
	{
		if (pPSO.second)
			pPSO.second->Release();
	}
	mPSOs.clear();

	for (std::unordered_map<HWND, FWindowRenderContext>::iterator it = mRenderContextLookup.begin(); it != mRenderContextLookup.end(); ++it)
	{
		auto& ctx = it->second;
		for (ID3D12CommandAllocator* pCmdAlloc : ctx.mCommandAllocatorsGFX)     if (pCmdAlloc) pCmdAlloc->Release();
		for (ID3D12CommandAllocator* pCmdAlloc : ctx.mCommandAllocatorsCompute) if (pCmdAlloc) pCmdAlloc->Release();
		for (ID3D12CommandAllocator* pCmdAlloc : ctx.mCommandAllocatorsCopy)    if (pCmdAlloc) pCmdAlloc->Release();

		ctx.mDynamicHeap_ConstantBuffer.Destroy();

		ctx.SwapChain.Destroy(); // syncs GPU before releasing resources

		ctx.PresentQueue.Destroy();
		if (ctx.pCmdList_GFX)
			ctx.pCmdList_GFX->Release();
	}

	mGFXQueue.Destroy();
	mComputeQueue.Destroy();
	mCopyQueue.Destroy();

	mDevice.Destroy();

	mTextureUploadThread.join();
}

void VQRenderer::OnWindowSizeChanged(HWND hwnd, unsigned w, unsigned h)
{
	if (!CheckContext(hwnd)) return;
	FWindowRenderContext& ctx = mRenderContextLookup.at(hwnd);

	ctx.MainRTResolutionX = w; // TODO: RenderScale
	ctx.MainRTResolutionY = h; // TODO: RenderScale
}

SwapChain& VQRenderer::GetWindowSwapChain(HWND hwnd) { return mRenderContextLookup.at(hwnd).SwapChain; }

short VQRenderer::GetSwapChainBackBufferCount(Window* pWnd) const { return pWnd ? this->GetSwapChainBackBufferCount(pWnd->GetHWND()) : 0; }
short VQRenderer::GetSwapChainBackBufferCount(HWND hwnd) const
{
	if (!CheckContext(hwnd)) return 0;

	const FWindowRenderContext& ctx = mRenderContextLookup.at(hwnd);
	return ctx.SwapChain.GetNumBackBuffers();
	
}


void VQRenderer::InitializeRenderContext(const Window* pWin, int NumSwapchainBuffers, bool bVSync, bool bHDRSwapchain)
{
	Device*       pVQDevice = &mDevice;
	ID3D12Device* pDevice = pVQDevice->GetDevicePtr();

	FWindowRenderContext ctx = {};

	ctx.pDevice = pVQDevice;
	char c[127] = {}; sprintf_s(c, "PresentQueue<0x%p>", pWin->GetHWND());
	ctx.PresentQueue.Create(ctx.pDevice, CommandQueue::ECommandQueueType::GFX, c); // Create the GFX queue for presenting the SwapChain

	// Create the SwapChain
	FSwapChainCreateDesc swapChainDesc = {};
	swapChainDesc.numBackBuffers = NumSwapchainBuffers;
	swapChainDesc.pDevice        = ctx.pDevice->GetDevicePtr();
	swapChainDesc.pWindow        = pWin;
	swapChainDesc.pCmdQueue      = &ctx.PresentQueue;
	swapChainDesc.bVSync         = bVSync;
	swapChainDesc.bHDR           = bHDRSwapchain;
	swapChainDesc.bitDepth       = bHDRSwapchain ? _16 : _8; // currently no support for HDR10 / R10G10B10A2 signals
	swapChainDesc.bFullscreen    = false; // TODO: exclusive fullscreen to be deprecated. App shouldn't make dxgi mode changes.
	ctx.SwapChain.Create(swapChainDesc);
	if (bHDRSwapchain)
	{
		FSetHDRMetaDataParams p = {}; // default
		// Set default HDRMetaData - the engine is likely loading resources at this stage 
		// and all the HDRMetaData parts are not ready yet.
		// Engine dispatches an event to set HDR MetaData when mSystemInfo is initialized.
		ctx.SwapChain.SetHDRMetaData(p);
	}

	// Create command allocators
	ctx.mCommandAllocatorsGFX.resize(NumSwapchainBuffers);
	ctx.mCommandAllocatorsCompute.resize(NumSwapchainBuffers);
	ctx.mCommandAllocatorsCopy.resize(NumSwapchainBuffers);
	for (int f = 0; f < NumSwapchainBuffers; ++f)
	{
		ID3D12CommandAllocator* pCmdAlloc = {};
		pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ctx.mCommandAllocatorsGFX[f]));
		pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&ctx.mCommandAllocatorsCompute[f]));
		pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&ctx.mCommandAllocatorsCopy[f]));
		SetName(ctx.mCommandAllocatorsGFX[f], "RenderContext::CmdAllocGFX[%d]", f);
		SetName(ctx.mCommandAllocatorsCompute[f], "RenderContext::CmdAllocCompute[%d]", f);
		SetName(ctx.mCommandAllocatorsCopy[f], "RenderContext::CmdAllocCopy[%d]", f);
	}

	// Create command lists
	pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ctx.mCommandAllocatorsGFX[0], nullptr, IID_PPV_ARGS(&ctx.pCmdList_GFX));
	ctx.pCmdList_GFX->SetName(L"RenderContext::CmdListGFX");
	ctx.pCmdList_GFX->Close();

	// Create dynamic buffer heap
	ctx.mDynamicHeap_ConstantBuffer.Create(pDevice, NumSwapchainBuffers, 16 * MEGABYTE);

	// Save other context data
	ctx.MainRTResolutionX = pWin->GetWidth();
	ctx.MainRTResolutionY = pWin->GetHeight();


	// save the render context
	this->mRenderContextLookup.emplace(pWin->GetHWND(), std::move(ctx));
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

FWindowRenderContext& VQRenderer::GetWindowRenderContext(HWND hwnd)
{
	if (!CheckContext(hwnd))
	{
		Log::Error("VQRenderer::GetWindowRenderContext(): Context not found!");
		//return FWindowRenderContext{};
	}
	return mRenderContextLookup.at(hwnd);
}

// ================================================================================================================================================
// ================================================================================================================================================
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

void VQRenderer::InitializeHeaps()
{
	ID3D12Device* pDevice = mDevice.GetDevicePtr();

	const uint32 UPLOAD_HEAP_SIZE = 513 * MEGABYTE; // TODO: from RendererSettings.ini
	mHeapUpload.Create(pDevice, UPLOAD_HEAP_SIZE, this->mGFXQueue.pQueue);

	constexpr uint32 NumDescsCBV = 100;
	constexpr uint32 NumDescsSRV = 3800;
	constexpr uint32 NumDescsUAV = 100;
	constexpr bool   bCPUVisible = false;
	mHeapCBV_SRV_UAV.Create(pDevice, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, NumDescsCBV + NumDescsSRV + NumDescsUAV, bCPUVisible);

	constexpr uint32 NumDescsDSV = 100;
	mHeapDSV.Create(pDevice, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, NumDescsDSV);

	constexpr uint32 NumDescsRTV = 500;
	mHeapRTV.Create(pDevice, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, NumDescsRTV);

	constexpr uint32 STATIC_GEOMETRY_MEMORY_SIZE = 64 * MEGABYTE;
	constexpr bool USE_GPU_MEMORY = true;
	mStaticHeap_VertexBuffer.Create(pDevice, EBufferType::VERTEX_BUFFER, STATIC_GEOMETRY_MEMORY_SIZE, USE_GPU_MEMORY, "VQRenderer::mStaticVertexBufferPool");
	mStaticHeap_IndexBuffer .Create(pDevice, EBufferType::INDEX_BUFFER , STATIC_GEOMETRY_MEMORY_SIZE, USE_GPU_MEMORY, "VQRenderer::mStaticIndexBufferPool");
}

std::string VQRenderer::ShaderCacheDirectory = "ShaderCache";
std::string VQRenderer::PSOCacheDirectory    = "PSOCache";
void VQRenderer::InitializeShaderAndPSOCacheDirectory()
{
	DirectoryUtil::CreateFolderIfItDoesntExist(VQRenderer::ShaderCacheDirectory);
	DirectoryUtil::CreateFolderIfItDoesntExist(VQRenderer::PSOCacheDirectory);
}

static std::wstring GetAssetFullPath(LPCWSTR assetName)
{
	std::wstring fullPath = L"Shaders/";
	return fullPath + assetName;
}


static void ReportErrorAndReleaseBlob(ComPtr<ID3DBlob>& pBlob)
{
	if (pBlob)
	{
		OutputDebugStringA((char*)pBlob->GetBufferPointer());
		pBlob->Release();
	}
}

void VQRenderer::LoadRootSignatures()
{
	HRESULT hr = {};
	ID3D12Device* pDevice = mDevice.GetDevicePtr();

	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;

	// ROOT SIGNATURES 
	//hardcoded for now. TODO: http://simonstechblog.blogspot.com/2019/06/d3d12-root-signature-management.html
	// https://youtu.be/Wbnw87tYqVg?t=1903 : Root signature examples
	
	D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

	// This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
	if (FAILED(pDevice->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
	{
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	}
	
	// Hello-World-Triangle Root Signature : [0]
	{
		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		mpBuiltinRootSignatures.push_back(pRS);
		SetName(pRS, "RootSignature_HelloWorldTriangle");
	}

	// Fullscreen-Triangle Root Signature : [1]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);

		CD3DX12_ROOT_PARAMETER1 rootParameters[1];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.MipLODBias = 0;
		sampler.MaxAnisotropy = 0;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		mpBuiltinRootSignatures.push_back(pRS);
		SetName(pRS, "RootSignature_FSTriangle");
	}

	// Hello-World-Cube Root Signature : [2]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

		CD3DX12_ROOT_PARAMETER1 rootParameters[2];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		//rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_VERTEX); // InitAsCBufferView?
		rootParameters[1].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_VERTEX);

		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.MipLODBias = 0;
		sampler.MaxAnisotropy = 0;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		mpBuiltinRootSignatures.push_back(pRS);
		SetName(pRS, "RootSignature_HelloWorldCube");
	}

	// Tonemapper Root Signature : [3]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		//ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

		CD3DX12_ROOT_PARAMETER1 rootParameters[3];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[2].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE);

		hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
		if (!SUCCEEDED(hr)) { ReportErrorAndReleaseBlob(error); assert(false); }
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		mpBuiltinRootSignatures.push_back(pRS);
		SetName(pRS, "RootSignature_Tonemapper");
	}

	// Object Root Signature : [4]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

		CD3DX12_ROOT_PARAMETER1 rootParameters[2];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[1].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);

		D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.MipLODBias = 0;
		sampler.MaxAnisotropy = 0;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		samplers[0] = sampler;

		sampler.ShaderRegister = 1;
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		samplers[1] = sampler;

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, _countof(samplers), &samplers[0], D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		mpBuiltinRootSignatures.push_back(pRS);
		SetName(pRS, "RootSignature_Object");
	}

	// ForwardLighting Root Signature : [5]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[5];
		// material textures
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		// shadow maps
		ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1                          , 13, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, NUM_SHADOWING_LIGHTS__SPOT , 16, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, NUM_SHADOWING_LIGHTS__POINT, 22, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 10, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		//ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC); // perView  cb's are DescRanges
		//ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC); // perFrame cb's are DescRanges

		CD3DX12_ROOT_PARAMETER1 rootParameters[8]; 
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[1].InitAsConstantBufferView(2, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);
#if 0
		// ConstantBufferView functionality for dynamic buffer heaps (which hold constant buffer data) is currently not supported
		rootParameters[2].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL); // perView
		rootParameters[3].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL); // perFrame
#else
		// use RootConstantBufferViews for now (2 DWORDS each for this RS, = 6 DWORDS for CBVs alone)
		rootParameters[2].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[3].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_PIXEL);
#endif
		rootParameters[4].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[5].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[6].InitAsDescriptorTable(1, &ranges[3], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[7].InitAsDescriptorTable(1, &ranges[4], D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_STATIC_SAMPLER_DESC samplers[3] = {};
		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.MipLODBias = 0;
		sampler.MaxAnisotropy = 0;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		samplers[0] = sampler;

		sampler.ShaderRegister = 1;
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		samplers[1] = sampler;

		sampler.ShaderRegister = 2;
		sampler.Filter = D3D12_FILTER_ANISOTROPIC;
		samplers[2] = sampler;

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, _countof(samplers), &samplers[0], D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		mpBuiltinRootSignatures.push_back(pRS);
		SetName(pRS, "RootSignature_ForwardLighting");
	}


	// Wireframe/Unlit Root Signature : [6]
	{
		//CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
		//ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		
		CD3DX12_ROOT_PARAMETER1 rootParameters[1];
		rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_VERTEX);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		mpBuiltinRootSignatures.push_back(pRS);
		SetName(pRS, "RootSignature_WireframeUnlit");
	}

	// DepthPass Root Signatures [7-9]
	{

		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.MipLODBias = 0;
		sampler.MaxAnisotropy = 0;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		{
			CD3DX12_ROOT_PARAMETER1 rootParameters[1];
			rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_VERTEX);

			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
			rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

			ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
			ID3D12RootSignature* pRS = nullptr;
			ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
			mpBuiltinRootSignatures.push_back(pRS);
			SetName(pRS, "RootSignature_DepthOnlyVS");
		}
		{
			CD3DX12_ROOT_PARAMETER1 rootParameters[2];
			rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_VERTEX);
			rootParameters[1].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_PIXEL);

			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
			rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

			ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
			ID3D12RootSignature* pRS = nullptr;
			ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
			mpBuiltinRootSignatures.push_back(pRS);
			SetName(pRS, "RootSignature_LinearDepthVSPS");
		}
		{
			CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
			ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

			CD3DX12_ROOT_PARAMETER1 rootParameters[3];
			rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_VERTEX);
			rootParameters[1].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_PIXEL);
			rootParameters[2].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);

			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
			rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

			ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
			ID3D12RootSignature* pRS = nullptr;
			ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
			mpBuiltinRootSignatures.push_back(pRS);
			SetName(pRS, "RootSignature_MaskedDepthVSPS");
		}
		
	}
	
	// Convolution Cubemap Root Signature [10]
	{

		CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);

		CD3DX12_ROOT_PARAMETER1 rootParameters[4];
		rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_GEOMETRY);
		rootParameters[1].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[2].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[3].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_STATIC_SAMPLER_DESC samplers[1] = {};
		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.MipLODBias = 0;
		sampler.MaxAnisotropy = 0;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		samplers[0] = sampler;

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(
			  _countof(rootParameters), rootParameters
			, _countof(samplers), samplers
			, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		mpBuiltinRootSignatures.push_back(pRS);
		SetName(pRS, "RootSignature_ConvolutionCubemap");
	}
}

void VQRenderer::LoadPSOs()
{
	// temp: singl thread pso load from vector
	// todo: enqueue load descs into the MT queue
	std::vector< std::pair<PSO_ID, FPSOLoadDesc >> PSOLoadDescs; 

	// HELLO WORLD TRIANGLE PSO
	{
		const std::wstring ShaderFilePath = GetAssetFullPath(L"hello-triangle.hlsl");

		FPSOLoadDesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_TonemapperCS";

		// Shader description
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_0" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_0" });

		// D3D12 description (without compiled / reflected data such as input layout, /*root signature*/ and shader bytecodes)
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.pRootSignature = mpBuiltinRootSignatures[0];
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;

		// TODO: enqueue
		PSOLoadDescs.push_back({ EBuiltinPSOs::HELLO_WORLD_TRIANGLE_PSO, psoLoadDesc });
		//mLookup_PSOLoadContext[taskID] = psoLoadDesc;
	}

	// FULLSCREEN TRIANGLE PSO
	{
		const std::wstring ShaderFilePath = GetAssetFullPath(L"FullscreenTriangle.hlsl");

		FPSOLoadDesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_FullscreenTriangle";

		// Shader description
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_0" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_0" });

		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.InputLayout = { };
		psoDesc.pRootSignature = mpBuiltinRootSignatures[1];
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;


		PSOLoadDescs.push_back({ EBuiltinPSOs::FULLSCREEN_TRIANGLE_PSO, psoLoadDesc });

		// HDR SWAPCHAIN PSO
		psoLoadDesc.PSOName = "PSO_HDRSwapchain";
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		PSOLoadDescs.push_back({ EBuiltinPSOs::HDR_FP16_SWAPCHAIN_PSO, psoLoadDesc });
	}

	// HELLO CUBE PSO
	{
		const std::wstring ShaderFilePath = GetAssetFullPath(L"hello-cube.hlsl");

		FPSOLoadDesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_HelloCube";

		// Shader description
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_0" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_0" });

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.pRootSignature = mpBuiltinRootSignatures[2];
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = TRUE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::HELLO_WORLD_CUBE_PSO, psoLoadDesc });

		// MSAA PSO
		psoLoadDesc.PSOName = "PSO_HelloCube_MSAA4";
		psoDesc.SampleDesc.Count = 4;
		PSOLoadDescs.push_back({ EBuiltinPSOs::HELLO_WORLD_CUBE_PSO_MSAA_4, psoLoadDesc });
	}

	// SKYDOME PSO
	{
		const std::wstring ShaderFilePath = GetAssetFullPath(L"Skydome.hlsl");

		FPSOLoadDesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_Skydome";

		// Shader description
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_0" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_0" });

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.pRootSignature = mpBuiltinRootSignatures[2];
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = TRUE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::SKYDOME_PSO, psoLoadDesc });

		// MSAA PSO
		psoLoadDesc.PSOName = "PSO_Skydome_MSAA4";
		psoDesc.SampleDesc.Count = 4;
		PSOLoadDescs.push_back({ EBuiltinPSOs::SKYDOME_PSO_MSAA_4, psoLoadDesc });
	}


	// OBJECT PSO
	{
		const std::wstring ShaderFilePath = GetAssetFullPath(L"Object.hlsl");

		FPSOLoadDesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_Object";

		// Shader description
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_1" });

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.pRootSignature = mpBuiltinRootSignatures[4];
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = TRUE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::OBJECT_PSO, psoLoadDesc });

		// MSAA PSO
		psoLoadDesc.PSOName = "PSO_Object_MSAA4";
		psoDesc.SampleDesc.Count = 4;
		PSOLoadDescs.push_back({ EBuiltinPSOs::OBJECT_PSO_MSAA_4, psoLoadDesc });
	}


	// TONEMAPPER CS PSO
	{
		const std::wstring ShaderFilePath = GetAssetFullPath(L"Tonemapper.hlsl");

		FPSOLoadDesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_TonemapperCS";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain", "cs_5_1"});
		psoLoadDesc.D3D12ComputeDesc.pRootSignature = mpBuiltinRootSignatures[3];

		PSOLoadDescs.push_back({ EBuiltinPSOs::TONEMAPPER_PSO, psoLoadDesc });
	}

	// FORWARD LIGHTING PSO
	{
		const std::wstring ShaderFilePath = GetAssetFullPath(L"ForwardLighting.hlsl");

		FPSOLoadDesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_FwdLightingVSPS";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_1" });
		psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mpBuiltinRootSignatures[5];

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = TRUE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::FORWARD_LIGHTING_PSO, psoLoadDesc });

		// MSAA PSO
		psoLoadDesc.PSOName = "PSO_FwdLightingVSPS_MSAA4";
		psoDesc.SampleDesc.Count = 4;
		PSOLoadDescs.push_back({ EBuiltinPSOs::FORWARD_LIGHTING_PSO_MSAA_4, psoLoadDesc });
	}

	// WIREFRAME/UNLIT PSOs
	{
		const std::wstring ShaderFilePath = GetAssetFullPath(L"Unlit.hlsl");

		FPSOLoadDesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_UnlitVSPS";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_1" });
		psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mpBuiltinRootSignatures[6];

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;

		// unlit
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = TRUE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::UNLIT_PSO, psoLoadDesc });
		{
			// MSAA
			psoLoadDesc.PSOName = "PSO_UnlitVSPS_MSAA4";
			psoDesc.SampleDesc.Count = 4;
			PSOLoadDescs.push_back({ EBuiltinPSOs::UNLIT_PSO_MSAA_4, psoLoadDesc });
		}

		// wireframe
		psoLoadDesc.PSOName = "PSO_WireframeVSPS";
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
		psoDesc.SampleDesc.Count = 1;
		PSOLoadDescs.push_back({ EBuiltinPSOs::WIREFRAME_PSO, psoLoadDesc });
		{
			// MSAA
			psoLoadDesc.PSOName = "PSO_WireframeVSPS_MSAA4";
			psoDesc.SampleDesc.Count = 4;
			PSOLoadDescs.push_back({ EBuiltinPSOs::WIREFRAME_PSO_MSAA_4, psoLoadDesc });
		}
	}

	// SHADOWMAP PSOs
	{
		const std::wstring ShaderFilePath = GetAssetFullPath(L"DepthPass.hlsl");

		FPSOLoadDesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_DepthOnlyVS";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
		psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mpBuiltinRootSignatures[7];

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;

		// unlit
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = TRUE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 0;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::DEPTH_PASS_PSO, psoLoadDesc });
		{
			psoLoadDesc.PSOName = "PSO_LinearDepthVSPS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_1" });
			psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mpBuiltinRootSignatures[8];
			PSOLoadDescs.push_back({ EBuiltinPSOs::DEPTH_PASS_LINEAR_PSO, psoLoadDesc });
		}
		{
			psoLoadDesc.PSOName = "PSO_MaskedDepthVSPS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_1" });
			for (FShaderStageCompileDesc& shdDesc : psoLoadDesc.ShaderStageCompileDescs)
			{
				shdDesc.Macros.push_back({"ALPHA_MASK", "1"});
			}
			psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mpBuiltinRootSignatures[9];
			PSOLoadDescs.push_back({ EBuiltinPSOs::DEPTH_PASS_ALPHAMASKED_PSO, psoLoadDesc });
		}
	}


	// CUBEMAP CONVOLUTION PSOs
	{
		const std::wstring ShaderFilePath = GetAssetFullPath(L"CubemapConvolution.hlsl");

		FPSOLoadDesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_CubemapConvolutionVSGSPS_Diffuse";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "GSMain", "gs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain_DiffuseIrradiance", "ps_5_1" });
		psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mpBuiltinRootSignatures[10];

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;

		// unlit
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 6;
		for(uint rt=0; rt<psoDesc.NumRenderTargets; ++rt) psoDesc.RTVFormats[rt] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::CUBEMAP_CONVOLUTION_DIFFUSE_PSO, psoLoadDesc });


		psoLoadDesc.PSOName = "PSO_CubemapConvolutionVSGSPS_Specular";
		psoLoadDesc.ShaderStageCompileDescs.clear();
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "GSMain", "gs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain_SpecularIrradiance", "ps_5_1" });
		PSOLoadDescs.push_back({ EBuiltinPSOs::CUBEMAP_CONVOLUTION_SPECULAR_PSO, psoLoadDesc });
	}
	// ---------------------------------------------------------------------------------------------------------------1

	// TODO: threaded PSO loading
	// single threaded PSO loading for now (shader compilation is still MTd)
	for (const std::pair<PSO_ID, FPSOLoadDesc>& psoLoadDescIDPair : PSOLoadDescs)
	{
		mPSOs[psoLoadDescIDPair.first] = this->LoadPSO(psoLoadDescIDPair.second);
	}

	int a = 5;
}

void VQRenderer::LoadDefaultResources()
{
	ID3D12Device* pDevice = mDevice.GetDevicePtr();

	const UINT sizeX = 1024;
	const UINT sizeY = 1024;
	
	D3D12_RESOURCE_DESC textureDesc = {};
	{
		textureDesc = {};
		textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		textureDesc.Alignment = 0;
		textureDesc.Width = sizeX;
		textureDesc.Height = sizeY;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureDesc.MipLevels = 1;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;
		textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	}
	TextureCreateDesc desc("Checkerboard");
	desc.d3d12Desc = textureDesc;
	desc.ResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	// programmatically generated textures
	{
		std::vector<UINT8> texture = Texture::GenerateTexture_Checkerboard(sizeX);
		desc.pData = texture.data();
		TextureID texID = this->CreateTexture(desc);
		mLookup_ProceduralTextureIDs[EProceduralTextures::CHECKERBOARD] = texID;
		mLookup_ProceduralTextureSRVs[EProceduralTextures::CHECKERBOARD] = this->CreateAndInitializeSRV(texID);
	}
	{
		desc.TexName = "Checkerboard_Gray";
		std::vector<UINT8> texture = Texture::GenerateTexture_Checkerboard(sizeX, true);
		desc.pData = texture.data();
		TextureID texID = this->CreateTexture(desc);
		mLookup_ProceduralTextureIDs[EProceduralTextures::CHECKERBOARD_GRAYSCALE] = texID;
		mLookup_ProceduralTextureSRVs[EProceduralTextures::CHECKERBOARD_GRAYSCALE] = this->CreateAndInitializeSRV(texID);
	}
}
void VQRenderer::UploadVertexAndIndexBufferHeaps()
{
	std::lock_guard<std::mutex> lk(mMtxTextureUploadQueue);
	mStaticHeap_VertexBuffer.UploadData(mHeapUpload.GetCommandList());
	mStaticHeap_IndexBuffer.UploadData(mHeapUpload.GetCommandList());
}


ID3D12DescriptorHeap* VQRenderer::GetDescHeap(EResourceHeapType HeapType) 
{
	ID3D12DescriptorHeap* pHeap = nullptr;
	switch (HeapType)
	{
	case RTV_HEAP:          pHeap = mHeapRTV.GetHeap(); break;
	case DSV_HEAP:          pHeap = mHeapDSV.GetHeap(); break;
	case CBV_SRV_UAV_HEAP:  pHeap = mHeapCBV_SRV_UAV.GetHeap(); break;
	case SAMPLER_HEAP:      pHeap = mHeapSampler.GetHeap(); break;
	}
	return pHeap;
}


TextureID VQRenderer::GetProceduralTexture(EProceduralTextures tex) const
{
	while (!mbDefaultResourcesLoaded);
	if (mLookup_ProceduralTextureIDs.find(tex) == mLookup_ProceduralTextureIDs.end())
	{
		Log::Error("Couldn't find procedural texture %d", tex);
		return INVALID_ID;
	}
	return mLookup_ProceduralTextureIDs.at(tex);
}




// ================================================================================================================================================

//
// STATIC
//
/*
@bEnumerateSoftwareAdapters : Basic Render Driver adapter.
*/
std::vector< FGPUInfo > VQRenderer::EnumerateDX12Adapters(bool bEnableDebugLayer, bool bEnumerateSoftwareAdapters /*= false*/, IDXGIFactory6* pFactory /*= nullptr*/)
{
	std::vector< FGPUInfo > GPUs;
	HRESULT hr = {};

	IDXGIAdapter1* pAdapter = nullptr; // adapters are the graphics card (this includes the embedded graphics on the motherboard)
	int iAdapter = 0;                  // we'll start looking for DX12  compatible graphics devices starting at index 0
	bool bAdapterFound = false;        // set this to true when a good one was found

	// https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/d3d10-graphics-programming-guide-dxgi
	// https://stackoverflow.com/questions/42354369/idxgifactory-versions
	// Chuck Walbourn: For DIrect3D 12, you can assume CreateDXGIFactory2 and IDXGIFactory4 or later is supported. 
	// DXGIFactory6 supports preferences when querying devices: DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE
	IDXGIFactory6* pDxgiFactory = nullptr;
	if (pFactory)
	{
		pDxgiFactory = pFactory;
	}
	else
	{
		UINT DXGIFlags = 0;
		if (bEnableDebugLayer)
		{
			DXGIFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
		hr = CreateDXGIFactory2(DXGIFlags, IID_PPV_ARGS(&pDxgiFactory));
	}
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
		int a = 5;
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

	// if we're using the local factory and haven't provided one with the argument
	if (!pFactory)
	{
		pDxgiFactory->Release();
	}
	assert(bAdapterFound);

	return GPUs;
}
