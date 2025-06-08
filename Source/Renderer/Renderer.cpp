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
#include "Core/Common.h"
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
#if _DEBUG
std::string VQRenderer::PSOCacheDirectory = "Cache/PSOs/Debug";
#else
std::string VQRenderer::PSOCacheDirectory    = "Cache/PSOs";
#endif
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

D3D12_COMMAND_LIST_TYPE VQRenderer::GetDX12CmdListType(ECommandQueueType type)
{
	switch (type)
	{
	case ECommandQueueType::GFX    : return D3D12_COMMAND_LIST_TYPE_DIRECT;
	case ECommandQueueType::COMPUTE: return D3D12_COMMAND_LIST_TYPE_COMPUTE;
	case ECommandQueueType::COPY   : return D3D12_COMMAND_LIST_TYPE_COPY;
	}
	assert(false);
	return D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE; // shouldnt happen
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
	SCOPED_CPU_MARKER("Renderer.Initialize");

	mTextureManager.InitializeEarly();

	// initialize threads
	{
		SCOPED_CPU_MARKER("Threads");
		
		// TODO:
		//mFrameSubmitThread = std::thread(&VQRenderer::FrameSubmitThread_Main, this);
		//SetThreadDescription(mFrameSubmitThread.native_handle(), L"FrameSubmitThread");
		
		const size_t HWThreads = ThreadPool::sHardwareThreadCount;
		const size_t HWCores = HWThreads >> 1;
		mWorkers_ShaderLoad.Initialize(HWCores, "ShaderLoadWorkers");
		mWorkers_PSOLoad.Initialize(HWCores, "PSOLoadWorkers");
	}

	// create PSO & Shader cache folders
	DirectoryUtil::CreateFolderIfItDoesntExist(VQRenderer::ShaderCacheDirectory);
	DirectoryUtil::CreateFolderIfItDoesntExist(VQRenderer::PSOCacheDirectory);

	// Create the device
	{
		SCOPED_CPU_MARKER("Device");
		FDeviceCreateDesc deviceDesc = {};
		deviceDesc.bEnableDebugLayer = ENABLE_DEBUG_LAYER;
		deviceDesc.bEnableGPUValidationLayer = ENABLE_VALIDATION_LAYER;
		const bool bDeviceCreateSucceeded = mDevice.Create(deviceDesc);
		assert(bDeviceCreateSucceeded);

		mLatchDeviceInitialized.count_down();
	}

	const int NumSwapchainBuffers = Settings.bUseTripleBuffering ? 3 : 2;
	ID3D12Device* pDevice = mDevice.GetDevicePtr();

	// Create Command Queues of different types
	{
		SCOPED_CPU_MARKER("CmdQ");
		
		mRenderingCmdQueues[GFX].Create(&mDevice, GFX); SetName(mRenderingCmdQueues[GFX].pQueue, "Rendering GFX Q");
		mRenderingCmdQueues[COPY].Create(&mDevice, COPY); SetName(mRenderingCmdQueues[COPY].pQueue, "Rendering CPY Q");
		mRenderingCmdQueues[COMPUTE].Create(&mDevice, COMPUTE); SetName(mRenderingCmdQueues[COMPUTE].pQueue, "Rendering CMP Q");

		mBackgroundTaskCmdQueues[GFX].Create(&mDevice, GFX); SetName(mBackgroundTaskCmdQueues[GFX].pQueue, "BackgroundTask GFX Q");
		mBackgroundTaskCmdQueues[COPY].Create(&mDevice, COPY); SetName(mBackgroundTaskCmdQueues[COPY].pQueue, "BackgroundTask GFX Q");
		mBackgroundTaskCmdQueues[COMPUTE].Create(&mDevice, COMPUTE); SetName(mBackgroundTaskCmdQueues[COMPUTE].pQueue, "BackgroundTask GFX Q");
		
		mLatchCmdQueuesInitialized.count_down();
	}
	{
		SCOPED_CPU_MARKER("CmdAllocators");
		for (int q = 0; q < ECommandQueueType::NUM_COMMAND_QUEUE_TYPES; ++q)
		{
			D3D12_COMMAND_LIST_TYPE t = GetDX12CmdListType((ECommandQueueType)q);

			// background task command allocators
			for(int thr = 0; thr < EBackgroungTaskThread::NUM_BACKGROUND_TASK_THREADS; ++thr)
			{
				pDevice->CreateCommandAllocator(t, IID_PPV_ARGS(&this->mBackgroundTaskCommandAllocators[q][thr]));
				SetName(mBackgroundTaskCommandAllocators[q][thr], "BackgroundTaskCmdAlloc[%d][%d]", q, thr);
			}

			// rendering command allocators
			mRenderingCommandAllocators[q].resize(NumSwapchainBuffers); // allocate per-backbuffer containers
			auto fnGetCmdAllocName = [q](int b) -> std::string
			{
				std::string name = "RenderCmdAlloc";
				switch (q)
				{
				case ECommandQueueType::GFX:    name += "GFX"; break;
				case ECommandQueueType::COPY:   name += "Copy"; break;
				case ECommandQueueType::COMPUTE:name += "Compute"; break;
				}
				name += "[" + std::to_string(b) + "][0]";
				return name;
			};
			for (int b = 0; b < NumSwapchainBuffers; ++b)
			{
				mRenderingCommandAllocators[q][b].resize(1); // make at least one command allocator and command ready for each kind of queue, per back buffer
				
				pDevice->CreateCommandAllocator(t, IID_PPV_ARGS(&this->mRenderingCommandAllocators[q][b][0]));
				SetName(mRenderingCommandAllocators[q][b][0], fnGetCmdAllocName(b).c_str());
			}
		}
	}
	{
		SCOPED_CPU_MARKER("CmdLists");

		// create 1 command list for each type
		#if 0
		// TODO: device4 create command list1 doesn't require command allocator, figure out if a further refactor is needed.
		// https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nn-d3d12-id3d12device4
		auto pDevice4 = pVQDevice->GetDevice4Ptr();
		pDevice4->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, this->mRenderingCommandAllocators[ECommandQueueType::GFX][b][b][0]);
		#else	
		for (int q = 0; q < ECommandQueueType::NUM_COMMAND_QUEUE_TYPES; ++q)
		{
			D3D12_COMMAND_LIST_TYPE t = GetDX12CmdListType((ECommandQueueType)q);
			mpRenderingCmds[q].resize(NumSwapchainBuffers);
			mCmdClosed[q].resize(NumSwapchainBuffers);
			for (int b = 0; b < NumSwapchainBuffers; ++b)
			{
				mpRenderingCmds[q][b].resize(1);
				mCmdClosed[q][b].resize(1);
				pDevice->CreateCommandList(0, t, this->mRenderingCommandAllocators[q][b][0], nullptr, IID_PPV_ARGS(&this->mpRenderingCmds[q][b][0]));
				static_cast<ID3D12GraphicsCommandList*>(this->mpRenderingCmds[q][b][0])->Close();
			}
			for (int thr = 0; thr < EBackgroungTaskThread::NUM_BACKGROUND_TASK_THREADS; ++thr)
			{
				pDevice->CreateCommandList(0, t, this->mBackgroundTaskCommandAllocators[q][thr], nullptr, IID_PPV_ARGS(&this->mpBackgroundTaskCmds[q][thr]));
				static_cast<ID3D12GraphicsCommandList*>(this->mpBackgroundTaskCmds[q][thr])->Close();
			}
		}
		#endif

		// create 1 constant buffer
		mDynamicHeap_RenderingConstantBuffer.resize(1);
		mDynamicHeap_RenderingConstantBuffer[0].Create(pDevice, NumSwapchainBuffers, 64 * MEGABYTE);

		for (int thr = 0; thr < EBackgroungTaskThread::NUM_BACKGROUND_TASK_THREADS; ++thr)
		{
			mDynamicHeap_BackgroundTaskConstantBuffer[thr].Create(pDevice, 1, 4 * MEGABYTE);
		}
	}

	// Initialize memory
	InitializeD3D12MA(mpAllocator, mDevice);

	mLatchMemoryAllocatorInitialized.count_down();
	{
		SCOPED_CPU_MARKER("Heaps");
		ID3D12Device* pDevice = mDevice.GetDevicePtr();

		const uint32 UPLOAD_HEAP_SIZE = (512 + 256 + 128) * MEGABYTE; // TODO: from RendererSettings.ini
		mHeapUpload.Create(pDevice, UPLOAD_HEAP_SIZE, this->mRenderingCmdQueues[ECommandQueueType::GFX].pQueue);

		{
			SCOPED_CPU_MARKER("CBV_SRV_UAV");
			constexpr uint32 NumDescsCBV = 100;
			constexpr uint32 NumDescsSRV = 8192;
			constexpr uint32 NumDescsUAV = 100;
			constexpr bool   bCPUVisible = false;
			mHeapCBV_SRV_UAV.Create(pDevice, "HeapCBV_SRV_UAV", EResourceHeapType::CBV_SRV_UAV_HEAP, NumDescsCBV + NumDescsSRV + NumDescsUAV, bCPUVisible);
		}
		{
			SCOPED_CPU_MARKER("DSV");
			constexpr uint32 NumDescsDSV = 100;
			mHeapDSV.Create(pDevice, "HeapDSV", EResourceHeapType::DSV_HEAP, NumDescsDSV);
		}
		{
			SCOPED_CPU_MARKER("RTV");
			constexpr uint32 NumDescsRTV = 1000;
			mHeapRTV.Create(pDevice, "HeapRTV", EResourceHeapType::RTV_HEAP, NumDescsRTV);
		}

		constexpr uint32 STATIC_GEOMETRY_MEMORY_SIZE = 256 * MEGABYTE;
		constexpr bool USE_GPU_MEMORY = true;
		{
			SCOPED_CPU_MARKER("VB");
			mStaticHeap_VertexBuffer.Create(pDevice, EBufferType::VERTEX_BUFFER, STATIC_GEOMETRY_MEMORY_SIZE, USE_GPU_MEMORY, "VQRenderer::mStaticVertexBufferPool");
		}
		{
			SCOPED_CPU_MARKER("IB");
			mStaticHeap_IndexBuffer.Create(pDevice, EBufferType::INDEX_BUFFER, STATIC_GEOMETRY_MEMORY_SIZE, USE_GPU_MEMORY, "VQRenderer::mStaticIndexBufferPool");
		}
		mLatchHeapsInitialized.count_down();
	}

	mTextureManager.InitializeLate(
		mDevice.GetDevicePtr(),
		mpAllocator,
		mRenderingCmdQueues[ECommandQueueType::COPY].pQueue,
		mHeapUpload,
		mMtxUploadHeap
	);

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
		rsc.SRV_ShadowMaps_Point = mRenderer.AllocateSRV(NUM_SHADOWING_LIGHTS__POINT);
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
		FTextureRequest desc("DownsampledSceneDepthAtomicCounter");
		desc.D3D12Desc = CD3DX12_RESOURCE_DESC::Buffer(4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		desc.InitialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		rsc.Tex_DownsampledSceneDepthAtomicCounter = mRenderer.CreateTexture(desc);
		mRenderer.InitializeUAVForBuffer(rsc.UAV_DownsampledSceneDepthAtomicCounter, 0u, rsc.Tex_DownsampledSceneDepthAtomicCounter, DXGI_FORMAT_R32_UINT);
	}

	// shadow map passes
	{
		SCOPED_CPU_MARKER("ShadowMaps");
		FTextureRequest desc("ShadowMaps_Spot");
		// initialize texture memory
		constexpr UINT SHADOW_MAP_DIMENSION_SPOT = 1024;
		constexpr UINT SHADOW_MAP_DIMENSION_POINT = 1024;
		constexpr UINT SHADOW_MAP_DIMENSION_DIRECTIONAL = 2048;

		desc.D3D12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R32_TYPELESS
			, SHADOW_MAP_DIMENSION_SPOT
			, SHADOW_MAP_DIMENSION_SPOT
			, NUM_SHADOWING_LIGHTS__SPOT // Array Size
			, 1 // MIP levels
			, 1 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
		);
		desc.InitialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		rsc.Tex_ShadowMaps_Spot = mRenderer.CreateTexture(desc);

		desc.D3D12Desc.DepthOrArraySize = NUM_SHADOWING_LIGHTS__POINT * 6;
		desc.D3D12Desc.Width = SHADOW_MAP_DIMENSION_POINT;
		desc.D3D12Desc.Height = SHADOW_MAP_DIMENSION_POINT;
		desc.Name = "ShadowMaps_Point";
		desc.bCubemap = true;
		rsc.Tex_ShadowMaps_Point = mRenderer.CreateTexture(desc);


		desc.D3D12Desc.Width = SHADOW_MAP_DIMENSION_DIRECTIONAL;
		desc.D3D12Desc.Height = SHADOW_MAP_DIMENSION_DIRECTIONAL;
		desc.D3D12Desc.DepthOrArraySize = 1;
		desc.bCubemap = false;
		desc.Name = "ShadowMap_Directional";
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

	CreateProceduralTextures();

	mWorkers_PSOLoad.AddTask([=]() { LoadBuiltinRootSignatures(); });
	mWorkers_PSOLoad.AddTask([=]() { StartPSOCompilation_MT(); });
	mWorkers_PSOLoad.AddTask([=]() 
	{
		SCOPED_CPU_MARKER("InitRenderPasses");
		mRenderPasses.resize(NUM_RENDER_PASSES, nullptr);
		mRenderPasses[ERenderPass::AmbientOcclusion      ] = std::make_shared<AmbientOcclusionPass>(*this, AmbientOcclusionPass::EMethod::FFX_CACAO);
		mRenderPasses[ERenderPass::ZPrePass              ] = std::make_shared<DepthPrePass>(*this);
		mRenderPasses[ERenderPass::DepthMSAAResolve      ] = std::make_shared<DepthMSAAResolvePass>(*this);
		mRenderPasses[ERenderPass::ApplyReflections      ] = std::make_shared<ApplyReflectionsPass>(*this);
		mRenderPasses[ERenderPass::Magnifier             ] = std::make_shared<MagnifierPass>(*this, true); // true: outputs to swapchain
		mRenderPasses[ERenderPass::ObjectID              ] = std::make_shared<ObjectIDPass>(*this);
		mRenderPasses[ERenderPass::ScreenSpaceReflections] = std::make_shared<ScreenSpaceReflectionsPass>(*this);
		mRenderPasses[ERenderPass::Outline               ] = std::make_shared<OutlinePass>(*this);
		{
			SCOPED_CPU_MARKER_C("WaitRootSignatures", 0xFF0000AA);
			mLatchRootSignaturesInitialized.wait();
		}
		for (std::shared_ptr<IRenderPass>& pPass : mRenderPasses)
			pPass->Initialize();

		// std::latch doesn't guarantee memory ops, so the thread that immediately reads
		// mRenderPasses may get corrupted while reading it (e.g. LoadRenderPassPSODescs()).
		// use a thread fence to prevent corruption.
		std::atomic_thread_fence(std::memory_order_release);
		mLatchRenderPassesInitialized.count_down();
	});

	const float tDefaultRscs = timer.Tick();
	Log::Info("[Renderer]    DefaultRscs=%.2fs", tDefaultRscs);
	
	WaitHeapsInitialized();
	
	AllocateDescriptors(mResources_MainWnd, *this);
	CreateResourceViews(mResources_MainWnd, *this);
	LoadDefaultResources();
	mLatchDefaultResourcesLoaded.count_down();

	const float total = tDefaultRscs;
	Log::Info("[Renderer] Loaded in %.2fs.", total);
}

