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

#include "BRDF.hlsl"
#include "LightingConstantBufferData.h"
#include "LTC.hlsl"


//----------------------------------------------------------
// LIGHTING FUNCTIONS
//----------------------------------------------------------
inline float AttenuationBRDF(float dist)
{
	float d = dist * 0.01; // TODO: cm to m
	return 1.0f / (1.0f + d * d);	// quadratic attenuation (inverse square) is physically more accurate
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
// AREA LIGHTS
//----------------------------------------------------------

// code from [Frisvad2012]
void BuildOrthonormalBasis(in float3 n, out float3 b1, out float3 b2)
{
	if (n.z < -0.9999999)
	{
		b1 = float3(0.0, -1.0, 0.0);
		b2 = float3(-1.0, 0.0, 0.0);
		return;
	}
	float a = 1.0 / (1.0 + n.z);
	float b = -n.x * n.y * a;
	b1 = float3(1.0 - n.x * n.x * a, b, -n.x);
	b2 = float3(b, 1.0 - n.y * n.y * a, -n.y);
}

// distribution function: BRDF * cos(theta)
float3 D(float3 w, in BRDF_Surface s, float3 V /*Wo*/)
{
	return BRDF(s, w, V) * max(0, dot(s.N, w));
}

float3 I_cylinder_numerical(float3 p1, float3 p2, float R, float L, float3 CylinderAxis, in BRDF_Surface s, float3 V /*Wo*/, float3 P)
{
	// init orthonormal basis
	float3 wt = CylinderAxis; // normalize(p2 - p1); // tangent vector from p1 to p2 along the cylinder light
	float3 wt1, wt2;
	BuildOrthonormalBasis(wt, wt1, wt2);
	
	// integral discretization
	float3 I = 0.0f.xxx;
	const int nSamplesPhi = 20; // [0, 2PI]
	const int nSamplesL = 100;  // [0, L]
	
	for (int i = 0; i < nSamplesPhi; ++i)
	for (int j = 0; j < nSamplesL  ; ++j)
	{
		// normal
		float phi = TWO_PI * float(i)/float(nSamplesPhi);
		float3 wn = cos(phi)*wt1 + sin(phi)*wt2;
			
		// position
		float l = L * float(j)/float(nSamplesL - 1);
		float3 p = p1 + l*wt + R*wn;
			
		// normalized direction
		float3 wp = normalize(p); // shading spot location = (0,0,0);
			
		// integrate
		I +=  D(wp, s, V) * AttenuationBRDF(length(p)) * max(0.0f, dot(-wp, wn)) / dot(p, p);
	}
	
	I *= TWO_PI * R * L / float(nSamplesL * nSamplesPhi);
	
	return I;
}

float3 I_line_numerical(float3 p1, float3 p2, float L, float3 LightTangent, in BRDF_Surface s, float3 V /*Wo*/, float3 P)
{
	float3 wt = LightTangent; // normalize(p2 - p1);
	
	// integral discretization
	float3 I = 0.0f.xxx;
	const int nSamples = 100;
	for (int i = 0; i < nSamples; ++i)
	{
		// position on light
		float3 p = p1 + L * wt * float(i) / float(nSamples - 1);
		
		// normalized direction
		float3 wp = normalize(p); // shading spot location = (0,0,0);
		
		// integrate
		I += 2.0f * D(wp, s, V) * AttenuationBRDF(length(p)) * length(cross(wp, wt)) / dot(p, p);
	}
	
	I *= L / float(nSamples);
	
	return I;
}
// approximation is most accurate with
// - cylinders of small radius
// - cylinders far from the shading point
// - low-frequency (large roughness) materials
float3 I_cylinder_approx(float3 p1, float3 p2, float R, float L, float3 CylinderAxis, in BRDF_Surface s, float3 V /*Wo*/, float3 P)
{
	return min(1.0f.xxx, R * I_line_numerical(p1, p2, L, CylinderAxis, s, V, P));
}



//----------------------------------------------------------
// ILLUMINATION CALCULATION FUNCTIONS
//----------------------------------------------------------
float3 CalculatePointLightIllumination(in const PointLight l, in BRDF_Surface s, const in float3 P, const in float3 V)
{
	float3 IdIs = 0.0f.xxx;
	
	const float3 Lw = l.position;
	const float3 Wi = normalize(Lw - P);
	const float D = length(Lw - P);
	const float NdotL = saturate(dot(s.N, Wi));
	const float3 radiance = AttenuationBRDF(D) * l.color * l.brightness;

	if (D < l.range)
		IdIs += BRDF(s, Wi, V) * radiance * NdotL;
	
	return IdIs;
}
float3 CalculateSpotLightIllumination(in const SpotLight l, in BRDF_Surface s, const in float3 P, const in float3 V)
{
	float3 IdIs = 0.0f.xxx;
	
	const float3 Wi = normalize(l.position - P);
	const float3 radiance = SpotlightIntensity(l, P) * l.color * l.brightness * AttenuationBRDF(length(l.position - P));
	const float NdotL = saturate(dot(s.N, Wi));
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

// TODO: pass m with cbuffer to the shader & as a parameter to this function
float3x3 GetHDRIRotationMatrix(float fHDIROffsetInRadians)
{
	const float cosB = cos(-fHDIROffsetInRadians);
	const float sinB = sin(-fHDIROffsetInRadians);
	float3x3 m = {
		+cosB, 0, sinB,
		  0 ,  1,  0,
		-sinB, 0, cosB
	};
	return m;
}

float3 CalculateEnvironmentMapIllumination(
	  in const BRDF_Surface s
	, in const float3 V
	, in const int MAX_REFLECTION_LOD
	, TextureCube texEnvMapDiff
	, TextureCube texEnvMapSpec
	, Texture2D texBRDFIntegrationLUT
	, SamplerState smpEnvMap
	, float fHDRIOffsetRad
)
{
	const float3x3 m                             = GetHDRIRotationMatrix(fHDRIOffsetRad);
	const float    NdotV                         = saturate(dot(s.N, V));
	const float3   R                             = mul(reflect(-V, s.N), m);
	const float3   N                             = mul(s.N, m);
	const int      MIP_LEVEL                     = s.roughness * MAX_REFLECTION_LOD;
	const float3   IEnv_SpecularPreFilteredColor = texEnvMapSpec.SampleLevel(smpEnvMap, R, MIP_LEVEL).xyz;
	const float2   F0ScaleBias                   = texBRDFIntegrationLUT.SampleLevel(smpEnvMap, float2(NdotV, s.roughness), 0).rg;
	const float3   IEnv_DiffuseIrradiance        = texEnvMapDiff.Sample(smpEnvMap, N).rgb;
	return EnvironmentBRDF(NdotV, s.roughness, s.metalness, s.diffuseColor, IEnv_DiffuseIrradiance, IEnv_SpecularPreFilteredColor, F0ScaleBias);
}

float3 CalculateEnvironmentMapIllumination_DiffuseOnly(
	  in const BRDF_Surface s
	, float3 V
	, TextureCube texEnvMapDiff
	, SamplerState smpEnvMap
	, float fHDRIOffsetRad
)
{
	const float3x3 m = GetHDRIRotationMatrix(fHDRIOffsetRad);
	const float    NdotV = saturate(dot(s.N, V));
	const float3   N = mul(s.N, m);
	const float3 IEnv_DiffuseIrradiance = texEnvMapDiff.Sample(smpEnvMap, N).rgb;
	return EnvironmentBRDF(NdotV, s.roughness, s.metalness, s.diffuseColor, IEnv_DiffuseIrradiance, float3(0,0,0), float2(0, 0));
}

//float2 ParallaxUVs(float2 uv, float3 ViewVectorInTangentSpace, Texture2D HeightMap, SamplerState Sampler)
//{
//    const float height = 1.0f - HeightMap.Sample(Sampler, uv).r;
//    const float heightIntensity = 1.0f;
//    float2 uv_offset = ViewVectorInTangentSpace.xy / ViewVectorInTangentSpace.z * (height * heightIntensity);
//    return uv - uv_offset;
//}

float3 CalculateCylinderLightIllumination(CylinderLight l, in BRDF_Surface s, float3 V /*Wo*/, float3 P)
{
	float3 p1WorldSpace = l.position - l.tangent * l.length * 0.5f;
	float3 p2WorldSpace = l.position + l.tangent * l.length * 0.5f;
		
	float3 p1 = p1WorldSpace - P;
	float3 p2 = p2WorldSpace - P;
	
	float R = l.radius;
	float L = l.length; // length(p2 - p1);	
	
	//return I_cylinder_numerical(p1, p2, R, L, l.tangent, s, V, P) * l.brightness * l.color;
	return I_cylinder_approx(p1, p2, R, L, l.tangent, s, V, P) * l.brightness * l.color;
}

float3 CalculateLinearLightIllumination(
	LinearLight l,
	in BRDF_Surface s,
	float3 V /*Wo*/, 
	float3 P, // shading world position
	Texture2D texLTC1,
	Texture2D texLTC2,
	SamplerState LTCSampler // linear
)
{
	float3 p1WorldSpace = l.position - l.tangent * l.length * 0.5f;
	float3 p2WorldSpace = l.position + l.tangent * l.length * 0.5f;
		
	float3 p1 = p1WorldSpace - P;
	float3 p2 = p2WorldSpace - P;

	float L = l.length; // length(p2 - p1);
	
	bool LTC = l.LTC > 0;
	if (LTC) // analytic solution
	{
		float NdotV = saturate(dot(s.N, V));
		float3x3 Minv = LTCMinv(texLTC1, LTCSampler, s.roughness, NdotV);
		
		// Construct orthonormal basis around N
		float3 T1 = normalize(V - s.N * dot(V, s.N));
		float3 T2 = cross(s.N, T1);
		//float3 T2 = cross(T1, s.N);
		float3x3 B = float3x3(T1, T2, s.N);
		
		p1 = mul(B, p1);
		p2 = mul(B, p2);
		float3 I_Specular = I_ltc_line(p1, p2, Minv);
		
		float3 I_Diffuse = I_ltc_line(p1, p2, float3x3(1,0,0, 0,1,0, 0,0,1));
		I_Diffuse /= 2.0f* PI;
		
		return I_Diffuse * l.brightness * l.color;
	}
	
	else // numerical solution
	{
		return I_line_numerical(p1, p2, L, l.tangent, s, V, P) * l.brightness * l.color;
	}
}

float3 ClaculateRectangularLightIllumination(
	RectangularLight l, 
	in BRDF_Surface s, 
	float3 V /*Wo*/, 
	float3 P,
	Texture2D texLTC1,
	Texture2D texLTC2,
	SamplerState LTCSampler // linear
)
{
	// build rectangle corner points
	float halfH = l.height * 0.5f;
	float halfW = l.width * 0.5f;
	float3 points[4];
	points[0] = l.position + l.tangent * halfW + l.bitangent * halfH;
	points[1] = l.position + l.tangent * halfW - l.bitangent * halfH;
	points[2] = l.position - l.tangent * halfW - l.bitangent * halfH;
	points[3] = l.position - l.tangent * halfW + l.bitangent * halfH;
	
	// Construct orthonormal basis around N
	float3 T1 = normalize(V - s.N * dot(V, s.N));
	float3 T2 = cross(s.N, T1);
	float3x3 B = float3x3(T1, T2, s.N);
	
	points[0] = mul(B, points[0]);
	points[1] = mul(B, points[1]);
	points[2] = mul(B, points[2]);
	points[3] = mul(B, points[3]);
	
	// get LTC parameters
	float NdotV = saturate(dot(s.N, V));
	float3x3 Minv = LTCMinv(texLTC1, LTCSampler, s.roughness, NdotV);
	
	// calculate illumination
	float3 I_Specular = I_ltc_quad(s.N, V, P, Minv, points);
	float3 I_Diffuse = I_ltc_quad(s.N, V, P, float3x3(1, 0, 0,  0, 1, 0,  0, 0, 1), points);
	
	return (I_Specular + I_Diffuse) * l.brightness * l.color;
}