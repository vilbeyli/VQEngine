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


// Modified version of Fence from AMD / Cauldron
// https://github.com/GPUOpen-LibrariesAndSDKs/Cauldron/blob/fd91cd744d014505daef1780dceee49fd62ce953/src/DX12/base/Fence.cpp

#include "Fence.h"

#include <d3d12.h>

void Fence::Create(ID3D12Device* pDevice, const char* pDebugName)
{
    mFenceValue = 0;
    //ThrowIfFailed(
        pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mpFence))
    //)
    ;
    //SetName(mpFence, pDebugName);
    //mHEvent = CreateEventEx(nullptr, "FENCE_EVENT0", FALSE, EVENT_ALL_ACCESS);
    mHEvent = CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);
}

void Fence::Destroy() 
{
    if(mpFence) mpFence->Release();
    CloseHandle(mHEvent);
}

void Fence::Signal(ID3D12CommandQueue* pCommandQueue)
{
    ++mFenceValue;
    //ThrowIfFailed(
        pCommandQueue->Signal(mpFence, mFenceValue)

    //)
    ;
}


void Fence::WaitOnCPU(UINT64 FenceWaitValue)
{
    if (mpFence->GetCompletedValue() < FenceWaitValue)
    {
        ThrowIfFailed(mpFence->SetEventOnCompletion(FenceWaitValue, mHEvent));
        WaitForSingleObject(mHEvent, INFINITE);
    }
}

void Fence::WaitOnGPU(ID3D12CommandQueue* pCommandQueue)
{
    //ThrowIfFailed(
        pCommandQueue->Wait(mpFence, mFenceValue)
    //);
    ;
}