void VQRenderer::Unload()
{
	// todo: mirror Load() functions?
}


void VQRenderer::Destroy()
{
	SCOPED_CPU_MARKER("Renderer.Destroy");
	Log::Info("VQRenderer::Exit()");

	for (std::shared_ptr<IRenderPass>& pPass : mRenderPasses)
	{
		pPass->Destroy();
	}
	mRenderPasses.clear();

	mWorkers_PSOLoad.Destroy();
	mWorkers_ShaderLoad.Destroy();

	// clean up memory
	mHeapUpload.Destroy();
	mHeapCBV_SRV_UAV.Destroy();
	mHeapDSV.Destroy();
	mHeapRTV.Destroy();
	mStaticHeap_VertexBuffer.Destroy();
	mStaticHeap_IndexBuffer.Destroy();

	for (DynamicBufferHeap& Heap : mDynamicHeap_RenderingConstantBuffer) // per cmd recording thread
		Heap.Destroy();
	for (DynamicBufferHeap& Heap : mDynamicHeap_BackgroundTaskConstantBuffer) // per cmd recording thread
		Heap.Destroy();

	// clean up textures
	mTextureManager.Destroy();

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
	const size_t NumBackBuffers = mRenderingCommandAllocators[GFX].size();
	for (std::unordered_map<HWND, FWindowRenderContext>::iterator it = mRenderContextLookup.begin(); it != mRenderContextLookup.end(); ++it)
	{
		auto& ctx = it->second;
		ctx.CleanupContext();
	}

	// release command lists & allocators
	assert(mRenderingCommandAllocators[GFX    ].size() == mRenderingCommandAllocators[COMPUTE].size());
	assert(mRenderingCommandAllocators[COMPUTE].size() == mRenderingCommandAllocators[COPY   ].size());
	assert(mRenderingCommandAllocators[COPY   ].size() == mRenderingCommandAllocators[GFX    ].size());
	for (int q = 0; q < ECommandQueueType::NUM_COMMAND_QUEUE_TYPES; ++q)
	{
		// release commands
		for(int b = 0; b < NumBackBuffers; ++b)
		for (ID3D12CommandList* pCmd : mpRenderingCmds[q][b])
			if (pCmd)
				pCmd->Release();
		for (ID3D12CommandList* pCmd : mpBackgroundTaskCmds[q])
			if (pCmd)
				pCmd->Release();

		// release command allocators
		for (size_t b = 0; b < NumBackBuffers; ++b)
			for (ID3D12CommandAllocator* pCmdAlloc : mRenderingCommandAllocators[q][b])
				if (pCmdAlloc)
					pCmdAlloc->Release();
		for (ID3D12CommandAllocator* pCmdAlloc : mBackgroundTaskCommandAllocators[q])
			if (pCmdAlloc)
				pCmdAlloc->Release();
	}

	// release queues
	for (int i = 0; i < ECommandQueueType::NUM_COMMAND_QUEUE_TYPES; ++i)
	{
		mRenderingCmdQueues[i].Destroy();
		mBackgroundTaskCmdQueues[i].Destroy();
	}

	mDevice.Destroy(); // cleanp up device
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

	FWindowRenderContext ctx = FWindowRenderContext(mRenderingCmdQueues[ECommandQueueType::GFX]);
	{
		SCOPED_CPU_MARKER_C("WAIT_DEVICE_CREATE", 0xFF0000FF);
		mLatchDeviceInitialized.wait();
	}
	{
		SCOPED_CPU_MARKER_C("WAIT_CMDQ_CREATE", 0xFF0000FF);
		mLatchCmdQueuesInitialized.wait();
	}
	ctx.InitializeContext(pWin, pVQDevice, NumSwapchainBuffers, bVSync, bHDRSwapchain);

	// Save other context data
	ctx.WindowDisplayResolutionX = pWin->GetWidth();
	ctx.WindowDisplayResolutionY = pWin->GetHeight();

	// save the render context
	this->mRenderContextLookup.emplace(pWin->GetHWND(), std::move(ctx));
	mLatchSwapchainInitialized.count_down();
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

	mBackgroundTaskFencesPerQueue[GFX].Create(pDevice, "BackgroundTaskGFXFence");
	mBackgroundTaskFencesPerQueue[COPY].Create(pDevice, "BackgroundTaskCopyFence");
	mBackgroundTaskFencesPerQueue[COMPUTE].Create(pDevice, "BackgroundTaskComputeFence");
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

	mBackgroundTaskFencesPerQueue[GFX].Destroy();
	mBackgroundTaskFencesPerQueue[COPY].Destroy();
	mBackgroundTaskFencesPerQueue[COMPUTE].Destroy();
}

