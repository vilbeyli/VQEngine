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

#pragma once

// Adopted from AMD/Cauldron's Static & Dynamic Buffer Pool classes
// https://github.com/GPUOpen-LibrariesAndSDKs/Cauldron/blob/master/src/DX12/base/DynamicBufferRing.h
// https://github.com/GPUOpen-LibrariesAndSDKs/Cauldron/blob/master/src/DX12/base/StaticConstantBufferPool.h
// https://github.com/GPUOpen-LibrariesAndSDKs/Cauldron/blob/master/src/DX12/base/StaticBufferPool.h

// AMD AMDUtils code
// 
// Copyright(c) 2018 Advanced Micro Devices, Inc.All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

//-----------------------------------------------------------------------------------------------------------

#include "ResourceHeaps.h"
#include "Common.h"

#include <mutex>

struct ID3D12Device;
struct ID3D12Resource;
struct D3D12_CONSTANT_BUFFER_VIEW_DESC;
struct ID3D12GraphicsCommandList;

//
// VERTEX BUFFER DEFINITIONS
//
enum EVertexBufferType
{
    DEFAULT = 0,
    COLOR,
    COLOR_AND_ALPHA,
    NORMAL,
    NORMAL_AND_TANGENT,

    NUM_VERTEX_BUFFER_TYPES
};
struct FVertexDefault
{
    float position[3];
    float uv[2];
};
struct FVertexWithColor
{
    float position[3];
    float color[3];
    float uv[2];
};
struct FVertexWithColorAndAlpha
{
    float position[3];
    float color[4];
    float uv[2];
};
struct FVertexWithNormal
{
    float position[3];
    float normal[3];
    float uv[2];
};
struct FVertexWithNormalAndTangent
{
    float position[3];
    float normal[3];
    float tangent[3];
    float uv[2];
};



//
// BUFFER POOL DEFINITIONS
//
enum EBufferType
{
    VERTEX_BUFFER = 0,
    INDEX_BUFFER,
    CONSTANT_BUFFER,

    NUM_BUFFER_TYPES
};

struct FBufferDesc
{
    EBufferType Type;
    uint        NumElements;
    uint        Stride;
    const void* pData;
    std::string Name;
};

class StaticBufferPool
{
    static size_t MEMORY_ALIGNMENT; // TODO: potentially move to renderer settings ini or make a member
public:
    void Create(ID3D12Device* pDevice, EBufferType type, uint32 totalMemSize, bool bUseVidMem, const char* name);
    void Destroy();

    bool AllocBuffer      (uint32 numElements, uint32 strideInBytes, const void* pInitData, D3D12_GPU_VIRTUAL_ADDRESS* pBufferLocationOut, uint32* pSizeOut);
    bool AllocVertexBuffer(uint32 numVertices, uint32 strideInBytes, const void* pInitData, D3D12_VERTEX_BUFFER_VIEW* pViewOut);
    bool AllocIndexBuffer (uint32 numIndices , uint32 strideInBytes, const void* pInitData, D3D12_INDEX_BUFFER_VIEW* pOut);
    //bool AllocConstantBuffer(uint32 size, void* pData, D3D12_CONSTANT_BUFFER_VIEW_DESC* pViewDesc);

    void UploadData(ID3D12GraphicsCommandList* pCmdList);
    //void FreeUploadHeap();

private:
    bool AllocBuffer      (uint32 numElements, uint32 strideInBytes, void** ppDataOut, D3D12_GPU_VIRTUAL_ADDRESS* pBufferLocationOut, uint32* pSizeOut);
    bool AllocVertexBuffer(uint32 numVertices, uint32 strideInBytes, void** ppDataOut, D3D12_VERTEX_BUFFER_VIEW* pViewOut);
    bool AllocIndexBuffer (uint32 numIndices , uint32 strideInBytes, void** ppDataOut, D3D12_INDEX_BUFFER_VIEW* pIndexView);
    //bool AllocConstantBuffer(uint32 size, void** pData, D3D12_CONSTANT_BUFFER_VIEW_DESC* pViewDesc);

private:
    ID3D12Device*  mpDevice = nullptr;
    EBufferType    mType    = EBufferType::NUM_BUFFER_TYPES;
    std::mutex     mMtx;

    bool           mbUseVidMem     = true;
    char*          mpData          = nullptr;
    uint32         mMemInit        = 0;
    uint32         mMemOffset      = 0;
    uint32         mTotalMemSize   = 0;

    ID3D12Resource* mpSysMemBuffer = nullptr;
    ID3D12Resource* mpVidMemBuffer = nullptr;
};

#if 0
class StaticConstantBufferPool
{
public:
    void Create(ID3D12Device* pDevice, uint32 totalMemSize, ResourceViewHeaps* pHeaps, uint32 cbvEntriesSize, bool bUseVidMem);
    void Destroy();
    bool AllocConstantBuffer(uint32 size, void** pData, uint32* pIndex);
    bool CreateCBV(uint32 index, int srvOffset, CBV_SRV_UAV* pCBV);
    void UploadData(ID3D12GraphicsCommandList* pCmdList);
    void FreeUploadHeap();

private:
    ID3D12Device* mpDevice;
    ID3D12Resource* m_pMemBuffer;
    ID3D12Resource* mpSysMemBuffer;
    ID3D12Resource* mpVidMemBuffer;

    char*          mpData;
    uint32         mMemOffset;
    uint32         mTotalMemSize;

    uint32         m_cbvOffset;
    uint32         m_cbvEntriesSize;

    D3D12_CONSTANT_BUFFER_VIEW_DESC* m_pCBVDesc;

    bool            mbUseVidMem;
};

class DynamicBufferRing
{
public:
    void Create(ID3D12Device* pDevice, uint32 numberOfBackBuffers, uint32 memTotalSize, ResourceViewHeaps* pHeaps);
    void Destroy();

    bool AllocIndexBuffer(uint32 numIndices, uint32 strideInBytes, void** pData, D3D12_INDEX_BUFFER_VIEW* pView);
    bool AllocVertexBuffer(uint32 numVertices, uint32 strideInBytes, void** pData, D3D12_VERTEX_BUFFER_VIEW* pView);
    bool AllocConstantBuffer(uint32 size, void** pData, D3D12_GPU_VIRTUAL_ADDRESS* pBufferViewDesc);
    void OnBeginFrame();

private:
    uint32          m_memTotalSize;
    RingWithTabs    m_mem;
    char*           mpData = nullptr;
    ID3D12Resource* m_pBuffer = nullptr;
};
#endif