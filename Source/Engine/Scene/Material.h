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
#include "Renderer/Pipeline/Tessellation.h"


class VQRenderer;

enum EMaterialTextureMapBindings
{
	ALBEDO = 0,
	NORMALS,
	EMISSIVE,
	ALPHA_MASK,
	METALLIC,
	ROUGHNESS,
	OCCLUSION_ROUGHNESS_METALNESS,
	AMBIENT_OCCLUSION,
	HEIGHT,

	NUM_MATERIAL_TEXTURE_MAP_BINDINGS
};

struct alignas(16) Material
{
	DirectX::XMFLOAT3 diffuse           = {1, 1, 1};   // 12 Bytes
	float             alpha             = 1.0f;        // 4  Bytes
	DirectX::XMFLOAT3 emissiveColor     = { 1, 1, 1 }; // 12 Bytes
	float             emissiveIntensity = 0.0f;        // 4  Bytes
	DirectX::XMFLOAT3 specular          = { 1, 1, 1 }; // 12 Bytes
	float             normalMapMipBias  = 0.0f;        // 4 Bytes
	DirectX::XMFLOAT2 tiling            = { 1, 1 };    // 8 Bytes
	DirectX::XMFLOAT2 uv_bias           = { 0, 0 };    // 8 Bytes
	// Cook-Torrence BRDF
	float roughness                     = 0.8f;        // 4 Bytes
	float metalness                     = 0.0f;        // 4 Bytes
	float displacement                  = 0.0f;        // 4 Bytes
	//------------------------------------------------------------

	// bit 0  : bEnableTessellation
	// bit 1  : bEnableVertexTBNVisualization
	// bit 2-3: ETessellationDomain
	// bit 4-5: ETessellationOutputTopology
	// bit 6-7: ETessellationPartitioning;
	uint8 PackedTessellationConfig =
		(((uint8)ETessellationDomain::TRIANGLE_PATCH)                          << 2) |
		(((uint8)ETessellationOutputTopology::TESSELLATION_OUTPUT_TRIANGLE_CW) << 4) |
		(((uint8)ETessellationPartitioning::FRACTIONAL_ODD)                    << 6);
	bool bWireframe = false;
	uint8 padding0 = 0xDE;
	uint8 padding1 = 0xAD;

	SRV_ID SRVMaterialMaps = INVALID_ID;
	SRV_ID SRVHeightMap = INVALID_ID;

	TextureID TexDiffuseMap   = INVALID_ID;
	TextureID TexNormalMap    = INVALID_ID;
	TextureID TexEmissiveMap  = INVALID_ID;
	TextureID TexAlphaMaskMap = INVALID_ID;
	TextureID TexMetallicMap  = INVALID_ID;
	TextureID TexRoughnessMap = INVALID_ID;
	TextureID TexOcclusionRoughnessMetalnessMap = INVALID_ID;
	TextureID TexAmbientOcclusionMap = INVALID_ID;
	TextureID TexHeightMap    = INVALID_ID;
	TextureID padding2 = INVALID_ID; // to align TessellationData w/ a cache line

	VQ_SHADER_DATA::TessellationParams TessellationData;
	inline bool                        IsTessellationEnabled() const         { return PackedTessellationConfig & 0x1; }
	inline bool                        IsVertexTBNVisualization() const      { return PackedTessellationConfig & 0x2; }
	inline ETessellationDomain         GetTessellationDomain() const         { return static_cast<ETessellationDomain        >((PackedTessellationConfig >> 2) & 0x3); }
	inline ETessellationOutputTopology GetTessellationOutputTopology() const { return static_cast<ETessellationOutputTopology>((PackedTessellationConfig >> 4) & 0x3); }
	inline ETessellationPartitioning   GetTessellationPartitioning() const   { return static_cast<ETessellationPartitioning  >((PackedTessellationConfig >> 6) & 0x3); }
	inline void SetTessellationEnabled(bool b)                               { PackedTessellationConfig |= (b ? 1 : 0); }
	inline void SetVertexTBNVisualization(bool b)                            { PackedTessellationConfig |= (b ? 2 : 0); }
	inline void SetTessellationDomain(ETessellationDomain d)                 { PackedTessellationConfig |= ((uint8)d) << 2; }
	inline void SetTessellationOutputTopology(ETessellationOutputTopology t) { PackedTessellationConfig |= ((uint8)t) << 4; }
	inline void SetTessellationPartitioning(ETessellationPartitioning p)     { PackedTessellationConfig |= ((uint8)p) << 6; }

	inline void GetTessellationPSOConfig(
		uint8& iTess,
		uint8& iDomain,
		uint8& iPart,
		uint8& iOutTopo,
		uint8& iTessCull
	) const
	{
		const bool bFrustumCull  = TessellationData.bFrustumCull_FaceCull_AdaptiveTessellation & 0x1;
		const bool bFaceCull     = TessellationData.bFrustumCull_FaceCull_AdaptiveTessellation & 0x2;
		const bool bAdaptiveTess = TessellationData.bFrustumCull_FaceCull_AdaptiveTessellation & 0x4;
		const bool bSWCullTessellation = bFaceCull || bFrustumCull;

		iTess     = IsTessellationEnabled() ? 1 : 0;
		iDomain   = iTess == 0 ? 0 : (uint8)GetTessellationDomain();
		iPart     = iTess == 0 ? 0 : (uint8)GetTessellationPartitioning();
		iOutTopo  = iTess == 0 ? 0 : (uint8)GetTessellationOutputTopology();
		iTessCull = iTess == 1 && (bSWCullTessellation ? 1 : 0);
		return;
	}
	inline VQ_SHADER_DATA::TessellationParams GetTessellationCBufferData() const { return this->TessellationData; }
	inline void GetCBufferData(VQ_SHADER_DATA::MaterialData& data) const
	{ 
		// MaterialData and Material has almost identical elements, except for the float 
		// at the end of MaterialData for encoding the texture configuration.
		const size_t BytesToCopy = sizeof(VQ_SHADER_DATA::MaterialData);// -sizeof(float);
		memcpy(&data, this, BytesToCopy);
		data.textureConfig = static_cast<float>(GetTextureConfig());
	}

	int                          GetTextureConfig() const;
	bool IsTransparent(const VQRenderer& mRenderer) const;
	bool IsAlphaMasked(const VQRenderer& mRenderer) const;


};