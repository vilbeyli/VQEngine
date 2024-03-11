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

// Sources:
// Cem Yuksel / Interactive Graphics 18 - Tessellation Shaders: https://www.youtube.com/watch?v=OqRMNrvu6TE
// https://thedemonthrone.ca/projects/rendering-terrain/rendering-terrain-part-8-adding-tessellation/
// ----------------------------------------------------------------------------------------------------------------
// https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-tessellation
// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-semantics#system-value-semantics
//
//  VS   // passthrough vertex shader
//  |
//  HS   // once per patch, input: control points for surface | output: geometry patch (quad/tri/line) control points
//  |
//  TS   // fixed function tessellation/sundivision, vertex transformation
//  |
//  DS   // per vertex on tessellated mesh, handles transformation etc 
//  |
//  GS   // 
//  |
//  PS
// ----------------------------------------------------------------------------------------------------------------

struct VSInput
{
	float3 vLocalPosition : POSITION;
	float3 vNormal : NORMAL;
	float3 vTangent : TANGENT;
	float2 uv0 : TEXCOORD0;
	uint InstanceID : SV_InstanceID;
};

struct HSInput // control point
{
	float4 Position : POSITION;
	float3 Normal : NORMAL;
	float3 Tangent : TANGENT;
	float2 uv0 : TEXCOORD0;
};

struct HSOutput // control point
{
	float4 vPosition : POSITION;
	float3 vNormal   : NORMAL;
	float3 vTangent : TANGENT;
	float2 uv0 : TEXCOORD0;
};

struct PSInput
{
	float4 ClipPos : SV_Position;
	float3 WorldPos : COLOR;
	float3 Normal : NORMAL;
	float3 Tangent : TANGENT;
	float2 uv0 : TEXCOORD0;
};

// HS patch constants
struct HSOutputTriPatchConstants
{
	float EdgeTessFactor[3] : SV_TessFactor;
	float InsideTessFactor  : SV_InsideTessFactor;
};
struct HSOutputQuadPatchConstants
{
	float EdgeTessFactor[4]   : SV_TessFactor;
	float InsideTessFactor[2] : SV_InsideTessFactor;
};

// ----------------------------------------------------------------------------------------------------------------

#define VQ_GPU 1
#include "TerrainConstantBufferData.h"
#include "Lighting.hlsl"

cbuffer CBPerFrame     : register(b0) { PerFrameData cbPerFrame; }
cbuffer CBPerView      : register(b1) { PerViewData cbPerView;   }
cbuffer CBTerrain      : register(b2) { TerrainParams trrn; }
cbuffer CBTessellation : register(b3) { TerrainTessellationParams tess; }

SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);
SamplerState AnisoSampler : register(s2);
SamplerState ClampedLinearSampler : register(s3);

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

TextureCube texEnvMapDiff : register(t10);
TextureCube texEnvMapSpec : register(t11);
Texture2D texBRDFIntegral : register(t12);

Texture2D        texDirectionalLightShadowMap : register(t13);
Texture2DArray   texSpotLightShadowMaps       : register(t16);
TextureCubeArray texPointLightShadowMaps      : register(t22);

// ----------------------------------------------------------------------------------------------------------------
float SampleHeightMap_VS(float2 uv) { return texHeightmap.SampleLevel(LinearSampler, uv, 0).x; }
float SampleHeightMap   (float2 uv) { return texHeightmap.Sample(LinearSampler, uv, 0).x; }
float3 CalcHeightOffset(float2 uv)
{
	return float3(0, SampleHeightMap_VS(uv) * trrn.material.displacement, 0);
}
// ----------------------------------------------------------------------------------------------------------------

PSInput VSMain_Heightmap(VSInput v)
{
	float2 uv = v.uv0 * trrn.material.uvScaleOffset.xy + trrn.material.uvScaleOffset.zw;
	const float3 positionWithHeightOffset = v.vLocalPosition + CalcHeightOffset(uv);
	
	PSInput o;
	o.ClipPos  = mul(trrn.worldViewProj, float4(positionWithHeightOffset, 1.0f));
	o.WorldPos = mul(trrn.world, float4(positionWithHeightOffset, 1.0f));
	o.Normal   = mul(trrn.matNormal, float4(/*v.vNormal*/float3(0,1,0), 0.0f)).xyz;
	o.Tangent  = mul(trrn.matNormal, float4(/*v.vTangent*/float3(1,0,0), 0.0f)).xyz;;
	o.uv0 = v.uv0;
	return o;
}

