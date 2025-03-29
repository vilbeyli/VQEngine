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
#include "Resources/CubemapUtility.h"
#include "Pipeline/Shader.h"
#include "Rendering/WindowRenderContext.h"

#include "Rendering/RenderPass/AmbientOcclusion.h"
#include "Rendering/RenderPass/DepthPrePass.h"
#include "Rendering/RenderPass/DepthMSAAResolve.h"
#include "Rendering/RenderPass/ScreenSpaceReflections.h"
#include "Rendering/RenderPass/ApplyReflections.h"
#include "Rendering/RenderPass/MagnifierPass.h"
#include "Rendering/RenderPass/ObjectIDPass.h"
#include "Rendering/RenderPass/OutlinePass.h"

#include "Engine/Core/Window.h"
#include "Engine/Core/Platform.h"
#include "Engine/GPUMarker.h"
#include "Engine/EnvironmentMap.h"
#include "Engine/Math.h"
#include "Engine/Scene/Mesh.h"
#include "Shaders/LightingConstantBufferData.h"

#include "Libs/VQUtils/Source/Log.h"
#include "Libs/VQUtils/Source/utils.h"
#include "Libs/VQUtils/Source/Timer.h"
#include "Libs/D3D12MA/src/D3D12MemAlloc.h"

#include <cassert>
#include <atomic>
#include <memory>

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

	// allocate render passes
	{
		SCOPED_CPU_MARKER("AllocRenderPasses");
		mRenderPasses.resize(NUM_RENDER_PASSES, nullptr);
		mRenderPasses[ERenderPass::AmbientOcclusion      ] = std::make_shared<AmbientOcclusionPass>(*this, AmbientOcclusionPass::EMethod::FFX_CACAO);
		mRenderPasses[ERenderPass::ZPrePass              ] = std::make_shared<DepthPrePass>(*this);
		mRenderPasses[ERenderPass::DepthMSAAResolve      ] = std::make_shared<DepthMSAAResolvePass>(*this);
		mRenderPasses[ERenderPass::ApplyReflections      ] = std::make_shared<ApplyReflectionsPass>(*this);
		mRenderPasses[ERenderPass::Magnifier             ] = std::make_shared<MagnifierPass>(*this, true); // true: outputs to swapchain
		mRenderPasses[ERenderPass::ObjectID              ] = std::make_shared<ObjectIDPass>(*this);
		mRenderPasses[ERenderPass::ScreenSpaceReflections] = std::make_shared<ScreenSpaceReflectionsPass>(*this);
		mRenderPasses[ERenderPass::Outline               ] = std::make_shared<OutlinePass>(*this);
	}

	mNumFramesRendered = 0;

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	mFrameSceneDrawData.resize(NumFrameBuffers);
	mFrameShadowDrawData.resize(NumFrameBuffers);
#else
	mFrameSceneDrawData.resize(1);
	//mFrameShadowDrawData.resize(1); // TODO
#endif

	Log::Info("[Renderer] Initialized.");
}

