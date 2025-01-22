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

#include "../Engine/Core/Platform.h"

struct ID3D12CommandQueue;
struct ID3D12Fence;
struct ID3D12Device;

class Fence
{
public:
    void Create(ID3D12Device* pDevice, const char* pDebugName);
    void Destroy();

    void Signal(ID3D12CommandQueue* pCommandQueue);
    inline UINT64 GetValue() const { return mFenceValue; }

    void WaitOnCPU(UINT64 olderFence) const;
    void WaitOnGPU(ID3D12CommandQueue* pCommandQueue);
    void WaitOnGPU(ID3D12CommandQueue* pCommandQueue, UINT64 olderFence);

private:
    HANDLE       mHEvent = 0;
    ID3D12Fence* ptr = nullptr;
    UINT64       mFenceValue = 0;
};

