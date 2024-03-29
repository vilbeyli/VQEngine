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

#include "../Math.h"
#include "../Culling.h"

#include <array>
#include <memory>

class Camera;
class Input;

enum ECameraControllerType;

// ---------------------------------------------------------
// DATA
// ---------------------------------------------------------
struct FProjectionMatrixParameters
{
	float ViewportWidth;  // needed for orthographic projection
	float ViewportHeight; // needed for orthographic projection
	float NearZ;
	float FarZ;
	float FieldOfView;
	bool bPerspectiveProjection;
};
struct FCameraParameters
{
	float x, y, z;    // World Position
	float Yaw, Pitch; // Degrees

	FProjectionMatrixParameters ProjectionParams;

	bool bInitializeCameraController;

	ECameraControllerType ControllerType;
	float TranslationSpeed;
	float AngularSpeed;
	float Drag;
};

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
	virtual void UpdateCamera(const Input& input, float dt, bool bUseInput) = 0;
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
	FirstPersonController() = delete;
	FirstPersonController(Camera* pCam, float moveSpeed = 1000.0f, float angularSpeed = 0.05f, float drag = 9.5f);
	void UpdateCamera(const Input& input, float dt, bool bUseInput) override;
protected:
	CameraController* Clone_impl(Camera* pNewCam) override;
private:
	float Drag;
	float AngularSpeedDeg;
	float MoveSpeed;
};
class OrbitController : public CameraController
{
public:
	OrbitController(Camera* pCam);
	void UpdateCamera(const Input& input, float dt, bool bUseInput) override;
	inline void SetLookAtPosition(const DirectX::XMFLOAT3& f3Position) { mF3LookAt = f3Position; }
	inline void SetLookAtPosition(float x, float y, float z) { mF3LookAt = DirectX::XMFLOAT3(x,y,z); }
protected:
	CameraController* Clone_impl(Camera* pNewCam) override;
private:
	DirectX::XMFLOAT3 mF3LookAt;
};
enum ECameraControllerType
{
	FIRST_PERSON = 0,
	ORBIT,

	NUM_CAMERA_CONTROLLER_TYPES
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
	Camera Clone();

	void InitializeCamera(const FCameraParameters& data);
	void InitializeController(const FCameraParameters& data);

	void SetProjectionMatrix(const FProjectionMatrixParameters& params);

	void UpdateViewMatrix();
	void Update(float dt, const Input& input);

	inline float GetYaw() const { return mYaw; }
	inline float GetPitch() const { return mPitch; }
	DirectX::XMFLOAT3 GetPositionF() const;
	DirectX::XMMATRIX GetViewMatrix() const;
	DirectX::XMMATRIX GetViewInverseMatrix() const;
	DirectX::XMMATRIX GetProjectionMatrix() const;
	DirectX::XMMATRIX GetRotationMatrix() const;
	inline DirectX::XMMATRIX GetViewProjectionMatrix() const { return GetViewMatrix() * GetProjectionMatrix(); };
	inline const FProjectionMatrixParameters& GetProjectionParameters() const { return mProjParams; }
	inline       FProjectionMatrixParameters& GetProjectionParameters() { return mProjParams; }
	inline       FFrustumPlaneset GetViewFrustumPlanesInWorldSpace() const { return FFrustumPlaneset::ExtractFromMatrix(GetViewMatrix() * GetProjectionMatrix()); }
	
	ECameraControllerType GetControllerType() const { return static_cast<ECameraControllerType>(mControllerIndex); };
	void SetControllerType(ECameraControllerType c) { mControllerIndex = c; }
	void SetTargetPosition(const DirectX::XMFLOAT3& f3Position);

	inline void SetPosition(float x, float y, float z) { mPosition = DirectX::XMFLOAT3(x, y, z); }
	inline void SetPosition(const DirectX::XMFLOAT3& p){ mPosition = p; }
	       void Rotate(float yaw, float pitch);
	       void LookAt(const DirectX::XMVECTOR& point);
	inline void LookAt(const DirectX::XMFLOAT3& point) { DirectX::XMVECTOR p = XMLoadFloat3(&point); LookAt(p); }

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
	std::vector<std::unique_ptr<CameraController>> mpControllers;
	size_t mControllerIndex;
};
