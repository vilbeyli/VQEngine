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

#include "../Core/Types.h"
#include "Transform.h"
#include "Shaders/TerrainConstantBufferData.h"

struct Terrain
{
	MeshID MeshId = INVALID_ID;
	MaterialID MaterialId = INVALID_ID;
	Transform RootTransform; // XZ plane

	// TODO: Tessellation params
	float QuadInner[2];
	float QuadOuter[4];

	float TriOuter[3];
	float TriInner;

	// TODO: bounding box
};



class Camera;
VQ_SHADER_DATA::TerrainParams GetCBuffer_TerrainParams(const Terrain& TerrainIn, const Camera& CameraIn);
VQ_SHADER_DATA::TerrainTessellationParams GetCBuffer_TerrainTessellationParams(const Terrain& TerrainIn);

