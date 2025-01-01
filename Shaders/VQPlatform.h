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
#ifndef PLATFORM_H
#define PLATFORM_H

#ifndef VQ_GPU
#define VQ_CPU 1
#endif

#ifdef VQ_CPU
#include <array>
#include <DirectXMath.h>
// HLSL types for CPU
#define float4   DirectX::XMFLOAT4
#define float3   DirectX::XMFLOAT3
#define float2   DirectX::XMFLOAT2
#define int2     DirectX::XMINT2
#define int3     DirectX::XMINT3
#define int4     DirectX::XMINT4
#define uint4    DirectX::XMUINT4;
#define matrix   DirectX::XMMATRIX
#define float3x3 DirectX::XMFLOAT3X3
#define float4x3 DirectX::XMFLOAT4X3
#define float3x4 DirectX::XMFLOAT3X4
namespace VQ_SHADER_DATA {
#endif



#ifdef VQ_CPU
} // namespace VQ_SHADER_DATA
#endif

#endif // PLATFORM_H