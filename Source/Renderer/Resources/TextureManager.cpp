// VQE
// Copyright(C) 2025 - Volkan Ilbeyli
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//
// Contact: volkanilbeyli@gmail.com

#include "TextureManager.h"
#include "ResourceHeaps.h"
#include "DXGIUtils.h"

#include "Core/Common.h"
#include "Libs/VQUtils/Source/Image.h"
#include "Libs/VQUtils/Source/utils.h"
#include "Libs/D3D12MA/src/D3D12MemAlloc.h"
#include "Libs/D3DX12/d3dx12.h"

#include "Engine/GPUMarker.h"

using namespace Microsoft::WRL;


static TextureID LAST_USED_TEXTURE_ID = 1;
static TextureID GenerateUniqueID()
{
	return LAST_USED_TEXTURE_ID++;
}
static bool HasAlphaValues(const void* pData, uint W, uint H, DXGI_FORMAT Format)
{
	SCOPED_CPU_MARKER("HasAlphaValues");
	const uint BytesPerPixel = (uint)VQ_DXGI_UTILS::GetPixelByteSize(Format);
	bool bAllZero = true;
	bool bAllWhite = true;
	assert(BytesPerPixel >= 4);
	for (uint row = 0; row < H; ++row) // this is not OK, we need threading here.
	for (uint col = 0; col < W; ++col)
	{
		const char* pImg = static_cast<const char*>(pData);
		const uint iPixel = row * W + col;
		const uint AlphaByteOffset = 3 * BytesPerPixel / 4;
		const uint8 a = pImg[iPixel * BytesPerPixel + AlphaByteOffset];
		bAllZero = bAllZero && a == 0;
		bAllWhite = bAllWhite && a == 255;
		if (!bAllWhite && !bAllZero)
			return true;
	}
	return false; // all 1 or all 0
}


void TextureManager::InitializeEarly(std::latch& DeviceInitializedSignal, std::latch& HeapsInitializedSignal)
{
	SCOPED_CPU_MARKER("TextureManager::InitializeEarly");

	mpDeviceInitializedLatch = &DeviceInitializedSignal;
	mpHeapsInitializedLatch = &HeapsInitializedSignal;

	// Initialize thread pools for CPU tasks
	const size_t HWThreads = ThreadPool::sHardwareThreadCount;
	const size_t HWCores = HWThreads / 2;

	mDiskWorkers.Initialize(std::max<size_t>(2, HWCores / 2), "TextureManagerDiskWorkers");
	mMipWorkers.Initialize(HWCores, "TextureManagerMipWorkers");
	mGPUWorkers.Initialize(1, "TextureManagerGPUWorkers");
}

void TextureManager::InitializeLate(
	ID3D12Device* pDevice,
	D3D12MA::Allocator* pAllocator,
	ID3D12CommandQueue* pCopyQueue,
	StaticResourceViewHeap& SRVHeap,
	UploadHeap& UploadHeap,
	std::mutex& UploadHeapMutex
)
{
	SCOPED_CPU_MARKER("TextureManager::InitializeLate");

	mpDevice = pDevice;
	mpAllocator = pAllocator;
	mpCopyQueue = pCopyQueue;
	mpSRVHeap = &SRVHeap;
	mpUploadHeap = &UploadHeap;
	mpUploadQueueMutex = &UploadHeapMutex;

	// Start upload thread
	mUploadThread = std::thread([this]()
	{
		SCOPED_CPU_MARKER_C("TextureUploadThread", 0xFF33AAFF);

		{
			SCOPED_CPU_MARKER_C("WAIT_DEVICE_INIT", 0xFF0000FF);
			mpDeviceInitializedLatch->wait();
			mpHeapsInitializedLatch->wait();
		}

		while (!mExitUploadThread.load())
		{
			std::unique_lock<std::mutex> Lock(*mpUploadQueueMutex);
			mUploadSignal.Wait([this]() { return !mUploadQueue.empty() || mExitUploadThread.load(); });
			
			if (mExitUploadThread.load())
				break;
			
			ProcessTextureUploadQueue();
		}
	});

	mInitialized.count_down();
}