void VQRenderer::WaitCopyFenceOnCPU(HWND hwnd)

{
	SCOPED_CPU_MARKER_C("WAIT_COPY_Q", 0xFFFF0000);
	const int BACK_BUFFER_INDEX = this->GetWindowRenderContext(hwnd).GetCurrentSwapchainBufferIndex();
	Fence& CopyFence = this->mCopyObjIDDoneFence[BACK_BUFFER_INDEX];
	CopyFence.WaitOnCPU(CopyFence.GetValue());
}

void VQRenderer::WaitHeapsInitialized()
{
	SCOPED_CPU_MARKER_C("WAIT_HEAPS_READY", 0xFF770000);
	mLatchHeapsInitialized.wait();
}
void VQRenderer::WaitMemoryAllocatorInitialized()
{
	SCOPED_CPU_MARKER_C("WAIT_MEM_ALLOCATOR_READY", 0xFF770000);
	mLatchMemoryAllocatorInitialized.wait();
}
void VQRenderer::WaitLoadingScreenReady() const
{
	SCOPED_CPU_MARKER_C("WAIT_LOADING_SCR_READY", 0xFF770000);
	mLatchSignalLoadingScreenReady.wait();
}
void VQRenderer::SignalLoadingScreenReady()
{
	mLatchSignalLoadingScreenReady.count_down();
}
void VQRenderer::WaitMainSwapchainReady() const
{
	SCOPED_CPU_MARKER_C("WAIT_SWAPCHAIN", 0xFF770000);
	mLatchSwapchainInitialized.wait();
}

