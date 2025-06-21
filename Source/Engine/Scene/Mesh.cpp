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

#include "Mesh.h"

#include "Renderer/Renderer.h"
#include <cassert>

#define VERBOSE_LOGGING 0
#if VERBOSE_LOGGING
#include "Utilities/Log.h"
#endif


EBuiltInMeshes Mesh::GetBuiltInMeshType(const std::string& MeshTypeStr)
{
	static std::unordered_map<std::string, EBuiltInMeshes> MESH_TYPE_LOOKUP = 
	{
		  { "Cube", EBuiltInMeshes::CUBE }
		, { "Triangle", EBuiltInMeshes::TRIANGLE }
		// TODO
	};
	return MESH_TYPE_LOOKUP.at(MeshTypeStr);
}

std::pair<BufferID, BufferID> Mesh::GetIABufferIDs(int lod /*= 0*/) const
{
	assert(mLODBufferPairs.size() > 0); // maybe no assert and return <-1, -1> ?

	if (lod < mLODBufferPairs.size())
	{
		return mLODBufferPairs[lod].GetIABufferPair();
	}

#if VERBOSE_LOGGING
	Log::Warning("Requested LOD level (%d) doesn't exist (LOD levels = %d). Returning LOD=0", lod, static_cast<int>(mLODBufferPairs.size()));
#endif

	return mLODBufferPairs.back().GetIABufferPair();
}

BufferID Mesh::CreateBuffer(VQRenderer* pRenderer, const FBufferDesc& desc) 
{
	pRenderer->WaitHeapsInitialized(); 
	return pRenderer->CreateBuffer(desc); 
}

void Mesh::CreateBuffers(VQRenderer* pRenderer)
{
	if (!this->mLODBufferPairs.empty() || !mGeometryData.IsValid())
		return;

	for (size_t LOD = 0; LOD < mGeometryData.LODVertices.size(); ++LOD)
	{
		FBufferDesc bufferDesc = {};
		char VBName[128]; _snprintf_s(VBName, sizeof(VBName), "%s_LOD[%zu]_VB", mGeometryData.Name.c_str(), LOD);
		char IBName[128]; _snprintf_s(IBName, sizeof(IBName), "%s_LOD[%zu]_IB", mGeometryData.Name.c_str(), LOD);

		bufferDesc.Type = VERTEX_BUFFER;
		bufferDesc.NumElements = static_cast<unsigned>(mGeometryData.LODVertices[LOD].size() / mGeometryData.VertexStrides[LOD]);
		bufferDesc.pData = mGeometryData.LODVertices[LOD].data();
		bufferDesc.Stride = mGeometryData.VertexStrides[LOD];
		bufferDesc.Name = VBName;
		BufferID vertexBufferID = CreateBuffer(pRenderer, bufferDesc);

		bufferDesc.Type = INDEX_BUFFER;
		bufferDesc.NumElements = mGeometryData.NumIndices[LOD];
		bufferDesc.pData = mGeometryData.LODIndices[LOD].data();
		bufferDesc.Stride = mGeometryData.IndexStrides[LOD];
		bufferDesc.Name = IBName;
		BufferID indexBufferID = CreateBuffer(pRenderer, bufferDesc);

		mLODBufferPairs.push_back({ vertexBufferID, indexBufferID });
		mNumIndicesPerLODLevel.push_back(bufferDesc.NumElements);
	}

	// Clear geometry data
	mGeometryData = GeometryDataStorage();
}
