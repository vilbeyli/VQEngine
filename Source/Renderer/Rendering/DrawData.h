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

#include <d3d12.h>

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
	MaterialID matID = INVALID_ID;
	std::pair<BufferID, BufferID> vertexIndexBuffer;
	Material material;
	std::vector<DirectX::XMMATRIX> matWorldViewProj;
};
struct FInstancedMotionVectorMeshData
{
	std::vector<DirectX::XMMATRIX> matWorldViewProjPrev;
};
struct FInstancedMeshRenderData : public FInstancedMotionVectorMeshData, public FInstancedMeshRenderDataBase
{
	std::vector<DirectX::XMMATRIX> matWorld;
	std::vector<DirectX::XMMATRIX> matNormal;
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

struct FInstancedDrawParameters
{
	// constant buffers
	D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {}; // cbAddr contains material data, transformations, object/mesh/material IDs
	D3D12_GPU_VIRTUAL_ADDRESS cbAddr_Tessellation = {}; // contains tessellation param cbuffer
	
	// material-texture params
	SRV_ID SRVMaterialMaps = INVALID_ID;
	SRV_ID SRVHeightMap = INVALID_ID;

	// IA params
	D3D_PRIMITIVE_TOPOLOGY IATopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	BufferID VB = INVALID_ID; // renderer later derefs these IDs
	BufferID IB = INVALID_ID; // renderer later derefs these IDs

	// draw params
	uint numInstances = 0;
	uint numIndices = 0;

	// PSO-material configs
	//   Tessellation Config
	//     bit  0  : iTess
	//     bits 1-2: iDomain
	//     bits 3-4: iPartition
	//     bits 5-6: iOutputTopology
	//     bit  7  : iTessellationSWCull
	uint8 PackedTessellationConfig = 0;
	inline void PackTessellationConfig(bool bEnable, ETessellationDomain d, ETessellationPartitioning p, ETessellationOutputTopology t, bool bFaceCullOrFrustumCull)
	{
		PackedTessellationConfig = (bEnable ? 1 : 0) |
			((uint8)d) << 1 |
			((uint8)p) << 3 |
			((uint8)t) << 5 |
			((bFaceCullOrFrustumCull ? 1 : 0) << 7);
	}
	inline void UnpackTessellationConfig(size_t& iTess, size_t& iDomain, size_t& iPartition, size_t& iOutputTopology, size_t& iTessellationSWCull) const
	{
		iTess = PackedTessellationConfig & 0x1;
		iDomain = (PackedTessellationConfig >> 1) & 0x3;
		iPartition = (PackedTessellationConfig >> 3) & 0x3;
		iOutputTopology = (PackedTessellationConfig >> 5) & 0x3;
		iTessellationSWCull = (PackedTessellationConfig >> 7) & 0x1;
	}
		
	//   Material Config
	//     bit  0  : iAlpha
	//     bit  1  : iRaster
	//     bits 2-3: iFaceCull
	//     bit  4-7: <unused>
	uint8 PackedMaterialConfig;
	inline void PackMaterialConfig(bool bAlphaMasked, bool bWireFrame, uint8 iFaceCull)
	{
		PackedMaterialConfig = (bAlphaMasked ? 1 : 0) |
			(bWireFrame ? 1 : 0) << 1 |
			(iFaceCull & 0x3) << 2;
	}
	inline void UnpackMaterialConfig(size_t& iAlpha, size_t& iRaster, size_t& iFaceCull) const
	{
		iAlpha = PackedMaterialConfig & 0x1;
		iRaster = (PackedMaterialConfig >> 1) & 0x1;
		iFaceCull = (PackedMaterialConfig >> 2) & 0x3;
	}
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

struct FSceneDrawData
{
	std::vector<FInstancedDrawParameters> mainViewDrawParams;
	std::vector<std::vector<FInstancedDrawParameters>> pointShadowDrawParams;
	std::vector<std::vector<FInstancedDrawParameters>> spotShadowDrawParams;
	std::vector<FInstancedDrawParameters> directionalShadowDrawParams;

	std::vector<FLightRenderData> lightRenderParams;
	std::vector<FLightRenderData> lightBoundsRenderParams;
	std::vector<FOutlineRenderData> outlineRenderParams;
	std::vector<MeshRenderData_t> debugVertexAxesRenderParams;
	std::vector<BoundingBoxRenderData_t> boundingBoxRenderParams;


	//--------------------------------------------------------------------------------------------------------------------------------------------
	// collect instance data based on Material, and then Mesh.
	// In order to avoid clear, we also keep track of the number 
	// of valid instance data.
	//--------------------------------------------------------------------------------------------------------------------------------------------
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
};

