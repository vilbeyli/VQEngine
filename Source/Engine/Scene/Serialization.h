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

#include "Camera.h"
#include "Light.h"
#include "Transform.h"
#include <string>
#include <vector>

#define MATERIAL_UNINITIALIZED_VALUE -1.0f
struct FMaterialRepresentation
{
	std::string Name;
	DirectX::XMFLOAT3 DiffuseColor;
	float Alpha = MATERIAL_UNINITIALIZED_VALUE;
	DirectX::XMFLOAT3 EmissiveColor;
	float EmissiveIntensity = MATERIAL_UNINITIALIZED_VALUE;
	float Metalness = MATERIAL_UNINITIALIZED_VALUE;
	float Roughness = MATERIAL_UNINITIALIZED_VALUE;
	std::string DiffuseMapFilePath;
	std::string NormalMapFilePath;
	std::string EmissiveMapFilePath;
	std::string AlphaMaskMapFilePath;
	std::string MetallicMapFilePath;
	std::string RoughnessMapFilePath;
	std::string AOMapFilePath;

	FMaterialRepresentation();
};
struct FGameObjectRepresentation
{
	Transform tf;

	std::string ModelName;
	std::string ModelFilePath;

	std::string BuiltinMeshName;
	std::string MaterialName;
};
struct FSceneRepresentation
{
	std::string SceneName;
	std::string EnvironmentMapPreset;

	std::vector<FMaterialRepresentation>   Materials;
	std::vector<FCameraParameters>         Cameras;
	std::vector<FGameObjectRepresentation> Objects;
	std::vector<Light>                     Lights;

	char loadSuccess = 0;
};