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

#include "../../Renderer/Renderer.h"
#include "../Culling.h"

#include <string>
#include <vector>
#include <limits>

#include <DirectXMath.h>

#define NOMINMAX
#undef max

enum EBuiltInMeshes
{
	TRIANGLE = 0,
	QUAD,
	CUBE,
	SPHERE,
	CYLINDER,
	CONE,
	
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
	//
	// Constructors / Operators
	//
	template<class TVertex, class TIndex = unsigned>
	Mesh(
		VQRenderer* pRenderer,
		const std::vector<TVertex>&  vertices,
		const std::vector<TIndex>&   indices,
		const std::string&           name
	);

	template<class TVertex, class TIndex>
	Mesh(VQRenderer* pRenderer, const MeshLODData<TVertex, TIndex>& meshLODData);

	Mesh() = default;

	//
	// Interface
	//
	std::pair<BufferID, BufferID> GetIABufferIDs(int lod = 0) const;
	inline uint GetNumIndices(int lod = 0) const { return mNumIndicesPerLODLevel[lod]; }
	const FBoundingBox GetLocalSpaceBoundingBox() const { return mLocalSpaceBoundingBox; }
	
private:
	std::vector<VertexIndexBufferIDPair> mLODBufferPairs;
	std::vector<uint> mNumIndicesPerLODLevel;
	FBoundingBox mLocalSpaceBoundingBox;

private:

	template<class TVertex>
	static FBoundingBox CalculateBoundingBox(const std::vector<TVertex>& verts);
};

//
// Template Definitions
//
template<class TVertex, class TIndex>
Mesh::Mesh(
	VQRenderer* pRenderer,
	const std::vector<TVertex>& vertices,
	const std::vector<TIndex>& indices,
	const std::string& name
)
{
	assert(pRenderer);
	FBufferDesc bufferDesc = {};

	const std::string VBName = name + "_LOD[0]_VB";
	const std::string IBName = name + "_LOD[0]_IB";

	bufferDesc.Type         = VERTEX_BUFFER;
	//bufferDesc.Usage        = GPU_READ_WRITE;
	bufferDesc.NumElements  = static_cast<unsigned>(vertices.size());
	bufferDesc.Stride       = sizeof(vertices[0]);
	bufferDesc.pData        = static_cast<const void*>(vertices.data());
	bufferDesc.Name         = VBName;
	BufferID vertexBufferID = pRenderer->CreateBuffer(bufferDesc);

	bufferDesc.Type        = INDEX_BUFFER;
	//bufferDesc.Usage       = GPU_READ_WRITE;
	bufferDesc.NumElements = static_cast<unsigned>(indices.size());
	bufferDesc.Stride      = sizeof(unsigned);
	bufferDesc.pData       = static_cast<const void*>(indices.data());
	BufferID indexBufferID = pRenderer->CreateBuffer(bufferDesc);

	mLODBufferPairs.push_back({ vertexBufferID, indexBufferID }); // LOD[0]
	mNumIndicesPerLODLevel.push_back(bufferDesc.NumElements);

	mLocalSpaceBoundingBox = CalculateBoundingBox(vertices);
}

template<class TVertex, class TIndex>
Mesh::Mesh(VQRenderer* pRenderer, const MeshLODData<TVertex, TIndex>& meshLODData)
{
	for (size_t LOD = 0; LOD < meshLODData.LODVertices.size(); ++LOD)
	{
		FBufferDesc bufferDesc = {};

		const std::string VBName = meshLODData.meshName + "_LOD[" + std::to_string(LOD) + "]_VB";
		const std::string IBName = meshLODData.meshName + "_LOD[" + std::to_string(LOD) + "]_IB";

		bufferDesc.mType = VERTEX_BUFFER;
		//bufferDesc.mUsage = GPU_READ_WRITE;
		bufferDesc.mElementCount = static_cast<unsigned>(meshLODData.LODVertices[LOD].size());
		bufferDesc.mStride = sizeof(meshLODData.LODVertices[LOD][0]);
		BufferID vertexBufferID = pRenderer->CreateBuffer(bufferDesc, meshLODData.LODVertices[LOD].data(), VBName.c_str());

		bufferDesc.mType = INDEX_BUFFER;
		//bufferDesc.mUsage = GPU_READ_WRITE;
		bufferDesc.NumElements = static_cast<unsigned>(meshLODData.LODIndices[LOD].size());
		bufferDesc.mStride = sizeof(unsigned);
		BufferID indexBufferID = pRenderer->CreateBuffer(bufferDesc, meshLODData.LODIndices[LOD].data(), IBName.c_str());

		mLODBufferPairs.push_back({ vertexBufferID, indexBufferID });
		mNumIndicesPerLODLevel.push_back(bufferDesc.NumElements);

		if (LOD == 0)
		{
			mLocalSpaceBoundingBox = CalculateBoundingBox(meshLODData.LODVertices[LOD]);
		}
	}
}

template<class TVertex>
inline FBoundingBox Mesh::CalculateBoundingBox(const std::vector<TVertex>& verts)
{
	using namespace DirectX;
	const float max_f = std::numeric_limits<float>::max();
	const float min_f = -(max_f - 1.0f);

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