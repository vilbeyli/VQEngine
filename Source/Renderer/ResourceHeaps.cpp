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

#include "ResourceHeaps.h"
#include "ResourceViews.h"

#include "Libs/D3DX12/d3dx12.h"
#include "../../Libs/VQUtils/Source/Log.h"

#include <cassert>

//--------------------------------------------------------------------------------------
//
// StaticResourceViewHeap
//
//--------------------------------------------------------------------------------------

#include "../../Libs/VQUtils/Source/utils.h"
void StaticResourceViewHeap::Create(ID3D12Device* pDevice, const std::string& ResourceName, D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32 descriptorCount, bool forceCPUVisible)
{
    this->mDescriptorCount = descriptorCount;
    this->mIndex = 0;
        
    this->mDescriptorElementSize = pDevice->GetDescriptorHandleIncrementSize(heapType);

    D3D12_DESCRIPTOR_HEAP_DESC descHeap;
    descHeap.NumDescriptors = descriptorCount;
    descHeap.Type = heapType;
    descHeap.Flags = ((heapType == D3D12_DESCRIPTOR_HEAP_TYPE_RTV) || (heapType == D3D12_DESCRIPTOR_HEAP_TYPE_DSV)) 
        ? D3D12_DESCRIPTOR_HEAP_FLAG_NONE 
        : D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    
    if (forceCPUVisible)
    {
        descHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    }
    descHeap.NodeMask = 0;

    pDevice->CreateDescriptorHeap(&descHeap, IID_PPV_ARGS(&mpHeap));
    this->mbGPUVisible = descHeap.Flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    this->mpHeap->SetName(StrUtil::ASCIIToUnicode(ResourceName).c_str());
}

void StaticResourceViewHeap::Destroy()
{
    mpHeap->Release();
}

bool StaticResourceViewHeap::AllocDescriptor(uint32 size, ResourceView* pRV)
{
    if ((mIndex + size) > mDescriptorCount)
    {
        assert(!"StaticResourceViewHeapDX12 heap ran of memory, increase its size");
        return false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE CPUView = mpHeap->GetCPUDescriptorHandleForHeapStart();
    CPUView.ptr += mIndex * mDescriptorElementSize;

    
    D3D12_GPU_DESCRIPTOR_HANDLE GPUView = mbGPUVisible ? mpHeap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{};
    GPUView.ptr += mbGPUVisible ? mIndex * mDescriptorElementSize : GPUView.ptr;

    mIndex += size;

    pRV->SetResourceView(size, mDescriptorElementSize, CPUView, GPUView);

    return true;
}


// ===========================================================================================================================================


//--------------------------------------------------------------------------------------
//
// UploadHeap
//
//--------------------------------------------------------------------------------------
void UploadHeap::Create(ID3D12Device* pDevice, SIZE_T uSize, ID3D12CommandQueue* pQueue)
{
    mpDevice = pDevice;
    mpQueue = pQueue;

    // Create command list and allocators 

    pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mpCommandAllocator));
    mpCommandAllocator->SetName(L"UploadHeap::mpCommandAllocator");
    pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mpCommandAllocator, nullptr, IID_PPV_ARGS(&mpCommandList));
    mpCommandList->SetName(L"UploadHeap::mpCommandList");

    // Create buffer to suballocate
    HRESULT hr = {};
    hr = pDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(uSize),
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

    hr = pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mpFence));
    mHEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    mFenceValue = 1;
}

void UploadHeap::Destroy()
{
    mpUploadHeap->Release();

    if (mpFence) mpFence->Release();

    mpCommandList->Release();
    mpCommandAllocator->Release();
}


UINT8* UploadHeap::Suballocate(SIZE_T uSize, UINT64 uAlign)
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
    if (!pCmdQueue)
    {
        pCmdQueue = this->mpQueue;
    }

    mpCommandList->Close();
    pCmdQueue->ExecuteCommandLists(1, CommandListCast(&mpCommandList));
    pCmdQueue->Signal(mpFence, mFenceValue);
    if (mpFence->GetCompletedValue() < mFenceValue)
    {
        mpFence->SetEventOnCompletion(mFenceValue, mHEvent);
        WaitForSingleObject(mHEvent, INFINITE);
    }

    ++mFenceValue;

    // Reset so it can be reused
    mpCommandAllocator->Reset();
    mpCommandList->Reset(mpCommandAllocator, nullptr);

    mpDataCur = mpDataBegin;
}