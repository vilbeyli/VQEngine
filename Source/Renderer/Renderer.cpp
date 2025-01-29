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
#include "Core/Device.h"
#include "Resources/Texture.h"
#include "Pipeline/Shader.h"
#include "Rendering/WindowRenderContext.h"

#include "Engine/Core/Window.h"
#include "Engine/Core/Platform.h"
#include "Engine/GPUMarker.h"
#include "Engine/EnvironmentMap.h"
#include "Engine/Math.h"
#include "Engine/Scene/Mesh.h"
#include "Resources/CubemapUtility.h"
#include "Shaders/LightingConstantBufferData.h"

#include "Libs/VQUtils/Source/Log.h"
#include "Libs/VQUtils/Source/utils.h"
#include "Libs/VQUtils/Source/Timer.h"
#include "Libs/D3D12MA/src/D3D12MemAlloc.h"

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
static void InitializeD3D12MA(D3D12MA::Allocator*& mpAllocator, Device& mDevice)
{
	SCOPED_CPU_MARKER("Renderer.InitD3D12MA");
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

	HRESULT hr = D3D12MA::CreateAllocator(&desc, &mpAllocator);
	if (FAILED(hr))
	{
		Log::Error("D3D12MA failed w/ allocator creation");
		assert(false);
	}

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
// ---------------------------------------------------------------------------------------

//
// PUBLIC
//
void VQRenderer::Initialize(const FGraphicsSettings& Settings)
{
	Device* pVQDevice = &mDevice;
	mbMainSwapchainInitialized.store(false);

	// create PSO & Shader cache folders
	DirectoryUtil::CreateFolderIfItDoesntExist(VQRenderer::ShaderCacheDirectory);
	DirectoryUtil::CreateFolderIfItDoesntExist(VQRenderer::PSOCacheDirectory);

	// Create the device
	FDeviceCreateDesc deviceDesc = {};
	deviceDesc.bEnableDebugLayer = ENABLE_DEBUG_LAYER;
	deviceDesc.bEnableGPUValidationLayer = ENABLE_VALIDATION_LAYER;
	const bool bDeviceCreateSucceeded = mDevice.Create(deviceDesc);
	ID3D12Device* pDevice = pVQDevice->GetDevicePtr();

	assert(bDeviceCreateSucceeded);

	// Create Command Queues of different types
	for (int i = 0; i < CommandQueue::EType::NUM_COMMAND_QUEUE_TYPES; ++i)
	{
		mCmdQueues[i].Create(pVQDevice, (CommandQueue::EType)i);
	}

	// Initialize memory
	InitializeD3D12MA(mpAllocator, mDevice);
	{
		SCOPED_CPU_MARKER("Renderer.InitializeHeaps");
		ID3D12Device* pDevice = mDevice.GetDevicePtr();

		const uint32 UPLOAD_HEAP_SIZE = (512 + 256 + 128) * MEGABYTE; // TODO: from RendererSettings.ini
		mHeapUpload.Create(pDevice, UPLOAD_HEAP_SIZE, this->mCmdQueues[CommandQueue::EType::GFX].pQueue);

		constexpr uint32 NumDescsCBV = 100;
		constexpr uint32 NumDescsSRV = 8192;
		constexpr uint32 NumDescsUAV = 100;
		constexpr bool   bCPUVisible = false;
		mHeapCBV_SRV_UAV.Create(pDevice, "HeapCBV_SRV_UAV", EResourceHeapType::CBV_SRV_UAV_HEAP, NumDescsCBV + NumDescsSRV + NumDescsUAV, bCPUVisible);

		constexpr uint32 NumDescsDSV = 100;
		mHeapDSV.Create(pDevice, "HeapDSV", EResourceHeapType::DSV_HEAP, NumDescsDSV);

		constexpr uint32 NumDescsRTV = 1000;
		mHeapRTV.Create(pDevice, "HeapRTV", EResourceHeapType::RTV_HEAP, NumDescsRTV);

		constexpr uint32 STATIC_GEOMETRY_MEMORY_SIZE = 256 * MEGABYTE;
		constexpr bool USE_GPU_MEMORY = true;
		mStaticHeap_VertexBuffer.Create(pDevice, EBufferType::VERTEX_BUFFER, STATIC_GEOMETRY_MEMORY_SIZE, USE_GPU_MEMORY, "VQRenderer::mStaticVertexBufferPool");
		mStaticHeap_IndexBuffer.Create(pDevice, EBufferType::INDEX_BUFFER, STATIC_GEOMETRY_MEMORY_SIZE, USE_GPU_MEMORY, "VQRenderer::mStaticIndexBufferPool");
	}

	// initialize thread
	mbExitUploadThread.store(false);
	mTextureUploadThread = std::thread(&VQRenderer::TextureUploadThread_Main, this);

	const size_t HWThreads = ThreadPool::sHardwareThreadCount;
	const size_t HWCores   = HWThreads >> 1;
	mWorkers_ShaderLoad.Initialize(HWCores, "ShaderLoadWorkers");
	mWorkers_PSOLoad.Initialize(HWCores, "PSOLoadWorkers");

	Log::Info("[Renderer] Initialized.");
}

void VQRenderer::Load()
{
	SCOPED_CPU_MARKER("Renderer::Load()");
	Timer timer; timer.Start();
	Log::Info("[Renderer] Loading...");
	
	LoadBuiltinRootSignatures();
	float tRS = timer.Tick();
	Log::Info("[Renderer]    RootSignatures=%.2fs", tRS);

	LoadDefaultResources();
	float tDefaultRscs = timer.Tick();
	Log::Info("[Renderer]    DefaultRscs=%.2fs", tDefaultRscs);

	float total = tRS + tDefaultRscs;
	Log::Info("[Renderer] Loaded in %.2fs.", total);
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
	for (int i = 0; i < CommandQueue::EType::NUM_COMMAND_QUEUE_TYPES; ++i)
	{
		mCmdQueues[i].Destroy();
	}
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

	FWindowRenderContext ctx = FWindowRenderContext(mCmdQueues[CommandQueue::EType::GFX]);
	ctx.InitializeContext(pWin, pVQDevice, NumSwapchainBuffers, bVSync, bHDRSwapchain);

	// Save other context data
	ctx.WindowDisplayResolutionX = pWin->GetWidth();
	ctx.WindowDisplayResolutionY = pWin->GetHeight();

	// save the render context
	this->mRenderContextLookup.emplace(pWin->GetHWND(), std::move(ctx));
	this->mbMainSwapchainInitialized.store(true);
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

void VQRenderer::WaitMainSwapchainReady() const
{
	SCOPED_CPU_MARKER_C("WaitSwapchainReady", 0xFF770000);
	while (!mbMainSwapchainInitialized.load());
}

void VQRenderer::WaitForLoadCompletion() const
{
	SCOPED_CPU_MARKER_C("WaitRendererDefaultResourceLoaded", 0xFFAA0000);
	while (!mbDefaultResourcesLoaded); 
}

// ================================================================================================================================================
// ================================================================================================================================================
// ================================================================================================================================================

//
// PRIVATE
//

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
		desc.pDataArray.push_back( texture.data() );
		TextureID texID = this->CreateTexture(desc, false);
		mLookup_ProceduralTextureIDs[EProceduralTextures::CHECKERBOARD] = texID;
		mLookup_ProceduralTextureSRVs[EProceduralTextures::CHECKERBOARD] = this->AllocateAndInitializeSRV(texID);
		desc.pDataArray.pop_back();
	}
	{
		desc.TexName = "Checkerboard_Gray";
		std::vector<UINT8> texture = Texture::GenerateTexture_Checkerboard(sizeX, true);
		desc.pDataArray.push_back( texture.data() );
		TextureID texID = this->CreateTexture(desc, false);
		mLookup_ProceduralTextureIDs[EProceduralTextures::CHECKERBOARD_GRAYSCALE] = texID;
		mLookup_ProceduralTextureSRVs[EProceduralTextures::CHECKERBOARD_GRAYSCALE] = this->AllocateAndInitializeSRV(texID);
		desc.pDataArray.pop_back();
	}
	{
		desc.TexName = "IBL_BRDF_Integration";
		desc.ResourceState = D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		desc.d3d12Desc.Width  = 1024;
		desc.d3d12Desc.Height = 1024;
		desc.d3d12Desc.Format = DXGI_FORMAT_R16G16_FLOAT;
		desc.d3d12Desc.Flags = D3D12_RESOURCE_FLAGS::D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		TextureID texID = this->CreateTexture(desc, false);
		mLookup_ProceduralTextureIDs[EProceduralTextures::IBL_BRDF_INTEGRATION_LUT] = texID;
		mLookup_ProceduralTextureSRVs[EProceduralTextures::IBL_BRDF_INTEGRATION_LUT] = this->AllocateAndInitializeSRV(texID);
	}
}
void VQRenderer::UploadVertexAndIndexBufferHeaps()
{
	SCOPED_CPU_MARKER("UploadVertexAndIndexBufferHeaps");
	std::lock_guard<std::mutex> lk(mMtxTextureUploadQueue);
	
	mStaticHeap_VertexBuffer.UploadData(mHeapUpload.GetCommandList());
	mStaticHeap_IndexBuffer.UploadData(mHeapUpload.GetCommandList());
	mHeapUpload.UploadToGPUAndWait();
}


