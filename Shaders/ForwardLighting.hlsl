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
	float3 worldPos    : POSITION1;
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
cbuffer CBPerFrame : register(b0)
{
	PerFrameData cbPerFrame;
}
cbuffer CBPerView : register(b1)
{
	PerViewData cbPerView;
}
cbuffer CBPerObject : register(b2)
{
#ifdef INSTANCED
	PerObjectData cbPerObject[INSTANCE_COUNT];
#else
	PerObjectData cbPerObject;
#endif
}

Texture2D texDiffuse   : register(t0);
Texture2D texNormals   : register(t1);
Texture2D texEmissive  : register(t2);
Texture2D texAlphaMask : register(t3);
Texture2D texMetalness : register(t4);
Texture2D texRoughness : register(t5);

SamplerState LinearSampler : register(s0);
SamplerState PointSampler  : register(s1);


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
	result.vertTangent = mul(cbPerObject[vertex.instanceID].matNormal, vertex.tangent);
	result.worldPos    = mul(cbPerObject[vertex.instanceID].matWorld, float4(vertex.position, 1.0f));
#else
	result.position    = mul(cbPerObject.matWorldViewProj, float4(vertex.position, 1.0f));
	result.vertNormal  = mul(cbPerObject.matNormal, vertex.normal );
	result.vertTangent = mul(cbPerObject.matNormal, vertex.tangent);
	result.worldPos    = mul(cbPerObject.matWorld, float4(vertex.position, 1.0f));
#endif
	result.uv          = vertex.uv;
	
	return result;
}


float4 PSMain(PSInput In) : SV_TARGET
{
	const float2 uv = In.uv;
	
	float3 Albedo    = texDiffuse.SampleLevel(LinearSampler, uv, 0).rgb;
	float3 Normal    = texNormals.SampleLevel(PointSampler, uv, 0).rgb;
	float3 Emissive  = texEmissive.SampleLevel(LinearSampler, uv, 0).rgb;
	float3 AlphaMask = texAlphaMask.SampleLevel(LinearSampler, uv, 0).rgb;
	float3 Metalness = texMetalness.SampleLevel(LinearSampler, uv, 0).rgb;
	float3 Roughness = texRoughness.SampleLevel(LinearSampler, uv, 0).rgb;
	
	// TODO: handle empty alpha mask case
	//if (AlphaMask.x < 0.01f)
	//	discard;
	
	BRDF_Surface SurfaceParams = (BRDF_Surface)0;
	SurfaceParams.diffuseColor = Albedo;
	SurfaceParams.N = length(Normal) < 0.01 ? normalize(In.vertNormal) : Normal;
	SurfaceParams.roughness = 0.1f;
	SurfaceParams.metalness = 0.0f;
	
	// lighting & surface parameters (World Space)
	const float3 P = In.worldPos;
	const float3 N = normalize(In.vertNormal);
	const float3 T = normalize(In.vertTangent);
	const float3 V = normalize(cbPerView.CameraPosition - P);
	const float2 screenSpaceUV = In.position.xy / cbPerView.ScreenDimensions;
	
	const float3 B = normalize(cross(T, N));
	float3x3 TBN = float3x3(T, B, N);
	

	// illumination accumulators
	float ao = 0.001f;
	const float3 Ia = SurfaceParams.diffuseColor * ao; // ambient illumination
	float3 IdIs = float3(0.0f, 0.0f, 0.0f); // diffuse & specular illumination
	float3 IEnv = 0.0f.xxx;                 // environment lighting illumination
	float3 Ie = 0.0f.xxx; //s.emissiveColor;            // emissive illumination
	
	
	for (int p = 0; p < cbPerFrame.Lights.numPointLights; ++p)
	{
		IdIs += CalculatePointLightIllumination(cbPerFrame.Lights.point_lights[p], SurfaceParams, P, N, V);
	}
	for (int s = 0; s < cbPerFrame.Lights.numSpotLights; ++s)
	{
		IdIs += CalculateSpotLightIllumination(cbPerFrame.Lights.spot_lights[s], SurfaceParams, P, N, V);
	}
	IdIs += CalculateDirectionalLightIllumination(cbPerFrame.Lights.directional);
	
	float3 OutColor = Ia + IdIs + IEnv + Ie;
	return float4(N, 1);
}
