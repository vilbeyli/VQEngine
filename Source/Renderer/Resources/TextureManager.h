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

#include <d3d12.h>
#include <latch>
#include <unordered_map>

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

class TextureManager
{
public:
    void InitializeEarly(std::latch& DeviceInitializedSignal, std::latch& HeapsInitializedSignal);
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
    TaskSignal<void>& GetTextureCompletionSignal(TextureID ID) const;

    void Update(); // Process streaming, uploads

    const FTexture*   GetTexture(TextureID ID) const;
    ID3D12Resource*   GetTextureResource(TextureID ID) const;
    DXGI_FORMAT       GetTextureFormat(TextureID ID) const;
    void              GetTextureDimensions(TextureID ID, int& Width, int& Height, int& Slices, int& Mips) const;
    bool              GetTextureAlphaChannelUsed(TextureID ID) const;
    uint              GetTextureMips(TextureID ID) const;

    SRV_ID GetSRVID(TextureID ID) const;
    DSV_ID GetDSVID(TextureID ID) const;
    UAV_ID GetUAVID(TextureID ID) const;

private:
    struct FTextureState
    {
        FTextureRequest Request;
        std::vector<uint8_t> RawData;                 // From disk
        std::vector<std::vector<uint8_t>> MipData;    // Generated mips
        FTexture Texture;                             // Texture properties
        mutable TaskSignal<void> CPUTasksDoneSignal;  // Disk read, mip generation
        mutable TaskSignal<void> GPUTasksDoneSignal;  // Allocation, upload
        SRV_ID SRVID = INVALID_ID;
        DSV_ID DSVID = INVALID_ID;
        UAV_ID UAVID = INVALID_ID;
        float StreamingPriority = 0.0f;               // For streaming (in the future)
    };
    struct FTextureUploadTask
    {
        TextureID ID;
        std::vector<const void*> DataArray; // Pointers to mip data
        uint32_t MipCount;                  // Number of mips to upload
    };

    TextureID QueueTextureCreation(const FTextureRequest& Request);
    void DiskRead(TextureID ID);
    void GenerateMips(TextureID ID);
    void AllocateResource(TextureID ID);
    void UploadToGPU(TextureID ID);
    void TransitionState(TextureID ID);
    void ProcessTextureUpload(const FTextureUploadTask& Task);
    void ProcessTextureUploadQueue();

    // texture registry
    std::unordered_map<TextureID, FTextureState> mTextures;
    mutable std::mutex mTexturesMutex;

    // bookkeeping
    std::unordered_map<std::string, TextureID> mLoadedTexturePaths;
    std::mutex mLoadedTexturePathsMutex;

    // async
    ThreadPool mDiskWorkers;
    ThreadPool mMipWorkers;
    ThreadPool mGPUWorkers;

    std::queue<FTextureUploadTask> mUploadQueue;
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
    std::mutex* mpUploadQueueMutex = nullptr;
    std::latch* mpDeviceInitializedLatch = nullptr;
    std::latch* mpHeapsInitializedLatch = nullptr;
};