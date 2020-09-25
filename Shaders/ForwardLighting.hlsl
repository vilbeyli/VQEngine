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

Texture2DArray   texSpotLightShadowMaps  : register(t6);
TextureCubeArray texPointLightShadowMaps : register(t12);

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
	
	float4 AlbedoAlpha = texDiffuse.SampleLevel(LinearSampler, uv, 0);
	float3 Normal    = texNormals.SampleLevel(PointSampler, uv, 0).rgb;
	float3 Emissive  = texEmissive.SampleLevel(LinearSampler, uv, 0).rgb;
	float3 Metalness = texMetalness.SampleLevel(LinearSampler, uv, 0).rgb;
	float3 Roughness = texRoughness.SampleLevel(LinearSampler, uv, 0).rgb;
	
	if (HasDiffuseMap(cbPerObject.materialData.textureConfig) && AlbedoAlpha.a < 0.01f)
		discard;
	
	// read material textures & cbuffer data
	BRDF_Surface SurfaceParams = (BRDF_Surface)0;
	SurfaceParams.diffuseColor      = HasDiffuseMap(cbPerObject.materialData.textureConfig)   ? AlbedoAlpha.rgb : cbPerObject.materialData.diffuse;
	SurfaceParams.roughness         = HasRoughnessMap(cbPerObject.materialData.textureConfig) ? Roughness       : cbPerObject.materialData.roughness;
	SurfaceParams.metalness         = HasMetallicMap(cbPerObject.materialData.textureConfig)  ? Metalness       : cbPerObject.materialData.metalness;
	SurfaceParams.emissiveColor     = HasEmissiveMap(cbPerObject.materialData.textureConfig)  ? Emissive        : cbPerObject.materialData.emissiveColor;
	SurfaceParams.emissiveIntensity = cbPerObject.materialData.emissiveIntensity;
	
	// lighting & surface parameters (World Space)
	const float3 P = In.worldPos;
	const float3 N = normalize(In.vertNormal);
	const float3 T = normalize(In.vertTangent);
	const float3 V = normalize(cbPerView.CameraPosition - P);
	const float2 screenSpaceUV = In.position.xy / cbPerView.ScreenDimensions;
	
	const float3 B = normalize(cross(T, N));
	float3x3 TBN = float3x3(T, B, N);
	
	SurfaceParams.N = length(Normal) < 0.01 
		? normalize(In.vertNormal) 
		: UnpackNormals(texNormals, PointSampler, uv, N, T);
	
	// illumination accumulators
	const float3 Ia = SurfaceParams.diffuseColor * cbPerFrame.fAmbientLightingFactor; // ambient illumination
	float3 IdIs = float3(0.0f, 0.0f, 0.0f); // diffuse & specular illumination
	float3 Ie = SurfaceParams.emissiveColor * SurfaceParams.emissiveIntensity * 10.0f; // emissive illumination
	float3 IEnv = 0.0f.xxx;                 // environment lighting illumination
	
	// Non-shadowing lights
	for (int p = 0; p < cbPerFrame.Lights.numPointLights; ++p)
	{
		IdIs += CalculatePointLightIllumination(cbPerFrame.Lights.point_lights[p], SurfaceParams, P, N, V);
	}
	for (int s = 0; s < cbPerFrame.Lights.numSpotLights; ++s)
	{
		IdIs += CalculateSpotLightIllumination(cbPerFrame.Lights.spot_lights[s], SurfaceParams, P, N, V);
	}
	
	
	// Shadow casters - POINT
	for (int pc = 0; pc < cbPerFrame.Lights.numPointCasters; ++pc)
	{
		const float2 PointLightShadowMapDimensions = cbPerFrame.f2PointLightShadowMapDimensions;
		PointLight l = cbPerFrame.Lights.point_casters[pc];
		const float  D = length   (l.position - P);

		if(D < l.range)
		{
			const float3 L = normalize(l.position - P);
			
			ShadowTestPCFData pcfData = (ShadowTestPCFData) 0;
			pcfData.depthBias           = l.depthBias;
			pcfData.NdotL               = saturate(dot(SurfaceParams.N, L));
			pcfData.viewDistanceOfPixel = length(P - cbPerView.CameraPosition);
			//pcfData.lightSpacePos       = mul(cbPerFrame.Lights.shadowViews[pc], float4(P, 1));
		
			IdIs += CalculatePointLightIllumination(cbPerFrame.Lights.point_casters[pc], SurfaceParams, P, N, V)
			//		* OmnidirectionalShadowTestPCF(pcfData, texPointLightShadowMaps, PointSampler, PointLightShadowMapDimensions, pc, L, l.range)
			;
		}
	}
	
	// Shadow casters - SPOT
	for (int sc = 0; sc < cbPerFrame.Lights.numSpotCasters; ++sc)
	{
		SpotLight l = cbPerFrame.Lights.spot_casters[sc];
		const float3 L = normalize(l.position - P);
		const float2 SpotLightShadowMapDimensions = cbPerFrame.f2SpotLightShadowMapDimensions;
		
		ShadowTestPCFData pcfData = (ShadowTestPCFData) 0;
		pcfData.depthBias           = l.depthBias;
		pcfData.NdotL               = saturate(dot(SurfaceParams.N, L));
		pcfData.lightSpacePos       = mul(cbPerFrame.Lights.shadowViews[sc], float4(P, 1));
		pcfData.viewDistanceOfPixel = length(P - cbPerView.CameraPosition);
		
		IdIs += CalculateSpotLightIllumination(cbPerFrame.Lights.spot_casters[sc], SurfaceParams, P, N, V) 
			  * ShadowTestPCF(pcfData, texSpotLightShadowMaps, PointSampler, SpotLightShadowMapDimensions, sc);
	}
	
	if (cbPerFrame.Lights.directional.enabled)
		IdIs += CalculateDirectionalLightIllumination(cbPerFrame.Lights.directional, SurfaceParams, P, V, cbPerFrame.Lights.shadowViewDirectional);
	
	float3 OutColor = Ia + IdIs + IEnv + Ie;
	return float4(OutColor, 1);
}
