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
#include "WindowRenderContext.h"

#include "../Engine/Core/Window.h"
#include "../../Shaders/LightingConstantBufferData.h"

#include "../../Libs/VQUtils/Source/Log.h"
#include "../../Libs/VQUtils/Source/utils.h"
#include "../../Libs/VQUtils/Source/Timer.h"
#include "../../Libs/D3D12MA/src/Common.h"
#include "../../Libs/D3D12MA/src/D3D12MemAlloc.h"

#include <cassert>
#include <atomic>


using namespace Microsoft::WRL;
using namespace VQSystemInfo;

#ifdef _DEBUG
	#define ENABLE_DEBUG_LAYER      1
	#define ENABLE_VALIDATION_LAYER 1
#else
	#define ENABLE_DEBUG_LAYER      0
	#define ENABLE_VALIDATION_LAYER 0
#endif

// initialize statics
std::string VQRenderer::ShaderSourceFileDirectory = "Shaders";
std::string VQRenderer::PSOCacheDirectory    = "Cache/PSOs";
#if _DEBUG
std::string VQRenderer::ShaderCacheDirectory = "Cache/Shaders/Debug";
#else
std::string VQRenderer::ShaderCacheDirectory = "Cache/Shaders";
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

std::wstring VQRenderer::GetFullPathOfShader(LPCWSTR shaderFileName)
{
	std::wstring dir = StrUtil::ASCIIToUnicode(VQRenderer::ShaderSourceFileDirectory) + L"/";
	return dir + shaderFileName;
}

