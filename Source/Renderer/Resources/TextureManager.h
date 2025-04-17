//	VQE
//	Copyright(C) 2025  - Volkan Ilbeyli
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

#include "Texture.h"

#include "Engine/Core/Types.h"
#include "Libs/VQUtils/Source/Multithreading.h"
#include "Libs/VQUtils/Source/Image.h"

#include <d3d12.h>
#include <latch>
#include <shared_mutex>
#include <unordered_map>
#include <concurrent_queue.h>

struct ID3D12CommandQueue;
struct ID3D12Device;
class StaticResourceViewHeap;
namespace D3D12MA { class Allocator; }
class UploadHeap;

struct FTextureRequest
{
    std::string Name;                    // Texture identifier (e.g., "DownsampledSceneDepthAtomicCounter")
    std::string FilePath;                // Path to file on disk (if loading from disk)
    std::vector<const void*> DataArray;  // Initial data for buffers/textures
    D3D12_RESOURCE_DESC D3D12Desc = {};  // D3D12 resource description
    D3D12_RESOURCE_STATES InitialState;  // Desired state after upload (e.g., D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    bool bGenerateMips = false;          // Whether to generate mipmaps
    bool bCubemap = false;               // Whether this is a cubemap or not
    bool bCPUReadback = false;           // Whether to use D3D12_HEAP_TYPE_READBACK
    DXGI_FORMAT SRVFormat;               // Format for SRV (if applicable)
    DXGI_FORMAT UAVFormat;               // Format for UAV (if applicable)
};

enum class ETextureTaskState
{
    Pending,
    Reading,
    MipGenerating,
    Allocating,
    Uploading,
    Ready,
    Failed
};

class TextureManager
{
public:
    void InitializeEarly();
    void InitializeLate(
        ID3D12Device* pDevice,
        D3D12MA::Allocator* pAllocator,
        ID3D12CommandQueue* pCopyQueue,
        StaticResourceViewHeap& SRVHeap,
        UploadHeap& UploadHeap,
        std::mutex& UploadHeapMutex
    );
    void Destroy();

    TextureID CreateTexture(const FTextureRequest& Request, bool bCheckAlpha = false);
    void DestroyTexture(TextureID& ID);

    void WaitForTexture(TextureID ID) const;

    void Update(); // Process streaming, uploads

    const FTexture*        GetTexture(TextureID ID) const;
    inline ID3D12Resource* GetTextureResource(TextureID ID) const { return GetTexture(ID)->Resource; }
    inline DXGI_FORMAT     GetTextureFormat(TextureID ID) const { return GetTexture(ID)->Format; }
    void                   GetTextureDimensions(TextureID ID, int& Width, int& Height, int& Slices, int& Mips) const;
    inline bool            GetTextureAlphaChannelUsed(TextureID ID) const { return GetTexture(ID)->UsesAlphaChannel; }
    inline uint            GetTextureMips(TextureID ID) const { return GetTexture(ID)->MipCount; }

    SRV_ID GetSRVID(TextureID ID) const;
    DSV_ID GetDSVID(TextureID ID) const;
    UAV_ID GetUAVID(TextureID ID) const;

private:
    struct FTextureTaskState
    {
        ETextureTaskState State = ETextureTaskState::Pending;
        mutable std::latch CompletionSignal{ 1 }; // Signals when texture is ready
        float StreamingPriority = 0.0f;
    };
    struct FTextureMetaData
    {
        FTextureRequest Request;
        FTexture Texture; // Resource, Allocation, Format, etc.
        SRV_ID SRVID = INVALID_ID;
        DSV_ID DSVID = INVALID_ID;
        UAV_ID UAVID = INVALID_ID;
    };
    struct FTextureData
    {
        Image DiskImage;                     // For disk-loaded textures
        std::vector<Image> MipImages;        // For generated mip levels
        std::vector<uint8_t> OwnedData;      // For temporary buffers during mip generation
        std::vector<const void*> InputData;  // For procedural textures (points to FTextureRequest::DataArray)
    };
    struct FTextureUploadTask
    {
        TextureID ID;
        std::vector<const void*> DataArray; // Points to FTextureRequest::DataArray or FTextureData::MipData
        uint32_t MipCount; // Number of mips to upload
    };

    // Task state and synchronization
    std::unordered_map<TextureID, FTextureTaskState> mTaskStates;
    mutable std::mutex mTaskMutex;

    // Texture data (temporary, cleared after upload)
    std::unordered_map<TextureID, FTextureData> mTextureData;
    mutable std::mutex mDataMutex;
    
    // Texture metadata (immutable after creation)
    std::unordered_map<TextureID, FTextureMetaData> mMetadata;
    mutable std::shared_mutex mMetadataMutex;

    // Cache for file-based textures
    std::unordered_map<std::string, TextureID> mLoadedTexturePaths;
    std::mutex mLoadedTexturePathsMutex;

    // thread pools
    ThreadPool mDiskWorkers;
    ThreadPool mMipWorkers;
    ThreadPool mGPUWorkers;

    // upload thread state
    concurrency::concurrent_queue<FTextureUploadTask> mUploadQueue;
    std::thread mUploadThread;
    EventSignal mUploadSignal;
    std::atomic<bool> mExitUploadThread = false;

    // sync
    std::latch mInitialized{ 1 };

    // Renderer-provided resources
    ID3D12Device* mpDevice = nullptr;
    D3D12MA::Allocator* mpAllocator = nullptr;
    ID3D12CommandQueue* mpCopyQueue = nullptr;
    StaticResourceViewHeap* mpSRVHeap = nullptr;
    UploadHeap* mpUploadHeap = nullptr;
    std::mutex* mpUploadHeapMutex = nullptr;

    // private functions
    void DiskRead(TextureID id, const FTextureRequest& request);
    void LoadFromMemory(TextureID id, const FTextureRequest& request);
    void GenerateMips(TextureID ID);
    void AllocateResource(TextureID ID);
    void QueueUploadToGPUTask(TextureID ID);
    void ScheduleNextTask(TextureID id);

    void TextureUploadThread_Main();
    void ProcessTextureUploadQueue();
    void ProcessTextureUpload(const FTextureUploadTask& Task);
};