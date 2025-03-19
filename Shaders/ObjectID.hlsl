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

//#define INSTANCED_DRAW 1
//#define INSTANCE_COUNT 64

#define VQ_GPU 1
#include "LightingConstantBufferData.h"

struct VSInput
{
	float3 position : POSITION;
	float2 uv       : TEXCOORD0;
#if INSTANCED_DRAW
	uint instanceID : SV_InstanceID;
#endif
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
	float3 WorldSpacePosition : COLOR2;
#if INSTANCED_DRAW
	uint instanceID : INSTANCEID;
#endif
};

cbuffer CBPerView   : register(b1) { PerViewLightingData cbPerView; }
cbuffer CBPerObject : register(b2) { PerObjectLightingData cbPerObject; }

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
	
	PSInput result;
	result.position = mul(matWVP, vPosition);
	result.WorldSpacePosition = mul(matW, vPosition).xyz;
	result.uv = uv;
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
		0.0f.xxx,
		0.0f.xxx,
		VSIn.uv
	);
}

int4 PSMain(PSInput In) : SV_TARGET
{
	const float2 uv = In.uv * cbPerObject.materialData.uvScaleOffset.xy + cbPerObject.materialData.uvScaleOffset.zw;

#if ENABLE_ALPHA_MASK
	float4 AlbedoAlpha = texDiffuse.Sample(AnisoSampler, uv);
	if (AlbedoAlpha.a < 0.01f)
		discard;
#endif
#if INSTANCED_DRAW
	int objID = cbPerObject.ObjID[In.instanceID].x;
	int meshBBProjectedArea = cbPerObject.ObjID[In.instanceID].w;
#else
	int objID = cbPerObject.ObjID;
	int meshBBProjectedArea = cbPerObject.ObjID.w;
#endif
	return int4(objID, cbPerObject.meshID, cbPerObject.materialID, meshBBProjectedArea);
}
