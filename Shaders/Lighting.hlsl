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

#include "BRDF.hlsl"
#include "ShadingMath.hlsl"

#define VQ_GPU 1
#include "LightingConstantBufferData.h"


//----------------------------------------------------------
// LIGHTING FUNCTIONS
//----------------------------------------------------------
inline float AttenuationBRDF(float dist)
{
	return 1.0f / (dist * dist);	// quadratic attenuation (inverse square) is physically more accurate
}

inline float AttenuationPhong(float2 coeffs, float dist)
{
	return 1.0f / (
		1.0f
		+ coeffs[0] * dist
		+ coeffs[1] * dist * dist
		);
}

// LearnOpenGL: PBR Lighting
//
// You may still want to use the constant, linear, quadratic attenuation equation that(while not physically correct) 
// can offer you significantly more control over the light's energy falloff.
inline float Attenuation(in const float3 coeffs, float dist)
{
    return 1.0f / (
		coeffs[0]
		+ coeffs[1] * dist
		+ coeffs[2] * dist * dist
	);
}


float SpotlightIntensity(in SpotLight l, in const float3 worldPos)
{
	const float3 pixelDirectionInWorldSpace = normalize(worldPos - l.position);
	const float3 spotDir = normalize(l.spotDir);
#if 1
	const float theta = acos(dot(pixelDirectionInWorldSpace, spotDir));

	if (theta >  l.outerConeAngle)	return 0.0f;
	if (theta <= l.innerConeAngle)	return 1.0f;
	return 1.0f - (theta - l.innerConeAngle) / (l.outerConeAngle - l.innerConeAngle);
#else
	if (dot(spotDir, pixelDirectionInWorldSpace) < cos(l.outerConeAngle))
		return 0.0f;
	else
		return 1.0f;
#endif
}


//----------------------------------------------------------
// SHADOW TESTS
//----------------------------------------------------------
struct ShadowTestPCFData
{
	//-------------------------
	float4 lightSpacePos;
	//-------------------------
	float depthBias;
	float NdotL;
	float viewDistanceOfPixel;
	//...
	//-------------------------
};
float OmnidirectionalShadowTest(
	in ShadowTestPCFData pcfTestLightData
	, TextureCubeArray shadowCubeMapArr
	, SamplerState shadowSampler
	, float2 shadowMapDimensions
	, int shadowMapIndex
	, float3 lightVectorWorldSpace
	, float fFarPlane
)
{
	const float BIAS = pcfTestLightData.depthBias * tan(acos(pcfTestLightData.NdotL));
	float shadow = 0.0f;

	// shadow map is already in world space depth rather than simple depth buffer value
	const float closestDepthInLSpace = shadowCubeMapArr.Sample(shadowSampler, float4(-lightVectorWorldSpace, shadowMapIndex)).x;
	
	const float closestDepthInWorldSpace = closestDepthInLSpace * fFarPlane;
	shadow += (length(lightVectorWorldSpace) > closestDepthInWorldSpace + pcfTestLightData.depthBias) ? 1.0f : 0.0f;

	return 1.0f - shadow;
}
float OmnidirectionalShadowTestPCF(
	in ShadowTestPCFData pcfTestLightData
	, TextureCubeArray shadowCubeMapArr
	, SamplerState shadowSampler
	, float2 shadowMapDimensions
	, int shadowMapIndex
	, float3 lightVectorWorldSpace
	, float fFarPlane
)
{
#define NUM_OMNIDIRECTIONAL_PCF_TAPS 20
#define USE_NORMALIZED_TAP_DIRECTIONS 1

#if USE_NORMALIZED_TAP_DIRECTIONS
	#define f3 0.5773502691896258f
	#define f2 0.7071067811865475f
	const float3 SAMPLE_OFFSET_DIRS_NORMALIZED[NUM_OMNIDIRECTIONAL_PCF_TAPS] =
	{
		float3(f3, f3,  f3), float3(f3, -f3,  f3), float3(-f3, -f3,  f3), float3(-f3, f3,  f3),
		float3(f3, f3, -f3), float3(f3, -f3, -f3), float3(-f3, -f3, -f3), float3(-f3, f3, -f3),
		float3(f2, f2,   0), float3( f2, -f2,  0), float3(-f2, -f2,  0), float3(-f2, f2,   0), 
		float3(f2,  0,  f2), float3(-f2,  0,  f2), float3(f2,   0, -f2), float3(-f2,  0, -f2),
		float3(0,  f2,  f2), float3(0,  -f2,  f2), float3(0,  -f2, -f2), float3(0, f2,   -f2),
	};
#else
	const float3 SAMPLE_OFFSET_DIRS[NUM_OMNIDIRECTIONAL_PCF_TAPS] =
	{
	   float3(1,  1,  1), float3(1, -1,  1), float3(-1, -1,  1), float3(-1,  1,  1),
	   float3(1,  1, -1), float3(1, -1, -1), float3(-1, -1, -1), float3(-1,  1, -1),
	
	   float3(1,  1,  0), float3(1, -1,  0), float3(-1, -1,  0), float3(-1,  1,  0),
	   float3(1,  0,  1), float3(-1, 0,  1), float3(1 ,  0, -1), float3(-1,  0, -1),
	   float3(0,  1,  1), float3(0, -1,  1), float3(0 , -1, -1), float3(0 ,  1, -1)
	};
#endif
	
	 const float BIAS = pcfTestLightData.depthBias * tan(acos(pcfTestLightData.NdotL));
	// const float bias = 0.001f;

	float shadow = 0.0f;

	// parameters for determining shadow softness based on view distance to the pixel
	const float diskRadiusScaleFactor = 1.0f / 8.0f;
	const float diskRadius = (1.0f + (pcfTestLightData.viewDistanceOfPixel / fFarPlane)) * diskRadiusScaleFactor;

	//[unroll] // cannot unroll this without spilling without shader permutations.
	for (int i = 0; i < NUM_OMNIDIRECTIONAL_PCF_TAPS; ++i)
	{
#if USE_NORMALIZED_TAP_DIRECTIONS
		const float3 SAMPLE_DIR = SAMPLE_OFFSET_DIRS_NORMALIZED[i];
#else
		const float3 SAMPLE_DIR = normalize(SAMPLE_OFFSET_DIRS[i]);
#endif
		
		
		const float4 cubemapSampleVec = float4(-(lightVectorWorldSpace + SAMPLE_DIR*diskRadius), shadowMapIndex);
		
		// shadow map is already in world space depth rather than simple depth buffer value
		const float closestDepthInLSpace = shadowCubeMapArr.Sample(shadowSampler, cubemapSampleVec).x;
		const float closestDepthInWorldSpace = closestDepthInLSpace * fFarPlane;
		shadow += (length(lightVectorWorldSpace) > closestDepthInWorldSpace + pcfTestLightData.depthBias+0.001) ? 1.0f : 0.0f;
	}
	shadow /= NUM_OMNIDIRECTIONAL_PCF_TAPS;
	return 1.0f - shadow;
}

