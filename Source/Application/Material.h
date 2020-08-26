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

	NUM_MATERIAL_TEXTURE_MAP_BINDINGS
};

struct Material // 56 Bytes
{
	//------------------------------------------------------------
	MaterialID        ID                = INVALID_ID;  // 4  Bytes
	DirectX::XMFLOAT3 diffuse           = {1, 1, 1};   // 12 Bytes
	//------------------------------------------------------------
	float             alpha             = 1.0f;        // 4  Bytes
	DirectX::XMFLOAT3 specular          = { 1, 1, 1 }; // 12 Bytes
	//------------------------------------------------------------
	DirectX::XMFLOAT3 emissiveColor     = { 1, 1, 1 }; // 12 Bytes
	float             emissiveIntensity = 0.0f;        // 4 Bytes
	//------------------------------------------------------------
	DirectX::XMFLOAT2 tiling            = { 1, 1 };    // 8 Bytes
	DirectX::XMFLOAT2 uv_bias           = { 0, 0 };    // 8 Bytes
	//------------------------------------------------------------
	// Cook-Torrence BRDF
	float metalness                     = 0.0f;        // 4 Bytes
	float roughness                     = 0.8f;        // 4 Bytes
	float pad0, pad1;                                  // 8 Bytes
	//------------------------------------------------------------

	TextureID TexDiffuseMap   = INVALID_ID;
	TextureID TexNormalMap    = INVALID_ID;
	TextureID TexEmissiveMap  = INVALID_ID;
	TextureID TexHeightMap    = INVALID_ID;
	TextureID TexSpecularMap  = INVALID_ID;  // phong?
	TextureID TexAlphaMaskMap = INVALID_ID;
	TextureID TexMetallicMap  = INVALID_ID;
	TextureID TexRoughnessMap = INVALID_ID;
	
	SRV_ID SRVMaterialMaps = INVALID_ID;
	//------------------------------------------------------------

	inline bool IsTransparent() const { return alpha != 1.0f; }
#if 0
	Material(MaterialID _ID);
	~Material();

	void SetMaterialConstants(Renderer* renderer, EShaders shader, bool bIsDeferredRendering) const;
	int GetTextureConfig() const;
	inline bool HasTexture() const { return GetTextureConfig() != 0; }

	virtual SurfaceMaterial GetCBufferData() const = 0;
	virtual void Clear() = 0;

	static SurfaceMaterial GetDefaultMaterialCBufferData();
};

struct BRDF_Material : public Material
{	// Cook-Torrence BRDF
	float		metalness;
	float		roughness;

	BRDF_Material() : Material({ -1 }), metalness(0.0f), roughness(0.0f) {}

	SurfaceMaterial GetCBufferData() const override;
	void Clear() override;

private:
	friend class MaterialPool;	// only MaterialPool can create Material instances
	BRDF_Material(MaterialID _ID) : Material(_ID), metalness(0.1f), roughness(0.6f) {}
#endif
};