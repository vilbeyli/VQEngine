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
#include <DirectXMath.h>

#include "../Core/Types.h"
#include "Shaders/LightingConstantBufferData.h"

enum EMaterialTextureMapBindings
{
	ALBEDO = 0,
	NORMALS,
	EMISSIVE,
	// HEIGHT,
	// SPECULAR,
	ALPHA_MASK,
	METALLIC,
	ROUGHNESS,
	OCCLUSION_ROUGHNESS_METALNESS,
	AMBIENT_OCCLUSION,

	NUM_MATERIAL_TEXTURE_MAP_BINDINGS
};

struct Material // 56 Bytes
{
	//------------------------------------------------------------
	DirectX::XMFLOAT3 diffuse           = {1, 1, 1};   // 12 Bytes
	float             alpha             = 1.0f;        // 4  Bytes
	//------------------------------------------------------------
	DirectX::XMFLOAT3 specular          = { 1, 1, 1 }; // 12 Bytes
	float             emissiveIntensity = 0.0f;        // 4 Bytes
	//------------------------------------------------------------
	DirectX::XMFLOAT3 emissiveColor     = { 1, 1, 1 }; // 12 Bytes
	float pad1;                                        // 4 Byter
	//------------------------------------------------------------
	DirectX::XMFLOAT2 tiling            = { 1, 1 };    // 8 Bytes
	DirectX::XMFLOAT2 uv_bias           = { 0, 0 };    // 8 Bytes
	//------------------------------------------------------------
	// Cook-Torrence BRDF
	float metalness                     = 0.0f;        // 4 Bytes
	float roughness                     = 0.8f;        // 4 Bytes
	float pad0, pad2;                                  // 8 Bytes
	//------------------------------------------------------------

	TextureID TexDiffuseMap   = INVALID_ID;
	TextureID TexNormalMap    = INVALID_ID;
	TextureID TexEmissiveMap  = INVALID_ID;
	TextureID TexHeightMap    = INVALID_ID;
	TextureID TexAlphaMaskMap = INVALID_ID;
	TextureID TexMetallicMap  = INVALID_ID;
	TextureID TexRoughnessMap = INVALID_ID;
	TextureID TexOcclusionRoughnessMetalnessMap = INVALID_ID;
	TextureID TexAmbientOcclusionMap = INVALID_ID;

	SRV_ID SRVMaterialMaps = INVALID_ID;
	//------------------------------------------------------------

	VQ_SHADER_DATA::MaterialData GetCBufferData() const;
	int                          GetTextureConfig() const;
	inline bool IsTransparent() const { return alpha != 1.0f; }
};