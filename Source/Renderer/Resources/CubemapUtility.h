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

#include "stdafx.h"

namespace CubemapUtility
{
	// cube face order: https://msdn.microsoft.com/en-us/library/windows/desktop/ff476906(v=vs.85).aspx
	//------------------------------------------------------------------------------------------------------
	// 0: RIGHT		1: LEFT
	// 2: UP		3: DOWN
	// 4: FRONT		5: BACK
	//------------------------------------------------------------------------------------------------------
	enum ECubeMapLookDirections
	{
		CUBEMAP_LOOK_RIGHT = 0,
		CUBEMAP_LOOK_LEFT,
		CUBEMAP_LOOK_UP,
		CUBEMAP_LOOK_DOWN,
		CUBEMAP_LOOK_FRONT,
		CUBEMAP_LOOK_BACK,

		NUM_CUBEMAP_LOOK_DIRECTIONS
	};

	DirectX::XMMATRIX CalculateViewMatrix(ECubeMapLookDirections cubeFace, const DirectX::XMFLOAT3& position = DirectX::XMFLOAT3(0, 0, 0));
	inline DirectX::XMMATRIX CalculateViewMatrix(int face, const DirectX::XMFLOAT3& position = DirectX::XMFLOAT3(0, 0, 0)) { return CalculateViewMatrix(static_cast<ECubeMapLookDirections>(face), position); }
};