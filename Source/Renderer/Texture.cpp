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

#include "Texture.h"
#include "ResourceHeaps.h"
#include "Libs/D3DX12/d3dx12.h"

#include "../../Libs/D3D12MA/src/D3D12MemAlloc.h"

#include <cassert>


void Texture::CreateFromFile(const TextureDesc& desc, const std::string FilePath)
{
    assert(false);
}

void Texture::CreateFromData(const TextureDesc& desc, void* pData)
{
    assert(pData);
    HRESULT hr = {};

    ID3D12GraphicsCommandList* pCmd = desc.pUploadHeap->GetCommandList();

    // Create resource
    D3D12MA::ALLOCATION_DESC textureAllocDesc = {};
    textureAllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
    hr = desc.pAllocator->CreateResource(
        &textureAllocDesc,
        &desc.Desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, // pOptimizedClearValue
        &mpAlloc,
        IID_PPV_ARGS(&mpTexture));
    SetName(mpTexture, desc.TexName.c_str());
    if (FAILED(hr))
    {
        Log::Error("Couldn't create texture: ", desc.TexName.c_str());
        return;
    }

    // upload the data
    if (pData)
    {
        const UINT64 UploadBufferSize = GetRequiredIntermediateSize(mpTexture, 0, 1);

#if 1
        UINT8* pUploadBufferMem = desc.pUploadHeap->Suballocate(SIZE_T(UploadBufferSize), D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        if (pUploadBufferMem == NULL)
        {

            // TODO:
            // We ran out of mem in the upload heap, flush it and try allocating mem from it again
#if 0
            desc.pUploadHeap->UploadToGPUAndWait();
            pUploadBufferMem = desc.pUploadHeap->Suballocate(SIZE_T(UploadBufferSize), D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
            assert(pUploadBufferMem);
#else
            assert(false);
#endif    
        }

        UINT64 UplHeapSize;
        uint32_t num_rows = {};
        UINT64 row_sizes_in_bytes = {};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT placedTex2D = {};
        desc.pDevice->GetCopyableFootprints(&desc.Desc, 0, 1, 0, &placedTex2D, &num_rows, &row_sizes_in_bytes, &UplHeapSize);
        placedTex2D.Offset += UINT64(pUploadBufferMem - desc.pUploadHeap->BasePtr());

        // copy all the mip slices into the offsets specified by the footprint structure
        //
        for (uint32_t y = 0; y < num_rows; y++)
        {
            memcpy(pUploadBufferMem + y * placedTex2D.Footprint.RowPitch,
                (UINT8*)pData + y * placedTex2D.Footprint.RowPitch, row_sizes_in_bytes);
        }

        CD3DX12_TEXTURE_COPY_LOCATION Dst(mpTexture, 0);
        CD3DX12_TEXTURE_COPY_LOCATION Src(desc.pUploadHeap->GetResource(), placedTex2D);
        desc.pUploadHeap->GetCommandList()->CopyTextureRegion(&Dst, 0, 0, 0, &Src, NULL);


#else
        D3D12_SUBRESOURCE_DATA textureSubresourceData = {};
        textureSubresourceData.pData = pData;
        textureSubresourceData.RowPitch = imageBytesPerRow;
        textureSubresourceData.SlicePitch = imageBytesPerRow * desc.Desc.Height;

        UpdateSubresources(pCmd, mpTexture, textureUpload, 0, 0, 1, &textureSubresourceData);
#endif

        D3D12_RESOURCE_BARRIER textureBarrier = {};
        textureBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        textureBarrier.Transition.pResource = mpTexture;
        textureBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        textureBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        textureBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        pCmd->ResourceBarrier(1, &textureBarrier);
    }
    
}

void Texture::Destroy()
{
    if (mpTexture)
    {
        mpTexture->Release(); 
        mpTexture = nullptr;
    }
    if (mpAlloc)
    {
        mpAlloc->Release();
        mpAlloc = nullptr;
    }
}


#if 0
bool Texture::InitFromData(Device* pDevice, const char* pDebugName, UploadHeap& uploadHeap, const IMG_INFO& header, const void* data)
{
    assert(!m_pResource);
    assert(header.arraySize == 1 && header.mipMapCount == 1);

    m_header = header;

    CreateTextureCommitted(pDevice, pDebugName, false);

    // Get mip footprints (if it is an array we reuse the mip footprints for all the elements of the array)
    //
    UINT64 UplHeapSize;
    uint32_t num_rows = {};
    UINT64 row_sizes_in_bytes = {};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT placedTex2D = {};
    CD3DX12_RESOURCE_DESC RDescs = CD3DX12_RESOURCE_DESC::Tex2D((DXGI_FORMAT)m_header.format, m_header.width, m_header.height, 1, 1);
    pDevice->GetDevice()->GetCopyableFootprints(&RDescs, 0, 1, 0, &placedTex2D, &num_rows, &row_sizes_in_bytes, &UplHeapSize);

    //compute pixel size
    //
    //UINT32 bytePP = m_header.bitCount / 8;

    // allocate memory for mip chain from upload heap
    //
    UINT8* pUploadBufferMem = uploadHeap.Suballocate(SIZE_T(UplHeapSize), D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
    if (pUploadBufferMem == NULL)
    {
        // oh! We ran out of mem in the upload heap, flush it and try allocating mem from it again
        uploadHeap.FlushAndFinish();
        pUploadBufferMem = uploadHeap.Suballocate(SIZE_T(UplHeapSize), D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        assert(pUploadBufferMem);
    }

    placedTex2D.Offset += UINT64(pUploadBufferMem - uploadHeap.BasePtr());

    // copy all the mip slices into the offsets specified by the footprint structure
    //
    for (uint32_t y = 0; y < num_rows; y++)
    {
        memcpy(pUploadBufferMem + y * placedTex2D.Footprint.RowPitch, (UINT8*)data + y * placedTex2D.Footprint.RowPitch, row_sizes_in_bytes);
    }

    CD3DX12_TEXTURE_COPY_LOCATION Dst(m_pResource, 0);
    CD3DX12_TEXTURE_COPY_LOCATION Src(uploadHeap.GetResource(), placedTex2D);
    uploadHeap.GetCommandList()->CopyTextureRegion(&Dst, 0, 0, 0, &Src, NULL);

    // prepare to shader read
    //
    D3D12_RESOURCE_BARRIER RBDesc = {};
    RBDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    RBDesc.Transition.pResource = m_pResource;
    RBDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    RBDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    RBDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    uploadHeap.GetCommandList()->ResourceBarrier(1, &RBDesc);

    return true;
}



// Create Builtin textures.
{
    // Describe and create a Texture2D.
    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.Width = TextureWidth;
    textureDesc.Height = TextureHeight;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_texture)));

    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_texture.Get(), 0, 1);

    // Create the GPU upload buffer.
    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&textureUploadHeap)));

    // Copy data to the intermediate upload heap and then schedule a copy 
    // from the upload heap to the Texture2D.
    std::vector<UINT8> texture = GenerateTextureData();

    D3D12_SUBRESOURCE_DATA textureData = {};
    textureData.pData = &texture[0];
    textureData.RowPitch = TextureWidth * TexturePixelSize;
    textureData.SlicePitch = textureData.RowPitch * TextureHeight;

    UpdateSubresources(m_commandList.Get(), m_texture.Get(), textureUploadHeap.Get(), 0, 0, 1, &textureData);
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    // Describe and create a SRV for the texture.
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = textureDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    m_device->CreateShaderResourceView(m_texture.Get(), &srvDesc, m_srvHeap->GetCPUDescriptorHandleForHeapStart());
    }
