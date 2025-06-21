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


#include "Transform.h"

using namespace DirectX;

Transform::Transform(const XMFLOAT3& position, const Quaternion& rotation, const XMFLOAT3& scale)
	: _position(position)
	, _rotation(rotation)
	, _scale(scale)
	, _positionPrev(position)
{}

Transform::~Transform() {}

Transform & Transform::operator=(const Transform & t)
{
	this->_positionPrev = t._positionPrev;
	this->_position = t._position;
	this->_rotation = t._rotation;
	this->_scale    = t._scale;
	return *this;
}

void Transform::Translate(const XMFLOAT3& translation)
{
	_positionPrev = _position;
	XMVECTOR POSITION = XMLoadFloat3(&_position);
	XMVECTOR TRANSLATION = XMLoadFloat3(&translation);
	POSITION += TRANSLATION;
	XMStoreFloat3(&_position, POSITION);
}

void Transform::Translate(float x, float y, float z)
{
	XMFLOAT3 t = XMFLOAT3(x, y, z);
	_positionPrev = _position;
	XMVECTOR POSITION = XMLoadFloat3(&_position);
	XMVECTOR TRANSLATION = XMLoadFloat3(&t);
	POSITION += TRANSLATION;
	XMStoreFloat3(&_position, POSITION);
}

void Transform::Scale(const XMFLOAT3& scl)
{
	_scale = scl;
}

void Transform::RotateAroundPointAndAxis(const XMVECTOR& axis, float angle, const XMVECTOR& point)
{ 
	XMVECTOR R = XMLoadFloat3(&_position);
	R -= point; // R = _position - point;

	const Quaternion rot = Quaternion::FromAxisAngle(axis, angle);
	R = rot.TransformVector(R);
	R = point + R;
	XMStoreFloat3(&_position, R);
}

XMMATRIX Transform::matWorldTransformation() const
{
	XMVECTOR scale = XMLoadFloat3(&_scale);
	XMVECTOR translation = XMLoadFloat3(&_position);

	const Quaternion& Q = _rotation;
	XMVECTOR rotation = XMVectorSet(Q.V.x, Q.V.y, Q.V.z, Q.S);
	XMVECTOR rotOrigin = XMVectorZero();
	return XMMatrixAffineTransformation(scale, rotOrigin, rotation, translation);
}

DirectX::XMMATRIX Transform::matWorldTransformationPrev() const
{
	XMVECTOR scale = XMLoadFloat3(&_scale);
	XMVECTOR translation = XMLoadFloat3(&_positionPrev);

	const Quaternion& Q = _rotation;
	XMVECTOR rotation = XMVectorSet(Q.V.x, Q.V.y, Q.V.z, Q.S);
	XMVECTOR rotOrigin = XMVectorZero();
	return XMMatrixAffineTransformation(scale, rotOrigin, rotation, translation);
}

DirectX::XMMATRIX Transform::WorldTransformationMatrix_NoScale() const
{
	XMVECTOR scale = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
	XMVECTOR translation = XMLoadFloat3(&_position);
	const Quaternion& Q = _rotation;
	XMVECTOR rotation = XMVectorSet(Q.V.x, Q.V.y, Q.V.z, Q.S);
	XMVECTOR rotOrigin = XMVectorZero();
	return XMMatrixAffineTransformation(scale, rotOrigin, rotation, translation);
}

XMMATRIX Transform::RotationMatrix() const
{
	return _rotation.Matrix();
}

DirectX::XMMATRIX Transform::NormalMatrix(const XMMATRIX& world)
{
	XMMATRIX nrm = world;
	nrm.r[3].m128_f32[0] = nrm.r[3].m128_f32[1] = nrm.r[3].m128_f32[2] = 0;
	nrm.r[3].m128_f32[3] = 1; // undo translation
	XMVECTOR Det = XMMatrixDeterminant(nrm);
	nrm = XMMatrixInverse(&Det, nrm); // undo rotation+scale
	nrm = XMMatrixTranspose(nrm); // redo rotation
	return nrm;
}
