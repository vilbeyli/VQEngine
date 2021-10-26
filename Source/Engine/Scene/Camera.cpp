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

#include "Camera.h"
#include "Quaternion.h"
#include "Transform.h"
#include "../Core/Input.h"
#include "imgui.h" // io

#if _DEBUG
#include "Libs/VQUtils/Source/Log.h"
#endif

#define CAMERA_DEBUG 1

using namespace DirectX;

Camera::Camera()
	: mPitch(0.0f)
	, mYaw(0.0f)
	, mPosition(0,0,0)
	, mVelocity(0,0,0)
	, mProjParams({})
	, mControllerIndex(0)
{
	XMStoreFloat4x4(&mMatProj, XMMatrixIdentity());
	XMStoreFloat4x4(&mMatView, XMMatrixIdentity());
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
	c.mControllerIndex = this->mControllerIndex;
	for (size_t i = 0; i < this->mpControllers.size(); ++i)
		c.mpControllers.push_back(std::move(this->mpControllers[i]->Clone(&c)));
	return c;
}


void Camera::InitializeCamera(const FCameraParameters& data)
{
	this->mProjParams = data.ProjectionParams;
	this->mProjParams.FieldOfView *= DEG2RAD; // convert FoV to radians
	this->mYaw = this->mPitch = 0; // set with Rotate() below
	

	SetProjectionMatrix(this->mProjParams);
	SetPosition(data.x, data.y, data.z);
	Rotate(data.Yaw * DEG2RAD, data.Pitch * DEG2RAD);
	UpdateViewMatrix();
}

void Camera::InitializeController(const FCameraParameters& data)
{
	mControllerIndex = 0;
	
	// initialize in ECameraControllerType order ----
	mpControllers.emplace_back(new OrbitController(this));
	mpControllers.emplace_back(new FirstPersonController(this, data.TranslationSpeed, data.AngularSpeed, data.Drag));
	// initialize in ECameraControllerType order ----
}