#endif



void Texture::CreateSRV(uint32 index, CBV_SRV_UAV* pRV, D3D12_SHADER_RESOURCE_VIEW_DESC* pSRVDesc)
{
    ID3D12Device* pDevice;
    mpTexture->GetDevice(__uuidof(*pDevice), reinterpret_cast<void**>(&pDevice));

    pDevice->CreateShaderResourceView(mpTexture, pSRVDesc, pRV->GetCPUDescHandle(index));

    pDevice->Release();
}

#if 0
void Texture::CreateRTV(uint32_t index, RTV* pRV, D3D12_RENDER_TARGET_VIEW_DESC* pRtvDesc)
{
    ID3D12Device* pDevice;
    m_pResource->GetDevice(__uuidof(*pDevice), reinterpret_cast<void**>(&pDevice));

    pDevice->CreateRenderTargetView(m_pResource, pRtvDesc, pRV->GetCPUDescHandle(index));

    pDevice->Release();
}

void Texture::CreateUAV(uint32_t index, Texture* pCounterTex, CBV_SRV_UAV* pRV, D3D12_UNORDERED_ACCESS_VIEW_DESC* pUavDesc)
{
    ID3D12Device* pDevice;
    m_pResource->GetDevice(__uuidof(*pDevice), reinterpret_cast<void**>(&pDevice));

    pDevice->CreateUnorderedAccessView(m_pResource, pCounterTex ? pCounterTex->GetResource() : NULL, pUavDesc, pRV->GetCPUDescHandle(index));

    pDevice->Release();
}

