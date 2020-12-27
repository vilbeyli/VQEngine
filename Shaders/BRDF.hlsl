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

#include "ShadingMath.hlsl"

// constants
#define EPSILON 0.000000000001f

// defines
#define _DEBUG

// ================================== BRDF NOTES =========================================
//	src: https://learnopengl.com/#!PBR/Theory
//	ref: http://blog.selfshadow.com/publications/s2012-shading-course/hoffman/s2012_pbs_physics_math_notes.pdf
//
// The Rendering Equation
//
//   Lo(p, w_o) = Le(p) + Integral_Omega()[fr(p, wi, wo) * L_i(p, wi) * dot(N, wi)]dwi
//
// Lo       : Radiance leaving surface point P along the direction w_o (V=eye)
// Le       : Emissive Radiance leaving surface point p | we're not gonna use emissive materials for now 
// Li       : Irradiance (Radiance coming) coming from a light source at point p 
// fr()     : reflectance equation representing the material property
// Integral : all incoming radiance over hemisphere omega, reflected towards the direction of w_o<->(V=eye) 
//            by the surface material
//
// The integral is numerically solved.
// 
// BRDF : Bi-directional Reflectance Distribution Function
// The Cook-Torrence BRDF : fr = kd * f_lambert + ks * f_cook-torrence
//                              where f_lambert = albedo / PI;
//
struct BRDF_Surface
{
	float3 N;
	float  roughness;
	float3 diffuseColor;
	float  metalness;
	float3 specularColor;
	float3 emissiveColor;
	float  emissiveIntensity;
};

// Trowbridge-Reitz GGX Distribution
inline float NormalDistributionGGX(float3 N, float3 H, float roughness)
{	// approximates microfacets :	approximates the amount the surface's microfacets are
	//								aligned to the halfway vector influenced by the roughness
	//								of the surface
	//							:	determines the size, brightness, and shape of the specular highlight
	// more: http://reedbeta.com/blog/hows-the-ndf-really-defined/
	//
	// NDF_GGXTR(N, H, roughness) = roughness^2 / ( PI * ( dot(N, H))^2 * (roughness^2 - 1) + 1 )^2
	const float a = roughness * roughness;
	const float a2 = a * a;
	const float nh2 = pow(max(dot(N, H), 0), 2);
	const float denom = (PI * pow((nh2 * (a2 - 1.0f) + 1.0f), 2));
	if (denom < EPSILON) return 1.0f;
	return a2 / denom;
}

// Smith's Schlick-GGX for Direct Lighting (non-IBL)
inline float Geometry_Smiths_SchlickGGX(float3 N, float3 V, float roughness)
{	// describes self shadowing of geometry
	//
	// G_ShclickGGX(N, V, k) = ( dot(N,V) ) / ( dot(N,V)*(1-k) + k )
	//
	// k		 :	remapping of roughness based on wheter we're using geometry function 
	//				for direct lighting or IBL
	// k_direct	 = (roughness + 1)^2 / 8
	// k_IBL	 = roughness^2 / 2
	//
	const float k = pow((roughness + 1.0f), 2) / 8.0f;
	const float NV = max(0.0f, dot(N, V));
	const float denom = (NV * (1.0f - k) + k) + 0.0001f;
	//if (denom < EPSILON) return 1.0f;
	return NV / denom;
}

// Smith's Schlick-GGX for Environment Maps
inline float Geometry_Smiths_SchlickGGX_EnvironmentMap(float3 N, float3 V, float roughness)
{ // describes self shadowing of geometry
	//
	// G_ShclickGGX(N, V, k) = ( dot(N,V) ) / ( dot(N,V)*(1-k) + k )
	//
	// k		 :	remapping of roughness based on wheter we're using geometry function 
	//				for direct lighting or IBL
	// k_direct	 = (roughness + 1)^2 / 8
	// k_IBL	 = roughness^2 / 2
	//
	const float k = pow(roughness, 2) / 2.0f;
	const float NV = max(0.0f, dot(N, V));
	const float denom = (NV * (1.0f - k) + k) + 0.0001f;
	//if (denom < EPSILON) return 1.0f;
	return NV / denom;
}