void TextureManager::Destroy()
{
	SCOPED_CPU_MARKER("TextureManager.Destroy");

	// thread cleanup
	mExitUploadThread.store(true);
	mUploadSignal.NotifyAll();
	if (mUploadThread.joinable())
	{
		SCOPED_CPU_MARKER("JoinUploadThread");
		mUploadThread.join();
	}

	// thread pool cleanup
	mDiskWorkers.Destroy();
	mMipWorkers.Destroy();
	mGPUWorkers.Destroy();

	// registry cleanup
	{
		SCOPED_CPU_MARKER("CleanUpTextures");
		std::lock_guard<std::mutex> Lock(mTexturesMutex);
		for (auto& [ID, State] : mTextures)
		{
			if (State.CPUTasksDoneSignal.IsReady())
			{
				State.CPUTasksDoneSignal.Wait();
			}
			if (State.GPUTasksDoneSignal.IsReady())
			{
				State.GPUTasksDoneSignal.Wait();
			}
			if (State.Texture.Resource)
			{
				State.Texture.Resource->Release();
			}
			if (State.Texture.Allocation)
			{
				State.Texture.Allocation->Release();
			}
		}
		mTextures.clear();
		mLoadedTexturePaths.clear();
	}

	// owned by Renderer, not our responsibility to release
	mpDevice = nullptr;
	mpAllocator = nullptr;
	mpCopyQueue = nullptr;
	mpSRVHeap = nullptr;
	mpUploadHeap = nullptr;
	mpDeviceInitializedLatch = nullptr;
	mpHeapsInitializedLatch = nullptr;
}

TextureID TextureManager::QueueTextureCreation(const FTextureRequest& Request)
{
	SCOPED_CPU_MARKER("QueueTextureCreation");
	
	TextureID ID;
	
	{
		std::lock_guard<std::mutex> Lock(mTexturesMutex);
		ID = GenerateUniqueID();
		FTextureState& State = mTextures[ID];
		State.Request = Request;
	}
	
	const bool bGenerateMips = Request.bGenerateMips;
	const bool bLoadFromFile = !Request.FilePath.empty();
	const bool bProcedural = !Request.DataArray.empty();

	const bool bHaveCPUTasks = bGenerateMips || bLoadFromFile;

	// Phase 1: CPU-bound tasks (disk read, mip generation)
	if(bHaveCPUTasks)
	{
		mDiskWorkers.AddTask([this, ID, bLoadFromFile, bGenerateMips, bProcedural]()
		{
			if (bLoadFromFile)
			{
				DiskRead(ID);
			}

			if (bGenerateMips)
			{
				TaskSignal<void> MipSignal;
				mMipWorkers.AddTask([this, ID, &MipSignal]() { GenerateMips(ID); MipSignal.Notify(); });
				{
					SCOPED_CPU_MARKER_C("WAIT_MIP_DONE", 0xFFAA0000);
					MipSignal.Wait();
				}
			}

			{
				std::lock_guard<std::mutex> Lock(mTexturesMutex);
				std::unordered_map<TextureID, FTextureState>::iterator It = mTextures.find(ID);
				FTextureState& State = It->second;
				State.CPUTasksDoneSignal.Notify();
			}
		});
	}
	else
	{
		std::lock_guard<std::mutex> Lock(mTexturesMutex);
		mTextures.at(ID).CPUTasksDoneSignal.Notify();
	}
	
	// Phase 2: GPU-bound tasks (allocation, upload, views)
	std::future<void> GPUTasksDoneSignal = mGPUWorkers.AddTask([this, ID]()
	{
		SCOPED_CPU_MARKER("TextureGPUTasks");
		FTextureState* pState = nullptr;
		{
			std::lock_guard<std::mutex> Lock(mTexturesMutex);
			auto It = mTextures.find(ID);
			if (It == mTextures.end())
			{
				Log::Warning("Texture ID %d not found for GPU tasks", ID);
				return;
			}
			pState = &It->second;
		}

		{
			SCOPED_CPU_MARKER_C("WAIT_DISK_IO_N_MIPMAP", 0xFF0000FF);
			pState->CPUTasksDoneSignal.Wait(); // Wait for CPU tasks to complete
		}
		
		// Wait for device initialization
		{
			SCOPED_CPU_MARKER_C("WAIT_DEVICE_INIT", 0xFF0000FF);
			mpDeviceInitializedLatch->wait();
		}
		
		{
			AllocateResource(ID);
			
			{
				SCOPED_CPU_MARKER_C("WAIT_HEAP_INIT", 0xFF0000FF);
				mpHeapsInitializedLatch->wait();
			}

			bool bUploadToGPU = false;
			{   // TODO
				std::lock_guard<std::mutex> Lock(mTexturesMutex);
				auto It = mTextures.find(ID);
				bUploadToGPU = !It->second.RawData.empty() || !It->second.MipData.empty();
			}
			if (bUploadToGPU)
			{
				UploadToGPU(ID);
			}
		}
	});
	
	return ID;
}

