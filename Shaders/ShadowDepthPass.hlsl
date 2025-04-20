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

#define VQ_GPU 1
#include "LightingConstantBufferData.h"

struct VSInput
{
	float3 position : POSITION;
#if INSTANCED_DRAW
	uint instanceID : SV_InstanceID;
#endif
#if ENABLE_ALPHA_MASK
	float2 uv       : TEXCOORD0;
#endif
};

struct PSInput
{
	float4 position : SV_POSITION;
	float3 WorldSpacePosition : COLOR0;
#if ENABLE_ALPHA_MASK
	float2 uv       : TEXCOORD0;
#endif
};


cbuffer CBPerView   : register(b0) { PerShadowViewData cbPerView; }
cbuffer CBPerObject : register(b1) { PerObjectShadowData cbPerObject; }
//cbuffer CBTessellation : register(b3) { TessellationParams tess; } // defined in Tessellation.h

Texture2D    texHeightmap      : register(t1); // VS / PS
SamplerState LinearSamplerTess : register(s1); // VS / HS / DS


#if INSTANCED_DRAW
matrix GetWorldMatrix(uint instID) { return cbPerObject.matWorld[instID]; }
matrix GetWorldViewProjectionMatrix(uint instID) { return cbPerObject.matWorldViewProj[instID]; }
#else
matrix GetWorldMatrix() { return cbPerObject.matWorld; }
matrix GetWorldViewProjectionMatrix() { return cbPerObject.matWorldViewProj; }
#endif
float2 GetUVScale()  { return cbPerObject.texScaleBias.xy; }
float2 GetUVOffset() { return cbPerObject.texScaleBias.zw; }

float3 CalcHeightOffset(float2 uv)
{
	float fHeightSample = texHeightmap.SampleLevel(LinearSamplerTess, uv, 0).r;
	float fHeightOffset = fHeightSample * cbPerObject.displacement;
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
	float4 PositionLS = float4(Position, 1);
	PositionLS.xyz += CalcHeightOffset(uv * cbPerObject.texScaleBias.xy + cbPerObject.texScaleBias.zw);

#if INSTANCED_DRAW
	matrix matW   = GetWorldMatrix(InstanceID);
	matrix matWVP = GetWorldViewProjectionMatrix(InstanceID);
#else
	matrix matW   = GetWorldMatrix();
	matrix matWVP = GetWorldViewProjectionMatrix();
#endif
	
	PSInput result;
	result.position = mul(matWVP, PositionLS);
	result.WorldSpacePosition = mul(matW, PositionLS).xyz;
#if ENABLE_ALPHA_MASK
	result.uv = uv;
#endif
	return result;
}

#include "Tessellation.hlsl"

PSInput VSMain(VSInput vertex)
{
#if ENABLE_ALPHA_MASK
	float2 uv = vertex.uv;
#else
	float2 uv = 0.xx;
#endif
	
	PSInput result = TransformVertex(
		#if INSTANCED_DRAW
		vertex.instanceID,
		#endif
		vertex.position,
		0.xxx, // N
		0.xxx, // T
		uv
	);
	
	return result;
}

#if ENABLE_ALPHA_MASK
Texture2D    texDiffuseAlpha   : register(t0);
SamplerState LinearSampler     : register(s0);
#endif

#if LINEAR_DEPTH
float 
#else
void
#endif
PSMain(PSInput In) 
#if LINEAR_DEPTH
: SV_DEPTH
#endif
{
	#if ENABLE_ALPHA_MASK
	float alpha = texDiffuseAlpha.SampleLevel(LinearSampler, In.uv, 0).a;
	if(alpha < 0.01f)
		discard;
	#endif
	
	
#if LINEAR_DEPTH
	//float fFarPlane = 1.0f;
	//switch (cbPerView.LightType)
	//{
	//	case 0: fFarPlane = cbPerFrame.Lights.point_casters[cbPerView.LightIndex].range; break;
	//	case 1: fFarPlane = cbPerFrame.Lights.spot_casters[cbPerView.LightIndex].range; break;
	//}
	
	const float depth = length(cbPerView.CameraPosition - In.WorldSpacePosition);
	return depth / cbPerView.fFarPlane;
#endif
}
