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

// ------------------------------------------------------------------------------------
// SINGLE DRAW COMMANDS
// ------------------------------------------------------------------------------------
struct FMeshRenderDataBase
{
	const Mesh* pMesh = nullptr;
	DirectX::XMMATRIX matWorldTransformation;
	DirectX::XMMATRIX matWorldTransformationPrev;
};
struct FMeshRenderData : public FMeshRenderDataBase
{
	MaterialID matID = INVALID_ID;
	DirectX::XMMATRIX matNormalTransformation; //ID ?
	
	// debug data
	std::string ModelName;
	std::string MaterialName;
};
struct FShadowMeshRenderData : public FMeshRenderDataBase
{
	DirectX::XMMATRIX matWorldViewProj;
	MaterialID matID = INVALID_ID;
};
struct FWireframeRenderData : public FMeshRenderDataBase
{
	DirectX::XMFLOAT4 color;
};
struct FOutlineRenderData
{
	const Mesh* pMesh = nullptr;
	MaterialID matID = INVALID_ID;
	const Material* pMaterial = nullptr;
	struct FConstantBuffer { 
		DirectX::XMMATRIX matWorld;
		DirectX::XMMATRIX matWorldView;
		DirectX::XMMATRIX matNormalView;
		DirectX::XMMATRIX matProj;
		DirectX::XMMATRIX matViewInverse;
		DirectX::XMFLOAT4 color;
		DirectX::XMFLOAT4 uvScaleBias;
		float scale;
		float heightDisplacement;
	} cb;
};
using FLightRenderData = FWireframeRenderData;
using FBoundingBoxRenderData = FWireframeRenderData;


// ------------------------------------------------------------------------------------
// INSTANCED DRAW COMMANDS
// ------------------------------------------------------------------------------------
struct FInstancedMeshRenderDataBase
{
	int numIndices = 0;
	std::pair<BufferID, BufferID> vertexIndexBuffer;
	MaterialID matID = INVALID_ID;
	const Material* pMaterial = nullptr;
	std::vector<DirectX::XMMATRIX> matWorldViewProj;
};
struct FInstancedMotionVectorMeshData
{
	std::vector<DirectX::XMMATRIX> matWorldViewProjPrev;
};
struct FInstancedMeshRenderData : public FInstancedMeshRenderDataBase, public FInstancedMotionVectorMeshData
{
	std::vector<DirectX::XMMATRIX> matNormal;
	std::vector<DirectX::XMMATRIX> matWorld;
	std::vector<int> objectID;
	std::vector<float> projectedArea;
};
struct FInstancedWireframeRenderData : public FInstancedMeshRenderDataBase
{
	// single color for all instances, ideally we could make the color an instance data
	DirectX::XMFLOAT4 color; 
};
struct FInstancedShadowMeshRenderData : public FInstancedMeshRenderDataBase
{
	std::vector<DirectX::XMMATRIX> matWorld;
};

using FInstancedBoundingBoxRenderData = FInstancedWireframeRenderData;