TextureID TextureManager::CreateTexture(const FTextureRequest& Request, bool bCheckAlpha)
{
	SCOPED_CPU_MARKER("CreateTextureFromFile");

	// check cache for file-based textures
	if (!Request.FilePath.empty())
	{
		std::lock_guard<std::mutex> Lock(mLoadedTexturePathsMutex);
		if (auto It = mLoadedTexturePaths.find(Request.FilePath); It != mLoadedTexturePaths.end())
		{
#if LOG_CACHED_RESOURCES_ON_LOAD
			Log::Info("Texture already loaded: %s", FilePath.c_str());
#endif
			return It->second;
		}
	}

	TextureID ID = QueueTextureCreation(Request);

	if (!Request.FilePath.empty())
	{
		std::lock_guard<std::mutex> Lock(mLoadedTexturePathsMutex);
		mLoadedTexturePaths[Request.FilePath] = ID;
	}

	return ID;
}

void TextureManager::DestroyTexture(TextureID& ID)
{
	SCOPED_CPU_MARKER("DestroyTexture");

	std::lock_guard<std::mutex> TextureLock(mTexturesMutex);
	auto It = mTextures.find(ID);
	if (It == mTextures.end())
	{
		Log::Warning("Texture ID %d not found for destruction", ID);
		return;
	}

	FTextureState& State = It->second;

#if 0
	// Wait for ongoing tasks
	if (State.CPUTasksDoneSignal.valid())
		State.CPUTasksDoneSignal.wait();
	
	if (State.GPUTasksDoneSignal.valid())
		State.GPUTasksDoneSignal.wait();
#endif

	// Release resources
	if (State.Texture.Resource)
	{
		State.Texture.Resource->Release();
		State.Texture.Resource = nullptr;
	}
	if (State.Texture.Allocation)
	{
		State.Texture.Allocation->Release();
		State.Texture.Allocation = nullptr;
	}

	// Free descriptors (assumes VQRenderer manages DSV/UAV heaps)
	if (State.SRVID != INVALID_ID)
	{
		// mpSRVHeap->Free(State.SRVID); // Implement if heap supports freeing
		State.SRVID = INVALID_ID;
	}
	if (State.DSVID != INVALID_ID)
	{
		// VQRenderer::FreeDSV(State.DSVID); // Placeholder
		State.DSVID = INVALID_ID;
	}
	if (State.UAVID != INVALID_ID)
	{
		// VQRenderer::FreeUAV(State.UAVID); // Placeholder
		State.UAVID = INVALID_ID;
	}

	// Remove from bookkeeping
	if (!State.Request.FilePath.empty())
	{
		std::lock_guard<std::mutex> PathLock(mLoadedTexturePathsMutex);
		mLoadedTexturePaths.erase(State.Request.FilePath);
	}

	// Purge pending uploads
	{
		std::lock_guard<std::mutex> QueueLock(*mpUploadQueueMutex);
		std::queue<FTextureUploadTask> TempQueue;
		while (!mUploadQueue.empty())
		{
			FTextureUploadTask Task = std::move(mUploadQueue.front());
			mUploadQueue.pop();
			if (Task.ID != ID)
			{
				TempQueue.push(std::move(Task));
			}
		}
		mUploadQueue = std::move(TempQueue);
	}

	// Remove from textures
	mTextures.erase(It);

	ID = INVALID_ID;
}

