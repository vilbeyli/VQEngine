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

#include <DirectXMath.h>
#include <array>

//------------------------------------------------------------------------------------------------------------------------------
//
// DATA STRUCTURES
//
//------------------------------------------------------------------------------------------------------------------------------
struct FFrustumPlaneset
{	// plane equations: aX + bY + cZ + d = 0
	DirectX::XMVECTOR abcd[6]; // planes[6]: r, l, t, b, n, f
	enum EPlaneset
	{
		PL_RIGHT = 0,
		PL_LEFT,
		PL_TOP,
		PL_BOTTOM,
		PL_FAR,
		PL_NEAR
	};

	// src: http://gamedevs.org/uploads/fast-extraction-viewing-frustum-planes-from-world-view-projection-matrix.pdfe
	// gets the frustum planes based on @projectionTransformation. if:
	//
	// - @projectionTransformation is proj          matrix ->  view space plane equations
	// - @projectionTransformation is viewProj      matrix -> world space plane equations
	// - @projectionTransformation is worldViewProj matrix -> model space plane equations
	// 
	inline static FFrustumPlaneset ExtractFromMatrix(const DirectX::XMMATRIX& projectionTransformation, const bool bNormalize = false)
	{
		DirectX::XMMATRIX m = DirectX::XMMatrixTranspose(projectionTransformation);

		FFrustumPlaneset viewPlanes;
		viewPlanes.abcd[FFrustumPlaneset::PL_RIGHT]  = DirectX::XMVectorSubtract(m.r[3], m.r[0]);
		viewPlanes.abcd[FFrustumPlaneset::PL_LEFT]   = DirectX::XMVectorAdd(m.r[3], m.r[0]);
		viewPlanes.abcd[FFrustumPlaneset::PL_TOP]    = DirectX::XMVectorSubtract(m.r[3], m.r[1]);
		viewPlanes.abcd[FFrustumPlaneset::PL_BOTTOM] = DirectX::XMVectorAdd(m.r[3], m.r[1]);
		viewPlanes.abcd[FFrustumPlaneset::PL_FAR]    = DirectX::XMVectorSubtract(m.r[3], m.r[2]);
		viewPlanes.abcd[FFrustumPlaneset::PL_NEAR]   = m.r[2];

		if (bNormalize)
		{
			for (int i = 0; i < 6; ++i)
			{
				DirectX::XMVECTOR& vPlane = viewPlanes.abcd[i];
				DirectX::XMVECTOR vLen = DirectX::XMVectorSqrt(DirectX::XMVector3Dot(vPlane, vPlane));
				vPlane = DirectX::XMVectorMultiply(vPlane, DirectX::XMVectorReciprocal(vLen));
			}
		}

		return viewPlanes;
	}
};

struct FSphere
{
	FSphere(const DirectX::XMFLOAT3& CenterIn, float RadiusIn) : CenterPosition(CenterIn), Radius(RadiusIn) {}
	DirectX::XMFLOAT3 CenterPosition;
	float Radius;
};
struct FBoundingBox
{
	DirectX::XMFLOAT3 ExtentMin;
	DirectX::XMFLOAT3 ExtentMax;
	FBoundingBox():ExtentMin(0,0,0),ExtentMax(0,0,0){}
	FBoundingBox(const FSphere& s); // makes a bounding box encapsulating the sphere
	DirectX::XMMATRIX GetWorldTransformMatrix() const;
	DirectX::XMVECTOR GetExtent() const;
	DirectX::XMVECTOR GetCenter() const;

	std::array<DirectX::XMVECTOR, 8> GetCornerPointsV4() const;
	std::array<DirectX::XMVECTOR, 8> GetCornerPointsV3() const;
	std::array<DirectX::XMFLOAT4, 8> GetCornerPointsF4() const;
	std::array<DirectX::XMFLOAT3, 8> GetCornerPointsF3() const;
};
