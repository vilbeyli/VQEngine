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


#define DEG2RAD (DirectX::XM_PI / 180.0f)
#define RAD2DEG (180.0f / DirectX::XM_PI)
constexpr float PI_DIV2 = DirectX::XM_PIDIV2;
constexpr float PI      = DirectX::XM_PI;

constexpr DirectX::XMFLOAT3 UpVector      = DirectX::XMFLOAT3( 0,  1,  0);
constexpr DirectX::XMFLOAT3 RightVector   = DirectX::XMFLOAT3( 1,  0,  0);
constexpr DirectX::XMFLOAT3 ForwardVector = DirectX::XMFLOAT3( 0,  0,  1);
constexpr DirectX::XMFLOAT3 LeftVector    = DirectX::XMFLOAT3(-1,  0,  0);
constexpr DirectX::XMFLOAT3 BackVector    = DirectX::XMFLOAT3( 0,  0, -1);
constexpr DirectX::XMFLOAT3 DownVector    = DirectX::XMFLOAT3( 0, -1,  0);

constexpr DirectX::XMFLOAT3 XAxis = DirectX::XMFLOAT3(1, 0, 0);
constexpr DirectX::XMFLOAT3 YAxis = DirectX::XMFLOAT3(0, 1, 0);
constexpr DirectX::XMFLOAT3 ZAxis = DirectX::XMFLOAT3(0, 0, 1);

DirectX::XMFLOAT4X4 MakeOthographicProjectionMatrix(float screenWidth, float screenHeight, float screenNear, float screenFar);
DirectX::XMFLOAT4X4 MakePerspectiveProjectionMatrix(float fovy, float screenAspect, float screenNear, float screenFar);
DirectX::XMFLOAT4X4 MakePerspectiveProjectionMatrixHFov(float fovx, float screenAspectInverse, float screenNear, float screenFar);
