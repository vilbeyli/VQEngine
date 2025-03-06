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

#include "Engine/Core/Types.h"

#include "Shaders/LightingConstantBufferData.h"
#include "Engine/Scene/Material.h"

#include <DirectXMath.h>
#include <string>
#include <vector>
#include <unordered_map>

struct Mesh;
struct Material;

// ------------------------------------------------------------------------------------
// NON-INSTANCED DRAW DATA
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

// ------------------------------------------------------------------------------------
// INSTANCED DRAW DATA
// ------------------------------------------------------------------------------------
struct FInstancedMeshRenderDataBase
{
	int numIndices = 0;
	std::pair<BufferID, BufferID> vertexIndexBuffer;
	MaterialID matID = INVALID_ID;
	Material material;
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


using FLightRenderData = FWireframeRenderData;
using FBoundingBoxRenderData = FWireframeRenderData;
using FInstancedBoundingBoxRenderData = FInstancedWireframeRenderData;

#if RENDER_INSTANCED_SCENE_MESHES
using MeshRenderData_t = FInstancedMeshRenderData;
#else
using MeshRenderData_t = FMeshRenderData;
#endif

#if RENDER_INSTANCED_BOUNDING_BOXES
using BoundingBoxRenderData_t = FInstancedBoundingBoxRenderData;
#else
using BoundingBoxRenderData_t = FBoundingBoxRenderData;
#endif

struct FInstanceDataWriteParam { int iDraw, iInst; };
struct FSceneDrawData
{
	std::vector<MeshRenderData_t> meshRenderParams;
	std::vector<FLightRenderData> lightRenderParams;
	std::vector<FLightRenderData> lightBoundsRenderParams;
	std::vector<FOutlineRenderData> outlineRenderParams;
	std::vector<MeshRenderData_t> debugVertexAxesRenderParams;
	std::vector<BoundingBoxRenderData_t> boundingBoxRenderParams;

#if RENDER_INSTANCED_SCENE_MESHES
	//--------------------------------------------------------------------------------------------------------------------------------------------
	// collect instance data based on Material, and then Mesh.
	// In order to avoid clear, we also keep track of the number 
	// of valid instance data.
	//--------------------------------------------------------------------------------------------------------------------------------------------
	struct FInstanceData { DirectX::XMMATRIX mWorld, mWorldViewProj, mWorldViewProjPrev, mNormal; int mObjID; float mProjectedArea; }; // transformation matrixes used in the shader
	struct FMeshInstanceDataArray { size_t NumValidData = 0; std::vector<FInstanceData> Data; };
	// MAT0
	// +---- PSO0                       MAT1       
	//     +----MESH0                 +----MESH37             
	//         +----LOD0                  +----LOD0                
	//             +----InstData0             +----InstData0                        
	//             +----InstData1             +----InstData1                        
	//          +----LOD1                     +----InstData2                
	//             +----InstData0      +----MESH225                        
	//             +----InstData1          +----LOD0                        
	// +---- PSO1
	//     +----MESH1                        +----InstData0             
	//         +----LOD0                     +----InstData1                
	//            +----InstData0          +----LOD1                        
	//            +----InstData1             +----InstData0                        
	//            +----InstData2             +----InstData1                        
	//     +----MESH2                              
	//         +----LOD0
	//            +----InstData0
	//

	// Bits[0  -  3] : LOD
	// Bits[4  - 33] : MeshID
	// Bits[34 - 34] : IsAlphaMasked (or opaque)
	// Bits[35 - 35] : IsTessellated
	static inline uint64 GetKey(MaterialID matID, MeshID meshID, int lod, /*UNUSED*/bool bTessellated)
	{
		assert(matID != -1);
		assert(meshID != -1);
		assert(lod >= 0 && lod < 16);
		constexpr int mask = 0x3FFFFFFF; // __11 1111 1111 1111 ... | use the first 30 bits of IDs
		uint64 hash = std::max(0, std::min(1 << 4, lod));
		hash |= ((uint64)(meshID & mask)) << 4;
		hash |= ((uint64)(matID & mask)) << 34;
		return hash;
	}
	static inline MaterialID GetMatIDFromKey(uint64 key) { return MaterialID(key >> 34); }
	static inline MeshID     GetMeshIDFromKey(uint64 key) { return MeshID((key >> 4) & 0x3FFFFFFF); }
	static inline int        GetLODFromKey(uint64 key) { return int(key & 0xF); }
	//--------------------------------------------------------------------------------------------------------------------------------------------
#endif
};

