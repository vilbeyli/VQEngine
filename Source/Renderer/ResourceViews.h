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

#include "Common.h"

#include <d3d12.h>

class ResourceView
{
public:
    inline uint32 GetSize() const { return mSize; }
    inline D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescHandle(uint32 i = 0) const { return D3D12_CPU_DESCRIPTOR_HANDLE{ mCPUDescriptor.ptr + static_cast<uint64>(i)*mDescriptorSize }; }
    inline D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescHandle(uint32 i = 0) const { return D3D12_GPU_DESCRIPTOR_HANDLE{ mGPUDescriptor.ptr + static_cast<uint64>(i)*mDescriptorSize }; }

    inline void SetResourceView(uint32 size, uint32 dsvDescriptorSize, D3D12_CPU_DESCRIPTOR_HANDLE CPUDescriptor, D3D12_GPU_DESCRIPTOR_HANDLE GPUDescriptor)
    {
        mSize = size;
        mCPUDescriptor = CPUDescriptor;
        mGPUDescriptor = GPUDescriptor;
        mDescriptorSize = dsvDescriptorSize;
    }

private:
    uint32 mSize = 0;
    uint32 mDescriptorSize = 0;

    D3D12_CPU_DESCRIPTOR_HANDLE mCPUDescriptor;
    D3D12_GPU_DESCRIPTOR_HANDLE mGPUDescriptor;
};

using VBV = D3D12_VERTEX_BUFFER_VIEW;
using IBV = D3D12_INDEX_BUFFER_VIEW;
class RTV         : public ResourceView { };
class DSV         : public ResourceView { };
class CBV_SRV_UAV : public ResourceView { };
class SAMPLER     : public ResourceView { };
using SRV = CBV_SRV_UAV;
using CBV = CBV_SRV_UAV;
using UAV = CBV_SRV_UAV;    