std::wstring VQRenderer::GetFullPathOfShader(const std::string& shaderFileName)
{
	std::wstring dir = StrUtil::ASCIIToUnicode(VQRenderer::ShaderSourceFileDirectory) + L"/";
	return dir + StrUtil::ASCIIToUnicode(shaderFileName);
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
	deviceDesc.bEnableGPUValidationLayer = ENABLE_VALIDATION_LAYER;
	const bool bDeviceCreateSucceeded = mDevice.Create(deviceDesc);
	ID3D12Device* pDevice = pVQDevice->GetDevicePtr();

	assert(bDeviceCreateSucceeded);

	// Create Command Queues of different types
	mGFXQueue.Create(pVQDevice, CommandQueue::EType::GFX);
	mComputeQueue.Create(pVQDevice, CommandQueue::EType::COMPUTE);
	mCopyQueue.Create(pVQDevice, CommandQueue::EType::COPY);

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
	
	LoadBuiltinRootSignatures();
	float tRS = timer.Tick();
	Log::Info("[Renderer]    RootSignatures=%.2fs", tRS);

	LoadBuiltinPSOs();
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


void VQRenderer::Destroy()
{
	Log::Info("VQRenderer::Exit()");
	mWorkers_PSOLoad.Destroy();
	mWorkers_ShaderLoad.Destroy();

	mbExitUploadThread.store(true);
	mSignal_UploadThreadWorkReady.NotifyAll();

	// clean up memory
	mHeapUpload.Destroy();
	mHeapCBV_SRV_UAV.Destroy();
	mHeapDSV.Destroy();
	mHeapRTV.Destroy();
	mStaticHeap_VertexBuffer.Destroy();
	mStaticHeap_IndexBuffer.Destroy();

	// clean up textures
	for (std::unordered_map<TextureID, Texture>::iterator it = mTextures.begin(); it != mTextures.end(); ++it)
	{
		it->second.Destroy();
	}
	mTextures.clear();
	mpAllocator->Release();
	
	// clean up root signatures and PSOs
	for (auto& pr : mRootSignatureLookup)
	{
		if (pr.second) pr.second->Release();
	}
	for (std::pair<PSO_ID, ID3D12PipelineState*> pPSO : mPSOs)
	{
		if (pPSO.second)
			pPSO.second->Release();
	}
	mPSOs.clear();

	// clean up contexts
	size_t NumBackBuffers = 0;
	for (std::unordered_map<HWND, FWindowRenderContext>::iterator it = mRenderContextLookup.begin(); it != mRenderContextLookup.end(); ++it)
	{
		auto& ctx = it->second;
		ctx.CleanupContext();
	}

	// cleanp up device
	mGFXQueue.Destroy();
	mComputeQueue.Destroy();
	mCopyQueue.Destroy();
	mDevice.Destroy();

	// clean up remaining threads
	mTextureUploadThread.join();
}

void VQRenderer::OnWindowSizeChanged(HWND hwnd, unsigned w, unsigned h)
{
	if (!CheckContext(hwnd)) return;
	FWindowRenderContext& ctx = mRenderContextLookup.at(hwnd);

	ctx.WindowDisplayResolutionX = w;
	ctx.WindowDisplayResolutionY = h;
}


SwapChain& VQRenderer::GetWindowSwapChain(HWND hwnd) { return mRenderContextLookup.at(hwnd).SwapChain; }

unsigned short VQRenderer::GetSwapChainBackBufferCount(Window* pWnd) const { return pWnd ? this->GetSwapChainBackBufferCount(pWnd->GetHWND()) : 0; }
unsigned short VQRenderer::GetSwapChainBackBufferCount(HWND hwnd) const
{
	if (!CheckContext(hwnd)) return 0;

	const FWindowRenderContext& ctx = mRenderContextLookup.at(hwnd);
	return ctx.GetNumSwapchainBuffers();
	
}


void VQRenderer::InitializeRenderContext(const Window* pWin, int NumSwapchainBuffers, bool bVSync, bool bHDRSwapchain)
{
	Device*       pVQDevice = &mDevice;
	ID3D12Device* pDevice = pVQDevice->GetDevicePtr();

	FWindowRenderContext ctx = {};
	ctx.InitializeContext(pWin, pVQDevice, NumSwapchainBuffers, bVSync, bHDRSwapchain);

	// Save other context data
	ctx.WindowDisplayResolutionX = pWin->GetWidth();
	ctx.WindowDisplayResolutionY = pWin->GetHeight();

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

	const uint32 UPLOAD_HEAP_SIZE = (512+256) * MEGABYTE; // TODO: from RendererSettings.ini
	mHeapUpload.Create(pDevice, UPLOAD_HEAP_SIZE, this->mGFXQueue.pQueue);

	constexpr uint32 NumDescsCBV = 100;
	constexpr uint32 NumDescsSRV = 8192;
	constexpr uint32 NumDescsUAV = 100;
	constexpr bool   bCPUVisible = false;
	mHeapCBV_SRV_UAV.Create(pDevice, "HeapCBV_SRV_UAV", D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, NumDescsCBV + NumDescsSRV + NumDescsUAV, bCPUVisible);

	constexpr uint32 NumDescsDSV = 100;
	mHeapDSV.Create(pDevice, "HeapDSV", D3D12_DESCRIPTOR_HEAP_TYPE_DSV, NumDescsDSV);

	constexpr uint32 NumDescsRTV = 1000;
	mHeapRTV.Create(pDevice, "HeapRTV", D3D12_DESCRIPTOR_HEAP_TYPE_RTV, NumDescsRTV);

	constexpr uint32 STATIC_GEOMETRY_MEMORY_SIZE = 256 * MEGABYTE;
	constexpr bool USE_GPU_MEMORY = true;
	mStaticHeap_VertexBuffer.Create(pDevice, EBufferType::VERTEX_BUFFER, STATIC_GEOMETRY_MEMORY_SIZE, USE_GPU_MEMORY, "VQRenderer::mStaticVertexBufferPool");
	mStaticHeap_IndexBuffer .Create(pDevice, EBufferType::INDEX_BUFFER , STATIC_GEOMETRY_MEMORY_SIZE, USE_GPU_MEMORY, "VQRenderer::mStaticIndexBufferPool");
}

void VQRenderer::InitializeShaderAndPSOCacheDirectory()
{
	DirectoryUtil::CreateFolderIfItDoesntExist(VQRenderer::ShaderCacheDirectory);
	DirectoryUtil::CreateFolderIfItDoesntExist(VQRenderer::PSOCacheDirectory);
}

static void ReportErrorAndReleaseBlob(ComPtr<ID3DBlob>& pBlob)
{
	if (pBlob)
	{
		OutputDebugStringA((char*)pBlob->GetBufferPointer());
		pBlob->Release();
	}
}

enum EDefaultSampler
{
	POINT_BORDER = 0,
	POINT_WRAP,
	POINT_CLAMP,

	BILINEAR_BORDER,
	BILINEAR_WRAP,
	BILINEAR_CLAMP,
	
	TRILINEAR_BORDER,
	TRILINEAR_WRAP,
	TRILINEAR_CLAMP,
	
	ANISOTROPIC_BORDER,
	ANISOTROPIC_WRAP,
	ANISOTROPIC_CLAMP,

	NUM_DEFAULT_SAMPLERS
};
static D3D12_STATIC_SAMPLER_DESC GetDefaultSamplerDesc(EDefaultSampler eSampler, D3D12_SHADER_VISIBILITY eShaderVisibility, UINT uShaderRegister = 0, UINT uRegisterSpace = 0)
{
	D3D12_STATIC_SAMPLER_DESC desc = {};

	switch (eSampler) // Address Mode
	{
	case POINT_BORDER:
	case BILINEAR_BORDER:
	case TRILINEAR_BORDER:
	case ANISOTROPIC_BORDER:
		desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		break;

	case POINT_WRAP:
	case BILINEAR_WRAP:
	case TRILINEAR_WRAP:
	case ANISOTROPIC_WRAP:
		desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		break;

	case POINT_CLAMP:
	case BILINEAR_CLAMP:
	case TRILINEAR_CLAMP:
	case ANISOTROPIC_CLAMP:
		desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		break;
	}

	switch (eSampler) // Filter
	{
	case POINT_BORDER:
	case POINT_WRAP:
	case POINT_CLAMP:
		desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		break;

	case BILINEAR_BORDER:
	case BILINEAR_WRAP:
	case BILINEAR_CLAMP:
		desc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
		break;

	case TRILINEAR_BORDER:
	case TRILINEAR_WRAP:
	case TRILINEAR_CLAMP:
		desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		break;

	case ANISOTROPIC_BORDER:
	case ANISOTROPIC_WRAP:
	case ANISOTROPIC_CLAMP:
		desc.Filter = D3D12_FILTER_ANISOTROPIC;
		break;
	}

	desc.MipLODBias = 0;
	desc.MaxAnisotropy = 0;
	desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;

	desc.MinLOD = 0.0f;
	desc.MaxLOD = D3D12_FLOAT32_MAX;
	desc.ShaderRegister = uShaderRegister;
	desc.RegisterSpace = uRegisterSpace;
	desc.ShaderVisibility = eShaderVisibility;
	return desc;
}

void VQRenderer::LoadBuiltinRootSignatures()
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
		SetName(pRS, "RootSignature_HelloWorldTriangle");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__HelloWorldTriangle] = pRS;
	}

	// Fullscreen-Triangle Root Signature : [1]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);

		CD3DX12_ROOT_PARAMETER1 rootParameters[2];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[1].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_PIXEL);
		D3D12_STATIC_SAMPLER_DESC sampler = GetDefaultSamplerDesc(EDefaultSampler::TRILINEAR_BORDER, D3D12_SHADER_VISIBILITY_PIXEL);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_FSTriangle");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__FullScreenTriangle] = pRS;
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

		D3D12_STATIC_SAMPLER_DESC sampler = GetDefaultSamplerDesc(EDefaultSampler::TRILINEAR_WRAP, D3D12_SHADER_VISIBILITY_PIXEL);
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_HelloWorldCube");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__HelloWorldCube] = pRS;
	}

	// Tonemapper Root Signature : [3]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);

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
		SetName(pRS, "CS__SRV1_UAV1_ROOTCBV1");
		mRootSignatureLookup[EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1] = pRS;
	}
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);

		CD3DX12_ROOT_PARAMETER1 rootParameters[4];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[3].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE);

		hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
		if (!SUCCEEDED(hr)) { ReportErrorAndReleaseBlob(error); assert(false); }
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "CS__SRV2_UAV1_ROOTCBV1");
		mRootSignatureLookup[EBuiltinRootSignatures::CS__SRV2_UAV1_ROOTCBV1] = pRS;
	}

	// Object Root Signature : [4]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

		CD3DX12_ROOT_PARAMETER1 rootParameters[2];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[1].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);

		D3D12_STATIC_SAMPLER_DESC samplers[2] = {
			GetDefaultSamplerDesc(EDefaultSampler::TRILINEAR_WRAP, D3D12_SHADER_VISIBILITY_PIXEL, 0),
			GetDefaultSamplerDesc(EDefaultSampler::POINT_WRAP    , D3D12_SHADER_VISIBILITY_PIXEL, 1) 
		};

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, _countof(samplers), &samplers[0], D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_Object");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__Object] = pRS;
	}

	// ForwardLighting Root Signature : [5]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[8];
		// material textures
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[7].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		// shadow maps
		ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1                          , 13, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, NUM_SHADOWING_LIGHTS__SPOT , 16, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, NUM_SHADOWING_LIGHTS__POINT, 22, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 10, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 11, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[6].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 12, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		//ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC); // perView  cb's are DescRanges
		//ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC); // perFrame cb's are DescRanges

		CD3DX12_ROOT_PARAMETER1 rootParameters[11]; 
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
		// Shadowmap bindings
		rootParameters[4].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[5].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[6].InitAsDescriptorTable(1, &ranges[3], D3D12_SHADER_VISIBILITY_PIXEL);
		
		// Environment map bindings
		rootParameters[7].InitAsDescriptorTable(1, &ranges[4], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[8].InitAsDescriptorTable(1, &ranges[5], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[9].InitAsDescriptorTable(1, &ranges[6], D3D12_SHADER_VISIBILITY_PIXEL);

		// SSAO map binding
		rootParameters[10].InitAsDescriptorTable(1, &ranges[7], D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_STATIC_SAMPLER_DESC samplers[4] = 
		{
			GetDefaultSamplerDesc(EDefaultSampler::TRILINEAR_WRAP , D3D12_SHADER_VISIBILITY_PIXEL, 0),
			GetDefaultSamplerDesc(EDefaultSampler::POINT_WRAP, D3D12_SHADER_VISIBILITY_PIXEL, 1),
			GetDefaultSamplerDesc(EDefaultSampler::ANISOTROPIC_WRAP, D3D12_SHADER_VISIBILITY_PIXEL, 2),
			GetDefaultSamplerDesc(EDefaultSampler::TRILINEAR_CLAMP, D3D12_SHADER_VISIBILITY_PIXEL, 3),
		};

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, _countof(samplers), &samplers[0], D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_ForwardLighting");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__ForwardLighting] = pRS;
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
		SetName(pRS, "RootSignature_WireframeUnlit");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__WireframeUnlit] = pRS;
	}

	// ShadowDepthPass Root Signatures [7-9]
	{
		D3D12_STATIC_SAMPLER_DESC sampler = GetDefaultSamplerDesc(EDefaultSampler::TRILINEAR_WRAP, D3D12_SHADER_VISIBILITY_PIXEL, 0);
		{
			CD3DX12_ROOT_PARAMETER1 rootParameters[1];
			rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_VERTEX);

			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
			rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

			ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
			ID3D12RootSignature* pRS = nullptr;
			ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
			SetName(pRS, "RootSignature_DepthOnlyVS");
			mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__ShadowPassDepthOnlyVS] = pRS;
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
			SetName(pRS, "RootSignature_LinearDepthVSPS");
			mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__ShadowPassLinearDepthVSPS] = pRS;
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
			SetName(pRS, "RootSignature_MaskedDepthVSPS");
			mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__ShadowPassMaskedDepthVSPS] = pRS;
		}
		
	}
	
	// Convolution Cubemap Root Signature [10]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);

		CD3DX12_ROOT_PARAMETER1 rootParameters[4];
		//rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_GEOMETRY);
		rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL); // TODO: temp
		//rootParameters[1].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[1].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL); // TODO: temp
		rootParameters[2].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[3].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_STATIC_SAMPLER_DESC samplers[1] = { GetDefaultSamplerDesc(EDefaultSampler::TRILINEAR_WRAP , D3D12_SHADER_VISIBILITY_PIXEL, 0) };

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(
			  _countof(rootParameters), rootParameters
			, _countof(samplers), samplers
			, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_ConvolutionCubemap");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__ConvolutionCubemap] = pRS;
	}

	// BRDF Integration CS Root Signature : [12]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);

		CD3DX12_ROOT_PARAMETER1 rootParameters[1];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE);

		hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
		if (!SUCCEEDED(hr)) { ReportErrorAndReleaseBlob(error); assert(false); }
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_BRDFIntegrationCS");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__BRDFIntegrationCS] = pRS;
	}
	
	// FFX-SPD CS Root Signature : [13]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);

		CD3DX12_ROOT_PARAMETER1 rootParameters[4];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[3].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE);

		hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
		if (!SUCCEEDED(hr)) { ReportErrorAndReleaseBlob(error); assert(false); }
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_FFX-SPD_CS");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__FFX_SPD_CS] = pRS;
	}

	// DepthPrePass Root Signature : [14]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
		// material textures
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE/*D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC*/);
		
		CD3DX12_ROOT_PARAMETER1 rootParameters[2];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[1].InitAsConstantBufferView(2, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL); // VS-PS


		D3D12_STATIC_SAMPLER_DESC samplers[4] = 
		{
			GetDefaultSamplerDesc(EDefaultSampler::TRILINEAR_WRAP  , D3D12_SHADER_VISIBILITY_PIXEL, 0),
			GetDefaultSamplerDesc(EDefaultSampler::POINT_WRAP      , D3D12_SHADER_VISIBILITY_PIXEL, 1),
			GetDefaultSamplerDesc(EDefaultSampler::ANISOTROPIC_WRAP, D3D12_SHADER_VISIBILITY_PIXEL, 2),
			GetDefaultSamplerDesc(EDefaultSampler::TRILINEAR_CLAMP , D3D12_SHADER_VISIBILITY_PIXEL, 3),
		};

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, _countof(samplers), &samplers[0], D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));

		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_DepthPrePass");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__ZPrePass] = pRS;
	}

	// FSR1 Root Signature : [15]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		//ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);

		CD3DX12_ROOT_PARAMETER1 rootParameters[3];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[2].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);
		//rootParameters[3].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL);

		D3D12_STATIC_SAMPLER_DESC samplers[1] = { GetDefaultSamplerDesc(EDefaultSampler::TRILINEAR_WRAP, D3D12_SHADER_VISIBILITY_ALL, 0) };

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, _countof(samplers), samplers, D3D12_ROOT_SIGNATURE_FLAG_NONE);

		hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
		if (!SUCCEEDED(hr)) { ReportErrorAndReleaseBlob(error); assert(false); }
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_FidelityFX_SuperResolution");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__FFX_FSR1] = pRS;
	}

	// UIHDRComposite Root Signature : [16]
	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);

		CD3DX12_ROOT_PARAMETER1 rootParameters[3];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);
		//rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[2].InitAsConstants(1, 0);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_UIHDRComposite");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__UI_HDR_Composite] = pRS;
	}

	{
		CD3DX12_DESCRIPTOR_RANGE1 ranges[4];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1 , 0 , 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 13, 0 , 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1 , 13, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
		// CBV ?

		CD3DX12_ROOT_PARAMETER1 rootParameters[4];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[3].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE);

		hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
		if (!SUCCEEDED(hr)) { ReportErrorAndReleaseBlob(error); assert(false); }
		ID3D12RootSignature* pRS = nullptr;
		ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRS)));
		SetName(pRS, "RootSignature_DownsampleDepth");
		mRootSignatureLookup[EBuiltinRootSignatures::LEGACY__DownsampleDepthCS] = pRS;
	}
}

