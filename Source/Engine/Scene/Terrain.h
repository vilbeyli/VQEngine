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
	TextureID HeightMap = INVALID_ID;
	TextureID NormalMap = INVALID_ID;
	TextureID RoughnessMap = INVALID_ID;
	TextureID DiffuseMap0 = INVALID_ID;
	TextureID DiffuseMap1 = INVALID_ID;
	TextureID DiffuseMap2 = INVALID_ID;
	TextureID DiffuseMap3 = INVALID_ID;

	SRV_ID SRVHeightMap = INVALID_ID;
	SRV_ID SRVNormalMap = INVALID_ID;
	SRV_ID SRVRoughnessMap = INVALID_ID;
	SRV_ID SRVDiffuseMap0 = INVALID_ID;
	SRV_ID SRVDiffuseMap1 = INVALID_ID;
	SRV_ID SRVDiffuseMap2 = INVALID_ID;
	SRV_ID SRVDiffuseMap3 = INVALID_ID;

	Transform RootTransform; // XZ plane
	float HeightmapScale = 0.0f;

	// TODO: Tessellation params
	float QuadInner[2];

	// TODO: bounding box
};

class Camera;
VQ_SHADER_DATA::TerrainParams GetCBuffer_TerrainParams(const Terrain& TerrainIn, const Camera& CameraIn);