void VQRenderer::WaitForLoadCompletion() const
{
	SCOPED_CPU_MARKER_C("WaitRendererDefaultResourceLoaded", 0xFFAA0000);
	mLatchDefaultResourcesLoaded.wait();
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

static void ComputeBRDFIntegrationLUT(ID3D12GraphicsCommandList* pCmd, VQRenderer* pRenderer, SRV_ID& outSRV_ID)
{
	Log::Info("Environment Map:   ComputeBRDFIntegrationLUT");
	SCOPED_GPU_MARKER(pCmd, "CreateBRDFIntegralLUT");

	// Texture resource is created (on Renderer::LoadDefaultResources()) but not initialized at this point.
	const TextureID TexBRDFLUT = pRenderer->GetProceduralTexture(EProceduralTextures::IBL_BRDF_INTEGRATION_LUT);
	ID3D12Resource* pRscBRDFLUT = pRenderer->GetTextureResource(TexBRDFLUT);
	ID3D12DescriptorHeap* ppHeaps[] = { pRenderer->GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };

	int W, H;
	pRenderer->GetTextureDimensions(TexBRDFLUT, W, H);

	// Create & Initialize a UAV for the LUT 
	UAV_ID uavBRDFLUT_ID = pRenderer->AllocateUAV();
	pRenderer->InitializeUAV(uavBRDFLUT_ID, 0, TexBRDFLUT);
	const UAV& uavBRDFLUT = pRenderer->GetUAV(uavBRDFLUT_ID);

	pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	pCmd->SetPipelineState(pRenderer->GetPSO(EBuiltinPSOs::BRDF_INTEGRATION_CS_PSO));
	pCmd->SetComputeRootSignature(pRenderer->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__BRDFIntegrationCS));
	pCmd->SetComputeRootDescriptorTable(0, uavBRDFLUT.GetGPUDescHandle());

	// Dispatch
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

	outSRV_ID = pRenderer->GetProceduralTextureSRV_ID(EProceduralTextures::IBL_BRDF_INTEGRATION_LUT);
}


