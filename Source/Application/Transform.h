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

#include "Quaternion.h"
#include "Math.h"

#include <utility>


struct Transform
{
public:
	//----------------------------------------------------------------------------------------------------------------
	// CONSTRUCTOR / DESTRUCTOR
	//----------------------------------------------------------------------------------------------------------------
	Transform(  const DirectX::XMFLOAT3& position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
	            const Quaternion&        rotation = Quaternion::Identity(),
	            const DirectX::XMFLOAT3& scale    = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
	~Transform();
	Transform& operator=(const Transform&);

	//----------------------------------------------------------------------------------------------------------------
	// GETTERS & SETTERS
	//----------------------------------------------------------------------------------------------------------------
	inline void SetXRotationDeg(float xDeg)              { _rotation = Quaternion::FromAxisAngle(RightVector  , xDeg * DEG2RAD); }
	inline void SetYRotationDeg(float yDeg)              { _rotation = Quaternion::FromAxisAngle(UpVector     , yDeg * DEG2RAD); }
	inline void SetZRotationDeg(float zDeg)              { _rotation = Quaternion::FromAxisAngle(ForwardVector, zDeg * DEG2RAD); }
	inline void SetScale(float x, float y, float z)      { _scale = DirectX::XMFLOAT3(x, y, z); }
	inline void SetScale(const DirectX::XMFLOAT3& scl)   { _scale = scl; }
	inline void SetScale(const DirectX::XMVECTOR& scl)   { XMStoreFloat3(&_scale, scl); }
	inline void SetUniformScale(float s)                 { _scale = DirectX::XMFLOAT3(s, s, s); }
	inline void SetPosition(float x, float y, float z)   { _position = DirectX::XMFLOAT3(x, y, z); }
	inline void SetPosition(const DirectX::XMFLOAT3& pos){ _position = pos; }

	//----------------------------------------------------------------------------------------------------------------
	// TRANSFORMATIONS
	//----------------------------------------------------------------------------------------------------------------
	void Translate(const DirectX::XMFLOAT3& translation);
	void Translate(float x, float y, float z);
	void Scale    (const DirectX::XMFLOAT3& scl);
	
	       void RotateAroundPointAndAxis(const DirectX::XMVECTOR& axis, float angle, const DirectX::XMVECTOR& point);
	inline void RotateAroundAxisRadians (const DirectX::XMVECTOR& axis, float angle) { RotateInWorldSpace(Quaternion::FromAxisAngle(axis, angle)); }
	inline void RotateAroundAxisDegrees(const DirectX::XMVECTOR& axis, float angle) { RotateInWorldSpace(Quaternion::FromAxisAngle(axis, angle * DEG2RAD)); }
	inline void RotateAroundAxisRadians(const DirectX::XMFLOAT3& axis, float angle) { DirectX::XMVECTOR Axis = XMLoadFloat3(&axis); RotateInWorldSpace(Quaternion::FromAxisAngle(axis, angle)); }
	inline void RotateAroundAxisDegrees(const DirectX::XMFLOAT3& axis, float angle) { DirectX::XMVECTOR Axis = XMLoadFloat3(&axis); RotateInWorldSpace(Quaternion::FromAxisAngle(axis, angle * DEG2RAD)); }

	inline void RotateAroundLocalXAxisDegrees(float angle)  { RotateInLocalSpace(Quaternion::FromAxisAngle(XAxis, std::forward<float>(angle * DEG2RAD))); }
	inline void RotateAroundLocalYAxisDegrees(float angle)  { RotateInLocalSpace(Quaternion::FromAxisAngle(YAxis, std::forward<float>(angle * DEG2RAD))); }
	inline void RotateAroundLocalZAxisDegrees(float angle)  { RotateInLocalSpace(Quaternion::FromAxisAngle(ZAxis, std::forward<float>(angle * DEG2RAD))); }
	inline void RotateAroundGlobalXAxisDegrees(float angle) { RotateAroundAxisDegrees(XAxis, std::forward<float>(angle)); }
	inline void RotateAroundGlobalYAxisDegrees(float angle) { RotateAroundAxisDegrees(YAxis, std::forward<float>(angle)); }
	inline void RotateAroundGlobalZAxisDegrees(float angle) { RotateAroundAxisDegrees(ZAxis, std::forward<float>(angle)); }

	inline void RotateInWorldSpace(const Quaternion& q) { _rotation = q * _rotation; }
	inline void RotateInLocalSpace(const Quaternion& q) { _rotation = _rotation * q; }

	inline void ResetPosition() { _position = DirectX::XMFLOAT3(0, 0, 0); }
	inline void ResetRotation() { _rotation = Quaternion::Identity(); }
	inline void ResetScale() { _scale = DirectX::XMFLOAT3(1, 1, 1); }
	inline void Reset() { ResetScale(); ResetRotation(); ResetPosition(); }
	
	DirectX::XMMATRIX WorldTransformationMatrix() const;
	DirectX::XMMATRIX WorldTransformationMatrix_NoScale() const;
	DirectX::XMMATRIX RotationMatrix() const;

	static DirectX::XMMATRIX NormalMatrix(const DirectX::XMMATRIX& world);

	//----------------------------------------------------------------------------------------------------------------
	// DATA
	//----------------------------------------------------------------------------------------------------------------
	DirectX::XMFLOAT3       _position;
	Quaternion              _rotation;
	DirectX::XMFLOAT3       _scale;
};

