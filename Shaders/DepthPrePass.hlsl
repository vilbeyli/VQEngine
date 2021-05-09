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

#include "Lighting.hlsl"

//---------------------------------------------------------------------------------------------------
//
// DATA
//
//---------------------------------------------------------------------------------------------------
struct VSInput
{
	float3 position : POSITION;
	float3 normal   : NORMAL;
	float3 tangent  : TANGENT;
	float2 uv       : TEXCOORD0;
#ifdef INSTANCED
	uint instanceID : SV_InstanceID;
#endif
};

struct PSInput
{
    float4 position    : SV_POSITION;
	float3 vertNormal  : COLOR0;
	float3 vertTangent : COLOR1;
	float2 uv          : TEXCOORD0;
#ifdef INSTANCED
	uint instanceID    : SV_InstanceID;
#endif
};


#ifdef INSTANCED
	#ifndef INSTANCE_COUNT
	#define INSTANCE_COUNT 50 // 50 instances assumed per default, this should be provided by the app
	#endif
#endif

//---------------------------------------------------------------------------------------------------
//
// RESOURCE BINDING
//
//---------------------------------------------------------------------------------------------------
cbuffer CBPerObject : register(b2)
{
#ifdef INSTANCED
	PerObjectData cbPerObject[INSTANCE_COUNT];
#else
	PerObjectData cbPerObject;
#endif
}

SamplerState LinearSampler : register(s0);
SamplerState PointSampler  : register(s1);
SamplerState AnisoSampler  : register(s2);
SamplerState ClampedLinearSampler : register(s3);

Texture2D texDiffuse        : register(t0);
Texture2D texNormals        : register(t1);
Texture2D texEmissive       : register(t2);
Texture2D texAlphaMask      : register(t3);
Texture2D texMetalness      : register(t4);
Texture2D texRoughness      : register(t5);
Texture2D texOcclRoughMetal : register(t6);
Texture2D texLocalAO        : register(t7);

//---------------------------------------------------------------------------------------------------
//
// KERNELS
//
//---------------------------------------------------------------------------------------------------
PSInput VSMain(VSInput vertex)
{
	PSInput result;
	
#ifdef INSTANCED
	result.position    = mul(cbPerObject[vertex.instanceID].matWorldViewProj, float4(vertex.position, 1.0f));
	result.vertNormal  = mul(cbPerObject[vertex.instanceID].matNormal, vertex.normal );
#else
	result.position    = mul(cbPerObject.matWorldViewProj, float4(vertex.position, 1.0f));
	result.vertNormal  = mul(cbPerObject.matNormal, float4(vertex.normal , 0.0f));
	result.vertTangent = mul(cbPerObject.matNormal, float4(vertex.tangent, 0.0f));
#endif
	result.uv          = vertex.uv;
	
	return result;
}


float4 PSMain(PSInput In) : SV_TARGET
{
	const float2 uv = In.uv;
	
	const int TEX_CFG = cbPerObject.materialData.textureConfig;	
	
#if ALPHA_MASKED
	float4 AlbedoAlpha = texDiffuse.Sample(AnisoSampler, uv);
	if (HasDiffuseMap(TEX_CFG) && AlbedoAlpha.a < 0.01f)
		discard;
#endif
	
	float3 Normal = texNormals.Sample(AnisoSampler, uv).rgb;
	const float3 N = normalize(In.vertNormal);
	const float3 T = normalize(In.vertTangent);
	float3 SurfaceN = length(Normal) < 0.01 ? N : UnpackNormal(Normal, N, T);
	SurfaceN = (SurfaceN + 1.0f.xxx) * 0.5; // FidelityFX-CACAO needs packed normals
	return float4(SurfaceN, 1);
}