void TextureManager::WaitForTexture(TextureID ID) const
{
	SCOPED_CPU_MARKER_C("WaitForTexture", 0xFFAA0000);

	std::lock_guard<std::mutex> Lock(mTexturesMutex);
	auto It = mTextures.find(ID);
	if (It == mTextures.end())
	{
		Log::Warning("Texture ID %d not found", ID);
		return;
	}

	It->second.GPUTasksDoneSignal.Wait();
}

void TextureManager::Update()
{
	SCOPED_CPU_MARKER("TextureManager::Update");
	// Process streaming priorities (placeholder for future streaming)
	std::lock_guard<std::mutex> Lock(mTexturesMutex);
	for (auto& [ID, State] : mTextures) 
	{
		// Example: Update StreamingPriority based on scene visibility
		// State.StreamingPriority = ComputePriority(ID);
	}
}

TaskSignal<void>& TextureManager::GetTextureCompletionSignal(TextureID ID) const
{
	std::lock_guard<std::mutex> Lock(mTexturesMutex);
	auto It = mTextures.find(ID);
	assert(It != mTextures.end());
	if (It == mTextures.end())
	{
		Log::Warning("Texture ID %d not found for completion signal", ID);
	}
	return It->second.GPUTasksDoneSignal;
}

ID3D12Resource* TextureManager::GetTextureResource(TextureID ID) const
{
	std::lock_guard<std::mutex> Lock(mTexturesMutex);
	auto It = mTextures.find(ID);
	if (It == mTextures.end())
	{
		Log::Warning("Texture ID %d not found", ID);
		return nullptr;
	}
	return It->second.Texture.Resource;
}

DXGI_FORMAT TextureManager::GetTextureFormat(TextureID ID) const
{
	std::lock_guard<std::mutex> Lock(mTexturesMutex);
	auto It = mTextures.find(ID);
	if (It == mTextures.end())
	{
		Log::Warning("Texture ID %d not found", ID);
		return DXGI_FORMAT_UNKNOWN;
	}
	return It->second.Texture.Format;
}

void TextureManager::GetTextureDimensions(TextureID ID, int& Width, int& Height, int& Slices, int& Mips) const
{
	std::lock_guard<std::mutex> Lock(mTexturesMutex);
	auto It = mTextures.find(ID);
	if (It == mTextures.end())
	{
		Log::Warning("Texture ID %d not found", ID);
		Width = Height = Slices = Mips = 0;
		return;
	}
	const FTexture& Texture = It->second.Texture;
	Width = Texture.Width;
	Height = Texture.Height;
	Slices = Texture.ArraySlices;
	Mips = Texture.MipCount;
}

bool TextureManager::GetTextureAlphaChannelUsed(TextureID ID) const
{
	std::lock_guard<std::mutex> Lock(mTexturesMutex);
	auto It = mTextures.find(ID);
	if (It == mTextures.end())
	{
		Log::Warning("Texture ID %d not found", ID);
		return false;
	}
	return It->second.Texture.UsesAlphaChannel;
}

uint TextureManager::GetTextureMips(TextureID ID) const
{
	std::lock_guard<std::mutex> Lock(mTexturesMutex);
	auto It = mTextures.find(ID);
	if (It == mTextures.end())
	{
		Log::Warning("Texture ID %d not found", ID);
		return 0;
	}
	return It->second.Texture.MipCount;
}