#ifdef _DEBUG
// returns a multiplier [0, 1] measuring microfacet shadowing
float Geometry(float3 N, float3 V, float3 L, float k)
{	
	float geomNV = Geometry_Smiths_SchlickGGX(N, V, k);
	float geomNL = Geometry_Smiths_SchlickGGX(N, L, k);
	return  geomNV * geomNL;
}
#else
inline float Geometry(float3 N, float3 V, float3 L, float k)
{	// essentially a multiplier [0, 1] measuring microfacet shadowing
	return  Geometry_Smiths_SchlickGGX(N, V, k) * Geometry_Smiths_SchlickGGX(N, L, k);
}
#endif

// returns a multiplier [0, 1] measuring microfacet shadowing
float GeometryEnvironmentMap(float3 N, float3 V, float3 L, float k)
{ 
	float geomNV = Geometry_Smiths_SchlickGGX_EnvironmentMap(N, V, k);
	float geomNL = Geometry_Smiths_SchlickGGX_EnvironmentMap(N, L, k);
	return geomNV * geomNL;
}

// Fresnel-Schlick approximation describes reflection
inline float3 Fresnel(float3 N, float3 V, float3 F0)
{	// F_Schlick(N, V, F0) = F0 - (1-F0)*(1 - dot(N,V))^5
	// F0 is the specular reflectance at normal incidence.
	return F0 + (float3(1, 1, 1) - F0) * pow(1.0f - max(0.0f, dot(N, V)), 5.0f);
}

// Fresnel-Schlick approx with a Spherical Gaussian approx
// src: https://de45xmedrsdbp.cloudfront.net/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf
inline float3 FresnelGaussian(float3 H, float3 V, float3 F0)
{ 
	// F0 is the specular reflectance at normal incidence.
	const float c0 = -5.55373f;
	const float c1 = -6.98316f;
	const float VdotH = max(0.0f, dot(V, H));
	return F0 + (float3(1, 1, 1) - F0) * pow(2.0f, (c0 * VdotH - c1) * VdotH);
}

