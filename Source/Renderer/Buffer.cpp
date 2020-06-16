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

#include "Libs/D3DX12/d3dx12.h"

#include <cassert>


size_t StaticBufferPool::MEMORY_ALIGNMENT = 256;


//
// CREATE / DESTROY
//
void StaticBufferPool::Create(ID3D12Device* pDevice, uint32 totalMemSize, bool bUseVidMem, const char* name)
{
    m_pDevice = pDevice;
    m_totalMemSize = totalMemSize;
    m_memOffset = 0;
    m_memInit = 0;
    m_pData = nullptr;
    m_bUseVidMem = bUseVidMem;

    HRESULT hr = {};
    if (bUseVidMem)
    {
        hr = m_pDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(totalMemSize),
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
            nullptr,
            IID_PPV_ARGS(&m_pVidMemBuffer));
        m_pVidMemBuffer->SetName(L"StaticBufferPool::m_pVidMemBuffer");

        if (FAILED(hr))
        {
            assert(false);
            // TODO
        }
    }

    hr = m_pDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(totalMemSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_pSysMemBuffer));

    if (FAILED(hr))
    {
        assert(false);
        // TODO
    }

    m_pSysMemBuffer->SetName(L"StaticBufferPool::m_pSysMemBuffer");

    m_pSysMemBuffer->Map(0, NULL, reinterpret_cast<void**>(&m_pData));
}

void StaticBufferPool::Destroy()
{
    if (m_bUseVidMem)
    {
        if (m_pVidMemBuffer)
        {
            m_pVidMemBuffer->Release();
            m_pVidMemBuffer = nullptr;
        }
    }

    if (m_pSysMemBuffer)
    {
        m_pSysMemBuffer->Release();
        m_pSysMemBuffer = nullptr;
    }
}



//
// ALLOC
//
bool StaticBufferPool::AllocBuffer(uint32 numElements, uint32 strideInBytes, void* pInitData, D3D12_GPU_VIRTUAL_ADDRESS* pBufferLocation, uint32* pSizeOut)
{
    void* pData;
    if (AllocBuffer(numElements, strideInBytes, &pData, pBufferLocation, pSizeOut))
    {
        memcpy(pData, pInitData, static_cast<size_t>(numElements) * strideInBytes);
        return true;
    }
    return false;
}

bool StaticBufferPool::AllocVertexBuffer(uint32 numVertices, uint32 strideInBytes, void* pInitData, D3D12_VERTEX_BUFFER_VIEW* pViewOut)
{
    void* pData = nullptr;
    if (AllocVertexBuffer(numVertices, strideInBytes, &pData, pViewOut))
    {
        memcpy(pData, pInitData, static_cast<size_t>(numVertices) * strideInBytes);
        return true;
    }

    return false;
}

bool StaticBufferPool::AllocIndexBuffer(uint32 numIndices, uint32 strideInBytes, void* pInitData, D3D12_INDEX_BUFFER_VIEW* pOut)
{
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
    assert(m_memOffset + size < m_totalMemSize); // if this is hit, initialize heap with a larger size.

    *ppDataOut = (void*)(m_pData + m_memOffset);

    ID3D12Resource* pRsc = m_bUseVidMem ? m_pVidMemBuffer : m_pSysMemBuffer;

    *pBufferLocationOut = m_memOffset + pRsc->GetGPUVirtualAddress();
    *pSizeOut = size;

    m_memOffset += size;

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
    if (m_bUseVidMem)
    {
        pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_pVidMemBuffer
            , D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
            , D3D12_RESOURCE_STATE_COPY_DEST)
        );

        pCmd->CopyBufferRegion(m_pVidMemBuffer, m_memInit, m_pSysMemBuffer, m_memInit, m_memOffset - m_memInit);

        // With 'dynamic resources' we can use a same resource to hold Constant, Index and Vertex buffers.
        // That is because we dont need to use a transition.
        //
        // With static buffers though we need to transition them and we only have 2 options
        //      1) D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
        //      2) D3D12_RESOURCE_STATE_INDEX_BUFFER
        // Because we need to transition the whole buffer we cant have now Index buffers to share the 
        // same resource with the Vertex or Constant buffers. Hence is why we need separate classes.
        // For Index and Vertex buffers we *could* use the same resource, but index buffers need their own resource.
        // Please note that in the interest of clarity vertex buffers and constant buffers have been split into two different classes though
        pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_pVidMemBuffer
            , D3D12_RESOURCE_STATE_COPY_DEST
            , D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)
        );

        m_memInit = m_memOffset;
    }
}