const FTexture* TextureManager::GetTexture(TextureID ID) const
{
	std::lock_guard<std::mutex> Lock(mTexturesMutex);
	auto It = mTextures.find(ID);
	if (It == mTextures.end())
	{
		Log::Warning("Texture ID %d not found", ID);
		return nullptr;
	}
	return &It->second.Texture;
}

SRV_ID TextureManager::GetSRVID(TextureID ID) const
{
	std::lock_guard<std::mutex> Lock(mTexturesMutex);
	auto It = mTextures.find(ID);
	if (It == mTextures.end())
	{
		Log::Warning("Texture ID %d not found for SRV retrieval", ID);
		return INVALID_ID;
	}
	return It->second.SRVID;
}

DSV_ID TextureManager::GetDSVID(TextureID ID) const
{
	std::lock_guard<std::mutex> Lock(mTexturesMutex);
	auto It = mTextures.find(ID);
	if (It == mTextures.end())
	{
		Log::Warning("Texture ID %d not found for DSV retrieval", ID);
		return INVALID_ID;
	}
	return It->second.DSVID;
}

UAV_ID TextureManager::GetUAVID(TextureID ID) const
{
	std::lock_guard<std::mutex> Lock(mTexturesMutex);
	auto It = mTextures.find(ID);
	if (It == mTextures.end())
	{
		Log::Warning("Texture ID %d not found for UAV retrieval", ID);
		return INVALID_ID;
	}
	return It->second.UAVID;
}


void TextureManager::DiskRead(TextureID ID)
{
	SCOPED_CPU_MARKER("DiskRead");
	std::lock_guard<std::mutex> Lock(mTexturesMutex);
	auto It = mTextures.find(ID);
	if (It == mTextures.end()) 
	{
		Log::Warning("Texture ID %d not found for disk read", ID);
		return;
	}
	FTextureState& State = It->second;

	if (State.Request.FilePath.empty()) 
	{
		Log::Warning("No file path for texture %s", State.Request.Name.c_str());
		return;
	}

	Image Img = Image::LoadFromFile(State.Request.FilePath.c_str());
	if (!Img.pData) 
	{
		Log::Error("Failed to load texture: %s", State.Request.FilePath.c_str());
		return;
	}

	size_t DataSize = Img.Width * Img.Height * Img.BytesPerPixel;
	State.RawData.assign(static_cast<uint8_t*>(Img.pData), static_cast<uint8_t*>(Img.pData) + DataSize);
	Img.Destroy();

	// Update request with image properties
	State.Request.D3D12Desc.Width = Img.Width;
	State.Request.D3D12Desc.Height = Img.Height;
	State.Request.D3D12Desc.Format = Img.IsHDR() ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
	State.Request.D3D12Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	State.Request.D3D12Desc.MipLevels = State.Request.bGenerateMips ? Img.CalculateMipLevelCount() : 1;
	State.Request.D3D12Desc.DepthOrArraySize = 1;
	State.Request.D3D12Desc.SampleDesc.Count = 1;
	State.Request.D3D12Desc.SampleDesc.Quality = 0;
	State.Request.D3D12Desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	State.Request.D3D12Desc.Flags = D3D12_RESOURCE_FLAG_NONE;
}