float4 PSMain_Heightmap(PSInput In) : SV_TARGET0
{
	const float3 VertNormalCS = normalize(In.Normal);
	const float2 uv = In.uv0 * trrn.material.uvScaleOffset.xy + trrn.material.uvScaleOffset.zw;
	
	const int TEX_CFG = trrn.material.textureConfig;
	
	float4 AlbedoAlpha = texDiffuse.Sample(AnisoSampler, uv);
	float3 Normal = texNormals.Sample(AnisoSampler, uv).rgb;
	float3 Emissive = texEmissive.Sample(LinearSampler, uv).rgb;
	float  Metalness = texMetalness.Sample(AnisoSampler, uv).r;
	float  Roughness = texRoughness.Sample(AnisoSampler, uv).r;
	float3 OcclRghMtl = texOcclRoughMetal.Sample(AnisoSampler, uv).rgb;
	float LocalAO = texLocalAO.Sample(AnisoSampler, uv).r;
	
	// no support for alpha masking
	// if (HasDiffuseMap(TEX_CFG) && AlbedoAlpha.a < 0.01f)
	// 	discard;
	
	// ensure linear space
	AlbedoAlpha.xyz = SRGBToLinear(AlbedoAlpha.xyz);
	Emissive = SRGBToLinear(Emissive);
	
	// read textures/cbuffer & assign sufrace material data
	float ao = cbPerFrame.fAmbientLightingFactor;
	BRDF_Surface Surface = (BRDF_Surface) 0;
	Surface.diffuseColor = HasDiffuseMap(TEX_CFG) ? AlbedoAlpha.rgb * trrn.material.diffuse : trrn.material.diffuse;
	Surface.emissiveColor = HasEmissiveMap(TEX_CFG) ? Emissive * trrn.material.emissiveColor : trrn.material.emissiveColor;
	Surface.emissiveIntensity = trrn.material.emissiveIntensity;
	Surface.roughness = trrn.material.roughness;
	Surface.metalness = trrn.material.metalness;
	
	const float3 N = normalize(In.Normal);
	const float3 T = normalize(In.Tangent);
	Surface.N = length(Normal) < 0.01 ? N : UnpackNormal(Normal, N, T);
	
	//ao = HasAmbientOcclusionMap(TEX_CFG) ? ao * LocalAO : ao;
	if (HasRoughnessMap(TEX_CFG))
		Surface.roughness *= Roughness;
	if (HasMetallicMap(TEX_CFG))
		Surface.metalness *= Metalness;
	if (HasOcclusionRoughnessMetalnessMap(TEX_CFG))
	{
		//ao *= OcclRghMtl.r; // TODO: handle no occlusion map case
		Surface.roughness *= OcclRghMtl.g;
		Surface.metalness *= OcclRghMtl.b;
	}
	
	const float3 I_Ambient = cbPerFrame.fAmbientLightingFactor * Surface.diffuseColor;
	const float3 I_Emission = Surface.emissiveColor * Surface.emissiveIntensity;
	float3 I_total = I_Ambient + I_Emission;
	
	// lighting & surface parameters (World Space)
	const float3 P = In.WorldPos;
	const float3 V = normalize(cbPerView.CameraPosition - P);
	const float2 screenSpaceUV = In.ClipPos.xy / cbPerView.ScreenDimensions;
	
	SceneLighting Lights = cbPerFrame.Lights;
	
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
	for (int p = 0; p < Lights.numPointLights; ++p)
	{
		I_total += CalculatePointLightIllumination(Lights.point_lights[p], Surface, P, V);
	}
	for (int s = 0; s < Lights.numSpotLights; ++s)
	{
		I_total += CalculateSpotLightIllumination(Lights.spot_lights[s], Surface, P, V);
	}
	
	
	// Shadow casters - POINT
	for (int pc = 0; pc < Lights.numPointCasters; ++pc)
	{
		const float2 PointLightShadowMapDimensions = cbPerFrame.f2PointLightShadowMapDimensions;
		PointLight l = Lights.point_casters[pc];
		const float D = length(l.position - P);

		if (D < l.range)
		{
			const float3 L = normalize(l.position - P);
			const float3 Lw = (l.position - P);
			ShadowTestPCFData pcfData = FillPCFData_PointLight(l, Surface.N, L, P, cbPerView.CameraPosition);
			I_total += CalculatePointLightIllumination(Lights.point_casters[pc], Surface, P, V)
					* OmnidirectionalShadowTestPCF(pcfData, texPointLightShadowMaps, PointSampler, PointLightShadowMapDimensions, pc, Lw, l.range);
		}
	}
	
	// Shadow casters - SPOT
	for (int sc = 0; sc < Lights.numSpotCasters; ++sc)
	{
		SpotLight l = Lights.spot_casters[sc];
		const float3 L = normalize(l.position - P);
		const float2 SpotLightShadowMapDimensions = cbPerFrame.f2SpotLightShadowMapDimensions;
		
		ShadowTestPCFData pcfData = (ShadowTestPCFData) 0;
		pcfData.depthBias = l.depthBias;
		pcfData.NdotL = saturate(dot(Surface.N, L));
		pcfData.lightSpacePos = mul(Lights.shadowViews[sc], float4(P, 1));
		pcfData.viewDistanceOfPixel = length(P - cbPerView.CameraPosition);
		
		I_total += CalculateSpotLightIllumination(Lights.spot_casters[sc], Surface, P, V)
			  * ShadowTestPCF(pcfData, texSpotLightShadowMaps, PointSampler, SpotLightShadowMapDimensions, sc);
	}
	
	
	// Directional light
	{
		DirectionalLight l = Lights.directional;
		if (l.enabled)
		{
			ShadowTestPCFData pcfData = (ShadowTestPCFData) 0;
			float ShadowingFactor = 1.0f; // no shadows
			if (l.shadowing)
			{
				const float3 L = normalize(-l.lightDirection);
				pcfData.lightSpacePos = mul(Lights.shadowViewDirectional, float4(P, 1));
				pcfData.NdotL = saturate(dot(Surface.N, L));
				pcfData.depthBias = l.depthBias;
				ShadowingFactor = ShadowTestPCF_Directional(pcfData, texDirectionalLightShadowMap, PointSampler, cbPerFrame.f2DirectionalLightShadowMapDimensions, Lights.shadowViewDirectional);
			}
			
			I_total += CalculateDirectionalLightIllumination(l, Surface, V) * ShadowingFactor;
		}
	}
	
	return float4(I_total, 1.0f);
	return float4(texHeightmap.SampleLevel(LinearSampler, uv, 0).rgb, 1);
	//return float4(VertNormalCS, 1);
	//return float4(0, 0.5, 0, 1);
}

