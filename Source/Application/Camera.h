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
#include <memory>

class Camera;
class Input;



// ---------------------------------------------------------
// DATA
// ---------------------------------------------------------
struct FProjectionMatrixParameters
{
	float ViewporWidth;  // needed for orthographic projection
	float ViewporHeight; // needed for orthographic projection
	float NearZ;
	float FarZ;
	float FieldOfView;
	bool bPerspectiveProjection;
};
#if 0  // TODO: remove duplication
struct FCameraParameters
{
	float x, y, z; // position
	float Yaw, Pitch; // in degrees

	FProjectionMatrixParameters ProjectionParams;

	bool FirstPerson; // First Person / orbit
	float TranslationSpeed;
	float AngularSpeed;
	float Drag;
};
#else
struct FCameraParameters
{
	float x, y, z; // position
	float Yaw, Pitch; // in degrees
	
	bool bPerspectiveProjection; // perspective / orthographic
	float Width;
	float Height;
	float NearPlane;
	float FarPlane;
	float FovV_Degrees;

	bool bInitializeCameraController;
	bool bFirstPerson; // First Person / orbit
	float TranslationSpeed;
	float AngularSpeed;
	float Drag;
};
#endif

struct FCameraInput
{
	FCameraInput() = delete;
	FCameraInput(const DirectX::XMVECTOR& v) : LocalTranslationVector(v), DeltaMouseXY{ 0, 0 } {}

	const DirectX::XMVECTOR& LocalTranslationVector;
	std::array<float, 2> DeltaMouseXY;
};



// ---------------------------------------------------------
// CAMERA CONTROLLERS
// ---------------------------------------------------------
class CameraController
{
public:
	virtual void UpdateCamera(const Input& input, float dt) = 0;
	inline std::unique_ptr<CameraController> Clone(Camera* pNewCam) { return std::unique_ptr<CameraController>(Clone_impl(pNewCam)); }
protected:
	virtual CameraController* Clone_impl(Camera* pNewCam) = 0;

	CameraController() = delete;
	CameraController(Camera* pCamera) : mpCamera(pCamera) {}

protected:
	Camera* mpCamera = nullptr;
};
class FirstPersonController : public CameraController
{
public: 
	FirstPersonController(Camera* pCam);// : CameraController(pCam)
	void UpdateCamera(const Input& input, float dt) override;
protected:
	CameraController* Clone_impl(Camera* pNewCam) override;
private:
	float Drag = 9.5f;
	float AngularSpeedDeg = 0.05f;
	float MoveSpeed = 1000.0f;
};
class OrbitController : public CameraController
{
public:
	OrbitController(Camera* pCam);
	void UpdateCamera(const Input& input, float dt) override;
protected:
	CameraController* Clone_impl(Camera* pNewCam) override;
private:
	DirectX::XMFLOAT3 mF3LookAt;
};



// ---------------------------------------------------------
// CAMERA
// ---------------------------------------------------------
class Camera
{
	friend class OrbitController;
	friend class FirstPersonController;
public:
	Camera();
	~Camera(void);
	Camera(Camera&& other);
	//Camera(const Camera& other);
	Camera Clone();

	void InitializeCamera(const FCameraParameters& data);
	void InitializeController(bool bFirstPersonController);

	void SetProjectionMatrix(const FProjectionMatrixParameters& params);

	void UpdateViewMatrix();
	inline void Update(float dt, const Input& input) { if(pController) pController->UpdateCamera(input, dt); }

	DirectX::XMFLOAT3 GetPositionF() const;
	DirectX::XMMATRIX GetViewMatrix() const;
	DirectX::XMMATRIX GetViewInverseMatrix() const;
	DirectX::XMMATRIX GetProjectionMatrix() const;
	DirectX::XMMATRIX GetRotationMatrix() const;
	inline const FProjectionMatrixParameters& GetProjectionParameters() const { return mProjParams; }
	inline       FProjectionMatrixParameters& GetProjectionParameters()       { return mProjParams; }

	// returns World Space frustum plane set 
	FFrustumPlaneset GetViewFrustumPlanes() const;
	
	void SetPosition(float x, float y, float z);
	void Rotate(float yaw, float pitch);

private:
	//--------------------------
	DirectX::XMFLOAT3 mPosition;
	float mYaw = 0.0f;
	//--------------------------
	DirectX::XMFLOAT3 mVelocity;
	float mPitch = 0.0f;
	// -------------------------
	FProjectionMatrixParameters mProjParams;
	// -------------------------
	DirectX::XMFLOAT4X4 mMatProj;
	// -------------------------
	DirectX::XMFLOAT4X4 mMatView;
	// -------------------------
	std::unique_ptr<CameraController> pController;
};