void TextureManager::GenerateMips(TextureID ID)
{
	SCOPED_CPU_MARKER("bGenerateMips");
	std::lock_guard<std::mutex> Lock(mTexturesMutex);
	auto It = mTextures.find(ID);
	if (It == mTextures.end()) 
	{
		Log::Warning("Texture ID %d not found for mip generation", ID);
		return;
	}
	FTextureState& State = It->second;

	if (!State.Request.bGenerateMips || State.RawData.empty()) 
	{
		return;
	}

	UINT64 UploadHeapSize;
	uint32_t NumRows[D3D12_REQ_MIP_LEVELS] = { 0 };
	UINT64 RowSizes[D3D12_REQ_MIP_LEVELS] = { 0 };
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprints[D3D12_REQ_MIP_LEVELS];
	mpDevice->GetCopyableFootprints(&State.Request.D3D12Desc, 0, State.Request.D3D12Desc.MipLevels, 0, Footprints, NumRows, RowSizes, &UploadHeapSize);

	State.MipData.resize(State.Request.D3D12Desc.MipLevels);
	State.MipData[0] = State.RawData;

	for (uint32_t Mip = 1; Mip < State.Request.D3D12Desc.MipLevels; ++Mip) 
	{
		State.MipData[Mip].resize(Footprints[Mip].Footprint.Height * Footprints[Mip].Footprint.RowPitch);
		VQ_DXGI_UTILS::MipImage(
			State.MipData[Mip - 1].data(),
			State.MipData[Mip].data(),
			Footprints[Mip - 1].Footprint.Width,
			NumRows[Mip - 1],
			(uint)VQ_DXGI_UTILS::GetPixelByteSize(State.Request.D3D12Desc.Format)
		);
	}
}

void TextureManager::AllocateResource(TextureID ID)
{
	SCOPED_CPU_MARKER("AllocateResource");
	auto It = mTextures.find(ID); // should already be locked
	if (It == mTextures.end()) 
	{
		Log::Warning("Texture ID %d not found for resource allocation", ID);
		return;
	}
	FTextureState& State = It->second;

	D3D12_RESOURCE_STATES ResourceState = State.RawData.empty() && State.MipData.empty() ? State.Request.InitialState : D3D12_RESOURCE_STATE_COPY_DEST;
	D3D12_CLEAR_VALUE* pClearValue = nullptr;
	D3D12_CLEAR_VALUE ClearValueData = {};

	if (State.Request.D3D12Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) 
	{
		ClearValueData.Format = State.Request.D3D12Desc.Format;
		if (ClearValueData.Format == DXGI_FORMAT_R32_TYPELESS)
		{
			ClearValueData.Format = DXGI_FORMAT_D32_FLOAT;
		}
		if (ClearValueData.Format == DXGI_FORMAT_R24_UNORM_X8_TYPELESS || ClearValueData.Format == DXGI_FORMAT_R24G8_TYPELESS)
		{
			ClearValueData.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		}
		ClearValueData.DepthStencil.Depth = 1.0f;
		ClearValueData.DepthStencil.Stencil = 0;
		pClearValue = &ClearValueData;
	}
	else if (State.Request.D3D12Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
	{
		ClearValueData.Format = State.Request.D3D12Desc.Format;
		pClearValue = &ClearValueData;
	}

	D3D12MA::ALLOCATION_DESC AllocDesc = {};
	AllocDesc.HeapType = State.Request.bCPUReadback ? D3D12_HEAP_TYPE_READBACK : D3D12_HEAP_TYPE_DEFAULT;

	{
		SCOPED_CPU_MARKER_C("WAIT_INIT", 0xFFAA0000);
		mInitialized.wait();
	}

	HRESULT Hr = mpAllocator->CreateResource(
		&AllocDesc,
		&State.Request.D3D12Desc,
		ResourceState,
		pClearValue,
		&State.Texture.Allocation,
		IID_PPV_ARGS(&State.Texture.Resource)
	);

	if (FAILED(Hr)) 
	{
		Log::Error("Failed to create texture: %s", State.Request.Name.c_str());
		return;
	}

	SetName(State.Texture.Resource, State.Request.Name.c_str());

	State.Texture.Format = State.Request.D3D12Desc.Format;
	State.Texture.Width = static_cast<int>(State.Request.D3D12Desc.Width);
	State.Texture.Height = static_cast<int>(State.Request.D3D12Desc.Height);
	State.Texture.ArraySlices = State.Request.D3D12Desc.DepthOrArraySize;
	State.Texture.MipCount = State.Request.D3D12Desc.MipLevels;
	State.Texture.IsCubemap = false; // Update if cubemap support is added
	State.Texture.IsTypeless = State.Request.D3D12Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	if (!State.RawData.empty() && State.Request.bCPUReadback) 
	{
		State.Texture.UsesAlphaChannel = HasAlphaValues(State.RawData.data(), State.Texture.Width, State.Texture.Height, State.Texture.Format);
	}
}

void TextureManager::UploadToGPU(TextureID ID)
{
	SCOPED_CPU_MARKER("UploadToGPU");
	std::lock_guard<std::mutex> Lock(mTexturesMutex);
	auto It = mTextures.find(ID);
	if (It == mTextures.end()) 
	{
		Log::Warning("Texture ID %d not found for upload", ID);
		return;
	}
	FTextureState& State = It->second;

	FTextureUploadTask Task;
	Task.ID = ID;
	if (State.MipData.empty()) 
	{
		if (!State.RawData.empty()) 
		{
			Task.DataArray.push_back(State.RawData.data());
		}
	}
	else 
	{
		Task.DataArray.reserve(State.MipData.size());
		for (const auto& Mip : State.MipData) 
		{
			Task.DataArray.push_back(Mip.data());
		}
	}

	if (Task.DataArray.empty()) 
	{
		Log::Warning("No data to upload for texture %s", State.Request.Name.c_str());
		return;
	}

	{
		std::lock_guard<std::mutex> QueueLock(*mpUploadQueueMutex);
		mUploadQueue.emplace(std::move(Task));
	}
	mUploadSignal.NotifyOne();
}


void TextureManager::TransitionState(TextureID ID)
{
	SCOPED_CPU_MARKER("TransitionState");
	std::lock_guard<std::mutex> Lock(mTexturesMutex);
	auto It = mTextures.find(ID);
	if (It == mTextures.end()) 
	{
		Log::Warning("Texture ID %d not found for state transition", ID);
		return;
	}
	FTextureState& State = It->second;

	if (State.RawData.empty() && State.MipData.empty()) 
	{
		return; // No upload, already in InitialState
	}

	ID3D12GraphicsCommandList* pCommandList = mpUploadHeap->GetCommandList();

	CD3DX12_RESOURCE_BARRIER Barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		State.Texture.Resource,
		D3D12_RESOURCE_STATE_COPY_DEST,
		State.Request.InitialState
	);
	pCommandList->ResourceBarrier(1, &Barrier);
	pCommandList->Close();

	ID3D12CommandList* ppCommandLists[] = { pCommandList };
	mpCopyQueue->ExecuteCommandLists(1, ppCommandLists);
}

