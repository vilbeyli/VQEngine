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
#include <stdlib.h>


size_t StaticBufferHeap::MEMORY_ALIGNMENT = 256;


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
// STATIC BUFFER HEAP
//
void StaticBufferHeap::Create(ID3D12Device* pDevice, EBufferType type, uint32 totalMemSize, bool bUseVidMem, const char* name)
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
        SetName(mpVidMemBuffer, name);

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

    SetName(mpSysMemBuffer, name);

    mpSysMemBuffer->Map(0, NULL, reinterpret_cast<void**>(&mpData));
}

void StaticBufferHeap::Destroy()
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


bool StaticBufferHeap::AllocBuffer(uint32 numElements, uint32 strideInBytes, const void* pInitData, D3D12_GPU_VIRTUAL_ADDRESS* pBufferLocation, uint32* pSizeOut)
{
    void* pData;
    if (AllocBuffer(numElements, strideInBytes, &pData, pBufferLocation, pSizeOut))
    {
        memcpy(pData, pInitData, static_cast<size_t>(numElements) * strideInBytes);
        return true;
    }
    return false;
}
bool StaticBufferHeap::AllocVertexBuffer(uint32 numVertices, uint32 strideInBytes, const void* pInitData, D3D12_VERTEX_BUFFER_VIEW* pViewOut)
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
bool StaticBufferHeap::AllocIndexBuffer(uint32 numIndices, uint32 strideInBytes, const void* pInitData, D3D12_INDEX_BUFFER_VIEW* pOut)
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


bool StaticBufferHeap::AllocBuffer(uint32 numElements, uint32 strideInBytes, void** ppDataOut, D3D12_GPU_VIRTUAL_ADDRESS* pBufferLocationOut, uint32* pSizeOut)
{
    std::lock_guard<std::mutex> lock(mMtx);

    uint32 size = AlignOffset(numElements * strideInBytes, (uint32)StaticBufferHeap::MEMORY_ALIGNMENT);
    const bool bHeapOutOfMemory = ((mMemOffset + size) > mTotalMemSize);
    //assert( !bHeapOutOfMemory); // if this is hit, initialize heap with a larger size.
    if (bHeapOutOfMemory)
    {
        Log::Error("Static Heap out of memory.");
        MessageBox(NULL, "Out of StaticBufferHeap memory", "Error", MB_ICONERROR | MB_OK);
        PostMessage(NULL, WM_QUIT, NULL, NULL);
        return false;
    }

    *ppDataOut = (void*)(mpData + mMemOffset);

    ID3D12Resource*& pRsc = mbUseVidMem ? mpVidMemBuffer : mpSysMemBuffer;

    *pBufferLocationOut = mMemOffset + pRsc->GetGPUVirtualAddress();
    *pSizeOut = size;

    mMemOffset += size;

    return true;
}
bool StaticBufferHeap::AllocVertexBuffer(uint32 numVertices, uint32 strideInBytes, void** ppDataOut, D3D12_VERTEX_BUFFER_VIEW* pViewOut)
{
    bool bSuccess = AllocBuffer(numVertices, strideInBytes, ppDataOut, &pViewOut->BufferLocation, &pViewOut->SizeInBytes);
    pViewOut->StrideInBytes = bSuccess ? strideInBytes : 0;
    return bSuccess;
}
bool StaticBufferHeap::AllocIndexBuffer(uint32 numIndices, uint32 strideInBytes, void** ppDataOut, D3D12_INDEX_BUFFER_VIEW* pViewOut)
{
    bool bSuccess = AllocBuffer(numIndices, strideInBytes, ppDataOut, &pViewOut->BufferLocation, &pViewOut->SizeInBytes);
    pViewOut->Format = bSuccess 
        ? ((strideInBytes == 4) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT)
        : DXGI_FORMAT_UNKNOWN;
    return bSuccess;
}


void StaticBufferHeap::UploadData(ID3D12GraphicsCommandList* pCmd)
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



//
// DYNAMIC BUFFER HEAP
//
void DynamicBufferHeap::Create(ID3D12Device* pDevice, uint32_t numberOfBackBuffers, uint32_t memTotalSize)
{
    m_memTotalSize = AlignOffset(memTotalSize, 256u);

    m_mem.Create(numberOfBackBuffers, memTotalSize);

    ThrowIfFailed(pDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(memTotalSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_pBuffer)));
    SetName(m_pBuffer, "DynamicBufferHeap::m_pBuffer");

    m_pBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_pData));
}

void DynamicBufferHeap::Destroy()
{
    m_pBuffer->Release();
    m_mem.Destroy();
}

