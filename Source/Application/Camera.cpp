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

#include "Camera.h"

#define CAMERA_DEBUG 1

using namespace DirectX;

Camera::Camera()
	:
	MoveSpeed(1000.0f),
	AngularSpeedDeg(0.05f),
	Drag(9.5f),
	mPitch(0.0f),
	mYaw(0.0f),
	mPosition(0,0,0),
	mVelocity(0,0,0)
{
	XMStoreFloat4x4(&mMatProj, XMMatrixIdentity());
	XMStoreFloat4x4(&mMatView, XMMatrixIdentity());
}

Camera::~Camera(void)
{}

void Camera::InitializeCamera(const FCameraData& data)
{
	const auto& NEAR_PLANE   = data.nearPlane;
	const auto& FAR_PLANE    = data.farPlane;
	const float AspectRatio  = data.width / data.height;
	const float VerticalFoV  = data.fovV_Degrees * DEG2RAD;
	const float& ViewportX   = data.width;
	const float& ViewportY   = data.height;

	this->mProjParams.NearZ = NEAR_PLANE;
	this->mProjParams.FarZ  = FAR_PLANE;
	this->mProjParams.ViewporHeight = ViewportY;
	this->mProjParams.ViewporWidth  = ViewportX;
	this->mProjParams.FieldOfView = data.fovV_Degrees * DEG2RAD;
	this->mProjParams.bPerspectiveProjection = data.bPerspectiveProjection;

	mYaw = mPitch = 0;
	SetProjectionMatrix(this->mProjParams);
	SetPosition(data.x, data.y, data.z);
	Rotate(data.yaw * DEG2RAD, data.pitch * DEG2RAD, 1.0f);
	UpdateViewMatrix();
}


void Camera::SetProjectionMatrix(const ProjectionMatrixParameters& params)
{
	assert(params.ViewporHeight > 0.0f);
	const float AspectRatio = params.ViewporWidth / params.ViewporHeight;

	mMatProj = params.bPerspectiveProjection
		? MakePerspectiveProjectionMatrix(params.FieldOfView, AspectRatio, params.NearZ, params.FarZ)
		: MakeOthographicProjectionMatrix(params.ViewporWidth, params.ViewporHeight, params.NearZ, params.FarZ);
}

void Camera::UpdateViewMatrix()
{
	XMVECTOR up         = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMVECTOR lookAt     = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	const XMVECTOR pos  = XMLoadFloat3(&mPosition);
	const XMMATRIX MRot = GetRotationMatrix();

	//transform the lookat and up vector by rotation matrix
	lookAt = XMVector3TransformCoord(lookAt, MRot);
	up = XMVector3TransformCoord(up, MRot);

	//translate the lookat
	lookAt = pos + lookAt;

	XMStoreFloat4x4(&mMatView, XMMatrixLookAtLH(pos, lookAt, up));
}

void Camera::Update(const float dt, const FCameraInput& input)
{
	Rotate(dt, input);
	Move(dt, input);

	UpdateViewMatrix();

	// move based on velocity
	XMVECTOR P = XMLoadFloat3(&mPosition);
	XMVECTOR V = XMLoadFloat3(&mVelocity);
	P += V * dt;
	XMStoreFloat3(&mPosition, P);
}


XMFLOAT3 Camera::GetPositionF() const
{
	return mPosition;
}

XMMATRIX Camera::GetViewMatrix() const
{
	return XMLoadFloat4x4(&mMatView);
}

XMMATRIX Camera::GetViewInverseMatrix() const
{
	const XMVECTOR up     = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	const XMVECTOR lookAt = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	const XMVECTOR pos    = XMLoadFloat3(&mPosition);
	const XMMATRIX MRot   = GetRotationMatrix();

	const XMVECTOR dir    = XMVector3Normalize(lookAt - pos);
	const XMVECTOR wing   = XMVector3Cross(up, dir);

	XMMATRIX R = XMMatrixIdentity(); 
	R.r[0] = wing;
	R.r[1] = up;
	R.r[2] = dir;
	R.r[0].m128_f32[3] = R.r[1].m128_f32[3] = R.r[2].m128_f32[3] = 0;
	//R = XMMatrixTranspose(R);
	
	XMMATRIX T = XMMatrixIdentity();
	T.r[3] = pos;
	T.r[3].m128_f32[3] = 1.0;
	
	XMMATRIX viewInverse = R * T;	// this is for ViewMatrix
	//	orienting our model using this, we want the inverse of view mat
	// XMMATRIX rotMatrix = XMMatrixTranspose(R) * T.inverse();

	XMMATRIX view = XMLoadFloat4x4(&mMatView);
	XMVECTOR det = XMMatrixDeterminant(view);
	XMMATRIX test = XMMatrixInverse(&det, view);

	return test;
}

XMMATRIX Camera::GetProjectionMatrix() const
{
	return  XMLoadFloat4x4(&mMatProj);
}

FFrustumPlaneset Camera::GetViewFrustumPlanes() const
{
	return FFrustumPlaneset::ExtractFromMatrix(GetViewMatrix() * GetProjectionMatrix());
}

XMMATRIX Camera::GetRotationMatrix() const
{
	return XMMatrixRotationRollPitchYaw(mPitch, mYaw, 0.0f);
}

void Camera::SetPosition(float x, float y, float z)
{
	mPosition = XMFLOAT3(x, y, z);
}

void Camera::Rotate(float yaw, float pitch, const float dt)
{
	mYaw   += yaw   * dt;
	mPitch += pitch * dt;
	
	if (mPitch > +90.0f * DEG2RAD) mPitch = +90.0f * DEG2RAD;
	if (mPitch < -90.0f * DEG2RAD) mPitch = -90.0f * DEG2RAD;
}

// internal update functions
void Camera::Rotate(const float dt, const FCameraInput& input)
{
	const float& dy = input.DeltaMouseXY[1];
	const float& dx = input.DeltaMouseXY[0];

	const float delta = AngularSpeedDeg * DEG2RAD; // rotation doesn't depend on time
	Rotate(dx, dy, delta);
}

void Camera::Move(const float dt, const FCameraInput& input)
{
	const XMMATRIX MRotation	 = GetRotationMatrix();
	const XMVECTOR WorldSpaceTranslation = XMVector3TransformCoord(input.LocalTranslationVector, MRotation);

	XMVECTOR V = XMLoadFloat3(&mVelocity);
	V += (WorldSpaceTranslation * MoveSpeed - V * Drag) * dt;
	XMStoreFloat3(&mVelocity, V);
}