static void AllocateDescriptors(FRenderingResources_MainWindow& rsc, VQRenderer& mRenderer)
{
	SCOPED_CPU_MARKER("AllocateDescriptors");
	
	// null cubemap SRV
	{
		rsc.SRV_NullCubemap = mRenderer.AllocateSRV();
		rsc.SRV_NullTexture2D = mRenderer.AllocateSRV();
	}

	// depth pre pass
	{
		rsc.DSV_SceneDepthMSAA = mRenderer.AllocateDSV();
		rsc.DSV_SceneDepth = mRenderer.AllocateDSV();
		rsc.UAV_SceneDepth = mRenderer.AllocateUAV();
		rsc.UAV_SceneColor = mRenderer.AllocateUAV();
		rsc.UAV_SceneNormals = mRenderer.AllocateUAV();
		rsc.RTV_SceneNormals = mRenderer.AllocateRTV();
		rsc.RTV_SceneNormalsMSAA = mRenderer.AllocateRTV();
		rsc.SRV_SceneNormals = mRenderer.AllocateSRV();
		rsc.SRV_SceneNormalsMSAA = mRenderer.AllocateSRV();
		rsc.SRV_SceneDepthMSAA = mRenderer.AllocateSRV();
		rsc.SRV_SceneDepth = mRenderer.AllocateSRV();
	}

	// shadow map passes
	{
		rsc.DSV_ShadowMaps_Spot = mRenderer.AllocateDSV(NUM_SHADOWING_LIGHTS__SPOT);
		rsc.SRV_ShadowMaps_Spot = mRenderer.AllocateSRV();
		rsc.DSV_ShadowMaps_Point = mRenderer.AllocateDSV(NUM_SHADOWING_LIGHTS__POINT * 6);
		rsc.SRV_ShadowMaps_Point = mRenderer.AllocateSRV();
		rsc.DSV_ShadowMaps_Directional = mRenderer.AllocateDSV();
		rsc.SRV_ShadowMaps_Directional = mRenderer.AllocateSRV();
	}

	// scene color pass
	{
		rsc.RTV_SceneColorMSAA = mRenderer.AllocateRTV();
		rsc.RTV_SceneColor = mRenderer.AllocateRTV();
		rsc.RTV_SceneColorBoundingVolumes = mRenderer.AllocateRTV();
		rsc.RTV_SceneVisualization = mRenderer.AllocateRTV();
		rsc.RTV_SceneVisualizationMSAA = mRenderer.AllocateRTV();
		rsc.RTV_SceneMotionVectors = mRenderer.AllocateRTV();
		rsc.RTV_SceneMotionVectorsMSAA = mRenderer.AllocateRTV();
		rsc.SRV_SceneColor = mRenderer.AllocateSRV();
		rsc.SRV_SceneColorMSAA = mRenderer.AllocateSRV();
		rsc.SRV_SceneColorBoundingVolumes = mRenderer.AllocateSRV();
		rsc.SRV_SceneVisualization = mRenderer.AllocateSRV();
		rsc.SRV_SceneMotionVectors = mRenderer.AllocateSRV();
		rsc.SRV_SceneVisualizationMSAA = mRenderer.AllocateSRV();
	}

	// reflection passes
	{
		rsc.UAV_DownsampledSceneDepth = mRenderer.AllocateUAV(13);
		rsc.UAV_DownsampledSceneDepthAtomicCounter = mRenderer.AllocateUAV(1);
	}

	// ambient occlusion pass
	{
		rsc.UAV_FFXCACAO_Out = mRenderer.AllocateUAV();
		rsc.SRV_FFXCACAO_Out = mRenderer.AllocateSRV();
	}

	// post process pass
	{
		rsc.UAV_PostProcess_TonemapperOut = mRenderer.AllocateUAV();
		rsc.UAV_PostProcess_VisualizationOut = mRenderer.AllocateUAV();
		rsc.UAV_PostProcess_BlurIntermediate = mRenderer.AllocateUAV();
		rsc.UAV_PostProcess_BlurOutput = mRenderer.AllocateUAV();
		rsc.UAV_PostProcess_FFXCASOut = mRenderer.AllocateUAV();
		rsc.UAV_PostProcess_FSR_EASUOut = mRenderer.AllocateUAV();
		rsc.UAV_PostProcess_FSR_RCASOut = mRenderer.AllocateUAV();

		rsc.SRV_PostProcess_TonemapperOut = mRenderer.AllocateSRV();
		rsc.SRV_PostProcess_VisualizationOut = mRenderer.AllocateSRV();
		rsc.SRV_PostProcess_BlurIntermediate = mRenderer.AllocateSRV();
		rsc.SRV_PostProcess_BlurOutput = mRenderer.AllocateSRV();
		rsc.SRV_PostProcess_FFXCASOut = mRenderer.AllocateSRV();
		rsc.SRV_PostProcess_FSR_EASUOut = mRenderer.AllocateSRV();
		rsc.SRV_PostProcess_FSR_RCASOut = mRenderer.AllocateSRV();
	}

	// UI HDR pass
	{
		rsc.RTV_UI_SDR = mRenderer.AllocateRTV();
		rsc.SRV_UI_SDR = mRenderer.AllocateSRV();
	}
}

