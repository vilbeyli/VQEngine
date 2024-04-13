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

#define PS_OUTPUT_ALBEDO_METALLIC   (OUTPUT_METALLIC || OUTPUT_ALBEDO)
#define PS_OUTPUT_MOTION_VECTORS    (OUTPUT_MOTION_VECTORS)

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
#if INSTANCED_DRAW
	uint instanceID : SV_InstanceID;
#endif
};

struct PSInput
{
	float4 position           : SV_POSITION;
	float3 WorldSpacePosition : COLOR2;
	float3 WorldSpaceNormal   : COLOR0;
	float3 WorldSpaceTangent  : COLOR1;
	float2 uv                 : TEXCOORD0;
#if PS_OUTPUT_MOTION_VECTORS
	float4 svPositionCurr : TEXCOORD1;
	float4 svPositionPrev : TEXCOORD2;
#endif
};


struct PSOutput
{
	float4 color : SV_TARGET0;
#if PS_OUTPUT_ALBEDO_METALLIC
	float4 albedo_metallic : SV_TARGET1;
#endif

#if PS_OUTPUT_ALBEDO_METALLIC && PS_OUTPUT_MOTION_VECTORS
	float2 motion_vectors : SV_TARGET2;
#elif !PS_OUTPUT_ALBEDO_METALLIC && PS_OUTPUT_MOTION_VECTORS
	float2 motion_vectors : SV_TARGET1;
#endif
};


//---------------------------------------------------------------------------------------------------
//
// RESOURCE BINDING
//
//---------------------------------------------------------------------------------------------------
cbuffer CBPerFrame     : register(b0) { PerFrameData cbPerFrame; }
cbuffer CBPerView      : register(b1) { PerViewData cbPerView; }
cbuffer CBPerObject    : register(b2) { PerObjectData cbPerObject; }
//cbuffer CBTessellation : register(b3) { TessellationParams tess; }

SamplerState LinearSampler        : register(s0);
SamplerState PointSampler         : register(s1);
SamplerState AnisoSampler         : register(s2);
SamplerState ClampedLinearSampler : register(s3);
SamplerState LinearSamplerTess    : register(s4);

Texture2D texDiffuse        : register(t0);
Texture2D texNormals        : register(t1);
Texture2D texEmissive       : register(t2);
Texture2D texAlphaMask      : register(t3);
Texture2D texMetalness      : register(t4);
Texture2D texRoughness      : register(t5);
Texture2D texOcclRoughMetal : register(t6);
Texture2D texLocalAO        : register(t7);

Texture2D texHeightmap      : register(t8); // VS or PS

Texture2D texScreenSpaceAO  : register(t9);

TextureCube texEnvMapDiff   : register(t10);
TextureCube texEnvMapSpec   : register(t11);
Texture2D   texBRDFIntegral : register(t12);

Texture2D        texDirectionalLightShadowMap : register(t13);
Texture2DArray   texSpotLightShadowMaps       : register(t16);
TextureCubeArray texPointLightShadowMaps      : register(t22);




//---------------------------------------------------------------------------------------------------
//
// FUNCS
//
//---------------------------------------------------------------------------------------------------

// CB fetchers
#if INSTANCED_DRAW
matrix GetWorldMatrix(uint instID) { return cbPerObject.matWorld[instID]; }
matrix GetWorldNormalMatrix(uint instID) { return cbPerObject.matNormal[instID]; }
matrix GetWorldViewProjectionMatrix(uint instID) { return cbPerObject.matWorldViewProj[instID]; }
#if PS_OUTPUT_MOTION_VECTORS
matrix GetPrevWorldViewProjectionMatrix(uint instID) { return cbPerObject.matWorldViewProjPrev[instID]; }
#endif
#else
matrix GetWorldMatrix() { return cbPerObject.matWorld; }
matrix GetWorldNormalMatrix() { return cbPerObject.matNormal; }
matrix GetWorldViewProjectionMatrix() { return cbPerObject.matWorldViewProj; }
#if PS_OUTPUT_MOTION_VECTORS
matrix GetPrevWorldViewProjectionMatrix() { return cbPerObject.matWorldViewProjPrev; }
#endif
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
	matrix matW   = GetWorldMatrix(InstanceID);
	matrix matWVP = GetWorldViewProjectionMatrix(InstanceID);
	matrix matWN = GetWorldNormalMatrix(InstanceID);
	
	#if PS_OUTPUT_MOTION_VECTORS
	matrix matPrevWVP = GetPrevWorldViewProjectionMatrix(InstanceID);
	#endif
#else
	matrix matW   = GetWorldMatrix();
	matrix matWVP = GetWorldViewProjectionMatrix();
	matrix matWN = GetWorldNormalMatrix();
	#if PS_OUTPUT_MOTION_VECTORS
	matrix matPrevWVP = GetPrevWorldViewProjectionMatrix();
	#endif