// todo: ESM - http://www.cad.zju.edu.cn/home/jqfeng/papers/Exponential%20Soft%20Shadow%20Mapping.pdf
float ShadowTestPCF(in ShadowTestPCFData pcfTestLightData, Texture2DArray shadowMapArr, SamplerState shadowSampler, in float2 shadowMapDimensions, in int shadowMapIndex)
{
	// homogeneous position after interpolation
	const float3 projLSpaceCoords = pcfTestLightData.lightSpacePos.xyz / pcfTestLightData.lightSpacePos.w;

	// frustum check
	if (projLSpaceCoords.x < -1.0f || projLSpaceCoords.x > 1.0f ||
		projLSpaceCoords.y < -1.0f || projLSpaceCoords.y > 1.0f ||
		projLSpaceCoords.z <  0.0f || projLSpaceCoords.z > 1.0f
		)
	{
		return 0.0f;
	}

	const float BIAS = pcfTestLightData.depthBias * tan(acos(pcfTestLightData.NdotL));
	float shadow = 0.0f;

    const float2 texelSize = 1.0f / (shadowMapDimensions);
	
	// clip space [-1, 1] --> texture space [0, 1]
	const float2 shadowTexCoords = float2(0.5f, 0.5f) + projLSpaceCoords.xy * float2(0.5f, -0.5f);	// invert Y
	const float pxDepthInLSpace = projLSpaceCoords.z;


	// PCF
	const int ROW_HALF_SIZE = 2;
    for (int x = -ROW_HALF_SIZE; x <= ROW_HALF_SIZE; ++x)
    {
		for (int y = -ROW_HALF_SIZE; y <= ROW_HALF_SIZE; ++y)
        {
			float2 texelOffset = float2(x,y) * texelSize;
			float closestDepthInLSpace = shadowMapArr.Sample(shadowSampler, float3(shadowTexCoords + texelOffset, shadowMapIndex)).x;

			// depth check
			shadow += (pxDepthInLSpace - BIAS> closestDepthInLSpace) ? 1.0f : 0.0f;
        }
    }

    shadow /= (ROW_HALF_SIZE * 2 + 1) * (ROW_HALF_SIZE * 2 + 1);

	return 1.0 - shadow;
}