static void CreateResourceViews(FRenderingResources_MainWindow& rsc, VQRenderer& mRenderer)
{
	SCOPED_CPU_MARKER("CreateResourceViews");
	// null cubemap SRV
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC nullSRVDesc = {};
		nullSRVDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		nullSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		nullSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		nullSRVDesc.TextureCube.MipLevels = 1;
		mRenderer.InitializeSRV(rsc.SRV_NullCubemap, 0, nullSRVDesc);

		nullSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		mRenderer.InitializeSRV(rsc.SRV_NullTexture2D, 0, nullSRVDesc);
	}

	// reflection passes
	{
		SCOPED_CPU_MARKER("Reflection");
		TextureCreateDesc desc("DownsampledSceneDepthAtomicCounter");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Buffer(4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		desc.ResourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		rsc.Tex_DownsampledSceneDepthAtomicCounter = mRenderer.CreateTexture(desc);
		mRenderer.InitializeUAVForBuffer(rsc.UAV_DownsampledSceneDepthAtomicCounter, 0u, rsc.Tex_DownsampledSceneDepthAtomicCounter, DXGI_FORMAT_R32_UINT);
	}

	// shadow map passes
	{
		SCOPED_CPU_MARKER("ShadowMaps");
		TextureCreateDesc desc("ShadowMaps_Spot");
		// initialize texture memory
		constexpr UINT SHADOW_MAP_DIMENSION_SPOT = 1024;
		constexpr UINT SHADOW_MAP_DIMENSION_POINT = 1024;
		constexpr UINT SHADOW_MAP_DIMENSION_DIRECTIONAL = 2048;

		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R32_TYPELESS
			, SHADOW_MAP_DIMENSION_SPOT
			, SHADOW_MAP_DIMENSION_SPOT
			, NUM_SHADOWING_LIGHTS__SPOT // Array Size
			, 1 // MIP levels
			, 1 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
		);
		desc.ResourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		rsc.Tex_ShadowMaps_Spot = mRenderer.CreateTexture(desc);

		desc.d3d12Desc.DepthOrArraySize = NUM_SHADOWING_LIGHTS__POINT * 6;
		desc.d3d12Desc.Width = SHADOW_MAP_DIMENSION_POINT;
		desc.d3d12Desc.Height = SHADOW_MAP_DIMENSION_POINT;
		desc.TexName = "ShadowMaps_Point";
		desc.bCubemap = true;
		rsc.Tex_ShadowMaps_Point = mRenderer.CreateTexture(desc);


		desc.d3d12Desc.Width = SHADOW_MAP_DIMENSION_DIRECTIONAL;
		desc.d3d12Desc.Height = SHADOW_MAP_DIMENSION_DIRECTIONAL;
		desc.d3d12Desc.DepthOrArraySize = 1;
		desc.bCubemap = false;
		desc.TexName = "ShadowMap_Directional";
		rsc.Tex_ShadowMaps_Directional = mRenderer.CreateTexture(desc);

		// initialize DSVs
		for (int i = 0; i < NUM_SHADOWING_LIGHTS__SPOT; ++i)      mRenderer.InitializeDSV(rsc.DSV_ShadowMaps_Spot, i, rsc.Tex_ShadowMaps_Spot, i);
		for (int i = 0; i < NUM_SHADOWING_LIGHTS__POINT * 6; ++i) mRenderer.InitializeDSV(rsc.DSV_ShadowMaps_Point, i, rsc.Tex_ShadowMaps_Point, i);
		mRenderer.InitializeDSV(rsc.DSV_ShadowMaps_Directional, 0, rsc.Tex_ShadowMaps_Directional);

		// initialize SRVs
		mRenderer.InitializeSRV(rsc.SRV_ShadowMaps_Spot, 0, rsc.Tex_ShadowMaps_Spot, true);
		mRenderer.InitializeSRV(rsc.SRV_ShadowMaps_Point, 0, rsc.Tex_ShadowMaps_Point, true, true);
		mRenderer.InitializeSRV(rsc.SRV_ShadowMaps_Directional, 0, rsc.Tex_ShadowMaps_Directional);
	}
}