void VQRenderer::LoadDefaultResources()
{
	SCOPED_CPU_MARKER("LoadDefaultResources");

	ID3D12Device* pDevice = mDevice.GetDevicePtr();

	CreateProceduralTextureViews();

	// ---------------------
	WaitMainSwapchainReady();
	// ---------------------

	ID3D12GraphicsCommandList* pCmd = (ID3D12GraphicsCommandList*)mpBackgroundTaskCmds[GFX][GPU_Generated_Textures];
	pCmd->Reset(mBackgroundTaskCommandAllocators[GFX][GPU_Generated_Textures], nullptr);
	assert(pCmd);
	{
		SCOPED_CPU_MARKER("WAIT_PSO_WORKER_DISPATCH");
		mLatchPSOLoaderDispatched.wait();
	}
	FPSOCompileResult result = WaitPSOReady(EBuiltinPSOs::BRDF_INTEGRATION_CS_PSO);
	mPSOs[result.id] = result.pPSO;

	ComputeBRDFIntegrationLUT(pCmd, this, mResources_MainWnd.SRV_BRDFIntegrationLUT);

	// Create a fence for synchronization
	Microsoft::WRL::ComPtr<ID3D12Fence> pFence;
	HRESULT hr = pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence));
	if (FAILED(hr))
	{
		Log::Error("Failed to create fence for BRDF LUT initialization");
		return;
	}
	HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr); // Create an event for CPU waiting
	if (!fenceEvent)
	{
		Log::Error("Failed to create fence event for BRDF LUT initialization");
		return;
	}
	UINT64 fenceValue = 1; // Initialize fence value

	pCmd->Close();
	{
		SCOPED_CPU_MARKER("ExecuteCommandLists()");
		mBackgroundTaskCmdQueues[ECommandQueueType::GFX].pQueue->ExecuteCommandLists(1, (ID3D12CommandList**)&pCmd);
	}
	mBackgroundTaskCmdQueues[ECommandQueueType::GFX].pQueue->Signal(pFence.Get(), fenceValue);

	// Wait for the GPU to complete the BRDF LUT initialization
	if (pFence->GetCompletedValue() < fenceValue) 
	{
		hr = pFence->SetEventOnCompletion(fenceValue, fenceEvent);
		if (SUCCEEDED(hr))
		{
			SCOPED_CPU_MARKER("WAIT_FENCE");
			WaitForSingleObject(fenceEvent, INFINITE);
		}
		else
		{
			Log::Error("Failed to set fence event for BRDF LUT initialization");
		}
	}
	CloseHandle(fenceEvent); // Cleanup event handle
}

