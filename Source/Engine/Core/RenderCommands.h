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

#include "Types.h"
#include <DirectXMath.h>
#include <string>

struct FMeshRenderCommandBase
{
	MeshID meshID = INVALID_ID;
	DirectX::XMMATRIX matWorldTransformation;
	DirectX::XMMATRIX matWorldTransformationPrev;
};
struct FMeshRenderCommand : public FMeshRenderCommandBase
{
	MaterialID matID = INVALID_ID;
	DirectX::XMMATRIX matNormalTransformation; //ID ?
	std::string ModelName;
	std::string MaterialName;
};
struct FShadowMeshRenderCommand : public FMeshRenderCommandBase
{
	DirectX::XMMATRIX matWorldViewProj;
	MaterialID matID = INVALID_ID;
	std::string ModelName;
};
struct FWireframeRenderCommand : public FMeshRenderCommandBase
{
	DirectX::XMFLOAT3 color;
};
using FLightRenderCommand = FWireframeRenderCommand;
using FBoundingBoxRenderCommand = FWireframeRenderCommand;