ID3D12PipelineState* VQRenderer::GetPSO(PSO_ID psoID) const
{
	assert(psoID >= EBuiltinPSOs::NUM_BUILTIN_PSOs);
	if (psoID == INVALID_ID) return nullptr;
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
	WaitForLoadCompletion();
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


void VQRenderer::PreFilterEnvironmentMap(ID3D12GraphicsCommandList* pCmd, FEnvironmentMapRenderingResources& env, const Mesh& CubeMesh, HWND hwnd)
{
	Log::Info("Environment Map: PreFilterEnvironmentMap");
	using namespace DirectX;

	FWindowRenderContext& ctx = this->GetWindowRenderContext(hwnd);
	DynamicBufferHeap& cbHeap = ctx.GetConstantBufferHeap(0);

	if (env.SRV_BRDFIntegrationLUT == INVALID_ID)
	{
		ComputeBRDFIntegrationLUT(pCmd, env.SRV_BRDFIntegrationLUT);
	}

	SCOPED_GPU_MARKER(pCmd, "RenderEnvironmentMapCubeFaces");

	constexpr int NUM_CUBE_FACES = 6;

	const SRV& srvEnv = this->GetSRV(env.SRV_HDREnvironment);
	const SRV& srvIrrDiffuse = this->GetSRV(env.SRV_IrradianceDiff);
	const SRV& srvIrrSpcecular = this->GetSRV(env.SRV_IrradianceSpec);

	const XMFLOAT4X4 f16proj = MakePerspectiveProjectionMatrix(PI_DIV2, 1.0f, 0.1f, 10.0f);
	const XMMATRIX proj = XMLoadFloat4x4(&f16proj);

	struct cb0_t { XMMATRIX viewProj[NUM_CUBE_FACES]; };
	struct cb1_t { float ViewDimX; float ViewDimY; float Roughness; int MIP; };

	// Diffuse Irradiance Convolution
	{
		Log::Info("Environment Map:   DiffuseIrradianceCubemap");
		SCOPED_GPU_MARKER(pCmd, "DiffuseIrradianceCubemap");

		// Since calculating the diffuse irradiance integral takes a long time,
		// we opt-in for drawing cube faces separately instead of doing all at one 
		// go using GS for RT slicing. Otherwise, a TDR can occur when drawing all
		// 6 faces per draw call as the integration takes long for each face.
		constexpr bool DRAW_CUBE_FACES_SEPARATELY = true;
		//------------------------------------------------------------------------

		const RTV& rtv = this->GetRTV(env.RTV_IrradianceDiff);

		// Viewport & Scissors
		int w, h, d;
		this->GetTextureDimensions(env.Tex_IrradianceDiff, w, h, d);
		D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f };
		D3D12_RECT     scissorsRect{ 0, 0, (LONG)w, (LONG)h };
		pCmd->RSSetViewports(1, &viewport);
		pCmd->RSSetScissorRects(1, &scissorsRect);

		// geometry input
		const auto VBIBIDs = CubeMesh.GetIABufferIDs();
		const uint32 NumIndices = CubeMesh.GetNumIndices();
		const BufferID& VB_ID = VBIBIDs.first;
		const BufferID& IB_ID = VBIBIDs.second;
		const VBV& vb = this->GetVertexBufferView(VB_ID);
		const IBV& ib = this->GetIndexBufferView(IB_ID);

		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmd->IASetVertexBuffers(0, 1, &vb);
		pCmd->IASetIndexBuffer(&ib);

		if constexpr (DRAW_CUBE_FACES_SEPARATELY)
		{
			pCmd->SetPipelineState(this->GetPSO(CUBEMAP_CONVOLUTION_DIFFUSE_PER_FACE_PSO));
			pCmd->SetGraphicsRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ConvolutionCubemap));
			pCmd->SetGraphicsRootDescriptorTable(2, srvEnv.GetGPUDescHandle());
			pCmd->SetGraphicsRootDescriptorTable(3, srvEnv.GetGPUDescHandle());

			for (int face = 0; face < NUM_CUBE_FACES; ++face)
			{
				D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtv.GetCPUDescHandle(face);

				D3D12_GPU_VIRTUAL_ADDRESS cbAddr0 = {};
				D3D12_GPU_VIRTUAL_ADDRESS cbAddr1 = {};
				cb0_t* pCB0 = {};
				cb1_t* pCB1 = {};
				cbHeap.AllocConstantBuffer(sizeof(cb0_t), (void**)(&pCB0), &cbAddr0);
				cbHeap.AllocConstantBuffer(sizeof(cb1_t), (void**)(&pCB1), &cbAddr1);
				pCB1->ViewDimX = static_cast<float>(w);
				pCB1->ViewDimY = static_cast<float>(h);
				pCB1->Roughness = 0.0f;
				pCB0->viewProj[0] = CubemapUtility::CalculateViewMatrix(face) * proj;

				pCmd->SetGraphicsRootConstantBufferView(1, cbAddr1);
				pCmd->SetGraphicsRootConstantBufferView(0, cbAddr0);
				pCmd->OMSetRenderTargets(1, &rtvHandle, TRUE, NULL);
				pCmd->DrawIndexedInstanced(NumIndices, 1, 0, 0, 0);
			}
		}

		else // Draw Instanced Cubes (1x instance / cube face) w/ GS RT slicing
		{
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtv.GetCPUDescHandle();

			D3D12_GPU_VIRTUAL_ADDRESS cbAddr0 = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr1 = {};
			cb0_t* pCB0 = {};
			cb1_t* pCB1 = {};
			cbHeap.AllocConstantBuffer(sizeof(cb0_t), (void**)(&pCB0), &cbAddr0);
			cbHeap.AllocConstantBuffer(sizeof(cb1_t), (void**)(&pCB1), &cbAddr1);
			pCB1->ViewDimX = static_cast<float>(w);
			pCB1->ViewDimY = static_cast<float>(h);
			pCB1->MIP = 0;
			pCB1->Roughness = 0.0f;
			for (int face = 0; face < NUM_CUBE_FACES; ++face)
			{
				pCB0->viewProj[face] = CubemapUtility::CalculateViewMatrix(face) * proj;
			}

			pCmd->SetPipelineState(this->GetPSO(CUBEMAP_CONVOLUTION_DIFFUSE_PSO));
			pCmd->SetGraphicsRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ConvolutionCubemap));
			pCmd->SetGraphicsRootDescriptorTable(2, srvEnv.GetGPUDescHandle());
			pCmd->SetGraphicsRootDescriptorTable(3, srvEnv.GetGPUDescHandle());
			pCmd->SetGraphicsRootConstantBufferView(0, cbAddr0);
			pCmd->SetGraphicsRootConstantBufferView(1, cbAddr1);
			pCmd->OMSetRenderTargets(1, &rtvHandle, TRUE, NULL);
			pCmd->DrawIndexedInstanced(NumIndices, NUM_CUBE_FACES, 0, 0, 0);
		}

		const CD3DX12_RESOURCE_BARRIER pBarriers[] =
		{
			  CD3DX12_RESOURCE_BARRIER::Transition(this->GetTextureResource(env.Tex_IrradianceDiff), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		};
		pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
	}

	// Blur Diffuse Irradiance
	for (int face = 0; face < NUM_CUBE_FACES; ++face)
	{
		std::string marker = "CubeFace[";
		marker += std::to_string(face);
		marker += "]";
		SCOPED_GPU_MARKER(pCmd, marker.c_str());


		// compute dispatch dimensions
		int InputImageWidth = 0;
		int InputImageHeight = 0;
		int InputImageNumSlices = 0;
		this->GetTextureDimensions(env.Tex_IrradianceDiff, InputImageWidth, InputImageHeight, InputImageNumSlices);
		assert(InputImageNumSlices == NUM_CUBE_FACES);

		const SRV& srv = this->GetSRV(env.SRV_IrradianceDiffFaces[face]);

		constexpr int DispatchGroupDimensionX = 8;
		constexpr int DispatchGroupDimensionY = 8;
		const     int DispatchX = (InputImageWidth + (DispatchGroupDimensionX - 1)) / DispatchGroupDimensionX;
		const     int DispatchY = (InputImageHeight + (DispatchGroupDimensionY - 1)) / DispatchGroupDimensionY;
		constexpr int DispatchZ = 1;

		const UAV& uav_BlurIntermediate = this->GetUAV(env.UAV_BlurTemp);
		const UAV& uav_BlurOutput = this->GetUAV(env.UAV_IrradianceDiffBlurred);
		const SRV& srv_BlurIntermediate = this->GetSRV(env.SRV_BlurTemp);
		ID3D12Resource* pRscBlurIntermediate = this->GetTextureResource(env.Tex_BlurTemp);
		ID3D12Resource* pRscBlurOutput = this->GetTextureResource(env.Tex_IrradianceDiffBlurred);

		struct FBlurParams { int iImageSizeX; int iImageSizeY; };
		FBlurParams* pBlurParams = nullptr;

		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		cbHeap.AllocConstantBuffer(sizeof(FBlurParams), (void**)&pBlurParams, &cbAddr);
		pBlurParams->iImageSizeX = InputImageWidth;
		pBlurParams->iImageSizeY = InputImageHeight;

		{
			SCOPED_GPU_MARKER(pCmd, "BlurX");
			pCmd->SetPipelineState(this->GetPSO(EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_X_PSO));
			pCmd->SetComputeRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1));

			pCmd->SetComputeRootDescriptorTable(0, srv.GetGPUDescHandle());
			pCmd->SetComputeRootDescriptorTable(1, uav_BlurIntermediate.GetGPUDescHandle());
			pCmd->SetComputeRootConstantBufferView(2, cbAddr);
			pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);

			const CD3DX12_RESOURCE_BARRIER pBarriers[] =
			{
				  CD3DX12_RESOURCE_BARRIER::Transition(pRscBlurIntermediate, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
			};
			pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
		}
		{
			SCOPED_GPU_MARKER(pCmd, "BlurY");
			pCmd->SetPipelineState(this->GetPSO(EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_Y_PSO));
			pCmd->SetComputeRootDescriptorTable(0, srv_BlurIntermediate.GetGPUDescHandle());
			pCmd->SetComputeRootDescriptorTable(1, uav_BlurOutput.GetGPUDescHandle(face));
			pCmd->SetComputeRootConstantBufferView(2, cbAddr);
			pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);

			const CD3DX12_RESOURCE_BARRIER pBarriers[] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(pRscBlurIntermediate, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
			};

			if (face != NUM_CUBE_FACES - 1) // skip the last barrier as its not necessary (also causes DXGI error)
				pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
		}
	} // for_each face

	// transition blurred diffuse irradiance map resource
	{
		const CD3DX12_RESOURCE_BARRIER pBarriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(this->GetTextureResource(env.Tex_IrradianceDiffBlurred), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		  , CD3DX12_RESOURCE_BARRIER::Transition(this->GetTextureResource(env.Tex_IrradianceDiff), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		};
		pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
	}


	// Specular Irradiance
	if constexpr (true)
	{
		Log::Info("Environment Map:   SpecularIrradianceCubemap");
		SCOPED_GPU_MARKER(pCmd, "SpecularIrradianceCubemap");

		pCmd->SetPipelineState(this->GetPSO(CUBEMAP_CONVOLUTION_SPECULAR_PSO));
		pCmd->SetGraphicsRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ConvolutionCubemap));
		pCmd->SetGraphicsRootDescriptorTable(2, srvEnv.GetGPUDescHandle());
		pCmd->SetGraphicsRootDescriptorTable(3, srvIrrDiffuse.GetGPUDescHandle());

		int w, h, d, MIP_LEVELS;
		this->GetTextureDimensions(env.Tex_IrradianceSpec, w, h, d, MIP_LEVELS);

		int inpTexW, inpTexH;
		this->GetTextureDimensions(env.Tex_HDREnvironment, inpTexW, inpTexH);

		for (int mip = 0; mip < MIP_LEVELS; ++mip)
		{
			for (int face = 0; face < NUM_CUBE_FACES; ++face)
			{
				std::string marker = "CubeFace["; marker += std::to_string(face); marker += "]";
				marker += ", MIP=" + std::to_string(mip);
				SCOPED_GPU_MARKER(pCmd, marker.c_str());

				const RTV& rtv = this->GetRTV(env.RTV_IrradianceSpec);
				D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtv.GetCPUDescHandle(mip * 6 + face);

				D3D12_GPU_VIRTUAL_ADDRESS cbAddr0 = {};
				D3D12_GPU_VIRTUAL_ADDRESS cbAddr1 = {};
				cb0_t* pCB0 = {};
				cb1_t* pCB1 = {};
				cbHeap.AllocConstantBuffer(sizeof(cb0_t), (void**)(&pCB0), &cbAddr0);
				cbHeap.AllocConstantBuffer(sizeof(cb1_t), (void**)(&pCB1), &cbAddr1);

				pCB0->viewProj[0] = CubemapUtility::CalculateViewMatrix(face) * proj;
				pCB1->Roughness = static_cast<float>(mip) / (MIP_LEVELS - 1); // min(0.04, ) ?
				pCB1->ViewDimX = static_cast<float>(inpTexW);
				pCB1->ViewDimY = static_cast<float>(inpTexH);
				pCB1->MIP = mip;

				assert(pCB1->ViewDimX > 0);
				assert(pCB1->ViewDimY > 0);

				pCmd->SetGraphicsRootConstantBufferView(0, cbAddr0);
				pCmd->SetGraphicsRootConstantBufferView(1, cbAddr1);

				LONG Viewport[2] = {};
				Viewport[0] = w >> mip;
				Viewport[1] = h >> mip;
				D3D12_VIEWPORT viewport{ 0.0f, 0.0f, (FLOAT)Viewport[0], (FLOAT)Viewport[1], 0.0f, 1.0f };
				D3D12_RECT     scissorsRect{ 0, 0, Viewport[0], Viewport[1] };
				pCmd->RSSetViewports(1, &viewport);
				pCmd->RSSetScissorRects(1, &scissorsRect);

				const auto VBIBIDs = CubeMesh.GetIABufferIDs();
				const uint32 NumIndices = CubeMesh.GetNumIndices();
				const BufferID& VB_ID = VBIBIDs.first;
				const BufferID& IB_ID = VBIBIDs.second;
				const VBV& vb = this->GetVertexBufferView(VB_ID);
				const IBV& ib = this->GetIndexBufferView(IB_ID);

				pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				pCmd->IASetVertexBuffers(0, 1, &vb);
				pCmd->IASetIndexBuffer(&ib);

				pCmd->OMSetRenderTargets(1, &rtvHandle, TRUE, NULL);
				pCmd->DrawIndexedInstanced(NumIndices, 1, 0, 0, 0);
			}
		}

		const CD3DX12_RESOURCE_BARRIER pBarriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(this->GetTextureResource(env.Tex_IrradianceSpec), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		};
		pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
	}
}