#endif // INSTANCED_DRAW
	
	PSInput result;
	result.position = mul(matWVP, vPosition);
	result.WorldSpacePosition = mul(matW, vPosition).xyz;
	result.WorldSpaceNormal = mul((float4x3)matWN, Normal).xyz;
	result.WorldSpaceTangent = mul((float4x3)matWN, Tangent).xyz;
	#if PS_OUTPUT_MOTION_VECTORS
	result.svPositionPrev = mul(matPrevWVP, vPosition);
	#endif
	result.uv = uv;

#if PS_OUTPUT_MOTION_VECTORS
	result.svPositionCurr = result.position;
#endif
	
	return result;
}


//---------------------------------------------------------------------------------------------------
//
// TESSELLATION
//
//---------------------------------------------------------------------------------------------------
#include "Tessellation.hlsl"

//---------------------------------------------------------------------------------------------------
//
// VERTEX SHADER
//
//---------------------------------------------------------------------------------------------------
PSInput VSMain(VSInput vertex)
{
	return TransformVertex(
#if INSTANCED_DRAW
		vertex.instanceID,
#endif
		vertex.position,
		vertex.normal,
		vertex.tangent,
		vertex.uv
	);
}


//---------------------------------------------------------------------------------------------------
//
// PIXEL SHADER
//
//---------------------------------------------------------------------------------------------------
PSOutput PSMain(PSInput In)
{
	PSOutput o = (PSOutput)0;

	const float2 uv = In.uv * cbPerObject.materialData.uvScaleOffset.xy + cbPerObject.materialData.uvScaleOffset.zw;
	const int TEX_CFG = cbPerObject.materialData.textureConfig;
	
	float4 AlbedoAlpha = texDiffuse  .Sample(AnisoSampler, uv);
	float3 Normal      = texNormals.Sample(AnisoSampler, uv).rgb;
	float3 Emissive    = texEmissive .Sample(LinearSampler, uv).rgb;
	float  Metalness   = texMetalness.Sample(AnisoSampler, uv).r;
	float  Roughness   = texRoughness.Sample(AnisoSampler, uv).r;
	float3 OcclRghMtl  = texOcclRoughMetal.Sample(AnisoSampler, uv).rgb;
	float LocalAO      = texLocalAO.Sample(AnisoSampler, uv).r;
	
	#if ENABLE_ALPHA_MASK
	if (HasDiffuseMap(TEX_CFG) && AlbedoAlpha.a < 0.01f)
		discard;
	#endif
	
	// ensure linear space
	AlbedoAlpha.xyz = SRGBToLinear(AlbedoAlpha.xyz);
	Emissive        = SRGBToLinear(Emissive);
	
	// read textures/cbuffer & assign sufrace material data
	float ao = cbPerFrame.fAmbientLightingFactor;
	BRDF_Surface Surface      = (BRDF_Surface)0;
	Surface.diffuseColor      = HasDiffuseMap(TEX_CFG)  ? AlbedoAlpha.rgb * cbPerObject.materialData.diffuse : cbPerObject.materialData.diffuse;
	Surface.emissiveColor     = HasEmissiveMap(TEX_CFG) ? Emissive * cbPerObject.materialData.emissiveColor : cbPerObject.materialData.emissiveColor;
	Surface.emissiveIntensity = cbPerObject.materialData.emissiveIntensity;
	Surface.roughness         = cbPerObject.materialData.roughness;
	Surface.metalness         = cbPerObject.materialData.metalness;
	
	#if ENABLE_TESSELLATION_SHADERS
	float fHeightSample = texHeightmap.SampleLevel(LinearSampler, uv, 0).r;
	float fHeightOffset = fHeightSample * cbPerObject.materialData.displacement;
	
	float fHeightSampleT = texHeightmap.SampleLevel(LinearSampler, uv + float2(0,0), 0).r;
	float fHeightSampleB = texHeightmap.SampleLevel(LinearSampler, uv + float2(0,0), 0).r;
	float fHeightSampleL = texHeightmap.SampleLevel(LinearSampler, uv + float2(0,0), 0).r;
	float fHeightSampleR = texHeightmap.SampleLevel(LinearSampler, uv + float2(0,0), 0).r;
	
	const float3 N = normalize(In.WorldSpaceNormal);
	const float3 T = normalize(In.WorldSpaceTangent);
	#else
	const float3 N = normalize(In.WorldSpaceNormal);
	const float3 T = normalize(In.WorldSpaceTangent);
	#endif
	Surface.N = length(Normal) < 0.01 ? N : UnpackNormal(Normal, N, T);
	
	if (HasAmbientOcclusionMap           (TEX_CFG)>0) ao *= LocalAO;
	if (HasRoughnessMap                  (TEX_CFG)>0) Surface.roughness *= Roughness;
	if (HasMetallicMap                   (TEX_CFG)>0) Surface.metalness *= Metalness;
	if (HasOcclusionRoughnessMetalnessMap(TEX_CFG)>0)
	{
		//ao *= OcclRghMtl.r; // TODO: handle no occlusion map case
		Surface.roughness *= OcclRghMtl.g;
		Surface.metalness *= OcclRghMtl.b;
	}

	// apply SSAO
	const float2 ScreenSpaceUV = (float2(In.position.xy) + 0.5f.xx) / float2(cbPerView.ScreenDimensions);
	ao *= texScreenSpaceAO.Sample(PointSampler, ScreenSpaceUV).r;
	
	// lighting & surface parameters (World Space)
	const float3 P = In.WorldSpacePosition;
	const float3 V = normalize(cbPerView.CameraPosition - P);
	const float2 screenSpaceUV = In.position.xy / cbPerView.ScreenDimensions;
	
	
	// illumination accumulators
	float3 I_total = 
	/* ambient  */   Surface.diffuseColor  * ao
	/* Emissive */ + Surface.emissiveColor * Surface.emissiveIntensity
	;

	
	// -------------------------------------------------------------------------------------------------------
	
	// Environment map
	if (cbPerView.EnvironmentMapDiffuseOnlyIllumination) // TODO: preprocessor
	{
		I_total += CalculateEnvironmentMapIllumination_DiffuseOnly(Surface, V, texEnvMapDiff, ClampedLinearSampler, cbPerFrame.fHDRIOffsetInRadians);
	}
	else
	{
		I_total += CalculateEnvironmentMapIllumination(Surface, V, cbPerView.MaxEnvMapLODLevels, texEnvMapDiff, texEnvMapSpec, texBRDFIntegral, ClampedLinearSampler, cbPerFrame.fHDRIOffsetInRadians);
	}
	
	
	// Non-shadowing lights
	for (int p = 0; p < cbPerFrame.Lights.numPointLights; ++p)
	{
		I_total += CalculatePointLightIllumination(cbPerFrame.Lights.point_lights[p], Surface, P, V);
	}
	for (int s = 0; s < cbPerFrame.Lights.numSpotLights; ++s)
	{
		I_total += CalculateSpotLightIllumination(cbPerFrame.Lights.spot_lights[s], Surface, P, V);
	}
	
	
	// Shadow casters - POINT
	for (int pc = 0; pc < cbPerFrame.Lights.numPointCasters; ++pc)
	{
		const float2 PointLightShadowMapDimensions = cbPerFrame.f2PointLightShadowMapDimensions;
		PointLight l = cbPerFrame.Lights.point_casters[pc];
		const float D = length(l.position - P);

		if(D < l.range)
		{
			const float3 L = normalize(l.position - P);
			const float3 Lw = (l.position - P);
			ShadowTestPCFData pcfData = (ShadowTestPCFData) 0;
			pcfData.depthBias           = l.depthBias;
			pcfData.NdotL               = saturate(dot(Surface.N, L));
			pcfData.viewDistanceOfPixel = length(P - cbPerView.CameraPosition);
		
			I_total += CalculatePointLightIllumination(cbPerFrame.Lights.point_casters[pc], Surface, P, V)
					* OmnidirectionalShadowTestPCF(pcfData, texPointLightShadowMaps, PointSampler, PointLightShadowMapDimensions, pc, Lw, l.range);
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
		pcfData.NdotL               = saturate(dot(Surface.N, L));
		pcfData.lightSpacePos       = mul(cbPerFrame.Lights.shadowViews[sc], float4(P, 1));
		pcfData.viewDistanceOfPixel = length(P - cbPerView.CameraPosition);
		
		I_total += CalculateSpotLightIllumination(cbPerFrame.Lights.spot_casters[sc], Surface, P, V)
			  * ShadowTestPCF(pcfData, texSpotLightShadowMaps, PointSampler, SpotLightShadowMapDimensions, sc);
	}
	
	
	// Directional light
	{
		DirectionalLight l = cbPerFrame.Lights.directional;
		if (l.enabled)
		{
			ShadowTestPCFData pcfData = (ShadowTestPCFData) 0;
			float ShadowingFactor = 1.0f; // no shadows
			if (l.shadowing)
			{
				const float3 L = normalize(-l.lightDirection);
				pcfData.lightSpacePos = mul(cbPerFrame.Lights.shadowViewDirectional, float4(P, 1));
				pcfData.NdotL = saturate(dot(Surface.N, L));
				pcfData.depthBias = l.depthBias;
				ShadowingFactor = ShadowTestPCF_Directional(pcfData, texDirectionalLightShadowMap, PointSampler, cbPerFrame.f2DirectionalLightShadowMapDimensions, cbPerFrame.Lights.shadowViewDirectional);
			}
			
			I_total += CalculateDirectionalLightIllumination(l, Surface, V) * ShadowingFactor;
		}
	}
	
	// write out
	o.color = float4(I_total, Surface.roughness.r);

	#if PS_OUTPUT_ALBEDO_METALLIC
	o.albedo_metallic = float4(Surface.diffuseColor, Surface.metalness);
	#endif
	#if PS_OUTPUT_MOTION_VECTORS
	//o.motion_vectors = float2(0.77777777777777, 0.8888888888888888); // debug output
	o.motion_vectors = float2(In.svPositionCurr.xy / In.svPositionCurr.w - In.svPositionPrev.xy / In.svPositionPrev.w);
	#endif

	return o;
}