void VQRenderer::Load()
{
	SCOPED_CPU_MARKER("Renderer.Load()");
	Timer timer; timer.Start();
	Log::Info("[Renderer] Loading...");
	
	LoadBuiltinRootSignatures();
	const float tRS = timer.Tick();
	Log::Info("[Renderer]    RootSignatures=%.2fs", tRS);

	LoadDefaultResources();
	const float tDefaultRscs = timer.Tick();
	Log::Info("[Renderer]    DefaultRscs=%.2fs", tDefaultRscs);

	AllocateDescriptors(mResources_MainWnd, *this);
	CreateResourceViews(mResources_MainWnd, *this);
	mbDefaultResourcesLoaded.store(true);
	const float tRenderRscs = timer.Tick();
	Log::Info("[Renderer]    RenderRscs=%.2fs", tDefaultRscs);

	{
		SCOPED_CPU_MARKER("InitRenderPasses");
		for (std::shared_ptr<IRenderPass>& pPass : mRenderPasses)
			pPass->Initialize();
	}
	const float tRenderPasses = timer.Tick();
	Log::Info("[Renderer]    RenderPasses=%.2fs", tRenderPasses);

	const float total = tRS + tDefaultRscs + tRenderRscs + tRenderPasses;
	Log::Info("[Renderer] Loaded in %.2fs.", total);
}

void VQRenderer::Unload()
{
	// todo: mirror Load() functions?
}


void VQRenderer::Destroy()
{
	Log::Info("VQRenderer::Exit()");

	for (std::shared_ptr<IRenderPass>& pPass : mRenderPasses)
	{
		pPass->Destroy();
	}

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
	if (!CheckContext(hwnd)) 
		return;

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
	this->mbMainSwapchainInitialized.store(true); // TODO: this should execute only for main window
}

void VQRenderer::InitializeFences(HWND hwnd)
{
	SCOPED_CPU_MARKER("InitQueueFences");
	ID3D12Device* pDevice = this->GetDevicePtr();
	const int NumBackBuffers = this->GetSwapChainBackBufferCount(hwnd);

	mAsyncComputeSSAOReadyFence.resize(NumBackBuffers);
	mAsyncComputeSSAODoneFence.resize(NumBackBuffers);
	mCopyObjIDDoneFence.resize(NumBackBuffers);
	for (int i = 0; i < NumBackBuffers; ++i)
	{
		mAsyncComputeSSAOReadyFence[i].Create(pDevice, "AsyncComputeSSAOReadyFence");
		mAsyncComputeSSAODoneFence[i].Create(pDevice, "AsyncComputeSSAODoneFence");
		mCopyObjIDDoneFence[i].Create(pDevice, "CopyObjIDDoneFence");
	}
}

void VQRenderer::DestroyFences(HWND hwnd)
{
	const int NumBackBuffers = this->GetSwapChainBackBufferCount(hwnd);
	for (int i = 0; i < NumBackBuffers; ++i)
	{
		mCopyObjIDDoneFence[i].Destroy();
		mAsyncComputeSSAOReadyFence[i].Destroy();
		mAsyncComputeSSAODoneFence[i].Destroy();
	}

}

void VQRenderer::WaitCopyFenceOnCPU(HWND hwnd)

{
	SCOPED_CPU_MARKER_C("WAIT_COPY_Q", 0xFFFF0000);
	const int BACK_BUFFER_INDEX = this->GetWindowRenderContext(hwnd).GetCurrentSwapchainBufferIndex();
	Fence& CopyFence = this->mCopyObjIDDoneFence[BACK_BUFFER_INDEX];
	CopyFence.WaitOnCPU(CopyFence.GetValue());
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