void VQRenderer::ComputeBRDFIntegrationLUT(ID3D12GraphicsCommandList* pCmd, SRV_ID& outSRV_ID)
{
	Log::Info("Environment Map:   ComputeBRDFIntegrationLUT");
	SCOPED_GPU_MARKER(pCmd, "CreateBRDFIntegralLUT");

	// Texture resource is created (on Renderer::LoadDefaultResources()) but not initialized at this point.
	const TextureID TexBRDFLUT = this->GetProceduralTexture(EProceduralTextures::IBL_BRDF_INTEGRATION_LUT);
	ID3D12Resource* pRscBRDFLUT = this->GetTextureResource(TexBRDFLUT);

	int W, H;
	this->GetTextureDimensions(TexBRDFLUT, W, H);

	// Create & Initialize a UAV for the LUT 
	UAV_ID uavBRDFLUT_ID = this->AllocateUAV();
	this->InitializeUAV(uavBRDFLUT_ID, 0, TexBRDFLUT);
	const UAV& uavBRDFLUT = this->GetUAV(uavBRDFLUT_ID);

	// Dispatch
	pCmd->SetPipelineState(this->GetPSO(EBuiltinPSOs::BRDF_INTEGRATION_CS_PSO));
	pCmd->SetComputeRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__BRDFIntegrationCS));
	pCmd->SetComputeRootDescriptorTable(0, uavBRDFLUT.GetGPUDescHandle());

	constexpr int THREAD_GROUP_X = 8;
	constexpr int THREAD_GROUP_Y = 8;
	const int DISPATCH_DIMENSION_X = (W + (THREAD_GROUP_X - 1)) / THREAD_GROUP_X;
	const int DISPATCH_DIMENSION_Y = (H + (THREAD_GROUP_Y - 1)) / THREAD_GROUP_Y;
	constexpr int DISPATCH_DIMENSION_Z = 1;
	pCmd->Dispatch(DISPATCH_DIMENSION_X, DISPATCH_DIMENSION_Y, DISPATCH_DIMENSION_Z);

	const CD3DX12_RESOURCE_BARRIER pBarriers[] =
	{
		  CD3DX12_RESOURCE_BARRIER::Transition(pRscBRDFLUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
	};
	pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);

	outSRV_ID = this->GetProceduralTextureSRV_ID(EProceduralTextures::IBL_BRDF_INTEGRATION_LUT);
}

