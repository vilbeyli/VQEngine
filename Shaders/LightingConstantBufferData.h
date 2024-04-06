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
#ifndef LIGHTING_CONSTANT_BUFFER_DATA_H
#define LIGHTING_CONSTANT_BUFFER_DATA_H

#include "VQPlatform.h"

#ifdef VQ_CPU
namespace VQ_SHADER_DATA {
#endif

//----------------------------------------------------------
// LIGHTS
//----------------------------------------------------------

// defines maximum number of dynamic lights
// don't forget to update CPU define too (RenderPasses.h)
#define NUM_LIGHTS__POINT 100
#define NUM_LIGHTS__SPOT  20

#define NUM_SHADOWING_LIGHTS__POINT 5
#define NUM_SHADOWING_LIGHTS__SPOT  5
#define NUM_SHADOWING_LIGHTS__DIRECTIONAL 1

#define LIGHT_INDEX_SPOT       0
#define LIGHT_INDEX_POINT      1


struct PointLight
{	// 48 bytes
	float3 position;
	float  range;
	//---------------
	float3 color;
	float  brightness;
	//---------------
	float3 attenuation;
	float  depthBias;
	//---------------
};

struct SpotLight
{	// 48 bytes
	float3 position;
	float  outerConeAngle;
	//---------------
	float3 color;
	float  brightness;
	float3 spotDir;
	float  depthBias;
	//---------------
	float innerConeAngle;
	float dummy;
	float dummy1;
	float dummy2;
	//---------------
};

struct DirectionalLight
{	// 40 bytes
	float3 lightDirection;
	float  brightness;
	//---------------
	float3 color;
	float depthBias;
	//---------------
	int shadowing;
	int enabled;
};

struct SceneLighting
{
	// non-shadow caster counts
	int numPointLights;  
	int numSpotLights;
	// shadow caster counts
	int numPointCasters;  
	int numSpotCasters;
	//----------------------------------------------
	DirectionalLight directional;
	matrix shadowViewDirectional;
	//----------------------------------------------
	PointLight point_lights[NUM_LIGHTS__POINT];
	PointLight point_casters[NUM_SHADOWING_LIGHTS__POINT];
	//----------------------------------------------
	SpotLight spot_lights[NUM_LIGHTS__SPOT];
	SpotLight spot_casters[NUM_SHADOWING_LIGHTS__SPOT];
	//----------------------------------------------
	matrix shadowViews[NUM_SHADOWING_LIGHTS__SPOT];
};


//----------------------------------------------------------
// MATERIAL
//----------------------------------------------------------


// Has*Map() encoding should match Material::GetTextureConfig()
//
inline int HasDiffuseMap(int textureConfig)                     { return ((textureConfig & (1 << 0)) > 0 ? 1 : 0); }
inline int HasNormalMap(int textureConfig)	                    { return ((textureConfig & (1 << 1)) > 0 ? 1 : 0); }
inline int HasAmbientOcclusionMap(int textureConfig)            { return ((textureConfig & (1 << 2)) > 0 ? 1 : 0); }
inline int HasAlphaMask(int textureConfig)                      { return ((textureConfig & (1 << 3)) > 0 ? 1 : 0); }
inline int HasRoughnessMap(int textureConfig)                   { return ((textureConfig & (1 << 4)) > 0 ? 1 : 0); }
inline int HasMetallicMap(int textureConfig)                    { return ((textureConfig & (1 << 5)) > 0 ? 1 : 0); }
inline int HasHeightMap(int textureConfig)                      { return ((textureConfig & (1 << 6)) > 0 ? 1 : 0); }
inline int HasEmissiveMap(int textureConfig)                    { return ((textureConfig & (1 << 7)) > 0 ? 1 : 0); }
inline int HasOcclusionRoughnessMetalnessMap(int textureConfig) { return ((textureConfig & (1 << 8)) > 0 ? 1 : 0); }

struct MaterialData
{
    float3 diffuse;
    float alpha;

    float3 emissiveColor;
    float emissiveIntensity;

    float3 specular;
    float roughness;

    float4 uvScaleOffset;
    float metalness;

	float displacement;
    int textureConfig;
};



//----------------------------------------------------------
// SHADER CONSTANT BUFFER INTERFACE
//----------------------------------------------------------
#define RENDER_INSTANCED_SCENE_MESHES    1  // 0 is broken, TODO: fix
#define MAX_INSTANCE_COUNT__SCENE_MESHES 64

#ifndef INSTANCE_COUNT
#define INSTANCE_COUNT  MAX_INSTANCE_COUNT__SCENE_MESHES
#endif

// adapter for C++ code
#if RENDER_INSTANCED_SCENE_MESHES
#define INSTANCED_DRAW 1
#endif

struct PerFrameData
{
	SceneLighting Lights;
	float2 f2PointLightShadowMapDimensions;
	float2 f2SpotLightShadowMapDimensions;
	float2 f2DirectionalLightShadowMapDimensions;
	float fAmbientLightingFactor;
	float fHDRIOffsetInRadians;
};
struct PerViewData
{
	matrix matView;
	matrix matViewToWorld; // i.e. matViewInverse
	matrix matProjInverse;

	float4 WorldFrustumPlanes[6];

	float3 CameraPosition;
	float  MaxEnvMapLODLevels;
	float2 ScreenDimensions;
	int    EnvironmentMapDiffuseOnlyIllumination;
	float pad1;
};
struct PerObjectData
{
#if INSTANCED_DRAW
	matrix matWorldViewProj    [INSTANCE_COUNT];
	matrix matWorld            [INSTANCE_COUNT];
	matrix matWorldViewProjPrev[INSTANCE_COUNT];
	matrix matNormal           [INSTANCE_COUNT]; // could be 4x3
#else
	matrix matWorldViewProj;
	matrix matWorld;
	matrix matWorldViewProjPrev;
	matrix matNormal;
#endif

	MaterialData materialData;
	float pad2;

	int meshID;
	int materialID;
	int2 pad3;

#if INSTANCED_DRAW
	int4 ObjID[INSTANCE_COUNT]; // int[] causes alignment issues as each element is aligned to 16B on the GPU. use int4.x
#else
	int ObjID;
#endif
};

struct TessellationParams
{
	float4 QuadEdgeTessFactor;
	float2 QuadInsideFactor;
	float2 pad;
	float3 TriEdgeTessFactor;
	float TriInnerTessFactor;

	int bFrustumCull;
	int bFaceCull;
	int bAdaptiveTessellation;
	int padi;

	float fHSFrustumCullEpsilon;
	float fHSFaceCullEpsilon;
	float fHSAdaptiveTessellationMaxDist;
	float fHSAdaptiveTessellationMinDist;


#ifdef VQ_CPU
	TessellationParams() : 
		  QuadEdgeTessFactor(1,1,1,1)
		, QuadInsideFactor(1, 1)
		, TriEdgeTessFactor(1,1,1)
		, TriInnerTessFactor(1)
		, pad(99999.0f, 99999.0f)
		, bFrustumCull(0)
		, bFaceCull(0)
		, bAdaptiveTessellation(0)
		, padi(0x0BADF00D)
		, fHSFrustumCullEpsilon(0)
		, fHSFaceCullEpsilon(0)
		, fHSAdaptiveTessellationMaxDist(500.0f)
		, fHSAdaptiveTessellationMinDist(10.0f)
	{}
#endif
};


#ifdef VQ_CPU
} // namespace VQ_SHADER_DATA
#endif

#endif // LIGHTING_CONSTANT_BUFFER_DATA_H