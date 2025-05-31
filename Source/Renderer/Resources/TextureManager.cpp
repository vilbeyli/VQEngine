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
#include "Libs/VQUtils/Source/utils.h"
#include "Libs/D3D12MA/src/D3D12MemAlloc.h"
#include "Libs/D3DX12/d3dx12.h"

#include "Engine/GPUMarker.h"


using namespace Microsoft::WRL;

#define LOG_CACHED_RESOURCES_ON_LOAD 0
#define LOG_TEXTURE_CREATE           1

#define DISK_WORKER_MAKRER SCOPED_CPU_MARKER_C("DiskWorker", 0xFF00AAAA);
#define MIP_WORKER_MAKRER SCOPED_CPU_MARKER_C("MipWorker", 0xFFAA0000);
#define GPU_WORKER_MAKRER SCOPED_CPU_MARKER_C("GPUWorker", 0xFFAA00AA);

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// STATIC
//
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static std::atomic<TextureID> LAST_USED_TEXTURE_ID = 1;
static TextureID GenerateUniqueID()
{
	return LAST_USED_TEXTURE_ID.fetch_add(1);
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
static bool HasAlphaValuesSIMD(const void* pData, uint W, uint H, DXGI_FORMAT Format)
{
	SCOPED_CPU_MARKER("HasAlphaValuesSIMD");

	const uint BytesPerPixel = (uint)VQ_DXGI_UTILS::GetPixelByteSize(Format);
	assert(BytesPerPixel >= 4 && "Format must have at least 4 bytes per pixel");

	const uint AlphaByteOffset = 3 * BytesPerPixel / 4;
	bool bAllZero = true;
	bool bAllWhite = true;

	// SIMD constants for 128-bit SSE (4 pixels at a time for 4-byte pixels)
	const __m128i zero = _mm_setzero_si128();
	const __m128i all255 = _mm_set1_epi8(255);

	for (uint row = 0; row < H; ++row)
	{
		const char* pRow = static_cast<const char*>(pData) + row * W * BytesPerPixel;
		uint col = 0;

		// Process 4 pixels at a time using SSE
		for (; col <= W - 4; col += 4)
		{
			// Load 4 pixels (16 bytes for 4-byte pixels)
			__m128i pixels = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pRow + col * BytesPerPixel));

			// Extract alpha bytes (assuming alpha is at AlphaByteOffset)
			__m128i alpha;
			if (BytesPerPixel == 4 && AlphaByteOffset == 3) // Optimize for RGBA
			{
				// Shuffle to get alpha bytes (byte 3 of each pixel)
				alpha = _mm_shuffle_epi8(pixels, _mm_set_epi8(-1, -1, -1, 15, -1, -1, -1, 11, -1, -1, -1, 7, -1, -1, -1, 3));
				alpha = _mm_and_si128(alpha, _mm_set_epi8(0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255));
			}
			else
			{
				// Fallback: extract alpha bytes manually
				alignas(16) uint8_t temp[16];
				_mm_storeu_si128(reinterpret_cast<__m128i*>(temp), pixels);
				alignas(16) uint8_t alphaBytes[16] = { 0 };
				for (int i = 0; i < 4; ++i)
					alphaBytes[i * 4] = temp[i * BytesPerPixel + AlphaByteOffset];
				alpha = _mm_load_si128(reinterpret_cast<const __m128i*>(alphaBytes));
			}

			// Compare alphas against 0 and 255
			__m128i cmpZero = _mm_cmpeq_epi8(alpha, zero);
			__m128i cmp255 = _mm_cmpeq_epi8(alpha, all255);

			// Check if all are 0 or all are 255
			int maskZero = _mm_movemask_epi8(cmpZero);
			int mask255 = _mm_movemask_epi8(cmp255);

			bAllZero = bAllZero && (maskZero == 0xFFFF);
			bAllWhite = bAllWhite && (mask255 == 0xFFFF);

			if (!bAllZero && !bAllWhite)
				return true;
		}

		// Handle remaining pixels scalarly
		for (; col < W; ++col)
		{
			uint8_t a = pRow[col * BytesPerPixel + AlphaByteOffset];
			bAllZero = bAllZero && a == 0;
			bAllWhite = bAllWhite && a == 255;

			if (!bAllZero && !bAllWhite)
				return true;
		}
	}

	return false; // All 0 or all 255
}

