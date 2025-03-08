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

#include "Material.h"
#include "Scene.h"
#include "Renderer/Renderer.h"

VQ_SHADER_DATA::MaterialData Material::GetCBufferData() const
{
	VQ_SHADER_DATA::MaterialData d = {};

	d.diffuse = this->diffuse;
	d.alpha = this->alpha;

	d.emissiveColor = this->emissiveColor;
	d.emissiveIntensity = this->emissiveIntensity;

	d.specular = this->specular;
	d.roughness = this->roughness;
	d.displacement = this->displacement;

	d.metalness = this->metalness;
	d.uvScaleOffset = float4(this->tiling.x, this->tiling.y, this->uv_bias.x, this->uv_bias.y);

	d.textureConfig = static_cast<float>(this->GetTextureConfig());
	d.normalMapMipBias = this->normalMapMipBias;
	
	return d;
}

int Material::GetTextureConfig() const
{
	int textureConfig = 0;
	textureConfig |= TexDiffuseMap                     == -1 ? 0 : (1 << 0);
	textureConfig |= TexNormalMap                      == -1 ? 0 : (1 << 1);
	textureConfig |= TexAmbientOcclusionMap            == -1 ? 0 : (1 << 2);
	textureConfig |= TexAlphaMaskMap                   == -1 ? 0 : (1 << 3);
	textureConfig |= TexRoughnessMap                   == -1 ? 0 : (1 << 4);
	textureConfig |= TexMetallicMap                    == -1 ? 0 : (1 << 5);
	textureConfig |= TexHeightMap                      == -1 ? 0 : (1 << 6);
	textureConfig |= TexEmissiveMap                    == -1 ? 0 : (1 << 7);
	textureConfig |= TexOcclusionRoughnessMetalnessMap == -1 ? 0 : (1 << 8);
	return textureConfig;
}

bool Material::IsTransparent(const VQRenderer& mRenderer) const { return alpha != 1.0f || (TexDiffuseMap != INVALID_ID && mRenderer.GetTextureAlphaChannelUsed(TexDiffuseMap)); }
bool Material::IsAlphaMasked(const VQRenderer& mRenderer) const { return TexAlphaMaskMap != INVALID_ID || (TexDiffuseMap != INVALID_ID && mRenderer.GetTextureAlphaChannelUsed(TexDiffuseMap)); }