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
#include "../CullingData.h"
#include "MeshGeometryData.h"
#include "Renderer/Resources/Buffer.h"

#include <limits>
#include <cstdio>

struct FBufferDesc;
class VQRenderer;

enum EBuiltInMeshes
{
	TRIANGLE = 0,
	QUAD,
	CUBE,
	SPHERE,
	CYLINDER,
	CONE,
	GRID_SIMPLE_QUAD,
	GRID_DETAILED_QUAD0,
	GRID_DETAILED_QUAD1,
	GRID_DETAILED_QUAD2,

	TESSELLATION_CONTROL_POINTS__QUAD1,
	TESSELLATION_CONTROL_POINTS__QUAD4,
	TESSELLATION_CONTROL_POINTS__QUAD9,
	TESSELLATION_CONTROL_POINTS__QUAD16,
	TESSELLATION_CONTROL_POINTS__QUAD25,
	
	NUM_BUILTIN_MESHES
};

struct VertexIndexBufferIDPair
{
	BufferID  mVertexBufferID = -1;
	BufferID  mIndexBufferID  = -1;

	inline std::pair<BufferID, BufferID> GetIABufferPair() const { return std::make_pair(mVertexBufferID, mIndexBufferID); }
};

template<class TVertex, class TIndex = uint32>
struct MeshLODData
{
	MeshLODData< TVertex, TIndex >() = delete;
	MeshLODData< TVertex, TIndex >(int numLODs, const char* pMeshName)
		: LODVertices(numLODs)
		, LODIndices(numLODs)
		, meshName(pMeshName)
	{}
	
	std::vector<std::vector<TVertex>> LODVertices;
	std::vector<std::vector<TIndex>>  LODIndices ;
	std::string meshName;
};

// A Mesh is represented by a Vertex & Index buffer ID pair,
// where the buffers contain the local space vertex and connectivity data.
// Meshes can have multiple LOD levels, and a single local-space bounding box
struct Mesh
{
public:
	static EBuiltInMeshes GetBuiltInMeshType(const std::string& MeshTypeStr);

	// init
	template<class TVertex, class TIndex>
	Mesh(VQRenderer* pRenderer, GeometryData<TVertex, TIndex>&& meshLODData, const std::string& name);
	Mesh() = default;

	// Creates GPU buffers, needs renderer heaps initialized (used for deferred initialization)
	void CreateBuffers(VQRenderer* pRenderer);

	// getters
	std::pair<BufferID, BufferID> GetIABufferIDs(int lod = 0) const;
	inline uint GetNumIndices(int lod = 0) const { assert(mNumIndicesPerLODLevel.size()>lod); return mNumIndicesPerLODLevel[lod]; }
	inline uint GetNumLODs() const { return static_cast<uint>(mLODBufferPairs.size()); }
	const FBoundingBox GetLocalSpaceBoundingBox() const { return mLocalSpaceBoundingBox; }
	
private:
	std::vector<VertexIndexBufferIDPair> mLODBufferPairs;
	std::vector<uint> mNumIndicesPerLODLevel;
	FBoundingBox mLocalSpaceBoundingBox;

	struct GeometryDataStorage
	{
		std::vector<std::vector<char>> LODVertices; // Serialized vertex data
		std::vector<std::vector<char>> LODIndices;  // Serialized index data
		std::vector<uint> VertexStrides;
		std::vector<uint> IndexStrides;
		std::vector<unsigned> NumIndices;
		std::string Name;
		bool IsValid() const { return !LODVertices.empty(); }
	};
	GeometryDataStorage mGeometryData; // temporary, until uploaded to GPU


private:

	template<class TVertex>
	static FBoundingBox CalculateBoundingBox(const std::vector<TVertex>& verts);

	static BufferID CreateBuffer(VQRenderer* pRenderer, const FBufferDesc& desc);
};