static bool HasAlphaValuesSIMD3(const void* pData, uint32_t W, uint32_t H, DXGI_FORMAT Format) // this is 100x slower, why?
{
	SCOPED_CPU_MARKER("HasAlphaValuesSIMD3"); 
	const uint32_t BytesPerPixel = (uint32_t)VQ_DXGI_UTILS::GetPixelByteSize(Format);
	assert(BytesPerPixel >= 4);

	const uint32_t NumPixels = W * H;
	const uint8_t* data = static_cast<const uint8_t*>(pData);

	// Fast path: use SSE to process 4 pixels (16 bytes) per loop
	const uint32_t PixelsPerBatch = 4;
	const uint32_t BatchSize = PixelsPerBatch * BytesPerPixel;
	uint32_t i = 0;

	__m128i zero = _mm_setzero_si128();
	__m128i allFF = _mm_set1_epi32(0xFF);

	for (; i + PixelsPerBatch <= NumPixels; i += PixelsPerBatch)
	{
		// Load 4 pixels (assumes 4 bytes per pixel minimum)
		__m128i pixels = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i * BytesPerPixel));

		// Shift right logical each 32-bit int by 24 to isolate alpha (byte 3)
		__m128i alphas = _mm_srli_epi32(pixels, 24);

		// Check if any alpha != 0 and != 255
		__m128i isZero = _mm_cmpeq_epi32(alphas, zero);
		__m128i isFF = _mm_cmpeq_epi32(alphas, allFF);
		__m128i either = _mm_or_si128(isZero, isFF); // 1 if 0 or 255

		// Now invert: if not all are 0 or 255, we have a meaningful alpha
		int mask = _mm_movemask_epi8(either);
		if (mask != 0xFFFF) // not all elements are 0 or 255
			return true;
	}

	// Scalar tail (if remaining pixels < 4)
	for (; i < NumPixels; ++i)
	{
		const uint8_t* pixel = data + i * BytesPerPixel;
		uint8_t a = pixel[3]; // assuming alpha is the 4th byte (RGBA)
		if (a != 0 && a != 255)
			return true;
	}

	return false; // all alpha are 0 or 255
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// INIT / SHUTDOWN
//
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void TextureManager::InitializeEarly()
{
	SCOPED_CPU_MARKER("TextureManager::InitializeEarly");

	// Initialize thread pools for CPU tasks
	const size_t HWThreads = ThreadPool::sHardwareThreadCount;
	const size_t HWCores = HWThreads / 2;

	mDiskWorkers.Initialize(HWCores, "TextureManagerDiskWorkers");
	mMipWorkers.Initialize(std::max<size_t>(2, HWCores / 2), "TextureManagerMipWorkers");
	mGPUWorkers.Initialize(1, "TextureManagerGPUWorkers");
}

void TextureManager::InitializeLate(
	ID3D12Device* pDevice,
	D3D12MA::Allocator* pAllocator,
	ID3D12CommandQueue* pCopyQueue,
	UploadHeap& UploadHeap,
	std::mutex& UploadHeapMutex
)
{
	SCOPED_CPU_MARKER("TextureManager.InitializeLate");

	// set refs
	mpDevice = pDevice;
	mpAllocator = pAllocator;
	mpCopyQueue = pCopyQueue;
	mpUploadHeap = &UploadHeap;
	mpUploadHeapMutex = &UploadHeapMutex;

	// Start upload thread
	mUploadThread = std::thread(&TextureManager::TextureUploadThread_Main, this);
	SetThreadDescription(mUploadThread.native_handle(), L"TextureManagerUploadThread");

	// signal ready
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
		std::lock_guard<std::mutex> taskLock(mTaskMutex);
		for (auto& [id, state] : mTaskStates)
		{
			if (state.State != ETextureTaskState::Ready && state.State != ETextureTaskState::Failed)
			{
				state.CompletionSignal.wait();
			}
		}
		mTaskStates.clear();
	}

	{
		std::lock_guard<std::shared_mutex> metaLock(mMetadataMutex);
		for (auto& [id, meta] : mMetadata)
		{
			if (meta.Texture.Resource)
			{
				meta.Texture.Resource->Release();
			}
			if (meta.Texture.Allocation)
			{
				meta.Texture.Allocation->Release();
			}
		}
		mMetadata.clear();
	}


	{
		std::lock_guard<std::mutex> dataLock(mDataMutex);
		mTextureData.clear();
	}

	{
		std::lock_guard<std::mutex> pathLock(mLoadedTexturePathsMutex);
		mLoadedTexturePaths.clear();
	}

	// owned by Renderer, not our responsibility to release
	mpDevice = nullptr;
	mpAllocator = nullptr;
	mpCopyQueue = nullptr;
	mpUploadHeap = nullptr;
}


