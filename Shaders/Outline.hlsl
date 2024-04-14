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

#define VQ_GPU 1
#include "LightingConstantBufferData.h"


struct VSInput
{
	float3 position : POSITION;
	float2 uv       : TEXCOORD0;
	float3 normal   : NORMAL;
#if INSTANCED_DRAW
	uint instanceID : SV_InstanceID;
#endif
};

struct PSInput
{
	float4 position           : SV_POSITION;
	float4 color              : COLOR0;
	float3 WorldSpacePosition : COLOR2;
	float2 uv                 : TEXCOORD0;
#if INSTANCED_DRAW
	uint instanceID : INSTANCEID;
#endif
};

cbuffer OutlineData : register(b0)
{
	float4x4 matWorldView;
	float4x4 matNormalView;
	float4x4 matProj;
	float4 Color;
	float Scale;
}

#if 0
cbuffer CBPerView   : register(b1) { PerViewData cbPerView; }
cbuffer CBPerObject : register(b2) { PerObjectData cbPerObject; }

SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);
SamplerState AnisoSampler : register(s2);
SamplerState ClampedLinearSampler : register(s3);
SamplerState LinearSamplerTess : register(s4);

Texture2D texDiffuse : register(t0);
Texture2D texHeightmap : register(t8);


// CB fetchers
#if INSTANCED_DRAW
matrix GetWorldMatrix(uint instID) { return cbPerObject.matWorld[instID]; }
matrix GetWorldNormalMatrix(uint instID) { return cbPerObject.matNormal[instID]; }
matrix GetWorldViewProjectionMatrix(uint instID) { return cbPerObject.matWorldViewProj[instID]; }
#else
matrix GetWorldMatrix() { return cbPerObject.matWorld; }
matrix GetWorldNormalMatrix() { return cbPerObject.matNormal; }
matrix GetWorldViewProjectionMatrix() { return cbPerObject.matWorldViewProj; }
#endif
float2 GetUVScale() { return cbPerObject.materialData.uvScaleOffset.xy; }
float2 GetUVOffset() { return cbPerObject.materialData.uvScaleOffset.zw; }

float3 CalcHeightOffset(float2 uv)
{
	float fHeightSample = texHeightmap.SampleLevel(LinearSamplerTess, uv, 0).r;
	float fHeightOffset = fHeightSample * cbPerObject.materialData.displacement;
	return float3(0, fHeightOffset, 0);
}


PSInput TransformVertex(
#if INSTANCED_DRAW
	int InstanceID,
#endif
	float3 Position,
	float3 Normal,
	float3 Tangent,
	float2 uv
)
{
	float4 vPosition = float4(Position, 1.0f);
	vPosition.xyz += CalcHeightOffset(uv * cbPerObject.materialData.uvScaleOffset.xy + cbPerObject.materialData.uvScaleOffset.zw);

#if INSTANCED_DRAW
	matrix matW = GetWorldMatrix(InstanceID);
	matrix matWVP = GetWorldViewProjectionMatrix(InstanceID);
	matrix matWN = GetWorldNormalMatrix(InstanceID);
#else
	matrix matW = GetWorldMatrix();
	matrix matWVP = GetWorldViewProjectionMatrix();
	matrix matWN = GetWorldNormalMatrix();
#endif // INSTANCED_DRAW
	
	float3 viewPosition = mul(matWorldView, vPosition).xyz;
	float3 viewNormal = normalize(mul(matNormalView, float4(VSIn.normal, 0)).xyx);
	float3 viewPositionWithOffset = viewPosition + viewNormal * distance;
	
	PSInput result;
	result.position = mul(matWVP, vPosition);
	result.WorldSpacePosition = mul(matW, vPosition).xyz;
	result.uv = uv;
	result.color = pow(Color, 2.2); // nonHDR
#if INSTANCED_DRAW
	result.instanceID = InstanceID;
#endif
	return result;
}
#include "Tessellation.hlsl"
#endif



PSInput VSMain(VSInput VSIn)
{
	PSInput result;
	result.position = mul(mul(matProj, matWorldView), float4(VSIn.position, 1));
	result.color = Color;
	return result;
}

PSInput VSMainOutline(VSInput VSIn)
{
	PSInput result;
	
	float distance = 0.5; // *Scale;
	
	float3 viewPosition = mul(matWorldView, float4(VSIn.position, 1)).xyz;
	float3 viewNormal   = normalize(mul(matNormalView, float4(VSIn.normal, 0)).xyx);
	float3 viewPositionWithOffset = viewPosition + viewNormal * distance;
	
	result.position = mul(matProj, float4(viewPositionWithOffset, 1));
	result.color = pow(Color, 2.2); // nonHDR
	return result;
}

float4 PSMain(PSInput In) : SV_TARGET
{
#if ENABLE_ALPHA_MASK
	float4 AlbedoAlpha = texDiffuse.Sample(AnisoSampler, uv);
	if (AlbedoAlpha.a < 0.01f)
		discard;
#endif
	return In.color;
}
