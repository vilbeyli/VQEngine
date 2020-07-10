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

#include "Common.h"

#include <vector>

namespace D3D12MA { class Allocation; class Allocator; }

class UploadHeap;
class CBV_SRV_UAV;
struct D3D12_SHADER_RESOURCE_VIEW_DESC;

struct TextureCreateDesc
{
	ID3D12Device*         pDevice   = nullptr;
	D3D12MA::Allocator*   pAllocator = nullptr;
	UploadHeap*           pUploadHeap = nullptr;
	D3D12_RESOURCE_DESC   Desc = {};
	std::string           TexName;
};


class Texture
{
public:
	static std::vector<uint8> GenerateTexture_Checkerboard(uint Dimension);

	Texture()  = default;
	~Texture() = default;

	void CreateFromFile(const TextureCreateDesc& desc, const std::string& FilePath);
	void CreateFromData(const TextureCreateDesc& desc, const void* pData);
	void CreateDepthBuffer(const TextureCreateDesc& desc);

	void Destroy();

	void CreateSRV(uint32 index, CBV_SRV_UAV* pRV, D3D12_SHADER_RESOURCE_VIEW_DESC* pSRVDesc = nullptr);

public:

private:
	D3D12MA::Allocation* mpAlloc = nullptr;
	ID3D12Resource*      mpTexture = nullptr;
};