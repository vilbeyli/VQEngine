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


struct FrustumPlaneset
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
	inline static FrustumPlaneset ExtractFromMatrix(const DirectX::XMMATRIX& projectionTransformation)
	{
		const DirectX::XMMATRIX& m = projectionTransformation;

		FrustumPlaneset viewPlanes;
		// TODO: XMVECTOR impl;
		viewPlanes.abcd[FrustumPlaneset::PL_RIGHT] = DirectX::XMFLOAT4(
			m.r[0].m128_f32[3] - m.r[0].m128_f32[0],
			m.r[1].m128_f32[3] - m.r[1].m128_f32[0],
			m.r[2].m128_f32[3] - m.r[2].m128_f32[0],
			m.r[3].m128_f32[3] - m.r[3].m128_f32[0]
		);
		viewPlanes.abcd[FrustumPlaneset::PL_LEFT] = DirectX::XMFLOAT4(
			m.r[0].m128_f32[3] + m.r[0].m128_f32[0],
			m.r[1].m128_f32[3] + m.r[1].m128_f32[0],
			m.r[2].m128_f32[3] + m.r[2].m128_f32[0],
			m.r[3].m128_f32[3] + m.r[3].m128_f32[0]
		);
		viewPlanes.abcd[FrustumPlaneset::PL_TOP] = DirectX::XMFLOAT4(
			m.r[0].m128_f32[3] - m.r[0].m128_f32[1],
			m.r[1].m128_f32[3] - m.r[1].m128_f32[1],
			m.r[2].m128_f32[3] - m.r[2].m128_f32[1],
			m.r[3].m128_f32[3] - m.r[3].m128_f32[1]
		);
		viewPlanes.abcd[FrustumPlaneset::PL_BOTTOM] = DirectX::XMFLOAT4(
			m.r[0].m128_f32[3] + m.r[0].m128_f32[1],
			m.r[1].m128_f32[3] + m.r[1].m128_f32[1],
			m.r[2].m128_f32[3] + m.r[2].m128_f32[1],
			m.r[3].m128_f32[3] + m.r[3].m128_f32[1]
		);
		viewPlanes.abcd[FrustumPlaneset::PL_FAR] = DirectX::XMFLOAT4(
			m.r[0].m128_f32[3] - m.r[0].m128_f32[2],
			m.r[1].m128_f32[3] - m.r[1].m128_f32[2],
			m.r[2].m128_f32[3] - m.r[2].m128_f32[2],
			m.r[3].m128_f32[3] - m.r[3].m128_f32[2]
		);
		viewPlanes.abcd[FrustumPlaneset::PL_NEAR] = DirectX::XMFLOAT4(
			m.r[0].m128_f32[2],
			m.r[1].m128_f32[2],
			m.r[2].m128_f32[2],
			m.r[3].m128_f32[2]
		);
		return viewPlanes;
	}
};

struct CameraData
{
	union
	{
		float fovH_Degrees;
		float fovV_Degrees;
	};
	float nearPlane;
	float farPlane;
	float aspect;
	float x, y, z;
	float yaw, pitch; // in degrees
};

class Camera
{
public:
	Camera();
	~Camera(void);

	void InitializeCamera(const CameraData& data, int ViewportX, int ViewportY);

	void SetOthoMatrix(int screenWidth, int screenHeight, float screenNear, float screenFar);
	void SetProjectionMatrix(float fovy, float screenAspect, float screenNear, float screenFar);
	void SetProjectionMatrixHFov(float fovx, float screenAspectInverse, float screenNear, float screenFar);

	// updates View Matrix @mMatView
	void Update(float dt);

	DirectX::XMFLOAT3 GetPositionF() const;
	DirectX::XMMATRIX GetViewMatrix() const;
	DirectX::XMMATRIX GetViewInverseMatrix() const;
	DirectX::XMMATRIX GetProjectionMatrix() const;

	// returns World Space frustum plane set 
	//
	FrustumPlaneset GetViewFrustumPlanes() const;
	
	void SetPosition(float x, float y, float z);
	void Rotate(float yaw, float pitch, const float dt);

	void Reset();	// resets camera transform to initial position & orientation

public:
	float Drag;             // 15.0f
	float AngularSpeedDeg;  // 40.0f
	float MoveSpeed;        // 1000.0f

private:
	void Move(const float dt);
	void Rotate(const float dt);
	DirectX::XMMATRIX RotMatrix() const;

private:
	DirectX::XMFLOAT3 mPosition;
	float mYaw = 0.0f;
	DirectX::XMFLOAT3 mVelocity;
	float mPitch = 0.0f;
	DirectX::XMFLOAT4X4 mMatView;
	DirectX::XMFLOAT4X4 mMatProj;
};