//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// INTERFACE
//
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
TextureID TextureManager::CreateTexture(const FTextureRequest& Request, bool bCheckAlpha)
{
	SCOPED_CPU_MARKER("CreateTexture");

	const bool bInitFromFile = !Request.FilePath.empty();
	const bool bInitFromRAM = !Request.DataArray.empty();

	// check cache for file-based textures
	if (bInitFromFile)
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

	// Assign ID and initialize metadata
	TextureID id = GenerateUniqueID();
#if LOG_TEXTURE_CREATE
	Log::Info("CreateTexture (%d) %s", id, Request.Name.c_str());
#endif
	{
		std::lock_guard<std::shared_mutex> lock(mMetadataMutex);
		mMetadata[id].Request = Request;
	}

	// Initialize task state
	{
		std::lock_guard<std::mutex> lock(mTaskMutex);
		mTaskStates[id].State = ETextureTaskState::Pending;
	}

	// Queue initial task
	if (bInitFromFile)
	{
		mDiskWorkers.AddTask([this, id, Request]()
		{
			DISK_WORKER_MAKRER;
			DiskRead(id, Request);
		});
	}
	else if (bInitFromRAM)
	{
		mDiskWorkers.AddTask([this, id, Request]()
		{
			DISK_WORKER_MAKRER;
			LoadFromMemory(id, Request);
		});
	}
	else
	{
		// GPU-only texture (e.g., render target)
		mGPUWorkers.AddTask([this, id]()
		{
			GPU_WORKER_MAKRER;
			AllocateResource(id);
		});
	}

	// update cache
	if (bInitFromFile)
	{
		std::lock_guard<std::mutex> Lock(mLoadedTexturePathsMutex);
		mLoadedTexturePaths[Request.FilePath] = id;
	}

	return id;
}

void TextureManager::DestroyTexture(TextureID& ID)
{
	SCOPED_CPU_MARKER("DestroyTexture");

	// Wait for tasks to complete
	{
		std::lock_guard<std::mutex> lock(mTaskMutex);
		auto taskIt = mTaskStates.find(ID);
		if (taskIt != mTaskStates.end())
		{
			taskIt->second.CompletionSignal.wait();
			mTaskStates.erase(taskIt);
		}
	}
	// Get metadata
	FTextureMetaData meta;
	{
		std::lock_guard<std::shared_mutex> lock(mMetadataMutex);
		auto it = mMetadata.find(ID);
		if (it == mMetadata.end())
		{
			Log::Warning("Texture ID %d not found for destruction", ID);
			return;
		}
		meta = std::move(it->second);
		mMetadata.erase(it);
	}


	// Release resources
	if (meta.Texture.Resource)
	{
		meta.Texture.Resource->Release();
		meta.Texture.Resource = nullptr;
	}
	if (meta.Texture.Allocation)
	{
		meta.Texture.Allocation->Release();
		meta.Texture.Allocation = nullptr;
	}

	// Remove from bookkeeping
	if (!meta.Request.FilePath.empty())
	{
		std::lock_guard<std::mutex> PathLock(mLoadedTexturePathsMutex);
		mLoadedTexturePaths.erase(meta.Request.FilePath);
	}

	// Purge pending uploads
	{
		concurrency::concurrent_queue<FTextureUploadTask> tempQueue;
		FTextureUploadTask task;
		while (mUploadQueue.try_pop(task))
		{
			if (task.ID != ID)
			{
				tempQueue.push(std::move(task));
			}
		}
		// Push back non-matching tasks
		while (tempQueue.try_pop(task))
		{
			mUploadQueue.push(std::move(task));
		}
	}

	// Clear data
	{
		std::lock_guard<std::mutex> lock(mDataMutex);
		mTextureData.erase(ID);
	}

	ID = INVALID_ID;
}

void TextureManager::WaitForTexture(TextureID ID) const
{
	SCOPED_CPU_MARKER_C("WaitForTexture", 0xFFAA0000);

	std::latch* signal = nullptr;
	{
		std::lock_guard<std::mutex> lock(mTaskMutex);
		auto it = mTaskStates.find(ID);
		if (it == mTaskStates.end())
		{
			Log::Warning("Texture ID %d not found for WaitForTexture", ID);
			return;
		}
		signal = &it->second.CompletionSignal;
	}

	if (signal)
	{
		signal->wait();
	}
}

void TextureManager::Update()
{
	SCOPED_CPU_MARKER("TextureManager::Update");
	// Process streaming priorities (placeholder for future streaming)

#if 0
	std::vector<std::pair<TextureID, float>> pendingTasks;
	{
		std::lock_guard<std::mutex> lock(mTaskMutex);
		if (mTaskStates.empty())
		{
			return;
		}
		for (const auto& [id, state] : mTaskStates)
		{
			if (state.State != ETextureTaskState::Ready && state.State != ETextureTaskState::Failed)
			{
				pendingTasks.emplace_back(id, state.StreamingPriority);
			}
		}
	}

	// Sort by priority (optional)
	std::sort(pendingTasks.begin(), pendingTasks.end(),
		[](const auto& a, const auto& b) { return a.second > b.second; });

	for (const auto& [id, priority] : pendingTasks)
	{
		ScheduleNextTask(id);
	}
#endif
}