//FPSOLoadDesc::FPSOLoadDesc(FPSOLoadDesc&& o)
//	: PSOName(o.PSOName)
//	, ShaderStageCompileDescs(o.ShaderStageCompileDescs)
//{
//	const bool bComputePSO = std::find_if(o.ShaderStageCompileDescs.begin(), o.ShaderStageCompileDescs.end() // check if ShaderModel has cs_*_*
//		, [](const FShaderStageCompileDesc& desc) { return ShaderUtils::GetShaderStageEnumFromShaderModel(desc.ShaderModel) == EShaderStage::CS; }
//	) != o.ShaderStageCompileDescs.end();
//	if (bComputePSO)
//		this->D3D12ComputeDesc = o.D3D12ComputeDesc;
//	else
//		this->D3D12GraphicsDesc = o.D3D12GraphicsDesc;
//}

void VQRenderer::LoadBuiltinPSOs()
{
	// temp: singl thread pso load from vector
	// todo: enqueue load descs into the MT queue
	std::vector< std::pair<PSO_ID, FPSODesc >> PSOLoadDescs;

	// FULLSCREEN TRIANGLE PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"FullscreenTriangle.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_FullscreenTriangle";

		// Shader description
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_0" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_0" });

		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.InputLayout = { };
		psoDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__FullScreenTriangle);
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

	// DEBUG VISUALIZATION PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"Visualization.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_Visualization";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain", "cs_5_1" });
		psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1);

		PSOLoadDescs.push_back({ EBuiltinPSOs::VIZUALIZATION_CS_PSO, psoLoadDesc });
	}


	// UI PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"UI.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_UI";

		// Shader description
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_0" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_0" });

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__HelloWorldCube);
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.BlendState.RenderTarget[0].BlendEnable = true;
		psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::UI_PSO, psoLoadDesc });
	}

	// UIHDRComposition PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"UIHDRComposite.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_UI_HDR";

		// Shader description
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_0" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_0" });

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__UI_HDR_Composite);
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.BlendState.RenderTarget[0].BlendEnable = false;
		psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::UI_HDR_scRGB_PSO, psoLoadDesc });
	}

	// SKYDOME PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"Skydome.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_Skydome";

		// Shader description
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_0" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_0" });

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__HelloWorldCube);
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
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"Object.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_Object";

		// Shader description
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_1" });

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__Object);
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
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"Tonemapper.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_TonemapperCS";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain", "cs_5_1" });
		psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1);

		PSOLoadDescs.push_back({ EBuiltinPSOs::TONEMAPPER_PSO, psoLoadDesc });
	}

	// DEPTH PREPASS PSOs
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"DepthPrePass.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_FDepthPrePassVSPS";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_1" });
		psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__ZPrePass);
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({"INSTANCED_DRAW", std::to_string(RENDER_INSTANCED_SCENE_MESHES) });
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({"INSTANCE_COUNT", std::to_string(MAX_INSTANCE_COUNT__SCENE_MESHES)});

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
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::DEPTH_PREPASS_PSO, psoLoadDesc });


		// MSAA PSO
		psoLoadDesc.PSOName = "PSO_FDepthPrePassVSPS_MSAA4";
		psoDesc.SampleDesc.Count = 4;
		PSOLoadDescs.push_back({ EBuiltinPSOs::DEPTH_PREPASS_PSO_MSAA_4, psoLoadDesc });

	}

	// FORWARD LIGHTING PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"ForwardLighting.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_FwdLightingVSPS";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_1" });
		psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__ForwardLighting);
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCED_DRAW", std::to_string(RENDER_INSTANCED_SCENE_MESHES) });
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCE_COUNT", std::to_string(MAX_INSTANCE_COUNT__SCENE_MESHES) });

		// PSO description
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = TRUE;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::FORWARD_LIGHTING_PSO, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_FwdLightingVSPS_MV";
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "OUTPUT_MOTION_VECTORS", "1" });
		psoLoadDesc.ShaderStageCompileDescs[1].Macros.push_back({ "OUTPUT_MOTION_VECTORS", "1" });
		psoDesc.NumRenderTargets = 2;
		psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT;
		PSOLoadDescs.push_back({ EBuiltinPSOs::FORWARD_LIGHTING_AND_MV_PSO, psoLoadDesc });
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.pop_back();
		psoLoadDesc.ShaderStageCompileDescs[1].Macros.pop_back();

		// MSAA PSOs
		psoLoadDesc.PSOName = "PSO_FwdLightingVSPS_MSAA4";
		psoDesc.SampleDesc.Count = 4;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
		PSOLoadDescs.push_back({ EBuiltinPSOs::FORWARD_LIGHTING_PSO_MSAA_4, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_FwdLightingVSPS_MV_MSAA4";
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "OUTPUT_MOTION_VECTORS", "1" });
		psoLoadDesc.ShaderStageCompileDescs[1].Macros.push_back({ "OUTPUT_MOTION_VECTORS", "1" });
		psoDesc.NumRenderTargets = 2;
		psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT;
		PSOLoadDescs.push_back({ EBuiltinPSOs::FORWARD_LIGHTING_AND_MV_PSO_MSAA_4, psoLoadDesc });
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.pop_back();
		psoLoadDesc.ShaderStageCompileDescs[1].Macros.pop_back();

		psoLoadDesc.PSOName = "PSO_FwdLightingVSPS_Viz";
		psoLoadDesc.ShaderStageCompileDescs[1].Macros.push_back({ "OUTPUT_ALBEDO", "1" });
		psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT; // rgba16f vs rgba8unorm 
		psoDesc.SampleDesc.Count = 1;
		PSOLoadDescs.push_back({ EBuiltinPSOs::FORWARD_LIGHTING_AND_VIZ_PSO, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_FwdLightingVSPS_MSAA4_Viz";
		psoDesc.SampleDesc.Count = 4;
		PSOLoadDescs.push_back({ EBuiltinPSOs::FORWARD_LIGHTING_AND_VIZ_PSO_MSAA_4, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_FwdLightingVSPS_Viz_MV";
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "OUTPUT_MOTION_VECTORS", "1" });
		psoLoadDesc.ShaderStageCompileDescs[1].Macros.push_back({ "OUTPUT_MOTION_VECTORS", "1" });
		psoDesc.NumRenderTargets = 3;
		psoDesc.RTVFormats[2] = DXGI_FORMAT_R16G16_FLOAT;
		psoDesc.SampleDesc.Count = 1;
		PSOLoadDescs.push_back({ EBuiltinPSOs::FORWARD_LIGHTING_AND_VIZ_AND_MV_PSO, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_FwdLightingVSPS_MSAA4_Viz_MV";
		psoDesc.SampleDesc.Count = 4;
		PSOLoadDescs.push_back({ EBuiltinPSOs::FORWARD_LIGHTING_AND_VIZ_AND_MV_PSO_MSAA_4, psoLoadDesc });
		// ^ lol this is not maintainable, gotta refactor all this into render passes and proper shader perms
	}

	// WIREFRAME/UNLIT PSOs
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"Unlit.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_UnlitVSPS";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_1" });
		psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__WireframeUnlit);

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

		psoLoadDesc.PSOName = "PSO_Wireframe_VSPS_Instanced";
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCED_DRAW", "1" });
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCE_COUNT", std::to_string(MAX_INSTANCE_COUNT__UNLIT_SHADER) });
		PSOLoadDescs.push_back({ EBuiltinPSOs::WIREFRAME_INSTANCED_PSO, psoLoadDesc });
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.pop_back();
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.pop_back();
		{
			// MSAA
			psoLoadDesc.PSOName = "PSO_WireframeVSPS_MSAA4";
			psoDesc.SampleDesc.Count = 4;
			PSOLoadDescs.push_back({ EBuiltinPSOs::WIREFRAME_PSO_MSAA_4, psoLoadDesc });

			psoLoadDesc.PSOName = "PSO_WireframeVSPS_Instanced_MSAA4";
			psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCED_DRAW", "1" });
			psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCE_COUNT", std::to_string(MAX_INSTANCE_COUNT__UNLIT_SHADER) });
			PSOLoadDescs.push_back({ EBuiltinPSOs::WIREFRAME_INSTANCED_MSAA4_PSO, psoLoadDesc });
		}
	}

	// SHADOWMAP PSOs
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"ShadowDepthPass.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_DepthOnlyVS";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCED_DRAW", "1" });
		psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({ "INSTANCE_COUNT", std::to_string(MAX_INSTANCE_COUNT__SHADOW_MESHES) });
		psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__ShadowPassDepthOnlyVS);

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
		psoDesc.NumRenderTargets = 0;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::DEPTH_PASS_PSO, psoLoadDesc });
		{
			psoLoadDesc.PSOName = "PSO_LinearDepthVSPS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_1" });
			psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__ShadowPassLinearDepthVSPS);
			PSOLoadDescs.push_back({ EBuiltinPSOs::DEPTH_PASS_LINEAR_PSO, psoLoadDesc });
			psoLoadDesc.ShaderStageCompileDescs.pop_back();
		}
		{
			psoLoadDesc.PSOName = "PSO_MaskedDepthVSPS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_1" });
			for (FShaderStageCompileDesc& shdDesc : psoLoadDesc.ShaderStageCompileDescs)
			{
				shdDesc.Macros.push_back({ "ALPHA_MASK", "1" });
			}
			psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__ShadowPassMaskedDepthVSPS);
			PSOLoadDescs.push_back({ EBuiltinPSOs::DEPTH_PASS_ALPHAMASKED_PSO, psoLoadDesc });
			psoLoadDesc.ShaderStageCompileDescs.pop_back();
		}
	}


	// CUBEMAP CONVOLUTION PSOs
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"CubemapConvolution.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_CubemapConvolutionVSGSPS_Diffuse";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "GSMain", "gs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain_DiffuseIrradiance", "ps_5_1" });
		psoLoadDesc.D3D12GraphicsDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__ConvolutionCubemap);

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
		for (uint rt = 0; rt < psoDesc.NumRenderTargets; ++rt) psoDesc.RTVFormats[rt] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleDesc.Count = 1;

		PSOLoadDescs.push_back({ EBuiltinPSOs::CUBEMAP_CONVOLUTION_DIFFUSE_PSO, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_CubemapConvolutionVSPS_Diffuse";
		psoLoadDesc.ShaderStageCompileDescs.clear();
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain_PerFace", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain_DiffuseIrradiance", "ps_5_1" });
		for (uint rt = 1; rt < psoDesc.NumRenderTargets; ++rt) psoDesc.RTVFormats[rt] = DXGI_FORMAT_UNKNOWN;
		psoDesc.NumRenderTargets = 1;
		// determine diffuse irradiance integration iteration count based on GPU dedicated memory size
		const std::vector<FGPUInfo> iGPU = VQSystemInfo::GetGPUInfo();
		assert(!iGPU.empty());
		const bool bLowEndGPU = iGPU[0].DedicatedGPUMemory < 2.1 * GIGABYTE;
		const bool bHighEndGPU = iGPU[0].DedicatedGPUMemory > 4.0 * GIGABYTE;
		for (FShaderStageCompileDesc& shdDesc : psoLoadDesc.ShaderStageCompileDescs)
		{
			shdDesc.Macros.push_back({ "INTEGRATION_STEP_DIFFUSE_IRRADIANCE", (bLowEndGPU ? "0.050f" : (bHighEndGPU ? "0.010f" : "0.025f")) });
		}
		PSOLoadDescs.push_back({ EBuiltinPSOs::CUBEMAP_CONVOLUTION_DIFFUSE_PER_FACE_PSO, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_CubemapConvolutionVSPS_Specular";
		psoLoadDesc.ShaderStageCompileDescs.clear();
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain_PerFace", "vs_5_1" });
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain_SpecularIrradiance", "ps_5_1" });
		PSOLoadDescs.push_back({ EBuiltinPSOs::CUBEMAP_CONVOLUTION_SPECULAR_PSO, psoLoadDesc });
	}

	// GAUSSIAN BLUR CS PSOs
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"GaussianBlur.hlsl");
		{
			FPSODesc psoLoadDesc = {};
			psoLoadDesc.PSOName = "PSO_GaussianBlurNaiveXCS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain_X", "cs_5_1" });
			psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1);
			PSOLoadDescs.push_back({ EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_X_PSO, psoLoadDesc });
		}
		{
			FPSODesc psoLoadDesc = {};
			psoLoadDesc.PSOName = "PSO_GaussianBlurNaiveYCS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain_Y", "cs_5_1" });
			psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1);
			PSOLoadDescs.push_back({ EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_Y_PSO, psoLoadDesc });
		}
	}

	// BRDF INTEGRATION CS PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"CubemapConvolution.hlsl");
		{
			FPSODesc psoLoadDesc = {};
			psoLoadDesc.PSOName = "PSO_BRDFIntegrationCS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain_BRDFIntegration", "cs_5_1" });
			psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__BRDFIntegrationCS);
			PSOLoadDescs.push_back({ EBuiltinPSOs::BRDF_INTEGRATION_CS_PSO, psoLoadDesc });
		}
	}

	// AMD FidelityFX PSOs
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"AMDFidelityFX.hlsl");

		// FFX-CAS
		{
			FPSODesc psoLoadDesc = {};
			psoLoadDesc.PSOName = "PSO_FFXCASCS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CAS_CSMain", "cs_6_1", {{"FFXCAS_CS", "1"}} });
			psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1); // share root signature with tonemapper pass
			PSOLoadDescs.push_back({ EBuiltinPSOs::FFX_CAS_CS_PSO, psoLoadDesc });
		}

		// FFX-SPD
		{
			FPSODesc psoLoadDesc = {};
			psoLoadDesc.PSOName = "PSO_FFXSPDCS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "SPD_CSMain", "cs_6_0", {{"FFXSPD_CS", "1"}} });
			psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__FFX_SPD_CS);
			//PSOLoadDescs.push_back({ EBuiltinPSOs::FFX_SPD_CS_PSO, psoLoadDesc }); 
		}

		// FSR / EASU 
		{
			FPSODesc psoLoadDesc = {};
			psoLoadDesc.PSOName = "PSO_FSR_EASU_CS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "FSR_EASU_CSMain", "cs_6_2", {{"FSR_EASU_CS", "1"}} });
			psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__FFX_FSR1);
			PSOLoadDescs.push_back({ EBuiltinPSOs::FFX_FSR1_EASU_CS_PSO, psoLoadDesc });
		}
		// FSR / EASU 
		{
			FPSODesc psoLoadDesc = {};
			psoLoadDesc.PSOName = "PSO_FSR_RCAS_CS";
			psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "FSR_RCAS_CSMain", "cs_6_2", {{"FSR_RCAS_CS", "1"}} });
			psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__FFX_FSR1);
			PSOLoadDescs.push_back({ EBuiltinPSOs::FFX_FSR1_RCAS_CS_PSO, psoLoadDesc });
		}
	}

	// DepthDownsampleCS PSO
	{
		const std::wstring ShaderFilePath = GetFullPathOfShader(L"DownsampleDepth.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "PSO_DownsampleDepthCS";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain", "cs_6_0" });
		psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRootSignatureLookup.at(EBuiltinRootSignatures::LEGACY__DownsampleDepthCS);
		PSOLoadDescs.push_back({ EBuiltinPSOs::DOWNSAMPLE_DEPTH_CS_PSO, psoLoadDesc });
	}

	// ---------------------------------------------------------------------------------------------------------------1

	// TODO: threaded PSO loading
	// single threaded PSO loading for now (shader compilation is still MTd)
	for (const std::pair<PSO_ID, FPSODesc>& psoLoadDescIDPair : PSOLoadDescs)
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
		mLookup_ProceduralTextureSRVs[EProceduralTextures::CHECKERBOARD] = this->AllocateAndInitializeSRV(texID);
	}
	{
		desc.TexName = "Checkerboard_Gray";
		std::vector<UINT8> texture = Texture::GenerateTexture_Checkerboard(sizeX, true);
		desc.pData = texture.data();
		TextureID texID = this->CreateTexture(desc);
		mLookup_ProceduralTextureIDs[EProceduralTextures::CHECKERBOARD_GRAYSCALE] = texID;
		mLookup_ProceduralTextureSRVs[EProceduralTextures::CHECKERBOARD_GRAYSCALE] = this->AllocateAndInitializeSRV(texID);
	}
	{
		desc.TexName = "IBL_BRDF_Integration";
		desc.pData = nullptr;
		desc.ResourceState = D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		desc.d3d12Desc.Width  = 1024;
		desc.d3d12Desc.Height = 1024;
		desc.d3d12Desc.Format = DXGI_FORMAT_R16G16_FLOAT;
		desc.d3d12Desc.Flags = D3D12_RESOURCE_FLAGS::D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		TextureID texID = this->CreateTexture(desc);
		mLookup_ProceduralTextureIDs[EProceduralTextures::IBL_BRDF_INTEGRATION_LUT] = texID;
		mLookup_ProceduralTextureSRVs[EProceduralTextures::IBL_BRDF_INTEGRATION_LUT] = this->AllocateAndInitializeSRV(texID);
	}
}
void VQRenderer::UploadVertexAndIndexBufferHeaps()
{
	std::lock_guard<std::mutex> lk(mMtxTextureUploadQueue);
	mStaticHeap_VertexBuffer.UploadData(mHeapUpload.GetCommandList());
	mStaticHeap_IndexBuffer.UploadData(mHeapUpload.GetCommandList());
}


ID3D12PipelineState* VQRenderer::GetPSO(PSO_ID psoID) const
{
	assert(psoID >= EBuiltinPSOs::NUM_BUILTIN_PSOs);

	return mPSOs.at(psoID);
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

ID3D12RootSignature* VQRenderer::GetBuiltinRootSignature(EBuiltinRootSignatures eRootSignature) const
{
	return mRootSignatureLookup.at((RS_ID)eRootSignature);
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
