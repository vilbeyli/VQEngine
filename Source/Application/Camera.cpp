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
#include "Input.h"

#define CAMERA_DEBUG 1

using namespace DirectX;

Camera::Camera()
	: mPitch(0.0f)
	, mYaw(0.0f)
	, mPosition(0,0,0)
	, mVelocity(0,0,0)
{
	XMStoreFloat4x4(&mMatProj, XMMatrixIdentity());
	XMStoreFloat4x4(&mMatView, XMMatrixIdentity());
}

Camera::~Camera(void)
{}

Camera::Camera(Camera && other)
{
	mPosition = other.mPosition;
	mYaw = other.mYaw;
	mVelocity = other.mVelocity;
	mPitch = other.mPitch;
	mProjParams = other.mProjParams;
	mMatProj = other.mMatProj;
	mMatView = other.mMatView;
	pController = std::move(other.pController);
}

Camera Camera::Clone()
{
	Camera c = {};
	c.mPosition = this->mPosition;
	c.mYaw = this->mYaw;
	c.mVelocity = this->mVelocity;
	c.mPitch = this->mPitch;
	c.mProjParams = this->mProjParams;
	c.mMatProj = this->mMatProj;
	c.mMatView = this->mMatView;
	c.pController = std::move(this->pController->Clone(&c));
	return c; // is this dangling too?
}

void Camera::InitializeCamera(const FCameraParameters& data)
{
	const auto& NEAR_PLANE   = data.NearPlane;
	const auto& FAR_PLANE    = data.FarPlane;
	const float AspectRatio  = data.Width / data.Height;
	const float VerticalFoV  = data.FovV_Degrees * DEG2RAD;
	const float& ViewportX   = data.Width;
	const float& ViewportY   = data.Height;

	this->mProjParams.NearZ = NEAR_PLANE;
	this->mProjParams.FarZ  = FAR_PLANE;
	this->mProjParams.ViewporHeight = ViewportY;
	this->mProjParams.ViewporWidth  = ViewportX;
	this->mProjParams.FieldOfView = data.FovV_Degrees * DEG2RAD;
	this->mProjParams.bPerspectiveProjection = data.bPerspectiveProjection;

	mYaw = mPitch = 0;
	SetProjectionMatrix(this->mProjParams);
	SetPosition(data.x, data.y, data.z);
	Rotate(data.Yaw * DEG2RAD, data.Pitch * DEG2RAD);
	UpdateViewMatrix();
}

void Camera::InitializeController(bool bFirstPersonController)
{
	if (bFirstPersonController)
	{
		pController = std::make_unique<FirstPersonController>(this);
	}
	else
	{
		pController = std::make_unique<OrbitController>(this);
	}
}

void Camera::SetProjectionMatrix(const FProjectionMatrixParameters& params)
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

void Camera::Rotate(float yaw, float pitch)
{
	mYaw   += yaw;
	mPitch += pitch;
	
	if (mPitch > +90.0f * DEG2RAD) mPitch = +90.0f * DEG2RAD;
	if (mPitch < -90.0f * DEG2RAD) mPitch = -90.0f * DEG2RAD;
}

//==============================================================================================================


OrbitController::OrbitController(Camera* pCam)
	: CameraController(pCam)
{
}

void OrbitController::UpdateCamera(const Input& input, float dt)

{
}

CameraController* OrbitController::Clone_impl(Camera* pNewCam)
{
	OrbitController* p = new OrbitController(pNewCam); 
	p->mF3LookAt = this->mF3LookAt;
	return p;
}

FirstPersonController::FirstPersonController(Camera* pCam)
	: CameraController(pCam)
{
}

void FirstPersonController::UpdateCamera(const Input& input, float dt)
{
	constexpr float CAMERA_MOVEMENT_SPEED_MULTIPLER = 0.75f;
	constexpr float CAMERA_MOVEMENT_SPEED_SHIFT_MULTIPLER = 2.0f;

	XMVECTOR LocalSpaceTranslation = XMVectorSet(0, 0, 0, 0);
	if (input.IsKeyDown('A'))      LocalSpaceTranslation += XMLoadFloat3(&LeftVector);
	if (input.IsKeyDown('D'))      LocalSpaceTranslation += XMLoadFloat3(&RightVector);
	if (input.IsKeyDown('W'))      LocalSpaceTranslation += XMLoadFloat3(&ForwardVector);
	if (input.IsKeyDown('S'))      LocalSpaceTranslation += XMLoadFloat3(&BackVector);
	if (input.IsKeyDown('E'))      LocalSpaceTranslation += XMLoadFloat3(&UpVector);
	if (input.IsKeyDown('Q'))      LocalSpaceTranslation += XMLoadFloat3(&DownVector);
	if (input.IsKeyDown(VK_SHIFT)) LocalSpaceTranslation *= CAMERA_MOVEMENT_SPEED_SHIFT_MULTIPLER;
	LocalSpaceTranslation *= CAMERA_MOVEMENT_SPEED_MULTIPLER;

	// update camera
	FCameraInput camInput(LocalSpaceTranslation);
	camInput.DeltaMouseXY = input.GetMouseDelta();


	//this->mpCamera->Update(dt, camInput);
	const float RotationSpeed = this->AngularSpeedDeg * DEG2RAD; // rotation doesn't depend on time
	const float dy = camInput.DeltaMouseXY[1] * RotationSpeed;
	const float dx = camInput.DeltaMouseXY[0] * RotationSpeed;
	this->mpCamera->Rotate(dx, dy);


	//this->mpCamera->Move(dt, camInput);
	const XMMATRIX MRotation = this->mpCamera->GetRotationMatrix();
	const XMVECTOR WorldSpaceTranslation = XMVector3TransformCoord(camInput.LocalTranslationVector, MRotation);

	XMVECTOR V = XMLoadFloat3(&this->mpCamera->mVelocity);
	V += (WorldSpaceTranslation * MoveSpeed - V * Drag) * dt;
	XMStoreFloat3(&this->mpCamera->mVelocity, V);

	this->mpCamera->UpdateViewMatrix();

	// move based on velocity
	XMVECTOR P = XMLoadFloat3(&this->mpCamera->mPosition);
	P += V * dt;
	XMStoreFloat3(&this->mpCamera->mPosition, P);
}

CameraController* FirstPersonController::Clone_impl(Camera* pNewCam)
{
	FirstPersonController* p = new FirstPersonController(pNewCam);
	p->AngularSpeedDeg = this->AngularSpeedDeg;
	p->Drag = this->Drag;
	p->MoveSpeed = this->MoveSpeed;
	return p;
}
