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
	float3 normal   : NORMAL;
	float2 uv       : TEXCOORD0;
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
	float4x4 matWorld;
	float4x4 matWorldView;
	float4x4 matNormalView;
	float4x4 matProj;
	float4x4 matViewInverse;
	float4 Color;
	float4 uvScaleBias;
	float Scale;
	float HeightDisplacement;
}
cbuffer CBPerView   : register(b1) { PerViewLightingData cbPerView; }

SamplerState LinearSampler        : register(s0);
SamplerState PointSampler         : register(s1);
SamplerState AnisoSampler         : register(s2);
SamplerState ClampedLinearSampler : register(s3);
SamplerState LinearSamplerTess    : register(s4);

Texture2D texDiffuse   : register(t0);
Texture2D texHeightmap : register(t8);

// CB fetchers
#if INSTANCED_DRAW
matrix GetWorldMatrix(uint instID)               { return matWorld; }
matrix GetWorldViewProjectionMatrix(uint instID) { return mul(matProj, matWorldView); }
#else
matrix GetWorldMatrix()               { return matWorld; }
matrix GetWorldViewProjectionMatrix() { return mul(matProj, matWorldView); }
#endif
float2 GetUVScale()  { return uvScaleBias.xy; }
float2 GetUVOffset() { return uvScaleBias.zw; }

float3 CalcHeightOffset(float2 uv)
{
	float fHeightSample = texHeightmap.SampleLevel(LinearSamplerTess, uv, 0).r;
	float fHeightOffset = fHeightSample * HeightDisplacement;
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
	vPosition.xyz += CalcHeightOffset(uv * GetUVScale() + GetUVOffset());

#if INSTANCED_DRAW
	matrix matW = GetWorldMatrix(InstanceID);
	matrix matWVP = GetWorldViewProjectionMatrix(InstanceID);
	matrix matP = matProj;
#else
	matrix matW = GetWorldMatrix();
	matrix matWVP = GetWorldViewProjectionMatrix();
	matrix matP = matProj;
#endif // INSTANCED_DRAW
	
	#if OUTLINE_PASS
	float distance = 0.5; // *Scale;
	float3 viewPosition = mul(matWorldView, vPosition).xyz;
	float3 viewNormal = normalize(mul(matNormalView, float4(Normal, 0)).xyx);
	float3 viewPositionWithOffset = viewPosition + viewNormal * distance;
	#endif
	
	PSInput result;
	#if OUTLINE_PASS
	result.position = mul(matProj, float4(viewPositionWithOffset, 1));
	#else
	result.position = mul(matWVP, vPosition);
	#endif
	
	#if OUTLINE_PASS
	result.WorldSpacePosition = mul(matViewInverse, viewPositionWithOffset).xyz;
	#else
	result.WorldSpacePosition = mul(matW, vPosition).xyz;
	#endif
	
	result.uv = uv;
	
	#if OUTLINE_PASS
	result.color = pow(Color, 2.2); // nonHDR
	#else
	result.color = 0.0f.xxxx;
	#endif
	
	#if INSTANCED_DRAW
	result.instanceID = InstanceID;
	#endif
	return result;
}
#include "Tessellation.hlsl"


PSInput VSMain(VSInput VSIn)
{
	return TransformVertex(
	#if INSTANCED_DRAW
		VSIn.instanceID,
	#endif
		VSIn.position,
		VSIn.normal,
		0.0.xxx,
		VSIn.uv
	);
}

float4 PSMain(PSInput In) : SV_TARGET
{
#if ENABLE_ALPHA_MASK
	float4 AlbedoAlpha = texDiffuse.Sample(AnisoSampler, In.uv);
	if (AlbedoAlpha.a < 0.01f)
		discard;
#endif
	return In.color;
}