//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// GETTERS
//
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void TextureManager::GetTextureDimensions(TextureID ID, int& Width, int& Height, int& Slices, int& Mips) const
{
	const FTexture& Texture = *GetTexture(ID);
	Width = Texture.Width;
	Height = Texture.Height;
	Slices = Texture.ArraySlices;
	Mips = Texture.MipCount;
}

const FTexture* TextureManager::GetTexture(TextureID id) const
{
	std::shared_lock<std::shared_mutex> lock(mMetadataMutex);
	auto it = mMetadata.find(id);
	if (it == mMetadata.end())
	{
		Log::Warning("Texture ID %d not found GetTexture()", id);
		return nullptr;
	}
	return &it->second.Texture;
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// INTERNAL / TASK STAGES
//
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void TextureManager::DiskRead(TextureID id, const FTextureRequest& request)
{
	SCOPED_CPU_MARKER("DiskRead");
	
	// Update task state
	{
		std::lock_guard<std::mutex> lock(mTaskMutex);
		mTaskStates[id].State = ETextureTaskState::Reading;
	}
	
	// load image
	FTextureData data;
	{
		std::string fileName = DirectoryUtil::GetFileNameFromPath(request.FilePath);
		SCOPED_CPU_MARKER_F("LoadFromFile: %s", fileName.c_str());
		data.DiskImage = Image::LoadFromFile(request.FilePath.c_str());
	}

	// check image
	if (!data.DiskImage.IsValid())
	{
		Log::Error("Failed to load texture: %s", request.FilePath.c_str());
		std::lock_guard<std::mutex> lock(mTaskMutex);
		mTaskStates[id].State = ETextureTaskState::Failed;
		mTaskStates[id].CompletionSignal.count_down();
		return;
	}


	// Set InputData to point to the image data for consistency
	data.InputData.push_back(data.DiskImage.pData);

	// Update metadata with image properties
	{
		SCOPED_CPU_MARKER("UpdateRequestDesc");
		std::lock_guard<std::shared_mutex> lock(mMetadataMutex);
		auto& meta = mMetadata[id];
		meta.Request.D3D12Desc.Width = data.DiskImage.Width;
		meta.Request.D3D12Desc.Height = data.DiskImage.Height;
		meta.Request.D3D12Desc.Format = data.DiskImage.IsHDR() ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
		meta.Request.D3D12Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		meta.Request.D3D12Desc.MipLevels = request.bGenerateMips ? data.DiskImage.CalculateMipLevelCount() : 1;
		meta.Request.D3D12Desc.DepthOrArraySize = 1;
		meta.Request.D3D12Desc.SampleDesc.Count = 1;
		meta.Request.D3D12Desc.SampleDesc.Quality = 0;
		meta.Request.D3D12Desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		meta.Request.D3D12Desc.Flags = D3D12_RESOURCE_FLAG_NONE;
		meta.Request.InitialState = D3D12_RESOURCE_STATE_COMMON;
	}

	// Store data
	{
		std::lock_guard<std::mutex> lock(mDataMutex);
		mTextureData[id] = std::move(data);
	}

	ScheduleNextTask(id);
}

void TextureManager::LoadFromMemory(TextureID id, const FTextureRequest& request)
{
	SCOPED_CPU_MARKER("LoadFromMemory");

	// Update task state
	{
		std::lock_guard<std::mutex> lock(mTaskMutex);
		mTaskStates[id].State = ETextureTaskState::Reading;
	}

	// check
	if (request.DataArray.empty())
	{
		Log::Error("No data provided for procedural texture ID %d", id);
		std::lock_guard<std::mutex> lock(mTaskMutex);
		mTaskStates[id].State = ETextureTaskState::Failed;
		mTaskStates[id].CompletionSignal.count_down();
		return;
	}

	// assign data
	FTextureData data;
	data.InputData = request.DataArray; // Store pointers directly

	// Store data
	{
		std::lock_guard<std::mutex> lock(mDataMutex);
		mTextureData[id] = std::move(data);
	}

	ScheduleNextTask(id);
}

void TextureManager::GenerateMips(TextureID id)
{
	SCOPED_CPU_MARKER("bGenerateMips");
	
	// Update task state
	{
		std::lock_guard<std::mutex> lock(mTaskMutex);
		mTaskStates[id].State = ETextureTaskState::MipGenerating;
	}

	FTextureMetaData meta;
	{
		std::shared_lock<std::shared_mutex> metaLock(mMetadataMutex);
		meta = mMetadata[id];
	}
	FTextureData data;
	{
		std::lock_guard<std::mutex> dataLock(mDataMutex);
		auto it = mTextureData.find(id);
		if (it == mTextureData.end())
		{
			Log::Warning("Texture ID %d not found for mip generation", id);
			std::lock_guard<std::mutex> taskLock(mTaskMutex);
			mTaskStates[id].State = ETextureTaskState::Failed;
			mTaskStates[id].CompletionSignal.count_down();
			return;
		}
		data = std::move(it->second);
	}

	if (!meta.Request.bGenerateMips || data.InputData.empty() || !data.InputData[0])
	{
		{
			std::lock_guard<std::mutex> lock(mDataMutex);
			mTextureData[id] = std::move(data);
		}
		ScheduleNextTask(id);
		return;
	}

	{
		SCOPED_CPU_MARKER_C("WAIT_INIT", 0xFFAA0000);
		mInitialized.wait();
	}

	uint32_t NumRows[D3D12_REQ_MIP_LEVELS] = { 0 };
	UINT64 RowSizes[D3D12_REQ_MIP_LEVELS] = { 0 };
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprints[D3D12_REQ_MIP_LEVELS];
	UINT64 UploadHeapSize;
	mpDevice->GetCopyableFootprints(&meta.Request.D3D12Desc, 0, meta.Request.D3D12Desc.MipLevels, 0, Footprints, NumRows, RowSizes, &UploadHeapSize);

	// Initialize MipImages
	data.MipImages.resize(meta.Request.D3D12Desc.MipLevels);

	// Set first mip from InputData (DiskImage or procedural data)
	if (data.DiskImage.IsValid())
	{
		data.MipImages[0] = std::move(data.DiskImage); // Move DiskImage to MipImages[0]
		data.DiskImage = Image();
	}
	else
	{
		// For procedural textures, create an Image from InputData
		data.MipImages[0] = Image::CreateEmptyImage(Footprints[0].Footprint.Height * Footprints[0].Footprint.RowPitch);
		memcpy(data.MipImages[0].pData, data.InputData[0], Footprints[0].Footprint.Height * Footprints[0].Footprint.RowPitch);
		data.MipImages[0].Width = Footprints[0].Footprint.Width;
		data.MipImages[0].Height = Footprints[0].Footprint.Height;
		data.MipImages[0].BytesPerPixel = static_cast<int>(VQ_DXGI_UTILS::GetPixelByteSize(meta.Request.D3D12Desc.Format));
	}

	// Generate subsequent mips
	for (uint32_t Mip = 1; Mip < meta.Request.D3D12Desc.MipLevels; ++Mip)
	{
		// Allocate Image for the new mip level
		data.MipImages[Mip] = Image::CreateEmptyImage(Footprints[Mip].Footprint.Height * Footprints[Mip].Footprint.RowPitch);
		data.MipImages[Mip].Width = Footprints[Mip].Footprint.Width;
		data.MipImages[Mip].Height = Footprints[Mip].Footprint.Height;
		data.MipImages[Mip].BytesPerPixel = static_cast<int>(VQ_DXGI_UTILS::GetPixelByteSize(meta.Request.D3D12Desc.Format));

		// Generate mip data
		VQ_DXGI_UTILS::MipImage(
			data.MipImages[Mip - 1].pData,
			data.MipImages[Mip].pData,
			Footprints[Mip - 1].Footprint.Width,
			NumRows[Mip - 1],
			(uint)VQ_DXGI_UTILS::GetPixelByteSize(meta.Request.D3D12Desc.Format)
		);
	}

	{
		std::lock_guard<std::mutex> lock(mDataMutex);
		mTextureData[id] = std::move(data);
	}

	ScheduleNextTask(id);
}

void TextureManager::AllocateResource(TextureID id)
{
	SCOPED_CPU_MARKER("AllocateResource");

	// Update task state
	{
		std::lock_guard<std::mutex> lock(mTaskMutex);
		mTaskStates[id].State = ETextureTaskState::Allocating;
	}

	FTextureMetaData meta;
	bool bHasData = false;
	{
		std::shared_lock<std::shared_mutex> lock(mMetadataMutex);
		meta = mMetadata[id];
	}
	{
		std::lock_guard<std::mutex> lock(mDataMutex);
		auto it = mTextureData.find(id);
		bHasData = it != mTextureData.end() && (!it->second.InputData.empty() || !it->second.MipImages.empty() || it->second.DiskImage.IsValid());
	}

	D3D12_RESOURCE_STATES ResourceState = bHasData ? D3D12_RESOURCE_STATE_COPY_DEST : meta.Request.InitialState;
	D3D12_CLEAR_VALUE* pClearValue = nullptr;
	D3D12_CLEAR_VALUE ClearValueData = {};

	if (meta.Request.D3D12Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) 
	{
		ClearValueData.Format = meta.Request.D3D12Desc.Format;
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
	else if (meta.Request.D3D12Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
	{
		ClearValueData.Format = meta.Request.D3D12Desc.Format;
		pClearValue = &ClearValueData;
	}

	D3D12MA::ALLOCATION_DESC AllocDesc = {};
	AllocDesc.HeapType = meta.Request.bCPUReadback ? D3D12_HEAP_TYPE_READBACK : D3D12_HEAP_TYPE_DEFAULT;

	{
		SCOPED_CPU_MARKER_C("WAIT_INIT", 0xFFAA0000);
		mInitialized.wait();
	}

	HRESULT Hr = S_OK;
	{
		SCOPED_CPU_MARKER("Create");
		Hr = mpAllocator->CreateResource(
			&AllocDesc,
			&meta.Request.D3D12Desc,
			ResourceState,
			pClearValue,
			&meta.Texture.Allocation,
			IID_PPV_ARGS(&meta.Texture.Resource)
		);
	}

	if (FAILED(Hr)) 
	{
		SCOPED_CPU_MARKER("FAILED");
		Log::Error("Failed to create texture: %s", meta.Request.Name.c_str());
		std::lock_guard<std::mutex> lock(mTaskMutex);
		mTaskStates[id].State = ETextureTaskState::Failed;
		mTaskStates[id].CompletionSignal.count_down();
		return;
	}

	FTexture& Texture = meta.Texture;
	SetName(Texture.Resource, meta.Request.Name.c_str());

	Texture.Format = meta.Request.D3D12Desc.Format;
	Texture.Width = static_cast<int>(meta.Request.D3D12Desc.Width);
	Texture.Height = static_cast<int>(meta.Request.D3D12Desc.Height);
	Texture.ArraySlices = meta.Request.D3D12Desc.DepthOrArraySize;
	Texture.MipCount = meta.Request.D3D12Desc.MipLevels;
	Texture.IsCubemap = meta.Request.bCubemap;
	Texture.IsTypeless = meta.Request.D3D12Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	if (bHasData)
	{
		SCOPED_CPU_MARKER("CheckData-Alpha");
		std::lock_guard<std::mutex> lock(mDataMutex);
		auto it = mTextureData.find(id);
		if (it != mTextureData.end() && !it->second.InputData.empty())
		{
			meta.Texture.UsesAlphaChannel = HasAlphaValuesSIMD(it->second.InputData[0], meta.Texture.Width, meta.Texture.Height, meta.Texture.Format);
		}
	}

	{
		SCOPED_CPU_MARKER("RecordMetaData");
		std::lock_guard<std::shared_mutex> lock(mMetadataMutex);
		mMetadata[id] = meta;
	}

	// update task state to Uploading (dummy) for render target textures 
	// so that ScheduleNextTask can signal completion.
	if (!bHasData)
	{
		SCOPED_CPU_MARKER("UpdateState");
		std::lock_guard<std::mutex> lock(mTaskMutex);
		mTaskStates[id].State = ETextureTaskState::Uploading;
	}
	ScheduleNextTask(id);
}

void TextureManager::QueueUploadToGPUTask(TextureID id)
{
	SCOPED_CPU_MARKER("QueueUploadToGPUTask");
	
	// Get data
	FTextureMetaData meta;
	FTextureData* pData = nullptr;
	{
		std::shared_lock<std::shared_mutex> metaLock(mMetadataMutex);
		meta = mMetadata[id];
	}
	{
		std::lock_guard<std::mutex> lock(mDataMutex);
		auto it = mTextureData.find(id);
		if (it == mTextureData.end())
		{
			Log::Warning("Texture ID %d not found for upload", id);
			std::lock_guard<std::mutex> taskLock(mTaskMutex);
			mTaskStates[id].State = ETextureTaskState::Failed;
			mTaskStates[id].CompletionSignal.count_down();
			return;
		}
		pData = &it->second;
	}

	FTextureUploadTask Task;
	Task.ID = id;
	if (meta.Request.bGenerateMips && !pData->MipImages.empty())
	{
		Task.DataArray.resize(pData->MipImages.size());
		for (size_t i = 0; i < pData->MipImages.size(); ++i)
		{
			Task.DataArray[i] = pData->MipImages[i].pData;
		}
		Task.MipCount = static_cast<uint32_t>(pData->MipImages.size());
	}
	else if (!pData->InputData.empty())
	{
		Task.DataArray = std::move(pData->InputData);
		Task.MipCount = 1;
	}

	if (Task.DataArray.empty())
	{
		Log::Warning("No data to upload for texture ID %d", id);
		std::lock_guard<std::mutex> lock(mTaskMutex);
		mTaskStates[id].State = ETextureTaskState::Failed;
		mTaskStates[id].CompletionSignal.count_down();
		// Clean up FTextureData if upload fails
		{
			std::lock_guard<std::mutex> lock(mDataMutex);
			mTextureData.erase(id);
		}
		return;
	}

	mUploadQueue.push(std::move(Task));
	mUploadSignal.NotifyOne();
}

void TextureManager::ScheduleNextTask(TextureID id)
{
	SCOPED_CPU_MARKER("ScheduleNextTask");

	FTextureMetaData meta;
	{
		std::unique_lock<std::shared_mutex> lock(mMetadataMutex);
		auto it = mMetadata.find(id);
		if (it == mMetadata.end())
		{
			Log::Warning("Texture ID %d not found for scheduling", id);
			return;
		}
		meta = it->second;
	}

	ETextureTaskState nextState;
	{
		std::lock_guard<std::mutex> lock(mTaskMutex);
		FTextureTaskState& taskState = mTaskStates[id];
		switch (taskState.State)
		{
		case ETextureTaskState::Reading      : nextState = meta.Request.bGenerateMips ? ETextureTaskState::MipGenerating : ETextureTaskState::Allocating; break;
		case ETextureTaskState::MipGenerating: nextState = ETextureTaskState::Allocating; break;
		case ETextureTaskState::Allocating   : nextState = ETextureTaskState::Uploading; break;
		case ETextureTaskState::Uploading    : nextState = ETextureTaskState::Ready; break;
		default:
			Log::Warning("Invalid task state for texture ID %d", id);
			return;
		}
		taskState.State = nextState;
	}

	switch (nextState)
	{
	case ETextureTaskState::MipGenerating:
		mMipWorkers.AddTask([this, id]() { MIP_WORKER_MAKRER; GenerateMips(id); });
		break;
	case ETextureTaskState::Allocating:
		mGPUWorkers.AddTask([this, id]() { GPU_WORKER_MAKRER; AllocateResource(id); });
		break;
	case ETextureTaskState::Uploading: 
		QueueUploadToGPUTask(id);
		break;
	case ETextureTaskState::Ready:
	{
		std::lock_guard<std::mutex> lock(mTaskMutex);
		mTaskStates[id].CompletionSignal.count_down();
	}
	break;
	}
}


//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// INTERNAL / UPLOAD THREAD
//
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void TextureManager::TextureUploadThread_Main()
{
	SCOPED_CPU_MARKER_C("TextureUploadThread_Main()", 0xFFCC22CC);
	while (!mExitUploadThread.load())
	{
		SCOPED_CPU_MARKER("ProcessTextureUploadQueue");
		{
			SCOPED_CPU_MARKER_C("WAIT_TASK", 0xFFAA0000);
			mUploadSignal.Wait([this]() { return !mUploadQueue.empty() || mExitUploadThread.load(); });
		}

		if (mExitUploadThread.load())
			break;

		std::vector<TextureID> processedIDs;
		{
			SCOPED_CPU_MARKER("ProcessQueue");
			FTextureUploadTask task;
			while (mUploadQueue.try_pop(task))
			{
				processedIDs.push_back(task.ID);
				ProcessTextureUpload(task);
			}
		}

		if (processedIDs.empty())
		{
			return;
		}

		{
			SCOPED_CPU_MARKER("UploadToGPUAndWait");
			std::unique_lock<std::mutex> lock(*mpUploadHeapMutex);
			mpUploadHeap->UploadToGPUAndWait();
		}
		{
			SCOPED_CPU_MARKER("NotifyCompletion");
			std::lock_guard<std::mutex> lock(mTaskMutex);
			for (TextureID id : processedIDs)
			{
				auto it = mTaskStates.find(id);
				if (it != mTaskStates.end())
				{
					it->second.State = ETextureTaskState::Ready;
					it->second.CompletionSignal.count_down();
				}
			}
		}
	}
}

void TextureManager::ProcessTextureUpload(const FTextureUploadTask& Task)
{
	SCOPED_CPU_MARKER("ProcessTextureUpload");
	std::unique_lock<std::mutex> lock(*mpUploadHeapMutex);

	FTextureMetaData meta;
	{
		std::shared_lock<std::shared_mutex> lock(mMetadataMutex);
		auto it = mMetadata.find(Task.ID);
		if (it == mMetadata.end())
		{
			Log::Warning("Texture ID %d not found for upload processing", Task.ID);
			return;
		}
		meta = it->second;
	}


	if (!meta.Texture.Resource)
	{
		Log::Error("No resource for texture %s", meta.Request.Name.c_str());
		std::lock_guard<std::mutex> lock(mTaskMutex);
		mTaskStates[Task.ID].State = ETextureTaskState::Failed;
		mTaskStates[Task.ID].CompletionSignal.count_down();
		return;
	}

	ID3D12GraphicsCommandList* pCmd = mpUploadHeap->GetCommandList();
	const D3D12_RESOURCE_DESC& D3DDesc = meta.Request.D3D12Desc;
	const void* pData = Task.DataArray[0];
	assert(pData);

	const uint MipCount = Task.MipCount;
	UINT64 UploadHeapSize;
	uint32_t NumRows[D3D12_REQ_MIP_LEVELS] = { 0 };
	UINT64 RowSizes[D3D12_REQ_MIP_LEVELS] = { 0 };
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT PlacedSubresource[D3D12_REQ_MIP_LEVELS];

	mpDevice->GetCopyableFootprints(&D3DDesc, 0, MipCount, 0, PlacedSubresource, NumRows, RowSizes, &UploadHeapSize);

	UINT8* pUploadBufferMem = nullptr;
	{
		pUploadBufferMem = mpUploadHeap->Suballocate(SIZE_T(UploadHeapSize), D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
		if (pUploadBufferMem == nullptr)
		{
			mpUploadHeap->UploadToGPUAndWait();
			pUploadBufferMem = mpUploadHeap->Suballocate(SIZE_T(UploadHeapSize), D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
			assert(pUploadBufferMem);
		}
	}

	const bool bBufferResource = D3DDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;

	if (bBufferResource) 
	{
		SCOPED_CPU_MARKER("CopyBuffer");
		const UINT64 SourceRscOffset = pUploadBufferMem - mpUploadHeap->BasePtr();
		memcpy(pUploadBufferMem, pData, D3DDesc.Width);
		pCmd->CopyBufferRegion(meta.Texture.Resource, 0, mpUploadHeap->GetResource(), SourceRscOffset, D3DDesc.Width);
	}
	else 
	{
		SCOPED_CPU_MARKER("CopyPixels");
		const int ArraySize = 1; // Array textures not implemented yet
		const UINT BytePP = static_cast<UINT>(VQ_DXGI_UTILS::GetPixelByteSize(D3DDesc.Format));
		const UINT ImgSizeInBytes = BytePP * PlacedSubresource[0].Footprint.Width * PlacedSubresource[0].Footprint.Height;

		for (int ArrayIdx = 0; ArrayIdx < ArraySize; ++ArrayIdx) 
		{
			for (uint Mip = 0; Mip < MipCount; ++Mip) 
			{
				SCOPED_CPU_MARKER("Mip");
				{
					VQ_DXGI_UTILS::CopyPixels(
						Task.DataArray[Mip],
						pUploadBufferMem + PlacedSubresource[Mip].Offset,
						PlacedSubresource[Mip].Footprint.RowPitch,
						PlacedSubresource[Mip].Footprint.Width * BytePP,
						NumRows[Mip]
					);
				}
				D3D12_PLACED_SUBRESOURCE_FOOTPRINT Slice = PlacedSubresource[Mip];
				Slice.Offset += (pUploadBufferMem - mpUploadHeap->BasePtr());

				CD3DX12_TEXTURE_COPY_LOCATION Dst(meta.Texture.Resource, ArrayIdx * MipCount + Mip);
				CD3DX12_TEXTURE_COPY_LOCATION Src(mpUploadHeap->GetResource(), Slice);
				pCmd->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
			}
		}
	}

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = meta.Texture.Resource;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = meta.Request.InitialState;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	pCmd->ResourceBarrier(1, &barrier);

	// Queue Image destruction on worker threads
	{
		std::lock_guard<std::mutex> lock(mDataMutex);
		auto it = mTextureData.find(Task.ID);
		if (it != mTextureData.end())
		{
			FTextureData& data = it->second;

			// Queue DiskImage destruction
			if (data.DiskImage.IsValid())
			{
				Image diskImage = std::move(data.DiskImage);
				mDiskWorkers.AddTask([img = std::move(diskImage)]() mutable 
				{
					SCOPED_CPU_MARKER("DestroyDiskImage");
					img.Destroy();
				});
			}

			// Queue MipImages destruction
			mDiskWorkers.AddTask([it, this]() 
			{
				DISK_WORKER_MAKRER;
				FTextureData& data = it->second;
				for (Image& mipImage : data.MipImages)
				{
					SCOPED_CPU_MARKER("DestroyMipImage");
					if (mipImage.IsValid())
					{
						mipImage.Destroy();
					}
				}
				data.MipImages.clear();
				
				{
					std::lock_guard<std::mutex> lock(mDataMutex);
					mTextureData.erase(it);
				}
			});
		}
	}

}
