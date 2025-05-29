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

// Borrowed from: https://github.com/GPUOpen-LibrariesAndSDKs/Cauldron/blob/fd91cd744d014505daef1780dceee49fd62ce953/src/DX12/base/ResourceViewHeaps.h

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
#pragma once

#include "stdafx.h"
#include "Engine/Core/Types.h"

typedef void* HANDLE;

class ResourceView;
struct ID3D12Device;
struct ID3D12DescriptorHeap;
struct ID3D12CommandQueue;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;
struct ID3D12Fence;
struct ID3D12CommandAllocator;


enum EResourceHeapType
{
    RTV_HEAP = 0,
    DSV_HEAP,
    CBV_SRV_UAV_HEAP,
    SAMPLER_HEAP,

    NUM_HEAP_TYPES
};

class StaticResourceViewHeap
{
public:
    void Create(ID3D12Device* pDevice, const char* ResourceName, EResourceHeapType EHeapType, uint32 Capacity, bool CPUVisible = false);
    void Destroy();

    bool AllocateDescriptor(uint32 Count, ResourceView* pRV);
    void FreeDescriptor(ResourceView* pRV);

    inline ID3D12DescriptorHeap* GetHeap() const { return mpHeap; }

private:
    uint32 mCapacity;
    uint32 mDescriptorElementSize;
    std::vector<bool> mIsDescriptorFree;

    ID3D12DescriptorHeap* mpHeap;
    bool mbGPUVisible;
    EResourceHeapType mHeapType;
};

// Creates one Upload heap and suballocates memory from the heap
class UploadHeap
{
public:
    void Create(ID3D12Device* pDevice, size_t uSize, ID3D12CommandQueue* pQueue);
    void Destroy();

    uint8* Suballocate(size_t uSize, uint64 uAlign);

    inline uint8*                     BasePtr()         const { return mpDataBegin; }
    inline ID3D12Resource*            GetResource()     const { return mpUploadHeap; }
    inline ID3D12GraphicsCommandList* GetCommandList()  const { return mpCommandList; }

    void UploadToGPUAndWait(ID3D12CommandQueue* pCmdQueue = nullptr);

private:
    ID3D12Device*              mpDevice     = nullptr;
    ID3D12Resource*            mpUploadHeap = nullptr;
    ID3D12CommandQueue*        mpQueue      = nullptr;

    ID3D12GraphicsCommandList* mpCommandList      = nullptr;
    ID3D12CommandAllocator*    mpCommandAllocator = nullptr;

    uint8*                     mpDataCur   = nullptr;
    uint8*                     mpDataEnd   = nullptr;
    uint8*                     mpDataBegin = nullptr;

    ID3D12Fence*               ptr = nullptr;
    uint64                     mFenceValue = 0;
    HANDLE                     mHEvent;
};