void Camera::SetProjectionMatrix(const FProjectionMatrixParameters& params)
{
	assert(params.ViewportHeight > 0.0f);
	const float AspectRatio = params.ViewportWidth / params.ViewportHeight;

	mMatProj = params.bPerspectiveProjection
		? MakePerspectiveProjectionMatrix(params.FieldOfView, AspectRatio, params.NearZ, params.FarZ)
		: MakeOthographicProjectionMatrix(params.ViewportWidth, params.ViewportHeight, params.NearZ, params.FarZ);
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

void Camera::Update(float dt, const Input& input)
{
	assert(mControllerIndex < mpControllers.size());
	const bool bMouseDown = input.IsAnyMouseDown();
	const bool bMouseLeftDown  = input.IsMouseDown(Input::EMouseButtons::MOUSE_BUTTON_LEFT);
	const bool bMouseRightDown = input.IsMouseDown(Input::EMouseButtons::MOUSE_BUTTON_RIGHT);
	
	const ImGuiIO& io = ImGui::GetIO();

	if (bMouseLeftDown)  mControllerIndex = static_cast<size_t>(ECameraControllerType::ORBIT);
	if (bMouseRightDown) mControllerIndex = static_cast<size_t>(ECameraControllerType::FIRST_PERSON);
	
	const bool bMouseInputUsedByUI = io.MouseDownOwned[0] || io.MouseDownOwned[1];
	const bool bUseInput = (bMouseLeftDown || bMouseRightDown) && !bMouseInputUsedByUI;
	mpControllers[mControllerIndex]->UpdateCamera(input, dt, bUseInput);
}

XMFLOAT3 Camera::GetPositionF() const { return mPosition; }
XMMATRIX Camera::GetViewMatrix() const{ return XMLoadFloat4x4(&mMatView); }
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
XMMATRIX Camera::GetProjectionMatrix() const { return  XMLoadFloat4x4(&mMatProj); }
XMMATRIX Camera::GetRotationMatrix() const   { return XMMatrixRotationRollPitchYaw(mPitch, mYaw, 0.0f); }

void Camera::Rotate(float yaw, float pitch)
{
	mYaw   += yaw;
	mPitch += pitch;
	
	if (mPitch > +90.0f * DEG2RAD) mPitch = +90.0f * DEG2RAD;
	if (mPitch < -90.0f * DEG2RAD) mPitch = -90.0f * DEG2RAD;
	if (mYaw > ( XM_PI*2.0f)) mYaw -= (XM_PI * 2.0f);
	if (mYaw < (-XM_PI*2.0f)) mYaw += (XM_PI * 2.0f);
}

void Camera::LookAt(const XMVECTOR& target)
{
	// figure out target direction we want to look at
	const XMFLOAT3 posF3 = this->GetPositionF();
	XMVECTOR pos = XMLoadFloat3(&posF3);
	XMVECTOR VTargetDirection = XMVector3Normalize(target - pos);

	// figure out camera's direction
	XMFLOAT3 dirF3(0, 0, 1);
	XMVECTOR VSourceDir = XMLoadFloat3(&dirF3);

	// YAW
	//
	// figure out the quadrant of the yaw rotation angle
	XMVECTOR VTargetDirectionXZ = VTargetDirection;
	VTargetDirectionXZ.m128_f32[1] = 0.0f;
	VTargetDirectionXZ = XMVector3Normalize(VTargetDirectionXZ);
	float f = std::atan2(VTargetDirectionXZ.m128_f32[2], VTargetDirectionXZ.m128_f32[0]);
	f += PI_DIV2; if (f > PI) f -= 2*PI; // phase off: correct for X->Z rotation

	XMVECTOR dotY    = XMVector3Dot(VTargetDirectionXZ, VSourceDir);
	float yawRadians = std::acos(dotY.m128_f32[0]);

	// correct yaw rotation phase: right after 180 degs
	bool bQuadrandIIIorIV = std::signbit(f);
	if(bQuadrandIIIorIV) 
		yawRadians = PI * 2 - yawRadians;

	mYaw = yawRadians;

	// ------------------------------------------------------------------------

	// PITCH
	//
	XMVECTOR dotX = XMVector3Dot(VTargetDirection, VTargetDirectionXZ);
	bool bPositivePitch = std::signbit(VTargetDirection.m128_f32[1]);
	float pitchRadians = std::acos(min(dotX.m128_f32[0], 1.0f));
	assert(!std::isnan(pitchRadians));
	if (!bPositivePitch)
		pitchRadians *= -1.0f;

	if (pitchRadians > PI_DIV2)
		pitchRadians -= PI;
	if (pitchRadians < -PI_DIV2)
		pitchRadians += PI;

	mPitch = pitchRadians;

#if _DEBUG && 0
	Log::Warning("LookAt(): Yaw=%.2f Pitch=%.2f | DotY=%.2f DotX=%.2f | bQuadrandIIIorIV=%d"
		, yawRadians * RAD2DEG
		, pitchRadians * RAD2DEG
		, dotY.m128_f32[0]
		, dotX.m128_f32[0], bQuadrandIIIorIV);
#endif

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

OrbitController::OrbitController(Camera* pCam)
	: CameraController(pCam)
{
}

void OrbitController::UpdateCamera(const Input& input, float dt, bool bUseInput)
{
	const XMVECTOR vZERO = XMVectorZero();
	const XMFLOAT3 f3ZERO = XMFLOAT3(0, 0, 0);
	const XMVECTOR vROTATION_AXIS_AZIMUTH = XMLoadFloat3(&UpVector);
	      XMVECTOR vROTATION_AXIS_POLAR   = XMLoadFloat3(&LeftVector);
	Camera* pCam = this->mpCamera;

	// generate the FCameraInput data
	FCameraInput camInput(vZERO);
	camInput.DeltaMouseXY = input.GetMouseDelta();

	// build camera transform
	Transform tf(pCam->mPosition);
	tf.RotateAroundAxisDegrees(UpVector   , pCam->GetYaw());
	tf.RotateAroundAxisDegrees(RightVector, pCam->GetPitch());

	// calculate camera orbit movement & update the camera
	if(bUseInput)
	{
		XMVECTOR vPOSITION = XMLoadFloat3(&pCam->mPosition);

		// calculate camera directions
		const XMMATRIX mROT_CAM = pCam->GetRotationMatrix();
		const XMMATRIX mROT_CAM_Y_ONLY = XMMatrixRotationRollPitchYaw(0, pCam->GetYaw(), 0);
		constexpr float fLOOK_AT_DISTANCE = 5.0f; // currently unused - todo: instead of fixing on (0,0,0)
		XMVECTOR vLOOK_DIRECTION = XMLoadFloat3(&ForwardVector);
		vLOOK_DIRECTION          = XMVector3TransformCoord(vLOOK_DIRECTION, mROT_CAM);
		vROTATION_AXIS_POLAR     = XMVector3TransformCoord(vROTATION_AXIS_POLAR, mROT_CAM_Y_ONLY);
		XMVECTOR vLOOK_AT_POSITION = vPOSITION + vLOOK_DIRECTION * fLOOK_AT_DISTANCE;

		// do orbit rotation around the look target point
		constexpr float fROTATION_SPEED = 10.0f; // todo: drive by some config?
		const float fRotAngleAzimuth = camInput.DeltaMouseXY[0] * fROTATION_SPEED * dt * DEG2RAD;
		const float fRotAnglePolar   = camInput.DeltaMouseXY[1] * fROTATION_SPEED * dt * DEG2RAD;
		const bool bRotate = camInput.DeltaMouseXY[0] != 0.0f || camInput.DeltaMouseXY[1] != 0.0f;
		vLOOK_AT_POSITION = vZERO; // look at the origin
		tf.RotateAroundPointAndAxis(vROTATION_AXIS_AZIMUTH, fRotAngleAzimuth, vLOOK_AT_POSITION);
		tf.RotateAroundPointAndAxis(vROTATION_AXIS_POLAR  , -fRotAnglePolar, vLOOK_AT_POSITION);

		// translate camera based on mouse wheel input
		constexpr float CAMERA_MOVEMENT_SPEED_MULTIPLER = 5.0f;
		constexpr float CAMERA_MOVEMENT_SPEED_SHIFT_MULTIPLER = 4.0f;
		XMVECTOR LocalSpaceTranslation = XMVectorSet(0, 0, 0, 0);
		if (input.IsMouseScrollUp()  ) LocalSpaceTranslation += XMLoadFloat3(&ForwardVector);
		if (input.IsMouseScrollDown()) LocalSpaceTranslation += XMLoadFloat3(&BackVector);
		if (input.IsKeyDown(VK_SHIFT)) LocalSpaceTranslation *= CAMERA_MOVEMENT_SPEED_SHIFT_MULTIPLER;
		LocalSpaceTranslation *= CAMERA_MOVEMENT_SPEED_MULTIPLER;

		const XMVECTOR WorldSpaceTranslation = XMVector3TransformCoord(LocalSpaceTranslation, mROT_CAM);
		vPOSITION = XMLoadFloat3(&tf._position);
		vPOSITION += WorldSpaceTranslation;
		XMStoreFloat3(&tf._position, vPOSITION);

		// update the camera
		pCam->mPosition = tf._position;
		if (bRotate)
		{
			pCam->LookAt(vLOOK_AT_POSITION);
		}
		pCam->UpdateViewMatrix();
	}
}

CameraController* OrbitController::Clone_impl(Camera* pNewCam)
{
	OrbitController* p = new OrbitController(pNewCam); 
	p->mF3LookAt = this->mF3LookAt;
	return p;
}

//--------------------------------------------------------------------------------------------------------------------

FirstPersonController::FirstPersonController(Camera* pCam
	, float moveSpeed    /*= 1000.0f*/
	, float angularSpeed /*= 0.05f	*/
	, float drag         /*= 9.5f	*/
)
	: CameraController(pCam)
	, MoveSpeed(moveSpeed)
	, AngularSpeedDeg(angularSpeed)
	, Drag(drag)
{}

void FirstPersonController::UpdateCamera(const Input& input, float dt, bool bUseInput)
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
	if(bUseInput)
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