bool DynamicBufferHeap::AllocConstantBuffer(uint32_t size, void** pData, D3D12_GPU_VIRTUAL_ADDRESS* pBufferViewDesc)
{
    size = AlignOffset(size, 256u);

    uint32_t memOffset;
    if (m_mem.Alloc(size, &memOffset) == false)
    {
        Log::Error("Ran out of mem for 'dynamic' buffers, increase the allocated size\n");
        MessageBox(NULL, "Out of DynamicBufferHeap memory", "Error", MB_ICONERROR | MB_OK);
        PostMessage(NULL, WM_QUIT, NULL, NULL);
        return false;
    }

    *pData = (void*)(m_pData + memOffset);
#if _DEBUG
    memset(*pData, 0x77, size);
#endif
    *pBufferViewDesc = m_pBuffer->GetGPUVirtualAddress() + memOffset;

    return true;
}

bool DynamicBufferHeap::AllocVertexBuffer(uint32_t NumVertices, uint32_t strideInBytes, void** ppData, D3D12_VERTEX_BUFFER_VIEW* pView)
{
    uint32_t size = AlignOffset(NumVertices * strideInBytes, 256u);

    uint32_t memOffset;
    if (m_mem.Alloc(size, &memOffset) == false)
        return false;

    *ppData = (void*)(m_pData + memOffset);


    pView->BufferLocation = m_pBuffer->GetGPUVirtualAddress() + memOffset;
    pView->StrideInBytes = strideInBytes;
    pView->SizeInBytes = size;

    return true;
}

bool DynamicBufferHeap::AllocIndexBuffer(uint32_t NumIndices, uint32_t strideInBytes, void** ppData, D3D12_INDEX_BUFFER_VIEW* pView)
{
    uint32_t size = AlignOffset(NumIndices * strideInBytes, 256u);

    uint32_t memOffset;
    if (m_mem.Alloc(size, &memOffset) == false)
        return false;

    *ppData = (void*)(m_pData + memOffset);

    pView->BufferLocation = m_pBuffer->GetGPUVirtualAddress() + memOffset;
    pView->Format = (strideInBytes == 4) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    pView->SizeInBytes = size;

    return true;
}

//--------------------------------------------------------------------------------------
//
// OnBeginFrame
//
//--------------------------------------------------------------------------------------
void DynamicBufferHeap::OnBeginFrame()
{
    m_mem.OnBeginFrame();
}


//
// RING BUFFER
// 
void RingBuffer::Create(uint32_t TotalSize)
{
    m_Head = 0;
    m_AllocatedSize = 0;
    m_TotalSize = TotalSize;
}
uint32_t RingBuffer::PaddingToAvoidCrossOver(uint32_t size)
{
    int tail = GetTail();
    if ((tail + size) > m_TotalSize)
        return (m_TotalSize - tail);
    else
        return 0;
}
bool RingBuffer::Alloc(uint32_t size, uint32_t* pOut)
{
    if (m_AllocatedSize + size <= m_TotalSize)
    {
        if (pOut)
            *pOut = GetTail();

        m_AllocatedSize += size;
        return true;
    }

    assert(false);
    return false;
}
bool RingBuffer::Free(uint32_t size)
{
    if (m_AllocatedSize > size)
    {
        m_Head = (m_Head + size) % m_TotalSize;
        m_AllocatedSize -= size;
        return true;
    }
    return false;
}


//
// RING BUFFER WITH TABS
//
void RingBufferWithTabs::Create(uint32_t numberOfBackBuffers, uint32_t memTotalSize)
{
    m_backBufferIndex = 0;
    m_numberOfBackBuffers = numberOfBackBuffers;

    //init mem per frame tracker
    m_memAllocatedInFrame = 0;
    for (int i = 0; i < 4; i++)
        m_allocatedMemPerBackBuffer[i] = 0;

    m_mem.Create(memTotalSize);
}

void RingBufferWithTabs::Destroy()
{
    m_mem.Free(m_mem.GetSize());
}

bool RingBufferWithTabs::Alloc(uint32_t size, uint32_t* pOut)
{
    uint32_t padding = m_mem.PaddingToAvoidCrossOver(size);
    if (padding > 0)
    {
        m_memAllocatedInFrame += padding;

        if (m_mem.Alloc(padding, NULL) == false) //alloc chunk to avoid crossover, ignore offset        
        {
            return false;  //no mem, cannot allocate apdding
        }
    }

    if (m_mem.Alloc(size, pOut) == true)
    {
        m_memAllocatedInFrame += size;
        return true;
    }
    return false;
}

void RingBufferWithTabs::OnBeginFrame()
{
    m_allocatedMemPerBackBuffer[m_backBufferIndex] = m_memAllocatedInFrame;
    m_memAllocatedInFrame = 0;

    m_backBufferIndex = (m_backBufferIndex + 1) % m_numberOfBackBuffers;

    // free all the entries for the oldest buffer in one go
    uint32_t memToFree = m_allocatedMemPerBackBuffer[m_backBufferIndex];
    m_mem.Free(memToFree);
}