// ----------------------------------------------------------------------------------------------------------------


HSInput VSMain(VSInput v)
{
	HSInput o;
	o.Position = float4(v.vLocalPosition, 1.0f);
	o.Normal = v.vNormal;
	o.Tangent = v.vTangent;
	o.uv0 = v.uv0;
	return o;
}

//#define DOMAIN__TRIANGLE 1
//#define DOMAIN__QUAD 1
#define ENABLE_TESSELLATION_SHADERS defined(DOMAIN__TRIANGLE) || defined(DOMAIN__QUAD)

#if ENABLE_TESSELLATION_SHADERS

#ifdef DOMAIN__TRIANGLE
	#define NUM_CONTROL_POINTS 3
	#define HSOutputPatchConstants HSOutputTriPatchConstants
#elif defined(DOMAIN__QUAD)
	#define NUM_CONTROL_POINTS 4
	#define HSOutputPatchConstants HSOutputQuadPatchConstants
#endif

HSOutputPatchConstants CalcHSPatchConstants(InputPatch<HSInput, NUM_CONTROL_POINTS> ip, uint PatchID : SV_PrimitiveID)
{
	HSOutputPatchConstants c;
#ifdef DOMAIN__TRIANGLE
	c.EdgeTessFactor[0] = tess.TriEdgeTessFactor.x;
	c.EdgeTessFactor[1] = tess.TriEdgeTessFactor.y;
	c.EdgeTessFactor[2] = tess.TriEdgeTessFactor.z;
	c.InsideTessFactor = tess.TriInnerTessFactor;
#elif defined(DOMAIN__QUAD)
	c.EdgeTessFactor[0] = tess.QuadEdgeTessFactor.x;
	c.EdgeTessFactor[1] = tess.QuadEdgeTessFactor.y;
	c.EdgeTessFactor[2] = tess.QuadEdgeTessFactor.z;
	c.EdgeTessFactor[3] = tess.QuadEdgeTessFactor.w;
	c.InsideTessFactor[0] = tess.QuadInsideFactor.x;
	c.InsideTessFactor[1] = tess.QuadInsideFactor.y;
#endif
	return c;
}