void Texture::CreateRTV(uint32_t index, RTV* pRV, int mipLevel, int arraySize, int firstArraySlice)
{
    D3D12_RESOURCE_DESC texDesc = m_pResource->GetDesc();
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = texDesc.Format;
    if (texDesc.DepthOrArraySize == 1)
    {
        assert(arraySize == -1);
        assert(firstArraySlice == -1);
        if (texDesc.SampleDesc.Count == 1)
        {
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Texture2D.MipSlice = (mipLevel == -1) ? 0 : mipLevel;
        }
        else
        {
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
            assert(mipLevel == -1);
        }
    }
    else
    {
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        rtvDesc.Texture2DArray.ArraySize = arraySize;
        rtvDesc.Texture2DArray.FirstArraySlice = firstArraySlice;
        rtvDesc.Texture2DArray.MipSlice = (mipLevel == -1) ? 0 : mipLevel;
    }

    CreateRTV(index, pRV, &rtvDesc);
}
#endif

// ================================================================================================================================================

//
// STATIC
//

// from Microsoft's D3D12HelloTexture
std::vector<uint8> Texture::GenerateTexture_Checkerboard(uint Dimension)
{
    constexpr UINT TexturePixelSizeInBytes = 4; // byte/px
    const UINT& TextureWidth = Dimension;
    const UINT& TextureHeight = Dimension;

    const UINT rowPitch = TextureWidth * TexturePixelSizeInBytes;
    const UINT cellPitch = rowPitch >> 3;        // The width of a cell in the checkboard texture.
    const UINT cellHeight = TextureWidth >> 3;    // The height of a cell in the checkerboard texture.
    const UINT textureSize = rowPitch * TextureHeight;

    std::vector<UINT8> data(textureSize);
    UINT8* pData = &data[0];

    for (UINT n = 0; n < textureSize; n += TexturePixelSizeInBytes)
    {
        UINT x = n % rowPitch;
        UINT y = n / rowPitch;
        UINT i = x / cellPitch;
        UINT j = y / cellHeight;

        if (i % 2 == j % 2)
        {
            pData[n + 0] = 0x00;    // R
            pData[n + 1] = 0x00;    // G
            pData[n + 2] = 0x00;    // B
            pData[n + 3] = 0xff;    // A
        }
        else
        {
            pData[n + 0] = 0xff;    // R
            pData[n + 1] = 0xff;    // G
            pData[n + 2] = 0xff;    // B
            pData[n + 3] = 0xff;    // A
        }
    }

    return data;
}


