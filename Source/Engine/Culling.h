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

#include "Core/Types.h"
#include "Core/Memory.h"
#include "Scene/Mesh.h"
#include "Scene/Material.h"
#include "Scene/SceneViews.h"

#include <unordered_map>
#include <functional>
#include <future>

class GameObject;
class ThreadPool;
class Scene;
struct Transform;
class SceneBoundingBoxHierarchy;
using MeshLookup_t = std::unordered_map<MeshID, Mesh>;
using MaterialLookup_t = std::unordered_map<MaterialID, Material>;

//------------------------------------------------------------------------------------------------------------------------------
//
// CULLING FUNCTIONS
//
//------------------------------------------------------------------------------------------------------------------------------
bool IsSphereIntersectingFurstum(const FFrustumPlaneset& FrustumPlanes, const FSphere& Sphere);
bool IsBoundingBoxIntersectingFrustum(const FFrustumPlaneset FrustumPlanes, const FBoundingBox& BBox);
bool IsFrustumIntersectingFrustum(const FFrustumPlaneset& FrustumPlanes0, const FFrustumPlaneset& FrustumPlanes1);


//------------------------------------------------------------------------------------------------------------------------------
//
// THREADING
//
//------------------------------------------------------------------------------------------------------------------------------
// Struct-of-Arrays worker context: 
// - all vectors same size
// - each thread given a range of the vectors to process
struct FThreadWorkerContext
{
	virtual void Process(size_t iRangeBegin, size_t iRangeEnd, ThreadPool* pWorkerThreadPool) = 0;
};

struct FFrustumCullWorkerContext : public FThreadWorkerContext
{	
	using IndexList_t = std::vector<size_t>;
	// Hot Data (per view): used during culling --------------------------------------------------------------------------------------
	/*in */ std::vector<FFrustumPlaneset > vFrustumPlanes;
	/*in */ std::vector<DirectX::XMMATRIX> vMatViewProj;

	// Common data for each view
	/*in */ std::vector<FBoundingBox     > vBoundingBoxList;
	 
	// store the index of the surviving bounding box in a list, per view frustum
	/*out*/ std::vector<std::vector<size_t>> vVisibleBBIndicesPerView;
	/*out*/ std::vector<std::vector<FVisibleMeshSortData>> vSortData;
	/*out*/ std::vector<FFrustumRenderList>* pFrustumRenderLists; // for each view


	using SortingFunction_t = std::function<bool(const FVisibleMeshSortData&, const FVisibleMeshSortData&)>;
	/*in */ std::vector<SortingFunction_t> vSortFunctions;
	/*in */ std::vector<char> vForceLOD0;
	// Hot Data ------------------------------------------------------------------------------------------------------------

	//std::vector<int> vLightMovementTypeID; // index to access light type vectors: [0]:static, [1]:stationary, [2]:dynamic

	size_t NumValidInputElements = 0;
	const SceneBoundingBoxHierarchy& BBH;
	const MeshLookup_t& mMeshes;
	const MemoryPool<Material>& mMaterials;

	// ====================================================================================
	FFrustumCullWorkerContext() = delete;
	FFrustumCullWorkerContext(
		const SceneBoundingBoxHierarchy& BBH, 
		const MeshLookup_t& mMeshes, 
		const MemoryPool<Material>& mMaterials
	) 
		: BBH(BBH)
		, mMeshes(mMeshes)
		, mMaterials(mMaterials)
		, pFrustumRenderLists(nullptr)
	{}
	
	void AddWorkerItem(
		  const std::vector<FBoundingBox>& vBoundingBoxList
		, const  size_t i
		, SortingFunction_t SortFunction
		, bool bForceLOD0
	);
	inline void InvalidateContextData() { NumValidInputElements = 0; }
	void ClearMemory();

	void ProcessWorkItems_SingleThreaded();
	void ProcessWorkItems_MultiThreaded(const size_t NumThreadsIncludingThisThread, ThreadPool& WorkerThreadPool);

	const std::vector<std::pair<size_t, size_t>> GetWorkRanges(size_t NumThreadsIncludingThisThread) const;

	void AllocInputMemoryIfNecessary(size_t sz);
//private:
	void Process(size_t iRangeBegin, size_t iRangeEnd, ThreadPool* pWorkerThreadPool) override;
	void SortMeshData(size_t iFrustum, ThreadPool* pWorkerThreadPool);
	void GatherVisibleMeshData(size_t iFrustum);

};