//	VQE
//	Copyright(C) 2024  - Volkan Ilbeyli
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

#if INSTANCED_DRAW
#ifndef INSTANCE_COUNT
#define INSTANCE_COUNT 512
#endif
#endif

struct VSInput
{
	float3 position : POSITION;
	float2 uv       : TEXCOORD0;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv       : TEXCOORD0;
};

cbuffer CBPerObject : register(b2)
{
	PerObjectData cbPerObject;
}


SamplerState LinearSampler : register(s0);

Texture2D texDiffuse : register(t0);


PSInput VSMain(VSInput vertex, uint instID : SV_InstanceID)
{
	PSInput result;
	
#if INSTANCED_DRAW
	result.position = mul(matModelViewProj[instID], float4(vertex.position, 1));
#else
	result.position = mul(matModelViewProj, float4(vertex.position, 1));
#endif
	
	result.uv = vertex.uv;
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	const float2 uv = In.uv;
	const int TEX_CFG = cbPerObject.materialData.textureConfig;
	
	float4 AlbedoAlpha = texDiffuse.Sample(AnisoSampler, uv);
	if (HasDiffuseMap(TEX_CFG) && AlbedoAlpha.a < 0.01f)
		discard;
	
	return float4(ObjIDMeshIDMaterialID, 1.0f);
}
