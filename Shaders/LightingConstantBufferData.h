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
#ifndef VQ_GPU
#define VQ_CPU 1
#endif

#ifdef VQ_CPU
#pragma once
#include <array>
#include <DirectXMath.h>
// HLSL types for CPU
#define float3   DirectX::XMFLOAT3
#define float2   DirectX::XMFLOAT2
#define matrix   DirectX::XMMATRIX
#define float3x3 DirectX::XMFLOAT3X3
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
inline int HasDiffuseMap(int textureConfig)  { return ((textureConfig & 1  ) > 0 ? 1 : 0); }
inline int HasNormalMap(int textureConfig)	 { return ((textureConfig & 2  ) > 0 ? 1 : 0); }
inline int HasSpecularMap(int textureConfig) { return ((textureConfig & 4  ) > 0 ? 1 : 0); }
inline int HasAlphaMask(int textureConfig)   { return ((textureConfig & 8  ) > 0 ? 1 : 0); }
inline int HasRoughnessMap(int textureConfig){ return ((textureConfig & 16 ) > 0 ? 1 : 0); }
inline int HasMetallicMap(int textureConfig) { return ((textureConfig & 32 ) > 0 ? 1 : 0); }
inline int HasHeightMap(int textureConfig)   { return ((textureConfig & 64 ) > 0 ? 1 : 0); }
inline int HasEmissiveMap(int textureConfig) { return ((textureConfig & 128) > 0 ? 1 : 0); }

struct SurfaceMaterial
{
    float3 diffuse;
    float alpha;

    float3 specular;
    float roughness;

    float metalness;
    float shininess;
	float2 uvScale;

	int textureConfig;
	int pad0, pad1, pad2;

    float3 emissiveColor;
    float emissiveIntensity;
};



//----------------------------------------------------------
// SHADER CONSTANT BUFFER INTERFACE
//----------------------------------------------------------
struct PerFrameData
{
	// Shadow Maps
	SceneLighting Lights;
};
struct PerViewData
{
	matrix matView;
	matrix matViewToWorld;
	matrix matProjInverse;

	float3 CameraPosition;
	float2 ScreenDimensions;
};
struct PerObjectData
{
	matrix matWorldViewProj;
	matrix matWorld;
	//float3x3 matNormal;
	matrix matNormal;
};


#ifdef VQ_CPU
} // namespace VQ_SHADER_DATA
#endif