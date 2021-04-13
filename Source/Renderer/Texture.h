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

#include <DirectXMath.h>

#include <atomic>
#include <vector>

namespace D3D12MA { class Allocation; class Allocator; }

class UploadHeap;
class CBV_SRV_UAV;
class DSV;
class RTV;
struct D3D12_SHADER_RESOURCE_VIEW_DESC;
struct Image;

struct TextureCreateDesc
{
	TextureCreateDesc(const std::string& name) : TexName(name) {}
	std::string           TexName;
	const void*           pData = nullptr;
	D3D12_RESOURCE_DESC   d3d12Desc = {};
	D3D12_RESOURCE_STATES ResourceState = D3D12_RESOURCE_STATE_COMMON;
	bool                  bCubemap = false;
	bool                  bGenerateMips = false;
};


class Texture
{
public:
	struct CubemapUtility
	{
		// cube face order: https://msdn.microsoft.com/en-us/library/windows/desktop/ff476906(v=vs.85).aspx
		//------------------------------------------------------------------------------------------------------
		// 0: RIGHT		1: LEFT
		// 2: UP		3: DOWN
		// 4: FRONT		5: BACK
		//------------------------------------------------------------------------------------------------------
		enum ECubeMapLookDirections
		{
			CUBEMAP_LOOK_RIGHT = 0,
			CUBEMAP_LOOK_LEFT,
			CUBEMAP_LOOK_UP,
			CUBEMAP_LOOK_DOWN,
			CUBEMAP_LOOK_FRONT,
			CUBEMAP_LOOK_BACK,

			NUM_CUBEMAP_LOOK_DIRECTIONS
		};

		       static DirectX::XMMATRIX CalculateViewMatrix(ECubeMapLookDirections cubeFace, const DirectX::XMFLOAT3& position = DirectX::XMFLOAT3(0,0,0));
		inline static DirectX::XMMATRIX CalculateViewMatrix(int face, const DirectX::XMFLOAT3& position = DirectX::XMFLOAT3(0,0,0)) { return CalculateViewMatrix(static_cast<ECubeMapLookDirections>(face), position); }
	};
	static std::vector<uint8> GenerateTexture_Checkerboard(uint Dimension, bool bUseMidtones = false);

	Texture()  = default;
	~Texture() = default;
	Texture(const Texture& other);
	Texture& operator=(const Texture& other);

	void Create(ID3D12Device* pDevice, D3D12MA::Allocator* pAllocator, const TextureCreateDesc& desc);

	void Destroy();

	void InitializeSRV(uint32 index, CBV_SRV_UAV* pRV, bool bInitAsArrayView = false, bool bInitAsCubeView = false, UINT ShaderComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING, D3D12_SHADER_RESOURCE_VIEW_DESC* pSRVDesc = nullptr);
	void InitializeDSV(uint32 index, DSV* pRV, int ArraySlice = 1);
	void InitializeRTV(uint32 index, RTV* pRV, D3D12_RENDER_TARGET_VIEW_DESC* pRTVDesc = nullptr);
	void InitializeUAV(uint32 index, CBV_SRV_UAV* pRV, D3D12_UNORDERED_ACCESS_VIEW_DESC* pUAVDesc = nullptr, const Texture* pCounterTexture = nullptr);

	inline const ID3D12Resource* GetResource() const { return mpTexture; }
	inline       ID3D12Resource* GetResource()       { return mpTexture; }

private:
	friend class VQRenderer;

	D3D12MA::Allocation* mpAlloc = nullptr;
	ID3D12Resource*      mpTexture = nullptr;
	std::atomic<bool>    mbResident = false;
	
	// some texture desc fields
	bool mbTypelessTexture = false;
	uint mStructuredBufferStride = 0;
	int  mMipMapCount = 1;
	bool mbCubemap = false;
	int  mWidth;
	int  mHeight;
	int  mNumArraySlices;

	DXGI_FORMAT mFormat = DXGI_FORMAT_UNKNOWN;
};