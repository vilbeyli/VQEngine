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

#pragma once

#include "Shaders/LightingConstantBufferData.h"
#include "Transform.h"
#include "Settings.h"
#include "../Renderer/Texture.h"
#include <DirectXMath.h>


// Only used for point lights when querying LightSpaceMatrix, ViewMatrix and ViewFrustumPlanes.
//
#define DEFAULT_POINT_LIGHT_LOOK_DIRECTION Texture::CubemapUtility::ECubeMapLookDirections::CUBEMAP_LOOK_FRONT


// Design considerations here:
//
// INHERITANCE
// - if we were to use inheritance for different types of lights, then we can utilize pure virtual functions
//   to enforce class-specific behavior. However, now, we cannot store a vector<Light> due to pure virtuality.
//   most likely solution is the store pointers to derived types, which now requires a memory manager for lights
//   if we want to iterate over lights in a linear-memory-access fashion.
//
// C-STYLE 
// - instead, we can collect the light-specific data under a union and enforce light-specific behavior
//   through the ELightType enum. Currently favoring this approach over inheritance to avoid maintaining the memory
//   of the derived types and simply making use of a vector to hold all light data.
//
struct Light
{
	enum EType : int
	{
		POINT = 0,
		SPOT,
		DIRECTIONAL,

		LIGHT_TYPE_COUNT
	};
	enum EMobility : int
	{
		// Static lights = constant lights
		STATIC = 0,

		// Stationary Lights are lights that are intended to stay in one position, 
		// but are able to change in other ways, such as their brightness and color. 
		// This is the primary way in which they differ from Static Lights, 
		// which cannot change in any way during simulation.
		STATIONARY,

		// Moving lights
		DYNAMIC,

		NUM_LIGHT_MOBILITY_TYPES
	};
	struct FShadowData
	{
		FShadowData(float bias, float near, float far) : DepthBias(bias), NearPlane(near), FarPlane(far) { }
		float DepthBias;
		float NearPlane;
		float FarPlane;
	};

	// ===========================================================================================================
	// returns a view matrix for each light type that supports shadow mapping
	// 
	static DirectX::XMMATRIX CalculateDirectionalLightViewMatrix(const Light& mDirLight);
	static DirectX::XMMATRIX CalculateSpotLightViewMatrix(const Transform& mTransform);
	static DirectX::XMMATRIX CalculatePointLightViewMatrix(Texture::CubemapUtility::ECubeMapLookDirections lookDir, const DirectX::XMFLOAT3& position);
	static DirectX::XMMATRIX CalculateProjectionMatrix(Light::EType eType, float near, float far, const DirectX::XMFLOAT2 viewPortSize = DirectX::XMFLOAT2(0, 0));

	// Creates a default Light type with some fields pre-initialized
	//
	static Light MakePointLight();
	static Light MakeDirectionalLight();
	static Light MakeSpotLight();
	// ===========================================================================================================
	
	Light();
	void GetGPUData(VQ_SHADER_DATA::DirectionalLight* pLight) const;
	void GetGPUData(VQ_SHADER_DATA::PointLight*       pLight) const;
	void GetGPUData(VQ_SHADER_DATA::SpotLight*        pLight) const;
	DirectX::XMMATRIX GetWorldTransformationMatrix() const;
	DirectX::XMMATRIX GetViewProjectionMatrix(Texture::CubemapUtility::ECubeMapLookDirections lookDir = Texture::CubemapUtility::ECubeMapLookDirections::CUBEMAP_LOOK_FRONT) const;
	Transform GetTransform() const;

	//
	// DATA
	//
public:
	// CPU (Hot) data
	DirectX::XMFLOAT3 Position;
	float             Range;
	Quaternion        RotationQuaternion;
	DirectX::XMFLOAT3 RenderScale;
	bool              bEnabled;
	bool              bCastingShadows;
	EMobility         Mobility;
	EType             Type;

	// GPU (Cold) data
	DirectX::XMFLOAT3 Color;
	float             Brightness;
	FShadowData       ShadowData; //float[3]
	union // LIGHT-SPECIFIC DATA
	{
		// DIRECTIONAL LIGHT ----------------------------------------
		//  
		//   |  |  |  |  |
		//   |  |  |  |  |
		//   v  v  v  v  v
		//    
		struct
		{
			float ViewportX;
			float ViewportY;
			float DistanceFromOrigin;
		};


		// POINT LIGHT -----------------------------------------------
		//
		//   \|/ 
		//  --*--
		//   /|\
		//
		struct // Point
		{
			float AttenuationConstant;  // Currently Unused: attenuation = 1/distance^2 in the shaders
			float AttenuationLinear;    // Currently Unused: attenuation = 1/distance^2 in the shaders
			float AttenuationQuadratic; // Currently Unused: attenuation = 1/distance^2 in the shaders
		};


		// SPOT LIGHT --------------------------------------------------
		//     
		//       *
		//     /   \
		//    /_____\
		//   ' ' ' ' ' 
		//
		struct // Spot
		{
			float SpotOuterConeAngleDegrees;
			float SpotInnerConeAngleDegrees;
			float dummy1;
		};


		// TODO: linear lights from GPU Zen
		// Eric Heitz Slides: https://drive.google.com/file/d/0BzvWIdpUpRx_Z2pZWWFtam5xTFE/view
		// LINEAR LIGHT  ------------------------------------------------
		//
		//
		//
		//
		//
		//
		struct // Area
		{
			float dummy2;
			float dummy3;
			float dummy4;
		};
	};

};

//constexpr size_t SZ_LIGHT_STRUCT = sizeof(Light);
