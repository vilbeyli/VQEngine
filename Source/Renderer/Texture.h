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
class DSV;
class RTV;
struct D3D12_SHADER_RESOURCE_VIEW_DESC;

struct TextureCreateDesc
{
	TextureCreateDesc(const std::string& name) : TexName(name) {}
	ID3D12Device*         pDevice   = nullptr;
	D3D12MA::Allocator*   pAllocator = nullptr;
	UploadHeap*           pUploadHeap = nullptr;
	D3D12_RESOURCE_DESC   Desc = {};
	D3D12_RESOURCE_STATES ResourceState = D3D12_RESOURCE_STATE_COMMON;
	const std::string&    TexName;

};


class Texture
{
public:
	static std::vector<uint8> GenerateTexture_Checkerboard(uint Dimension);

	Texture()  = default;
	~Texture() = default;

	void CreateFromFile(const TextureCreateDesc& desc, const std::string& FilePath);
	void Create(const TextureCreateDesc& desc, const void* pData = nullptr);

	void Destroy();

	void InitializeSRV(uint32 index, CBV_SRV_UAV* pRV, D3D12_SHADER_RESOURCE_VIEW_DESC* pSRVDesc = nullptr);
	void InitializeDSV(uint32 index, DSV* pRV, int ArraySlice = 1);
	void InitializeRTV(uint32 index, RTV* pRV, D3D12_RENDER_TARGET_VIEW_DESC* pRTVDesc = nullptr);
	void InitializeUAV(uint32 index, CBV_SRV_UAV* pRV, D3D12_UNORDERED_ACCESS_VIEW_DESC* pUAVDesc = nullptr, const Texture* pCounterTexture = nullptr);

	inline const ID3D12Resource* GetResource() const { return mpTexture; }
	inline       ID3D12Resource* GetResource()       { return mpTexture; }
public:

private:
	D3D12MA::Allocation* mpAlloc = nullptr;
	ID3D12Resource*      mpTexture = nullptr;
};