// Fresnel-Schlick with roughness factored in used in image-based lighting 
// for factoring in irradiance coming from environment map
// src: https://seblagarde.wordpress.com/2011/08/17/hello-world/ 
float3 FresnelWithRoughness(float cosTheta, float3 F0, float roughness)
{
	// F0 is the specular reflectance at normal incidence.
	return F0 + (max((1.0f - roughness).xxx, F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

inline float3 F_LambertDiffuse(float3 kd)
{
	return kd / PI;
}

float3 BRDF(in BRDF_Surface s, float3 Wi, float3 V)
{
	// vectors
	const float3 Wo = normalize(V);
	const float3 N = normalize(s.N);
	const float3 H = normalize(Wo + Wi);

	// surface
	const float3 albedo = s.diffuseColor;
	const float  roughness = s.roughness;
	const float  metalness = s.metalness;
	const float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metalness);

	// Fresnel_Cook-Torrence BRDF
	const float3 F = Fresnel(H, V, F0);
	const float  G = Geometry(N, Wo, Wi, roughness);
	const float  NDF = NormalDistributionGGX(N, H, roughness);
	const float  denom = (4.0f * max(0.0f, dot(Wo, N)) * max(0.0f, dot(Wi, N))) + 0.0001f;
	const float3 specular = NDF * F * G / denom;
	const float3 Is = specular * s.specularColor;

	// Diffuse BRDF
	const float3 kS = F;
	const float3 kD = (float3(1, 1, 1) - kS) * (1.0f - metalness) * albedo;
	const float3 Id = F_LambertDiffuse(kD);

	return (Id + Is);
}

float3 EnvironmentBRDF(BRDF_Surface s, float3 V, float ao, float3 irradience, float3 envSpecular, float2 F0ScaleBias)
{
	const float NdotV = saturate(dot(s.N, V));
	const float3 F0 = lerp(0.04f.xxx, s.diffuseColor, s.metalness);

	const float3 Ks = FresnelWithRoughness(NdotV, F0, s.roughness);
	const float3 Kd = (1.0f.xxx - Ks) * (1.0f - s.metalness);

	const float3 diffuse = irradience * s.diffuseColor;
	const float3 specular = envSpecular * (Ks * F0ScaleBias.x + F0ScaleBias.y);

	return (Kd * diffuse + specular) * ao;
}


// Instead of uniformly or randomly (Monte Carlo) generating sample vectors over the integral's hemisphere, we'll generate 
// sample vectors biased towards the general reflection orientation of the microsurface halfway vector based on the surface's roughness. 
// This gives us a sample vector somewhat oriented around the expected microsurface's halfway vector based on some input roughness 
// and the low-discrepancy sequence value Xi. Note that Epic Games uses the squared roughness for better visual results as based on 
// Disney's original PBR research.
// https://de45xmedrsdbp.cloudfront.net/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf
float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
	const float a = roughness * roughness;

	const float phi = 2.0f * PI * Xi.x;
	const float cosTheta = sqrt((1.0f - Xi.y) / (1.0f + (a * a - 1.0f) * Xi.y));
	const float sinTheta = sqrt(1.0f - cosTheta * cosTheta);

	// from sphreical coords to cartesian coords
	float3 H;
	H.x = cos(phi) * sinTheta;
	H.y = sin(phi) * sinTheta;
	H.z = cosTheta;

	// from tangent-space to world space
	const float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
	const float3 tangent = normalize(cross(up, N));
	const float3 bitangent = cross(N, tangent);

	const float3 sample = tangent * H.x + bitangent * H.y + N * H.z;
	return normalize(sample);
}
float2 IntegrateBRDF(float NdotV, float roughness)
{
	float3 V;
	V.x = sqrt(1.0f - NdotV * NdotV); // sin ()
	V.y = 0;
	V.z = NdotV; // cos()

	float F0Scale = 0;	// Integral1
	float F0Bias = 0;	// Integral2

	const uint SAMPLE_COUNT = 2048;
	const float3 N = float3(0, 0, 1);
	for (uint i = 0; i < SAMPLE_COUNT; ++i)
	{
		const float2 Xi = Hammersley(i, SAMPLE_COUNT);
		const float3 H = ImportanceSampleGGX(Xi, N, roughness);
		const float3 L = normalize(reflect(-V, H));

		const float NdotL = max(L.z, 0);
		const float NdotH = max(H.z, 0);
		const float VdotH = max(dot(V, H), 0);

		if (NdotL > 0.0f)
		{
			const float G = GeometryEnvironmentMap(N, V, L, roughness);
			
			// Split Sum Approx : Specular BRDF integration using Quasi Monte Carlo
			// 
			// Microfacet specular = D*G*F / (4*NoL*NoV)
			// pdf = D * NoH / (4 * VoH)
			// G_Vis = Microfacet specular / (pdf * F)
			//
			// We do this to move F0 out of the integral so we can factor F0
			// in during the lighting pass, which just simplifies this LUT
			// into a representation of a scale and a bias to the F0
			// as in (F0 * scale + bias)
			const float G_Vis = max((G * VdotH) / (NdotH * NdotV), 0.0001);
			
			const float Fc = pow(1.0 - VdotH, 5.0f);

			F0Scale += (1.0f - Fc) * G_Vis;
			F0Bias += Fc * G_Vis;
		}
	}
	return float2(F0Scale, F0Bias) / SAMPLE_COUNT;
}
