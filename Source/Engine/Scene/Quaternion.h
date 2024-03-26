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

class Quaternion
{
public:
	static Quaternion Identity();
	static Quaternion FromAxisAngle(const DirectX::XMVECTOR& axis, const float angle);
	static Quaternion FromAxisAngle(const DirectX::XMFLOAT3& axis, const float angle);
	static Quaternion FromEulerDeg(const DirectX::XMFLOAT3& XYZRotations);
	static Quaternion FromEulerRad(const DirectX::XMFLOAT3& XYZRotations);
	static Quaternion Lerp(const Quaternion& from, const Quaternion& to, float t);
	static Quaternion Slerp(const Quaternion& from, const Quaternion& to, float t);
	static DirectX::XMFLOAT3 ToEulerRad(const Quaternion& Q);
	static DirectX::XMFLOAT3 ToEulerDeg(const Quaternion& Q);

	Quaternion(const DirectX::XMMATRIX& rotMatrix);
	Quaternion(float s, const DirectX::XMVECTOR& v);
	Quaternion(float s, const DirectX::XMFLOAT3& v) : S(s), V(v) {};

	Quaternion  operator+(const Quaternion& q) const;
	Quaternion  operator*(const Quaternion& q) const;
	Quaternion  operator*(float c) const;
	bool        operator==(const Quaternion& q) const;
	float       Dot(const Quaternion& q) const;
	float       Len() const;
	Quaternion  Inverse() const;
	Quaternion  Conjugate() const;
	DirectX::XMMATRIX Matrix() const;
	Quaternion& Normalize();

	DirectX::XMFLOAT3 TransformVector(const DirectX::XMFLOAT3& v) const;
	DirectX::XMVECTOR TransformVector(const DirectX::XMVECTOR& v) const;
private:	// used by operator()s
	Quaternion() = default;

public:
	// Q = [S, <V>]
	DirectX::XMFLOAT3 V;
	float S;
};