// https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-hull-shader-design
// https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-hull-shader-create
//
// control point phase + patch-constant phase
// input : 1-32 control points 
// output: 1-32 control points  --> DS
//         patch constants      --> DS
//         tessellation factors --> DS + TS

// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sm5-attributes-domain
// Domain: tri, quad, or isoline
#if DOMAIN__QUAD
[domain("quad")] 
#elif DOMAIN__TRIANGLE
[domain("tri")] 
#endif

// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sm5-attributes-partitioning
// Partitioning: integer, fractional_even, fractional_odd, pow2
#if PARTITIONING__INT
[partitioning("integer")]
#elif PARTITIONING__EVEN
[partitioning("fractional_even")]
#elif PARTITIONING__ODD
[partitioning("fractional_odd")]
#elif PARTITIONING__POW2
[partitioning("pow2")]
#endif

// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sm5-attributes-outputtopology
// Output topology: point, line, triangle_cw, triangle_ccw
#if OUTTOPO__POINT
[outputtopology("point")] 
#elif OUTTOPO__LINE
[outputtopology("line")] 
#elif OUTTOPO__TRI_CW
[outputtopology("triangle_cw")] 
#elif OUTTOPO__TRI_CCW
[outputtopology("triangle_ccw")] 
#endif

[outputcontrolpoints(NUM_CONTROL_POINTS)]
[patchconstantfunc("CalcHSPatchConstants")]
HSOutput HSMain(
	InputPatch<HSInput, NUM_CONTROL_POINTS> ip,
	uint i : SV_OutputControlPointID,
	uint PatchID : SV_PrimitiveID
)
{
	HSOutput o;
	o.vPosition = ip[i].Position;
	o.vNormal = ip[i].Normal;
	o.vTangent = ip[i].Tangent;
	o.uv0 = ip[i].uv0;
	return o;
}


//
// https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-domain-shader-design
// https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-domain-shader-create
//
#define INTERPOLATE3_PATCH_ATTRIBUTE(attr, bary)\
	patch[0].attr * bary.x +\
	patch[1].attr * bary.y +\
	patch[2].attr * bary.z

#define INTERPOLATE2_PATCH_ATTRIBUTE(attr, bary)\
	patch[0].attr * bary.x +\
	patch[1].attr * bary.y


#if DOMAIN__TRIANGLE
#define INTERPOLATE_PATCH_ATTRIBUTE(attr, bary) INTERPOLATE3_PATCH_ATTRIBUTE(attr, bary)
#elif DOMAIN__QUAD
#define INTERPOLATE_PATCH_ATTRIBUTE(attr, bary) INTERPOLATE2_PATCH_ATTRIBUTE(attr, bary)
#endif

// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sv-domainlocation
#if DOMAIN__QUAD
[domain("quad")]
#else
[domain("tri")]
#endif
PSInput DSMain(
	HSOutputPatchConstants In,
	const OutputPatch<HSOutput, NUM_CONTROL_POINTS> patch,
#if DOMAIN__QUAD
	float2 bary               : SV_DomainLocation,
	float EdgeTessFactor[4]   : SV_TessFactor,
	float InsideTessFactor[2] : SV_InsideTessFactor
#elif DOMAIN__TRIANGLE
	float3 bary               : SV_DomainLocation,
	float EdgeTessFactor[3]   : SV_TessFactor,
	float InsideTessFactor    : SV_InsideTessFactor
#endif
)
{
	float2 uv = INTERPOLATE_PATCH_ATTRIBUTE(uv0, bary);
	uv = uv * trrn.material.uvScaleOffset.xy + trrn.material.uvScaleOffset.zw;
	
	float3 vPosition = INTERPOLATE_PATCH_ATTRIBUTE(vPosition, bary);
	vPosition.xyz += CalcHeightOffset(uv);
	
	PSInput o;
	o.ClipPos = mul(trrn.worldViewProj, float4(vPosition, 1.0f));
	o.WorldPos = mul(trrn.world, float4(vPosition, 1.0f));
	o.Normal = mul(trrn.matNormal, float4( /*In.vNormal*/float3(0, 1, 0), 0.0f)).xyz;
	o.Tangent = mul(trrn.matNormal, float4( /*In.vTangent*/float3(1, 0, 0), 0.0f)).xyz;
	o.uv0 = uv;
	
	return o;
}

#endif // ENABLE_TESSELLATION_SHADERS