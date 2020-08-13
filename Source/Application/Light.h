//	VQEngine | DirectX11 Renderer
//	Copyright(C) 2018  - Volkan Ilbeyli
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


#include "Transform.h"
#include "Settings.h"
//#include "DataStructures.h"
//
//#include "Renderer/RenderingEnums.h"
//#include "Renderer/Texture.h"
//
//#include "Utilities/Color.h"

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
	enum ELightType : int
	{
		POINT = 0,
		SPOT,
		DIRECTIONAL,

		LIGHT_TYPE_COUNT
	};

	// returns a view matrix for each light type that supports shadow mapping
	// 
	static DirectX::XMMATRIX CalculateDirectionalLightViewMatrix(const Light& mDirLight);
	static DirectX::XMMATRIX CalculateSpotLightViewMatrix(const Transform& mTransform);
	//static DirectX::XMMATRIX CalculatePointLightViewMatrix(Texture::CubemapUtility::ECubeMapLookDirections lookDir, const vec3& position);
	
#if 0
	// returns a projection matrix based on @lightType, @near, @far and in the directional light case, @viewPortSize as well.
	//
	static DirectX::XMMATRIX CalculateProjectionMatrix(Light::ELightType lightType, float near, float far, vec2 viewPortSize = vec2(0, 0));


	//--------------------------------------------------------------------------------------------------------------
	// INTERFACE
	//--------------------------------------------------------------------------------------------------------------
	Light() // DEFAULTS
		: mColor(LinearColor::white)
		, mbEnabled(true)
		, mType(LIGHT_TYPE_COUNT)
		, mBrightness(300.0f)
		, mbCastingShadows(false)
		, mDepthBias(0.0f)
		, mNearPlaneDistance(0.0f)
		, mFarPlaneDistance(0.0f)
		, mRange(100.0f)
		, mTransform()
		, mMeshID(EGeometry::SPHERE)
	{}
	Light
	(
		LinearColor color
		, ELightType type
		, float brightness
		, bool bCastShadows
		, float depthBias
		, float nearPlaneDistance
		, float range
		, Transform transform
		, EGeometry mesh
	)
		: mColor(color)
		, mType(type)
		, mBrightness(brightness)
		, mbCastingShadows(bCastShadows)
		, mDepthBias(depthBias)
		, mNearPlaneDistance(nearPlaneDistance)
		, mRange(range)
		, mTransform(transform)
		, mMeshID(mesh)
	{}


	// returns the projection matrix for the light space transformation. 
	//
	DirectX::XMMATRIX GetProjectionMatrix() const;

	// returns the view matrix for Directional/Spot lights. 
	// Use 'Texture::CubemapUtility::ECubeMapLookDirections' to get view matrices for cubemap faces for PointLight.
	//
	DirectX::XMMATRIX GetViewMatrix(Texture::CubemapUtility::ECubeMapLookDirections lookDir = DEFAULT_POINT_LIGHT_LOOK_DIRECTION) const;

	// returns the frustum plane data for the light.
	// Use 'Texture::CubemapUtility::ECubeMapLookDirections' to get frustum planes for each direction for PointLight.
	//
	inline FrustumPlaneset GetViewFrustumPlanes(Texture::CubemapUtility::ECubeMapLookDirections lookDir = DEFAULT_POINT_LIGHT_LOOK_DIRECTION) const
	{ 
		return FrustumPlaneset::ExtractFromMatrix(GetLightSpaceMatrix(lookDir));
	}

	// Returns the View*Projection matrix that describes the light-space transformation of a world space position.
	// Use 'Texture::CubemapUtility::ECubeMapLookDirections' to get the light space matrix for each direction for PointLight.
	//
	inline DirectX::XMMATRIX GetLightSpaceMatrix(Texture::CubemapUtility::ECubeMapLookDirections lookDir = DEFAULT_POINT_LIGHT_LOOK_DIRECTION) const 
	{ 
		return GetViewMatrix(lookDir) * GetProjectionMatrix(); 
	}


	void GetGPUData(PointLightGPU& refData) const;
	void GetGPUData(SpotLightGPU& refData) const;
	void GetGPUData(DirectionalLightGPU& refData) const;

	// TODO: remove this arbitrary function for directional lights
	Settings::ShadowMap GetSettings() const; // ?

private:
	// cache the View and Projection matrices in the struct as computing a lot 
	// of them is quite expensive (many point lights) during frustum culling.
	DirectX::XMMATRIX mProjectionMatrix;
	std::array<DirectX::XMMATRIX, 6> mViewMatrix;
	
public:
	void SetMatrices();
	
	inline const Transform& GetTransform() const { return mTransform; }
	const void SetTransform(const Transform& transform);

public:
	//--------------------------------------------------------------------------------------------------------------
	// DATA
	//--------------------------------------------------------------------------------------------------------------
	ELightType mType;
	LinearColor	mColor;
	float mBrightness;

	bool mbCastingShadows;
	float mDepthBias;
	float mNearPlaneDistance;
	union 
	{
		// well, they're not essentially the same but good enough for saving space for now
		float mFarPlaneDistance;
		float mRange;
	};

	Transform mTransform;
	EGeometry mMeshID;

	bool mbEnabled;

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
			float mViewportX;
			float mViewportY;
			float mDistanceFromOrigin;
		};
		


		// POINT LIGHT -----------------------------------------------
		//
		//   \|/ 
		//  --*--
		//   /|\
		//
		struct // Point
		{
			float mAttenuationConstant ; // Currently Unused: attenuation = 1/distance^2 in the shaders
			float mAttenuationLinear   ; // Currently Unused: attenuation = 1/distance^2 in the shaders
			float mAttenuationQuadratic; // Currently Unused: attenuation = 1/distance^2 in the shaders
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
			float mSpotOuterConeAngleDegrees;
			float mSpotInnerConeAngleDegrees;
			float dummy1;
		};



		// TODO: v0.6.0 linear lights from GPU Zen
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
#endif
};
constexpr size_t SZ_LIGHT_STRUCT = sizeof(Light);