float ShadowTestPCF_Directional(
	in ShadowTestPCFData pcfTestLightData
	, Texture2D shadowMapArr
	, SamplerState shadowSampler
	, in float2 shadowMapDimensions
	, in matrix lightProj
)
{
	// homogeneous position after interpolation
	const float3 projLSpaceCoords = pcfTestLightData.lightSpacePos.xyz / pcfTestLightData.lightSpacePos.w;

	// frustum check
	if (projLSpaceCoords.x < -1.0f || projLSpaceCoords.x > 1.0f ||
		projLSpaceCoords.y < -1.0f || projLSpaceCoords.y > 1.0f ||
		projLSpaceCoords.z <  0.0f || projLSpaceCoords.z > 1.0f
		)
	{
		return 0.0f;
	}


	const float BIAS = pcfTestLightData.depthBias * tan(acos(pcfTestLightData.NdotL));
	float shadow = 0.0f;

	const float2 texelSize = 1.0f / (shadowMapDimensions);

	// clip space [-1, 1] --> texture space [0, 1]
	const float2 shadowTexCoords = float2(0.5f, 0.5f) + projLSpaceCoords.xy * float2(0.5f, -0.5f);	// invert Y
	const float pxDepthInLSpace = projLSpaceCoords.z;


	// PCF
	const int ROW_HALF_SIZE = 2;
	for (int x = -ROW_HALF_SIZE; x <= ROW_HALF_SIZE; ++x)
	{
		for (int y = -ROW_HALF_SIZE; y <= ROW_HALF_SIZE; ++y)
		{
			float2 texelOffset = float2(x, y) * texelSize;
			float closestDepthInLSpace = shadowMapArr.SampleLevel(shadowSampler, shadowTexCoords + texelOffset, 0).x;

			// depth check
			const float linearCurrentPx = LinearDepth(pxDepthInLSpace, lightProj);
			const float linearClosestPx = LinearDepth(closestDepthInLSpace, lightProj);
			shadow += (pxDepthInLSpace - pcfTestLightData.depthBias > closestDepthInLSpace) ? 1.0f : 0.0f;
		}
	}

	shadow /= (ROW_HALF_SIZE * 2 + 1) * (ROW_HALF_SIZE * 2 + 1);

	return 1.0f - shadow;
}


float3 ShadowTestDebug(float3 worldPos, float4 lightSpacePos, float3 illumination, Texture2D shadowMap, SamplerState shadowSampler)
{
	float3 outOfFrustum = -illumination + float3(1, 1, 0);
	float3 inShadows = -illumination + float3(1, 0, 0);
	float3 noShadows = -illumination + float3(0, 1, 0);

	float3 projLSpaceCoords = lightSpacePos.xyz / lightSpacePos.w;
	//projLSpaceCoords.x *= -1.0f;

	float2 shadowTexCoords = float2(0.5f, 0.5f) + projLSpaceCoords.xy * float2(0.5f, -0.5f);	// invert Y
	float pxDepthInLSpace = projLSpaceCoords.z;
	float closestDepthInLSpace = shadowMap.Sample(shadowSampler, shadowTexCoords).x;

	if (projLSpaceCoords.x < -1.0f || projLSpaceCoords.x > 1.0f ||
		projLSpaceCoords.y < -1.0f || projLSpaceCoords.y > 1.0f ||
		projLSpaceCoords.z <  0.0f || projLSpaceCoords.z > 1.0f
		)
	{
		return outOfFrustum;
	}

	if (pxDepthInLSpace > closestDepthInLSpace)
	{
		return inShadows;
	}

	return noShadows;
}


//----------------------------------------------------------
// ILLUMINATION CALCULATION FUNCTIONS
//----------------------------------------------------------
float3 CalculatePointLightIllumination(in const PointLight l, in BRDF_Surface s, const in float3 P, const in float3 N, const in float3 V)
{
	float3 IdIs = 0.0f.xxx;
	
	const float3 Lw = l.position;
	const float3 Wi = normalize(Lw - P);
	const float D = length(Lw - P);
	const float NdotL = saturate(dot(N, Wi));
	const float3 radiance = AttenuationBRDF(D) * l.color * l.brightness;

	if (D < l.range)
		IdIs += BRDF(s, Wi, V) * radiance * NdotL;
	
	return IdIs;
}
float3 CalculateSpotLightIllumination(in const SpotLight l, in BRDF_Surface s, const in float3 P, const in float3 N, const in float3 V)
{
	float3 IdIs = 0.0f.xxx;
	
	const float3 Wi = normalize(l.position - P);
	const float3 radiance = SpotlightIntensity(l, P) * l.color * l.brightness * AttenuationBRDF(length(l.position - P));
	const float NdotL = saturate(dot(N, Wi));
	IdIs += BRDF(s, Wi, V) * radiance * NdotL;
	
	return IdIs;
}
float3 CalculateDirectionalLightIllumination(
	  const in DirectionalLight l
	, const in BRDF_Surface s
	, const in float3 V_World
)
{
	const float3 Wi = normalize(-l.lightDirection);
	const float3 radiance = l.color * l.brightness;
	const float NdotL = saturate(dot(s.N, Wi));
	
	return BRDF(s, Wi, V_World) * radiance * NdotL;
}

//float2 ParallaxUVs(float2 uv, float3 ViewVectorInTangentSpace, Texture2D HeightMap, SamplerState Sampler)
//{
//    const float height = 1.0f - HeightMap.Sample(Sampler, uv).r;
//    const float heightIntensity = 1.0f;
//    float2 uv_offset = ViewVectorInTangentSpace.xy / ViewVectorInTangentSpace.z * (height * heightIntensity);
//    return uv - uv_offset;
//}