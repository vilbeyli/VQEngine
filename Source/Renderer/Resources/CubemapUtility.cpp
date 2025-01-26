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

#include "CubemapUtility.h"
#include "Engine/Math.h"

DirectX::XMMATRIX CubemapUtility::CalculateViewMatrix(CubemapUtility::ECubeMapLookDirections cubeFace, const DirectX::XMFLOAT3& position)
{
    using namespace DirectX;
    const XMVECTOR pos = XMLoadFloat3(&position);

    static XMVECTOR VEC3_UP      = XMLoadFloat3(&UpVector);
    static XMVECTOR VEC3_DOWN    = XMLoadFloat3(&DownVector);
    static XMVECTOR VEC3_LEFT    = XMLoadFloat3(&LeftVector);
    static XMVECTOR VEC3_RIGHT   = XMLoadFloat3(&RightVector);
    static XMVECTOR VEC3_FORWARD = XMLoadFloat3(&ForwardVector);
    static XMVECTOR VEC3_BACK    = XMLoadFloat3(&BackVector);

    // cube face order: https://msdn.microsoft.com/en-us/library/windows/desktop/ff476906(v=vs.85).aspx
    //------------------------------------------------------------------------------------------------------
    // 0: RIGHT		1: LEFT
    // 2: UP		3: DOWN
    // 4: FRONT		5: BACK
    //------------------------------------------------------------------------------------------------------
    switch (cubeFace)
    {
    case CubemapUtility::CUBEMAP_LOOK_RIGHT: return XMMatrixLookAtLH(pos, pos + VEC3_RIGHT  , VEC3_UP);
    case CubemapUtility::CUBEMAP_LOOK_LEFT:	 return XMMatrixLookAtLH(pos, pos + VEC3_LEFT   , VEC3_UP);
    case CubemapUtility::CUBEMAP_LOOK_UP:    return XMMatrixLookAtLH(pos, pos + VEC3_UP     , VEC3_BACK);
    case CubemapUtility::CUBEMAP_LOOK_DOWN:	 return XMMatrixLookAtLH(pos, pos + VEC3_DOWN   , VEC3_FORWARD);
    case CubemapUtility::CUBEMAP_LOOK_FRONT: return XMMatrixLookAtLH(pos, pos + VEC3_FORWARD, VEC3_UP);
    case CubemapUtility::CUBEMAP_LOOK_BACK:  return XMMatrixLookAtLH(pos, pos + VEC3_BACK   , VEC3_UP);
    default: return XMMatrixIdentity();
    }
}