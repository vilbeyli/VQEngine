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

#include "Math.h"

#include <array>

struct FFrustumPlaneset
{	// plane equations: aX + bY + cZ + d = 0
	DirectX::XMFLOAT4 abcd[6]; // r, l, t, b, n, f
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
	inline static FFrustumPlaneset ExtractFromMatrix(const DirectX::XMMATRIX& projectionTransformation)
	{
		const DirectX::XMMATRIX& m = projectionTransformation;

		FFrustumPlaneset viewPlanes;
		// TODO: XMVECTOR impl;
		viewPlanes.abcd[FFrustumPlaneset::PL_RIGHT] = DirectX::XMFLOAT4(
			m.r[0].m128_f32[3] - m.r[0].m128_f32[0],
			m.r[1].m128_f32[3] - m.r[1].m128_f32[0],
			m.r[2].m128_f32[3] - m.r[2].m128_f32[0],
			m.r[3].m128_f32[3] - m.r[3].m128_f32[0]
		);
		viewPlanes.abcd[FFrustumPlaneset::PL_LEFT] = DirectX::XMFLOAT4(
			m.r[0].m128_f32[3] + m.r[0].m128_f32[0],
			m.r[1].m128_f32[3] + m.r[1].m128_f32[0],
			m.r[2].m128_f32[3] + m.r[2].m128_f32[0],
			m.r[3].m128_f32[3] + m.r[3].m128_f32[0]
		);
		viewPlanes.abcd[FFrustumPlaneset::PL_TOP] = DirectX::XMFLOAT4(
			m.r[0].m128_f32[3] - m.r[0].m128_f32[1],
			m.r[1].m128_f32[3] - m.r[1].m128_f32[1],
			m.r[2].m128_f32[3] - m.r[2].m128_f32[1],
			m.r[3].m128_f32[3] - m.r[3].m128_f32[1]
		);
		viewPlanes.abcd[FFrustumPlaneset::PL_BOTTOM] = DirectX::XMFLOAT4(
			m.r[0].m128_f32[3] + m.r[0].m128_f32[1],
			m.r[1].m128_f32[3] + m.r[1].m128_f32[1],
			m.r[2].m128_f32[3] + m.r[2].m128_f32[1],
			m.r[3].m128_f32[3] + m.r[3].m128_f32[1]
		);
		viewPlanes.abcd[FFrustumPlaneset::PL_FAR] = DirectX::XMFLOAT4(
			m.r[0].m128_f32[3] - m.r[0].m128_f32[2],
			m.r[1].m128_f32[3] - m.r[1].m128_f32[2],
			m.r[2].m128_f32[3] - m.r[2].m128_f32[2],
			m.r[3].m128_f32[3] - m.r[3].m128_f32[2]
		);
		viewPlanes.abcd[FFrustumPlaneset::PL_NEAR] = DirectX::XMFLOAT4(
			m.r[0].m128_f32[2],
			m.r[1].m128_f32[2],
			m.r[2].m128_f32[2],
			m.r[3].m128_f32[2]
		);
		return viewPlanes;
	}
};

struct FCameraData
{
	float x, y, z; // position
	float width;
	float height;
	float nearPlane;
	float farPlane;
	float fovV_Degrees;
	float yaw, pitch; // in degrees
	bool bPerspectiveProjection;
};
struct ProjectionMatrixParameters
{
	float ViewporWidth;
	float ViewporHeight;
	float NearZ;
	float FarZ;
	float FieldOfView;
	bool bPerspectiveProjection;
};

struct FCameraInput
{
	FCameraInput() = delete;
	FCameraInput(const DirectX::XMVECTOR& v) : LocalTranslationVector(v), DeltaMouseXY{ 0, 0 } {}

	const DirectX::XMVECTOR& LocalTranslationVector;
	std::array<float, 2> DeltaMouseXY;
};

class Camera
{
public:
	Camera();
	~Camera(void);

	void InitializeCamera(const FCameraData& data);

	void SetProjectionMatrix(const ProjectionMatrixParameters& params);

	// updates View Matrix @mMatView
	void Update(const float dt, const FCameraInput& input);

	DirectX::XMFLOAT3 GetPositionF() const;
	DirectX::XMMATRIX GetViewMatrix() const;
	DirectX::XMMATRIX GetViewInverseMatrix() const;
	DirectX::XMMATRIX GetProjectionMatrix() const;
	DirectX::XMMATRIX GetRotationMatrix() const;

	// returns World Space frustum plane set 
	FFrustumPlaneset GetViewFrustumPlanes() const;
	
	void SetPosition(float x, float y, float z);
	void Rotate(float yaw, float pitch, const float dt);
	void Move(const float dt, const FCameraInput& input);
	void Rotate(const float dt, const FCameraInput& input);


	//--------------------------
	DirectX::XMFLOAT3 mPosition;
	float mYaw = 0.0f;
	//--------------------------
	DirectX::XMFLOAT3 mVelocity;
	float mPitch = 0.0f;
	// -------------------------
	ProjectionMatrixParameters mProjParams;
	// -------------------------
	float Drag;            
	float AngularSpeedDeg; 
	float MoveSpeed;       
	// -------------------------
	DirectX::XMFLOAT4X4 mMatProj;
	// -------------------------
	DirectX::XMFLOAT4X4 mMatView;
	// -------------------------
};