void TextureManager::ProcessTextureUpload(const FTextureUploadTask& Task)
{
	SCOPED_CPU_MARKER("ProcessTextureUpload");
	std::lock_guard<std::mutex> Lock(mTexturesMutex);
	auto It = mTextures.find(Task.ID);
	if (It == mTextures.end()) 
	{
		Log::Warning("Texture ID %d not found for upload processing", Task.ID);
		return;
	}
	FTextureState& State = It->second;

	if (!State.Texture.Resource) 
	{
		Log::Error("No resource for texture %s", State.Request.Name.c_str());
		return;
	}

	ID3D12GraphicsCommandList* pCmd = mpUploadHeap->GetCommandList();
	const D3D12_RESOURCE_DESC& D3DDesc = State.Request.D3D12Desc;
	const void* pData = Task.DataArray[0];
	assert(pData);

	const uint MipCount = Task.MipCount;
	UINT64 UploadHeapSize;
	uint32_t NumRows[D3D12_REQ_MIP_LEVELS] = { 0 };
	UINT64 RowSizes[D3D12_REQ_MIP_LEVELS] = { 0 };
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT PlacedSubresource[D3D12_REQ_MIP_LEVELS];

	mpDevice->GetCopyableFootprints(&D3DDesc, 0, MipCount, 0, PlacedSubresource, NumRows, RowSizes, &UploadHeapSize);

	UINT8* pUploadBufferMem = mpUploadHeap->Suballocate(SIZE_T(UploadHeapSize), D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
	if (pUploadBufferMem == nullptr) 
	{
		SCOPED_CPU_MARKER("UploadToGPUAndWait");
		mpUploadHeap->UploadToGPUAndWait();
		pUploadBufferMem = mpUploadHeap->Suballocate(SIZE_T(UploadHeapSize), D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
		assert(pUploadBufferMem);
	}

	const bool bBufferResource = D3DDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;

	if (bBufferResource) 
	{
		const UINT64 SourceRscOffset = pUploadBufferMem - mpUploadHeap->BasePtr();
		memcpy(pUploadBufferMem, pData, D3DDesc.Width);
		pCmd->CopyBufferRegion(State.Texture.Resource, 0, mpUploadHeap->GetResource(), SourceRscOffset, D3DDesc.Width);
	}
	else 
	{
		const int ArraySize = 1; // Array textures not implemented yet
		const UINT BytePP = static_cast<UINT>(VQ_DXGI_UTILS::GetPixelByteSize(D3DDesc.Format));
		const UINT ImgSizeInBytes = BytePP * PlacedSubresource[0].Footprint.Width * PlacedSubresource[0].Footprint.Height;

		for (int ArrayIdx = 0; ArrayIdx < ArraySize; ++ArrayIdx) 
		{
			for (uint Mip = 0; Mip < MipCount; ++Mip) 
			{
				VQ_DXGI_UTILS::CopyPixels(
					Task.DataArray[Mip],
					pUploadBufferMem + PlacedSubresource[Mip].Offset,
					PlacedSubresource[Mip].Footprint.RowPitch,
					PlacedSubresource[Mip].Footprint.Width * BytePP,
					NumRows[Mip]
				);

				D3D12_PLACED_SUBRESOURCE_FOOTPRINT Slice = PlacedSubresource[Mip];
				Slice.Offset += (pUploadBufferMem - mpUploadHeap->BasePtr());

				CD3DX12_TEXTURE_COPY_LOCATION Dst(State.Texture.Resource, ArrayIdx * MipCount + Mip);
				CD3DX12_TEXTURE_COPY_LOCATION Src(mpUploadHeap->GetResource(), Slice);
				pCmd->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
			}
		}
	}

	CD3DX12_RESOURCE_BARRIER Barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		State.Texture.Resource,
		D3D12_RESOURCE_STATE_COPY_DEST,
		State.Request.InitialState
	);
	pCmd->ResourceBarrier(1, &Barrier);
}

void TextureManager::ProcessTextureUploadQueue()
{
    SCOPED_CPU_MARKER("ProcessTextureUploadQueue");
    
    std::unique_lock<std::mutex> QueueLock(*mpUploadQueueMutex);
    if (mUploadQueue.empty())
    {
        return;
    }
    
    std::vector<TaskSignal<void>*> GPUTasksDoneSignals;
    while (!mUploadQueue.empty())
    {
        FTextureUploadTask Task = std::move(mUploadQueue.front());
        mUploadQueue.pop();
        
        {
            std::lock_guard<std::mutex> TextureLock(mTexturesMutex);
            auto It = mTextures.find(Task.ID);
            if (It == mTextures.end())
            {
                Log::Warning("Texture ID %d not found for upload processing", Task.ID);
                continue;
            }
            
            GPUTasksDoneSignals.push_back(&It->second.GPUTasksDoneSignal);
            QueueLock.unlock();
            ProcessTextureUpload(Task);
            QueueLock.lock();
        }
    }
    
    {
        SCOPED_CPU_MARKER("UploadToGPUAndWait");
        mpUploadHeap->UploadToGPUAndWait();
    }
    
    {
        SCOPED_CPU_MARKER("NotifyGPUTasksDoneSignals");
        std::lock_guard<std::mutex> TextureLock(mTexturesMutex);
        for (TaskSignal<void>* pFuture : GPUTasksDoneSignals)
        {
            pFuture->Notify();
        }
    }
}