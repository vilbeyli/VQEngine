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

// Borrowed from: https://github.com/GPUOpen-LibrariesAndSDKs/Cauldron/blob/fd91cd744d014505daef1780dceee49fd62ce953/src/DX12/base/ResourceViewHeaps.cpp

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

#include "Resources/ResourceHeaps.h"
#include "Resources/ResourceViews.h"
#include "Core/Common.h"

#include "Libs/D3DX12/d3dx12.h"
#include "Engine/GPUMarker.h"
#include "Libs/VQUtils/Source/utils.h"

//--------------------------------------------------------------------------------------
//
// StaticResourceViewHeap
//
//--------------------------------------------------------------------------------------

void StaticResourceViewHeap::Create(ID3D12Device* pDevice, const char* ResourceName, EResourceHeapType HeapTypeIn, uint32 DescriptorCount, bool CPUVisible /*= false*/)
{
    D3D12_DESCRIPTOR_HEAP_TYPE HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
    switch (HeapTypeIn)
    {
    case RTV_HEAP         : HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; break;
    case DSV_HEAP         : HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; break;
    case CBV_SRV_UAV_HEAP : HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; break;
    case SAMPLER_HEAP     : HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER; break;
    }

    this->mCapacity = DescriptorCount;
    this->mIsDescriptorFree.resize(DescriptorCount, true);
    this->mDescriptorElementSize = pDevice->GetDescriptorHandleIncrementSize(HeapType);
    this->mHeapType = HeapTypeIn;

    D3D12_DESCRIPTOR_HEAP_DESC DescHeap;
    DescHeap.NumDescriptors = DescriptorCount;
    DescHeap.Type = HeapType;
    DescHeap.Flags = ((HeapType == D3D12_DESCRIPTOR_HEAP_TYPE_RTV) || (HeapType == D3D12_DESCRIPTOR_HEAP_TYPE_DSV)) 
        ? D3D12_DESCRIPTOR_HEAP_FLAG_NONE 
        : D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    this->mbGPUVisible = DescHeap.Flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    
    if (CPUVisible)
    {
        DescHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    }
    DescHeap.NodeMask = 0;

    HRESULT hr = pDevice->CreateDescriptorHeap(&DescHeap, IID_PPV_ARGS(&mpHeap));
    if (FAILED(hr))
    {
        Log::Error("CreateDescriptorHeap() failed");
        return;
    }
    this->mpHeap->SetName(StrUtil::ASCIIToUnicode(ResourceName).c_str());
}

void StaticResourceViewHeap::Destroy()
{
    if (mpHeap)
    {
        mpHeap->Release();
        mpHeap = nullptr;
    }
    mIsDescriptorFree.clear();
}

bool StaticResourceViewHeap::AllocateDescriptor(uint32 Count, ResourceView* pRV)
{
    uint32 iStart = 0;
    bool bFound = false;

    while (iStart <= mCapacity - Count)
    {
        bFound = true;
        for (uint32 i = 0; i < Count; ++i)
        {
            if (!mIsDescriptorFree[iStart + i])
            {
                iStart += i + 1;
                bFound = false;
                break;
            }
        }

        if (bFound)
        {
            break;
        }
    }
    
    if (!bFound)
    {
        Log::Error("CreateDescriptorHeap() failed: couldn't find contiguous descriptor block");
        return false;
    }

    // mark allocated
    for (uint32 i = 0; i < Count; ++i)
    {
        mIsDescriptorFree[iStart + i] = false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = mpHeap->GetCPUDescriptorHandleForHeapStart();
    CPUHandle.ptr += static_cast<size_t>(iStart) * mDescriptorElementSize;

    D3D12_GPU_DESCRIPTOR_HANDLE GPUHandle = mbGPUVisible ? mpHeap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{};
    GPUHandle.ptr += mbGPUVisible ? static_cast<size_t>(iStart) * mDescriptorElementSize : 0;

    pRV->SetResourceView(Count, mDescriptorElementSize, CPUHandle, GPUHandle);

    return true;
}

void StaticResourceViewHeap::FreeDescriptor(ResourceView* pRV)
{
    assert(pRV);
    if (!pRV) 
        return;

    const size_t iRV = (pRV->GetCPUDescHandle().ptr - mpHeap->GetCPUDescriptorHandleForHeapStart().ptr) / mDescriptorElementSize;
    for (uint32_t i = 0; i < pRV->GetSize(); ++i) 
    {
        mIsDescriptorFree[iRV + i] = true;
    }
}


// ===========================================================================================================================================


//--------------------------------------------------------------------------------------
//
// UploadHeap
//
//--------------------------------------------------------------------------------------
void UploadHeap::Create(ID3D12Device* pDevice, size_t uSize, ID3D12CommandQueue* pQueue)
{
    SCOPED_CPU_MARKER("UploadHeapCreate");
    mpDevice = pDevice;
    mpQueue = pQueue;

    // Create command list and allocators 

    pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mpCommandAllocator));
    mpCommandAllocator->SetName(L"UploadHeap::mpCommandAllocator");
    pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mpCommandAllocator, nullptr, IID_PPV_ARGS(&mpCommandList));
    mpCommandList->SetName(L"UploadHeap::mpCommandList");

    // Create buffer to suballocate
    CD3DX12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uSize);
    HRESULT hr = {};
    hr = pDevice->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mpUploadHeap)
    );
    if (FAILED(hr))
    {
        Log::Error("Couldn't create upload heap.");
        return;
    }

    hr = mpUploadHeap->Map(0, NULL, (void**)&mpDataBegin);
    if (FAILED(hr))
    {
        Log::Error("Couldn't map upload heap.");
        return;
    }

    mpDataCur = mpDataBegin;
    mpDataEnd = mpDataBegin + mpUploadHeap->GetDesc().Width;

    hr = pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&ptr));
    mHEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    mFenceValue = 1;
}

void UploadHeap::Destroy()
{
    mpUploadHeap->Release();

    if (ptr) ptr->Release();

    mpCommandList->Release();
    mpCommandAllocator->Release();
}


UINT8* UploadHeap::Suballocate(size_t uSize, UINT64 uAlign)
{
    mpDataCur = reinterpret_cast<UINT8*>(AlignOffset(reinterpret_cast<SIZE_T>(mpDataCur), SIZE_T(uAlign)));

    // return NULL if we ran out of space in the heap
    if (mpDataCur >= mpDataEnd || mpDataCur + uSize >= mpDataEnd)
    {
        return NULL;
    }

    UINT8* pRet = mpDataCur;
    mpDataCur += uSize;
    return pRet;
}

void UploadHeap::UploadToGPUAndWait(ID3D12CommandQueue* pCmdQueue /* =nullptr */)
{
    SCOPED_CPU_MARKER("UploadToGPUAndWait");
    if (!pCmdQueue)
    {
        pCmdQueue = this->mpQueue;
    }

    mpCommandList->Close();
    pCmdQueue->ExecuteCommandLists(1, CommandListCast(&mpCommandList));
    pCmdQueue->Signal(ptr, mFenceValue);
    if (ptr->GetCompletedValue() < mFenceValue)
    {
        SCOPED_CPU_MARKER_C("Wait", 0xFF0000AA);
        ptr->SetEventOnCompletion(mFenceValue, mHEvent);
        WaitForSingleObject(mHEvent, INFINITE);
    }

    ++mFenceValue;

    // Reset so it can be reused
    mpCommandAllocator->Reset();
    mpCommandList->Reset(mpCommandAllocator, nullptr);

    mpDataCur = mpDataBegin;
}