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
#include "../../Libs/VQUtils/Source/Image.h"
#include "../../Libs/VQUtils/Source/Multithreading.h"

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
	TextureCreateDesc(const std::string& name, const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES state, bool bTexIsCubemap = false, bool bGenerateTexMips = false)
		: TexName(name)
		, d3d12Desc(desc)
		, ResourceState(state)
		, bCubemap(bTexIsCubemap)
		, bGenerateMips(bGenerateTexMips)
	{}

	std::string           TexName;
	std::vector<const void*> pDataArray; // mips, arrays
	D3D12_RESOURCE_DESC   d3d12Desc = {};
	D3D12_RESOURCE_STATES ResourceState = D3D12_RESOURCE_STATE_COMMON;
	bool                  bCubemap = false;
	bool                  bGenerateMips = false;
	bool                  bCPUReadback = false;
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

	void Create(ID3D12Device* pDevice, D3D12MA::Allocator* pAllocator, const TextureCreateDesc& desc, bool bCheckAlpha);
	void Destroy();

	void InitializeSRV(uint32 index, CBV_SRV_UAV* pRV, bool bInitAsArrayView = false, bool bInitAsCubeView = false, UINT ShaderComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING, D3D12_SHADER_RESOURCE_VIEW_DESC* pSRVDesc = nullptr);
	void InitializeDSV(uint32 index, DSV* pRV, int ArraySlice = 1);

	inline const ID3D12Resource* GetResource() const { return mpResource; }
	inline       ID3D12Resource* GetResource()       { return mpResource; }
	inline       DXGI_FORMAT     GetFormat()   const { return mFormat; }
	inline       bool            GetUsesAlphaChannel() const { return mbUsesAlphaChannel; }

private:
	friend class VQRenderer;

	D3D12MA::Allocation* mpAlloc = nullptr;
	ID3D12Resource*      mpResource = nullptr;
	Signal               mSignalResident;
	std::atomic<bool>    mbResident = false;

	// some texture desc fields
	bool mbTypelessTexture = false;
	uint mStructuredBufferStride = 0;
	int  mMipMapCount = 1;
	bool mbCubemap = false;
	int  mWidth = 0;
	int  mHeight = 0;
	int  mNumArraySlices = 1;
	bool mbUsesAlphaChannel = false;

	DXGI_FORMAT mFormat = DXGI_FORMAT_UNKNOWN;
};


struct FTextureUploadDesc
{
	FTextureUploadDesc(std::vector<Image>&& imgs_, TextureID texID, const TextureCreateDesc& tDesc) : imgs(imgs_), id(texID), desc(tDesc), pDataArr(tDesc.pDataArray) {}
	FTextureUploadDesc(Image&& img_, TextureID texID, const TextureCreateDesc& tDesc) : imgs(1, img_), id(texID), desc(tDesc), pDataArr(1, img_.pData) {}

	FTextureUploadDesc(const void* pData_, TextureID texID, const TextureCreateDesc& tDesc) : imgs({  }), id(texID), desc(tDesc), pDataArr(1, pData_) {}
	FTextureUploadDesc(std::vector<const void*> pDataArr, TextureID texID, const TextureCreateDesc& tDesc) : imgs({  }), id(texID), desc(tDesc), pDataArr(pDataArr) {}

	FTextureUploadDesc() = delete;

	std::vector<Image> imgs;
	std::vector<const void*> pDataArr;
	TextureID id;
	TextureCreateDesc desc;
};