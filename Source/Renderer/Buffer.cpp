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

#include "Buffer.h"
#include "Common.h"

#include "Libs/D3DX12/d3dx12.h"

#include <cassert>


size_t StaticBufferPool::MEMORY_ALIGNMENT = 256;


static D3D12_RESOURCE_STATES GetResourceTransitionState(EBufferType eType)
{
    D3D12_RESOURCE_STATES s;
    switch (eType)
    {
        case CONSTANT_BUFFER : s = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER; break;
        case VERTEX_BUFFER   : s = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER; break;
        case INDEX_BUFFER    : s = D3D12_RESOURCE_STATE_INDEX_BUFFER; break;
        default:
            Log::Warning("StaticBufferPool::Create(): unkown resource type, couldn't determine resource transition state for upload.");
            break;
        }
    return s;
}

//
// CREATE / DESTROY
//
void StaticBufferPool::Create(ID3D12Device* pDevice, EBufferType type, uint32 totalMemSize, bool bUseVidMem, const char* name)
{
    mpDevice = pDevice;
    mTotalMemSize = totalMemSize;
    mMemOffset = 0;
    mMemInit = 0;
    mpData = nullptr;
    mbUseVidMem = bUseVidMem;
    mType = type;

    HRESULT hr = {};
    if (bUseVidMem)
    {
        hr = mpDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(totalMemSize),
            GetResourceTransitionState(mType),
            nullptr,
            IID_PPV_ARGS(&mpVidMemBuffer));
        mpVidMemBuffer->SetName(L"StaticBufferPool::m_pVidMemBuffer");

        if (FAILED(hr))
        {
            assert(false);
            // TODO
        }
    }

    hr = mpDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(totalMemSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mpSysMemBuffer));

    if (FAILED(hr))
    {
        assert(false);
        // TODO
    }

    mpSysMemBuffer->SetName(L"StaticBufferPool::m_pSysMemBuffer");

    mpSysMemBuffer->Map(0, NULL, reinterpret_cast<void**>(&mpData));
}

void StaticBufferPool::Destroy()
{
    if (mbUseVidMem)
    {
        if (mpVidMemBuffer)
        {
            mpVidMemBuffer->Release();
            mpVidMemBuffer = nullptr;
        }
    }

    if (mpSysMemBuffer)
    {
        mpSysMemBuffer->Release();
        mpSysMemBuffer = nullptr;
    }
}



//
// ALLOC
//
bool StaticBufferPool::AllocBuffer(uint32 numElements, uint32 strideInBytes, const void* pInitData, D3D12_GPU_VIRTUAL_ADDRESS* pBufferLocation, uint32* pSizeOut)
{
    void* pData;
    if (AllocBuffer(numElements, strideInBytes, &pData, pBufferLocation, pSizeOut))
    {
        memcpy(pData, pInitData, static_cast<size_t>(numElements) * strideInBytes);
        return true;
    }
    return false;
}

bool StaticBufferPool::AllocVertexBuffer(uint32 numVertices, uint32 strideInBytes, const void* pInitData, D3D12_VERTEX_BUFFER_VIEW* pViewOut)
{
    assert(mType == EBufferType::VERTEX_BUFFER);
    void* pData = nullptr;
    if (AllocVertexBuffer(numVertices, strideInBytes, &pData, pViewOut))
    {
        memcpy(pData, pInitData, static_cast<size_t>(numVertices) * strideInBytes);
        return true;
    }

    return false;
}

bool StaticBufferPool::AllocIndexBuffer(uint32 numIndices, uint32 strideInBytes, const void* pInitData, D3D12_INDEX_BUFFER_VIEW* pOut)
{
    assert(mType == EBufferType::INDEX_BUFFER);
    void* pData = nullptr;
    if (AllocIndexBuffer(numIndices, strideInBytes, &pData, pOut))
    {
        memcpy(pData, pInitData, static_cast<size_t>(strideInBytes) * numIndices);
        return true;
    }
    return false;
}



bool StaticBufferPool::AllocBuffer(uint32 numElements, uint32 strideInBytes, void** ppDataOut, D3D12_GPU_VIRTUAL_ADDRESS* pBufferLocationOut, uint32* pSizeOut)
{
    std::lock_guard<std::mutex> lock(mMtx);

    uint32 size = AlignOffset(numElements * strideInBytes, (uint32)StaticBufferPool::MEMORY_ALIGNMENT);
    assert(mMemOffset + size < mTotalMemSize); // if this is hit, initialize heap with a larger size.

    *ppDataOut = (void*)(mpData + mMemOffset);

    ID3D12Resource*& pRsc = mbUseVidMem ? mpVidMemBuffer : mpSysMemBuffer;

    *pBufferLocationOut = mMemOffset + pRsc->GetGPUVirtualAddress();
    *pSizeOut = size;

    mMemOffset += size;

    return true;
}

bool StaticBufferPool::AllocVertexBuffer(uint32 numVertices, uint32 strideInBytes, void** ppDataOut, D3D12_VERTEX_BUFFER_VIEW* pViewOut)
{
    bool bSuccess = AllocBuffer(numVertices, strideInBytes, ppDataOut, &pViewOut->BufferLocation, &pViewOut->SizeInBytes);
    pViewOut->StrideInBytes = bSuccess ? strideInBytes : 0;
    return bSuccess;
}

bool StaticBufferPool::AllocIndexBuffer(uint32 numIndices, uint32 strideInBytes, void** ppDataOut, D3D12_INDEX_BUFFER_VIEW* pViewOut)
{
    bool bSuccess = AllocBuffer(numIndices, strideInBytes, ppDataOut, &pViewOut->BufferLocation, &pViewOut->SizeInBytes);
    pViewOut->Format = bSuccess 
        ? ((strideInBytes == 4) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT)
        : DXGI_FORMAT_UNKNOWN;
    return bSuccess;
}




//
// UPLOAD
//
void StaticBufferPool::UploadData(ID3D12GraphicsCommandList* pCmd)
{
    if (mbUseVidMem)
    {
        D3D12_RESOURCE_STATES state = GetResourceTransitionState(mType);

        pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mpVidMemBuffer
            , state
            , D3D12_RESOURCE_STATE_COPY_DEST)
        );

        pCmd->CopyBufferRegion(mpVidMemBuffer, mMemInit, mpSysMemBuffer, mMemInit, mMemOffset - mMemInit);

        pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mpVidMemBuffer
            , D3D12_RESOURCE_STATE_COPY_DEST
            , state)
        );

        mMemInit = mMemOffset;
    }
}
