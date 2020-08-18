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
#if 0
	static DirectX::XMMATRIX CalculatePointLightViewMatrix(Texture::CubemapUtility::ECubeMapLookDirections lookDir, const vec3& position);
#endif

	// TODO
};
constexpr size_t SZ_LIGHT_STRUCT = sizeof(Light);