static constexpr UINT PROCEDURAL_TEXTURE_SIZE_X = 1024;
static constexpr UINT PROCEDURAL_TEXTURE_SIZE_Y = 1024;
static const std::array<std::vector<UINT8>, EProceduralTextures::NUM_PROCEDURAL_TEXTURES> textureData =
{
	FTexture::GenerateTexture_Checkerboard(PROCEDURAL_TEXTURE_SIZE_X),
	FTexture::GenerateTexture_Checkerboard(PROCEDURAL_TEXTURE_SIZE_X, true),
	{} // GPU-initialized texture has no data
};
static const std::array<const char*, EProceduralTextures::NUM_PROCEDURAL_TEXTURES> textureNames =
{
	"Checkerboard",
	"Checkerboard_Gray",
	"IBL_BRDF_Integration"
};

void VQRenderer::CreateProceduralTextures()
{
	SCOPED_CPU_MARKER("CreateProceduralTextures");

	D3D12_RESOURCE_DESC textureDesc =
	{
		.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
		.Alignment = 0,
		.Width = PROCEDURAL_TEXTURE_SIZE_X,
		.Height = PROCEDURAL_TEXTURE_SIZE_Y,
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
		.SampleDesc = {.Count = 1, .Quality = 0 },
		.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
		.Flags = D3D12_RESOURCE_FLAG_NONE,
	};

	
	std::array<TextureID, NUM_PROCEDURAL_TEXTURES> textureIDs = { INVALID_ID, INVALID_ID, INVALID_ID };
	std::array<D3D12_RESOURCE_DESC, NUM_PROCEDURAL_TEXTURES> texDescs{ textureDesc, textureDesc, textureDesc };

	for (size_t i = 0; i < NUM_PROCEDURAL_TEXTURES; ++i)
	{
		FTextureRequest req(textureNames[i]);
		req.D3D12Desc = texDescs[i];
		req.InitialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

		if (!textureData[i].empty())
		{
			req.DataArray.push_back(textureData[i].data());
		}

		switch ((EProceduralTextures)i)
		{
		case EProceduralTextures::IBL_BRDF_INTEGRATION_LUT:
			req.InitialState = D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			req.D3D12Desc.Width = 1024;
			req.D3D12Desc.Height = 1024;
			req.D3D12Desc.Format = DXGI_FORMAT_R16G16_FLOAT;
			req.D3D12Desc.Flags = D3D12_RESOURCE_FLAGS::D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			break;
		}

		textureIDs[i] = this->CreateTexture(req, false);
		mLookup_ProceduralTextureIDs[(EProceduralTextures)i] = textureIDs[i];
	}
}

void VQRenderer::CreateProceduralTextureViews()
{
	SCOPED_CPU_MARKER("CreateProceduralTextureViews");
	for (size_t i = 0; i < NUM_PROCEDURAL_TEXTURES; ++i)
	{
		mLookup_ProceduralTextureSRVs[(EProceduralTextures)i] = this->AllocateAndInitializeSRV(mLookup_ProceduralTextureIDs.at((EProceduralTextures)i));
	}
}

void VQRenderer::UploadVertexAndIndexBufferHeaps()
{
	SCOPED_CPU_MARKER("UploadVertexAndIndexBufferHeaps");
	std::lock_guard<std::mutex> lk(mMtxUploadHeap);
	
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