//
// Template Definitions
//
#if 1
template<class TVertex, class TIndex>
Mesh::Mesh(VQRenderer* pRenderer, GeometryData<TVertex, TIndex>&& meshLODData, const std::string& name)
{
	//SCOPED_CPU_MARKER("Mesh::Mesh");

	mGeometryData.Name = name;

	// Move and serialize geometry data
	for (size_t LOD = 0; LOD < meshLODData.LODVertices.size(); ++LOD)
	{
		// Move vertex and index data
		std::vector<TVertex> vertices = std::move(meshLODData.LODVertices[LOD]);
		std::vector<TIndex> indices = std::move(meshLODData.LODIndices[LOD]);

		// Serialize
		std::vector<char> vertexData(reinterpret_cast<char*>(vertices.data()), reinterpret_cast<char*>(vertices.data() + vertices.size()));
		std::vector<char>  indexData(reinterpret_cast<char*>( indices.data()), reinterpret_cast<char*>( indices.data()  + indices.size()));
		mGeometryData.LODVertices.push_back(std::move(vertexData));
		mGeometryData.LODIndices.push_back(std::move(indexData));
		mGeometryData.VertexStrides.push_back(sizeof(TVertex));
		mGeometryData.IndexStrides.push_back(sizeof(TIndex));
		mGeometryData.NumIndices.push_back(static_cast<unsigned>(indices.size()));

		if (LOD == 0)
		{
			mLocalSpaceBoundingBox = CalculateBoundingBox(vertices);
		}
	}

	if (pRenderer) // Create buffers if renderer is provided
	{
		CreateBuffers(pRenderer);
	}
}
#else
template<class TVertex, class TIndex>
Mesh::Mesh(VQRenderer* pRenderer, GeometryData<TVertex, TIndex>&& meshLODData, const std::string& name)
{
	for (size_t LOD = 0; LOD < meshLODData.LODVertices.size(); ++LOD)
	{
		FBufferDesc bufferDesc = {};

		char VBName[128]; _snprintf_s(VBName, sizeof(VBName), "%s_LOD[%zu]_VB", name.c_str(), LOD);
		char IBName[128]; _snprintf_s(IBName, sizeof(IBName), "%s_LOD[%zu]_IB", name.c_str(), LOD);
		const std::vector<TVertex>& vertices = meshLODData.LODVertices[LOD];
		const std::vector<TIndex>& indices = meshLODData.LODIndices[LOD];

		bufferDesc.Type = VERTEX_BUFFER;
		//bufferDesc.Usage = GPU_READ_WRITE;
		bufferDesc.NumElements = static_cast<unsigned>(vertices.size());
		bufferDesc.pData = static_cast<const void*>(vertices.data());
		bufferDesc.Stride = sizeof(vertices[0]);
		bufferDesc.Name = VBName;
		BufferID vertexBufferID = CreateBuffer(pRenderer, bufferDesc);

		bufferDesc.Type = INDEX_BUFFER;
		//bufferDesc.Usage = GPU_READ_WRITE;
		bufferDesc.NumElements = static_cast<unsigned>(indices.size());
		bufferDesc.pData = static_cast<const void*>(indices.data());
		bufferDesc.Stride = sizeof(unsigned);
		bufferDesc.Name = IBName;
		BufferID indexBufferID = CreateBuffer(pRenderer, bufferDesc);

		mLODBufferPairs.push_back({ vertexBufferID, indexBufferID });
		mNumIndicesPerLODLevel.push_back(bufferDesc.NumElements);

		if (LOD == 0)
		{
			mLocalSpaceBoundingBox = CalculateBoundingBox(vertices);
		}
	}
}
#endif

template<class TVertex>
inline FBoundingBox Mesh::CalculateBoundingBox(const std::vector<TVertex>& verts)
{
	using namespace DirectX;
	constexpr float max_f = std::numeric_limits<float>::max();
	constexpr float min_f = -(max_f - 1.0f);

	FBoundingBox bb;
	XMFLOAT3& mins = bb.ExtentMin;
	XMFLOAT3& maxs = bb.ExtentMax;
	mins = XMFLOAT3(max_f, max_f, max_f);
	maxs = XMFLOAT3(min_f, min_f, min_f);
	XMVECTOR vMins = XMLoadFloat3(&mins);
	XMVECTOR vMaxs = XMLoadFloat3(&maxs);
	for (const TVertex& vert : verts)
	{
		XMFLOAT3 f3Pos(vert.position[0], vert.position[1], vert.position[2]);
		XMVECTOR vPos = XMLoadFloat3(&f3Pos);
		vMins = XMVectorMin(vMins, vPos);
		vMaxs = XMVectorMax(vMaxs, vPos);
	}
	XMStoreFloat3(&mins, vMins);
	XMStoreFloat3(&maxs, vMaxs);
	return bb;
}



using BuiltinMeshArray_t = std::array<Mesh, EBuiltInMeshes::NUM_BUILTIN_MESHES>;