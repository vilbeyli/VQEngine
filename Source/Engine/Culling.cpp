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

#include "Culling.h"
#include "MeshSorting.h"
#include "Math.h"
#include "Scene/Scene.h"
#include "Libs/VQUtils/Include/Multithreading/ThreadPool.h"

#include <algorithm>
#include <execution>

#include "GPUMarker.h"

using namespace DirectX;

//------------------------------------------------------------------------------------------------------------------------------
//
// CULLING FUNCTIONS
//
//------------------------------------------------------------------------------------------------------------------------------
bool IsSphereIntersectingFurstum(const FFrustumPlaneset& FrustumPlanes, const FSphere& Sphere)
{
#if 1 
	// approximate sphere as a bounding box and utilize bounding box 
	// until the implementaiton is complete for sphere-frustum check below
	return IsBoundingBoxIntersectingFrustum(FrustumPlanes, FBoundingBox(Sphere));
#else
	bool bIntersecting = true;

	// check each frustum plane against sphere
	for (size_t i = 0; i < 6; ++i)
	{
		// plane eq : 0 = ax + by + cz + d
		// plane Normal : Normalized(a, b, c)
		// plane translation along plane normal: d
		XMVECTOR vPlaneNormal = XMLoadFloat4(&FrustumPlanes.abcd[i]);
		XMVECTOR vPlaneDistanceInNormalDirection = XMVectorSwizzle(vPlaneNormal, 3, 3, 3, 3);
		vPlaneNormal.m128_f32[3] = 1.0f;
		vPlaneNormal = XMVector3Normalize(vPlaneNormal);
		
		XMVECTOR vSpherePosition = XMLoadFloat3(&Sphere.CenterPosition);
		XMVECTOR vSphereRadius = XMLoadFloat(&Sphere.Radius);

		// project sphere position vector onto plane normal : move sphere up/down parallel to the plane so that N and ProjectedPosition aligns
		//                                                    ^ this makes intersection test easier by considering the sphere radius at the projected position
		XMVECTOR vSpherePositionProjected = XMVector3Dot(vSpherePosition, vPlaneNormal);

		XMVECTOR vSpherePositionProjectedLengthSq = XMVector3Dot(vSpherePositionProjected, vSpherePositionProjected);
		XMVECTOR vSpherePositionProjectedLength = XMVectorSqrt(vSpherePositionProjectedLengthSq);
		{
			XMVECTOR Tmp0 = vSpherePositionProjectedLength + vSphereRadius;
			XMVECTOR Tmp1 = vSpherePositionProjectedLength - vSphereRadius;
			vSpherePositionProjectedLength.m128_f32[1] = Tmp1.m128_f32[0]; // min extent
			vSpherePositionProjectedLength.m128_f32[2] = Tmp0.m128_f32[0]; // max extent
		}

		// [0]: center, [1] center-radius, [2] center+radius
		XMVECTOR vResult = vPlaneDistanceInNormalDirection - vSpherePositionProjectedLength;
		const bool bCenterOutsideFrustum = vResult.m128_f32[0] > 0;
		const bool bCenterMinExtentOutsideFrustum = vResult.m128_f32[1] > 0;
		const bool bCenterMaxExtentOutsideFrustum = vResult.m128_f32[2] > 0;
		if (bCenterOutsideFrustum && bCenterMinExtentOutsideFrustum && bCenterMaxExtentOutsideFrustum)
		{
			bIntersecting = false;
		}
	}

	assert(false); // not done yet
	return bIntersecting;
#endif
}

bool IsBoundingBoxIntersectingFrustum(const FFrustumPlaneset FrustumPlanes, const FBoundingBox& BBox)
{
	constexpr float EPSILON = 0.000002f;
	const XMVECTOR V_EPSILON = XMVectorSet(EPSILON, EPSILON, EPSILON, EPSILON);

	// storing these points in worker context is simply too expensive,
	// both when initializing the context and using it here
	const std::array<XMFLOAT4, 8> vPoints = BBox.GetCornerPointsF4();

	for (int p = 0; p < 6; ++p)	// for each plane
	{
		bool bInside = false;
		for (const XMFLOAT4& f4Point : vPoints)
		{
			XMVECTOR vPoint = XMLoadFloat4(&f4Point);
			const XMVECTOR& vPlane = FrustumPlanes.abcd[p];
			XMVECTOR cmp = XMVectorGreater(XMVector4Dot(vPoint, vPlane), V_EPSILON);
			if (bInside = cmp.m128_u32[0]) // is point inside frustum ?
			{
				break;
			}
		}
		if (!bInside) // if all the BB points are outside the frustum plane
			return false;
	}
	
	return true;
}
static bool IsBoundingBoxIntersectingFrustum2(const FFrustumPlaneset& FrustumPlanes, const FBoundingBox& BBox)
{
	constexpr float EPSILON = 0.000002f;
	const XMVECTOR V_EPSILON = XMVectorSet(EPSILON, EPSILON, EPSILON, EPSILON);
	const XMVECTOR vExtent = BBox.GetExtent();
	XMVECTOR vCenter = BBox.GetCenter();
	vCenter.m128_f32[3] = 1.0f;
	for (int p = 0; p < 6; ++p)	// for each plane
	{
		const XMVECTOR& vPlane = FrustumPlanes.abcd[p];
		
		// N : get the absolute value of the plane normal, so all {x,y,z} are positive
		//     which aligns well with the extents vector which is all positive due to
		//     storing the distances of maxs and mins.
		XMVECTOR vN = XMVectorAbs(vPlane);

		// r : how far away is the furthest point along the plane normal
		XMVECTOR R = XMVector3Dot(vN, vExtent);
		// Intuition: see https://fgiesen.wordpress.com/2010/10/17/view-frustum-culling/

		// signed distance of the center point of AABB to the plane
		XMVECTOR Dist = XMVector4Dot(vCenter, vPlane);

		if (XMVectorLess((Dist + R), V_EPSILON).m128_f32[0])
			return false;
	}
	return true;
}

bool IsFrustumIntersectingFrustum(const FFrustumPlaneset& FrustumPlanes0, const FFrustumPlaneset& FrustumPlanes1)
{
	return true; // TODO:
	assert(false); // not done yet
	return true;
}

float CalculateProjectedBoundingBoxArea(const FBoundingBox& BBox, const XMMATRIX& ViewProjectionMatrix) 
{
	auto corners = BBox.GetCornerPointsF4();
	
	const XMFLOAT3 f3Min(+FLT_MAX, +FLT_MAX, +FLT_MAX);
	const XMFLOAT3 f3Max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	XMVECTOR vMin = XMLoadFloat3(&f3Min);
	XMVECTOR vMax = XMLoadFloat3(&f3Max);

	for (const auto& corner : corners) {
		XMVECTOR cornerVec = XMLoadFloat4(&corner);
		XMVECTOR projected = XMVector3TransformCoord(cornerVec, ViewProjectionMatrix);

		vMin = XMVectorMin(projected, vMin);
		vMax = XMVectorMax(projected, vMax);
	}

	XMVECTOR vLen = vMax - vMin;
	return vLen.m128_f32[0] * vLen.m128_f32[1]; // NDC[-1, 1] --> area [0, 4]
}


//------------------------------------------------------------------------------------------------------------------------------
//
// THREADING
//
//------------------------------------------------------------------------------------------------------------------------------
void FFrustumCullWorkerContext::AllocInputMemoryIfNecessary(size_t sz)
{
	NumValidInputElements = sz;
	if (vFrustumPlanes.size() < sz)
	{
		SCOPED_CPU_MARKER("AllocMem");
		vVisibleBBIndicesPerView.resize(sz);
		vFrustumPlanes.resize(sz);
		vMatViewProj.resize(sz);
		vSortFunctions.resize(sz);
		vForceLOD0.resize(sz);
		vSortData.resize(sz);
	}

	assert(pFrustumRenderLists);
	if (pFrustumRenderLists && pFrustumRenderLists->size() < sz)
		pFrustumRenderLists->resize(sz);
}
void FFrustumCullWorkerContext::ClearMemory()
{
	SCOPED_CPU_MARKER("ClearMemory()");
	vFrustumPlanes.clear();
	vMatViewProj.clear();
	vSortFunctions.clear();
	vForceLOD0.clear();
	vBoundingBoxList.clear();
	vVisibleBBIndicesPerView.clear();

	// initial startup won't have this assigned but consecutive 
	// scene loads will have.
	if(pFrustumRenderLists)
		pFrustumRenderLists->clear();
	
	NumValidInputElements = 0;
}

void FFrustumCullWorkerContext::AddWorkerItem(
	  const std::vector<FBoundingBox>& vBoundingBoxListIn
	, size_t i
	, SortingFunction_t SortFunction
	, bool bForceLOD0
)
{
	SCOPED_CPU_MARKER("AddWorkerItem()");
	vSortFunctions[i] = SortFunction;
	vForceLOD0[i] = bForceLOD0;
	vBoundingBoxList = vBoundingBoxListIn; // copy
}

void FFrustumCullWorkerContext::ProcessWorkItems_SingleThreaded()
{
	const size_t& NumWorkItems = NumValidInputElements;
	if (NumWorkItems == 0)
	{
		// LogWarning?
		return;
	}

	// process all items on this thread
	this->Process(0, NumWorkItems - 1, nullptr);
}

static void DispatchWorkers(ThreadPool& WorkerThreadPool, size_t NumWorkItems, void (*pfnProcess)(size_t iBegin, size_t iEnd))
{
	SCOPED_CPU_MARKER("DispatchWorkers");
	const std::vector<std::pair<size_t, size_t>> vRanges = PartitionWorkItemsIntoRanges(NumWorkItems, WorkerThreadPool.GetThreadPoolSize());
	size_t currRange = 0;
	for (const std::pair<size_t, size_t>& Range : vRanges)
	{
		if (currRange == 0)
		{
			++currRange; // skip the first range, and do it on this thread after dispatches
			continue;
		}
		const size_t& iBegin = Range.first;
		const size_t& iEnd = Range.second; // inclusive
		assert(iBegin <= iEnd); // ensure work context bounds

		WorkerThreadPool.AddTask([=, &WorkerThreadPool]()
		{
			SCOPED_CPU_MARKER_C("UpdateWorker", WorkerThreadPool.mMarkerColor);
			pfnProcess(iBegin, iEnd);
		});
	}
}


void FFrustumCullWorkerContext::ProcessWorkItems_MultiThreaded(const size_t NumThreadsIncludingThisThread, ThreadPool& WorkerThreadPool)
{
	//const size_t szFP = vFrustumPlanes.size();
	//const size_t szBB = vBoundingBoxLists.size();
	//assert(szFP == szBB); // ensure matching input vector length

	const size_t& NumWorkItems = NumValidInputElements;
	if (NumWorkItems == 0)
	{
		// LogWarning?
		return;
	}

#if ENABLE_CONCURRENCY_DIAGNOSTICS_LOGGING
	const size_t NumRequestedWorkerThreads = NumThreadsIncludingThisThread - 1;
	const size_t NumAvailableConcurrentWorkerThreads = WorkerThreadPool.GetThreadPoolSize();
	if (NumRequestedWorkerThreads > NumAvailableConcurrentWorkerThreads)
	{
		// Log::Warning(); // would this work division perform well?
	}
#endif

	// distribute ranges of work into worker threads
	const std::vector<std::pair<size_t, size_t>> vRanges = GetWorkRanges(NumThreadsIncludingThisThread);
	
	// dispatch worker threads
	{
		SCOPED_CPU_MARKER("Process_DispatchWorkers");
		size_t currRange = 0;
		for (const std::pair<size_t, size_t>& Range : vRanges)
		{
			const size_t& iBegin = Range.first;
			const size_t& iEnd = Range.second; // inclusive
			assert(iBegin <= iEnd); // ensure work context bounds
			WorkerThreadPool.AddTask([=, &WorkerThreadPool]()
			{
				SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
				this->Process(iBegin, iEnd, &WorkerThreadPool);
			}, ETaskPriority::REAL_TIME);
			++currRange;
		}
	}

	// process some work on this thread
	WorkerThreadPool.RunRemainingTasksOnThisThread();
	
	return;
}

const std::vector<std::pair<size_t, size_t>> FFrustumCullWorkerContext::GetWorkRanges(size_t NumThreadsIncludingThisThread) const
{
	return PartitionWorkItemsIntoRanges(NumValidInputElements, NumThreadsIncludingThisThread);
}


void FFrustumCullWorkerContext::Process(size_t iRangeBegin, size_t iRangeEnd, ThreadPool* pWorkerThreadPool)
{
	const std::string marker = "ProcessFrustums[" + std::to_string(iRangeBegin) + "-" + std::to_string(iRangeEnd) + "]";
	SCOPED_CPU_MARKER_C(marker.c_str(), 0xFF0000AA);
	const size_t szFP = vFrustumPlanes.size();
	assert(iRangeBegin <= szFP); // ensure work context bounds
	assert(iRangeEnd < szFP); // ensure work context bounds
	assert(iRangeBegin <= iRangeEnd); // ensure work context bounds

#define DEBUG_LOG_SORT 0
	const MeshLookup_t& MeshLookupCopy = mMeshes;
	const MemoryPool<Material>& MaterialPool = mMaterials;
	const std::vector<MeshID>& MeshBB_MeshID	= BBH.GetMeshesIDs();
	const std::vector<MaterialID>& MeshBB_MatID = BBH.GetMeshMaterialIDs();
	const std::vector<size_t>& MeshBB_GameObjHandles = BBH.GetMeshGameObjectHandles();
	const std::vector<const Transform*>& MeshBB_Transforms = BBH.GetMeshTransforms();
	
	// process each frustum
	{
		SCOPED_CPU_MARKER("Clear");
		for (size_t iWork = iRangeBegin; iWork <= iRangeEnd; ++iWork)
		{
			vVisibleBBIndicesPerView[iWork].clear();
		}
	}
	{
		SCOPED_CPU_MARKER_C("CullFrustums", 0xFF2222AA);
		for (size_t iWork = iRangeBegin; iWork <= iRangeEnd; ++iWork)
		{
			const std::string marker2 = "Frustum[" + std::to_string(iWork) + "]";
			{
				SCOPED_CPU_MARKER_C(marker2.c_str(), 0xFF2222AA);
				for (size_t bb = 0; bb < vBoundingBoxList.size(); ++bb)
				{
					if (IsBoundingBoxIntersectingFrustum2(vFrustumPlanes[iWork], vBoundingBoxList[bb]))
					{
						vVisibleBBIndicesPerView[iWork].push_back(bb); // grows as we go (no pre-alloc)
					}
				}
			}
			const size_t NumVisibleItems = vVisibleBBIndicesPerView[iWork].size();
			FVisibleMeshDataSoA& vVisibleMeshListSoA = (*pFrustumRenderLists)[iWork].Data;
			std::vector<FVisibleMeshSortData>& sortData = vSortData[iWork];
			{
				SCOPED_CPU_MARKER("AllocRenderData");
				if (NumVisibleItems > sortData.size())
					sortData.resize(NumVisibleItems);
				vVisibleMeshListSoA.Reserve(NumVisibleItems);
			}
		}
	}
	if(pWorkerThreadPool)
	{
		for (size_t iWork = iRangeBegin; iWork <= iRangeEnd; ++iWork)
		{
			pWorkerThreadPool->AddTask([iWork, this, pWorkerThreadPool]()
			{
				SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
				SortMeshData(iWork, nullptr); // GatherVisibleMeshData() called from within SortMeshData()

				{
					SCOPED_CPU_MARKER("SignalCount"); // unblocks dispatching batch workers which wait until GatherVisibleMeshData signals data ready.
					(*pFrustumRenderLists)[iWork].DataCountReadySignal.Notify(vVisibleBBIndicesPerView[iWork].size());
				}

				GatherVisibleMeshData(iWork);

			}, ETaskPriority::CRITICAL);
		}
	}
	else
	{
		for (size_t iWork = iRangeBegin; iWork <= iRangeEnd; ++iWork)
		{
			SortMeshData(iWork, nullptr);

			{
				SCOPED_CPU_MARKER("SignalCount"); // unblocks dispatching batch workers which wait until GatherVisibleMeshData signals data ready.
				(*pFrustumRenderLists)[iWork].DataCountReadySignal.Notify(vVisibleBBIndicesPerView[iWork].size());
			}

			GatherVisibleMeshData(iWork);
		}
	}
}

static int GetLODFromProjectedScreenArea(float fArea, int NumMaxLODs)
{
	// LOD0 >= 0.100 >= LOD1 >= 0.010 >= LOD2 >= 0.001
	//
	// coarse algorithm: just pick 1/10th for each available lod
	int CurrLOD = 0;
	float Threshold = 0.1f;
	while (CurrLOD < NumMaxLODs - 1 && fArea <= Threshold)
	{
		Threshold *= 0.1f;
		++CurrLOD;
	}
	assert(CurrLOD < NumMaxLODs && CurrLOD >= 0);
	return CurrLOD;
}
void FFrustumCullWorkerContext::SortMeshData(size_t iWork, ThreadPool* pWorkerThreadPool)
{
	SCOPED_CPU_MARKER_C("SortMeshData", 0xFFAA00AA);
	const MeshLookup_t& MeshLookupCopy = mMeshes;
	const MemoryPool<Material>& MaterialPool = mMaterials;
	const std::vector<MeshID>& MeshBB_MeshID = BBH.GetMeshesIDs();
	const std::vector<MaterialID>& MeshBB_MatID = BBH.GetMeshMaterialIDs();
	const std::vector<size_t>& MeshBB_GameObjHandles = BBH.GetMeshGameObjectHandles();
	const std::vector<const Transform*>& MeshBB_Transforms = BBH.GetMeshTransforms();

	const size_t NumVisibleItems = vVisibleBBIndicesPerView[iWork].size();
	std::vector<FVisibleMeshSortData>& sortData = vSortData[iWork];
	{
		SCOPED_CPU_MARKER_C("Set", 0xFFAA00AA);
		int ii = 0;
		XMMATRIX matVP = vMatViewProj[iWork];
		for (size_t bb : vVisibleBBIndicesPerView[iWork])
		{
			const float fBBArea = CalculateProjectedBoundingBoxArea(vBoundingBoxList[bb], matVP);
			const MeshID meshID = MeshBB_MeshID[bb];
			const MaterialID matID = MeshBB_MatID[bb];
			const Material& mat = *MaterialPool.Get(matID);
			const Mesh& mesh = MeshLookupCopy.at(meshID);

			sortData[ii].iBB = (int32)bb;
			sortData[ii].fBBArea = fBBArea;
			sortData[ii].matID = matID;
			sortData[ii].meshID = meshID;
			sortData[ii].bTess = mat.IsTessellationEnabled() ? 1 : 0;
			sortData[ii].iLOD = vForceLOD0[iWork] ? 0 : GetLODFromProjectedScreenArea(fBBArea, mesh.GetNumLODs());

			assert(sortData[ii].iLOD < 256);
			++ii;
		}
	}
	if (pWorkerThreadPool)
	{
		pWorkerThreadPool->AddTask([iWork, this, pWorkerThreadPool, &sortData, NumVisibleItems]()
		{
			SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
			{
				SCOPED_CPU_MARKER_C("Sort", 0xFFAA00AA);
				std::sort(std::execution::seq,
					sortData.begin(),
					sortData.begin() + NumVisibleItems,
					vSortFunctions[iWork]
				);
			}

			pWorkerThreadPool->AddTask([iWork, this]() 
			{
				SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
				GatherVisibleMeshData(iWork);
			}, ETaskPriority::HIGH);
		});
	}
	else
	{
		SCOPED_CPU_MARKER("Sort");
		std::sort(std::execution::seq,
			sortData.begin(),
			sortData.begin() + NumVisibleItems,
			vSortFunctions[iWork]
		);
	}
}

void FFrustumCullWorkerContext::GatherVisibleMeshData(size_t iWork)
{
	SCOPED_CPU_MARKER_C("GatherMeshData", 0xFFFF5500);

	const MeshLookup_t& MeshLookupCopy = mMeshes;
	const MemoryPool<Material>& MaterialPool = mMaterials;
	const std::vector<MeshID>& MeshBB_MeshID = BBH.GetMeshesIDs();
	const std::vector<MaterialID>& MeshBB_MatID = BBH.GetMeshMaterialIDs();
	const std::vector<size_t>& MeshBB_GameObjHandles = BBH.GetMeshGameObjectHandles();
	const std::vector<const Transform*>& MeshBB_Transforms = BBH.GetMeshTransforms();

	FFrustumRenderList& FrustumRenderList = (*pFrustumRenderLists)[iWork];
	const std::vector<FVisibleMeshSortData>& sortData = vSortData[iWork];
	const size_t NumVisibleItems = vVisibleBBIndicesPerView[iWork].size();
	FVisibleMeshDataSoA& vVisibleMeshListSoA = FrustumRenderList.Data;
	vVisibleMeshListSoA.pMaterialPool = &this->mMaterials;
	{
		SCOPED_CPU_MARKER("SortKey");
		for (size_t i = 0; i < NumVisibleItems; ++i)
		{
			const FVisibleMeshSortData& d = sortData[i];
			switch (FrustumRenderList.Type)
			{
			case FFrustumRenderList::EFrustumType::MainView:
				vVisibleMeshListSoA.SortKey[i] = MeshSorting::GetLitMeshKey(d.matID, d.meshID, d.iLOD, d.bTess);
				break;
			case FFrustumRenderList::EFrustumType::SpotShadow:
			case FFrustumRenderList::EFrustumType::PointShadow:
			case FFrustumRenderList::EFrustumType::DirectionalShadow:
				vVisibleMeshListSoA.SortKey[i] = MeshSorting::GetShadowMeshKey(d.matID, d.meshID, d.iLOD, d.bTess);
				break;
			default:
				assert(false); // shouldn't happen
			}
		}
	}
	for (size_t i = 0; i < NumVisibleItems; ++i)
	{
		const FVisibleMeshSortData& d = sortData[i];
		const Mesh& mesh = MeshLookupCopy.at(d.meshID);
		vVisibleMeshListSoA.PerDrawData[i] = FPerDrawData{
			.hMaterial = d.matID,
			.hMesh = d.meshID,
			.VBIB = mesh.GetIABufferIDs(d.iLOD),
			.NumIndices = mesh.GetNumIndices(d.iLOD),
			.SelectedLOD = d.iLOD,
		};
	}
	{
		SCOPED_CPU_MARKER("Transform");
		for (size_t i = 0; i < NumVisibleItems; ++i)
		{
			const FVisibleMeshSortData& d = sortData[i];
			vVisibleMeshListSoA.Transform[i] = *MeshBB_Transforms[d.iBB]; // copy the transform
		}
	}
	{
		SCOPED_CPU_MARKER("MatID");
		for (size_t i = 0; i < NumVisibleItems; ++i)
		{
			const FVisibleMeshSortData& d = sortData[i];
			vVisibleMeshListSoA.MaterialID[i] = d.matID;
		}
	}
	for (size_t i = 0; i < NumVisibleItems; ++i)
	{
		const FVisibleMeshSortData& d = sortData[i];
		vVisibleMeshListSoA.PerInstanceData[i] = FPerInstanceData{
			.hGameObject = MeshBB_GameObjHandles[d.iBB],
			.fBBArea = d.fBBArea,
		};
	}
	{
		SCOPED_CPU_MARKER_C("SignalDataReady", 0xFF00FF00);
		//Log::Info("Signal FrustumRenderList[%d] : %d", iWork, NumVisibleItems);
		FrustumRenderList.DataReadySignal.Notify();
	}
}


//------------------------------------------------------------------------------------------------------------------------------
//
// BOUNDING BOX 
//
//------------------------------------------------------------------------------------------------------------------------------
std::array<DirectX::XMFLOAT4, 8> FBoundingBox::GetCornerPointsF4() const
{
	return std::array<XMFLOAT4, 8>
	{
		XMFLOAT4(ExtentMin.x, ExtentMin.y, ExtentMin.z, 1.0f),
		XMFLOAT4(ExtentMax.x, ExtentMin.y, ExtentMin.z, 1.0f),
		XMFLOAT4(ExtentMax.x, ExtentMax.y, ExtentMin.z, 1.0f),
		XMFLOAT4(ExtentMin.x, ExtentMax.y, ExtentMin.z, 1.0f),

		XMFLOAT4(ExtentMin.x, ExtentMin.y, ExtentMax.z, 1.0f),
		XMFLOAT4(ExtentMax.x, ExtentMin.y, ExtentMax.z, 1.0f),
		XMFLOAT4(ExtentMax.x, ExtentMax.y, ExtentMax.z, 1.0f),
		XMFLOAT4(ExtentMin.x, ExtentMax.y, ExtentMax.z, 1.0f)
	};
}
std::array<DirectX::XMFLOAT3, 8> FBoundingBox::GetCornerPointsF3() const
{
	return std::array<XMFLOAT3, 8>
	{
		XMFLOAT3(ExtentMin.x, ExtentMin.y, ExtentMin.z),
		XMFLOAT3(ExtentMax.x, ExtentMin.y, ExtentMin.z),
		XMFLOAT3(ExtentMax.x, ExtentMax.y, ExtentMin.z),
		XMFLOAT3(ExtentMin.x, ExtentMax.y, ExtentMin.z),

		XMFLOAT3(ExtentMin.x, ExtentMin.y, ExtentMax.z),
		XMFLOAT3(ExtentMax.x, ExtentMin.y, ExtentMax.z),
		XMFLOAT3(ExtentMax.x, ExtentMax.y, ExtentMax.z),
		XMFLOAT3(ExtentMin.x, ExtentMax.y, ExtentMax.z)
	};
}
FBoundingBox::FBoundingBox(const FSphere& s)
{
	const XMFLOAT3& P = s.CenterPosition;
	const float   & R = s.Radius;
	this->ExtentMax = XMFLOAT3(P.x + R, P.y + R, P.z + R);
	this->ExtentMin = XMFLOAT3(P.x - R, P.y - R, P.z - R);
}

static const XMMATRIX IdentityMat = XMMatrixIdentity();
static const XMVECTOR IdentityQuaternion = XMQuaternionIdentity();
static const XMVECTOR ZeroVector = XMVectorZero();
DirectX::XMMATRIX FBoundingBox::GetWorldTransformMatrix() const
{
	XMVECTOR BBMax = XMLoadFloat3(&ExtentMax);
	XMVECTOR BBMin = XMLoadFloat3(&ExtentMin);
	XMVECTOR ScaleVec = (BBMax - BBMin) * 0.5f;
	XMVECTOR BBOrigin = (BBMax + BBMin) * 0.5f;
	XMMATRIX MatTransform = XMMatrixTransformation(ZeroVector, IdentityQuaternion, ScaleVec, ZeroVector, IdentityQuaternion, BBOrigin);
	return MatTransform;
}

DirectX::XMVECTOR FBoundingBox::GetExtent() const
{
	XMVECTOR Maxs = XMLoadFloat3(&this->ExtentMax);
	XMVECTOR Mins = XMLoadFloat3(&this->ExtentMin);
	return XMVectorScale(Maxs - Mins, 0.5f);
}

DirectX::XMVECTOR FBoundingBox::GetCenter() const
{
	XMVECTOR Maxs = XMLoadFloat3(&this->ExtentMax);
	XMVECTOR Mins = XMLoadFloat3(&this->ExtentMin);
	return XMVectorScale(Maxs + Mins, 0.5f);
}

std::array<DirectX::XMVECTOR, 8> FBoundingBox::GetCornerPointsV4() const
{
	std::array<DirectX::XMFLOAT4, 8> Points_F4 = GetCornerPointsF4();
	std::array<DirectX::XMVECTOR, 8> Points_V;
	for(int i=0; i<8; ++i) Points_V[i] = XMLoadFloat4(&Points_F4[i]);
	return Points_V;
}
std::array<DirectX::XMVECTOR, 8> FBoundingBox::GetCornerPointsV3() const
{
	std::array<DirectX::XMFLOAT3, 8> Points_F3 = GetCornerPointsF3();
	std::array<DirectX::XMVECTOR, 8> Points_V;
	for (int i = 0; i < 8; ++i) Points_V[i] = XMLoadFloat3(&Points_F3[i]);
	return Points_V;
}

static constexpr float max_f = std::numeric_limits<float>::max();
static constexpr float min_f = -(max_f - 1.0f);
static const XMFLOAT3 MINS = XMFLOAT3(max_f, max_f, max_f);
static const XMFLOAT3 MAXS = XMFLOAT3(min_f, min_f, min_f);
static FBoundingBox GetAxisAligned(const std::array<DirectX::XMVECTOR, 8>& CornerPoints)
{
	XMVECTOR vMins = XMLoadFloat3(&MINS);
	XMVECTOR vMaxs = XMLoadFloat3(&MAXS);

	FBoundingBox AABB;
	for (const XMVECTOR& vPoint : CornerPoints)
	{
		vMins = XMVectorMin(vMins, vPoint);
		vMaxs = XMVectorMax(vMaxs, vPoint);
	}
	XMStoreFloat3(&AABB.ExtentMax, vMaxs);
	XMStoreFloat3(&AABB.ExtentMin, vMins);
	return AABB;
}
static FBoundingBox GetAxisAligned(const std::array<DirectX::XMFLOAT3, 8>& CornerPoints)
{
	XMVECTOR vMins = XMLoadFloat3(&MINS);
	XMVECTOR vMaxs = XMLoadFloat3(&MAXS);

	FBoundingBox AABB;
	const std::array<XMFLOAT3, 8> vPoints = CornerPoints;
	for (const XMFLOAT3& f3Point : vPoints)
	{
		XMVECTOR vPoint = XMLoadFloat3(&f3Point);
		vMins = XMVectorMin(vMins, vPoint);
		vMaxs = XMVectorMax(vMaxs, vPoint);
	}
	XMStoreFloat3(&AABB.ExtentMax, vMaxs);
	XMStoreFloat3(&AABB.ExtentMin, vMins);
	return AABB;
}
static FBoundingBox GetAxisAligned(const FBoundingBox& WorldBoundingBox) { return GetAxisAligned(WorldBoundingBox.GetCornerPointsV4()); }
static FBoundingBox CalculateAxisAlignedBoundingBox(const XMMATRIX& MWorld, const FBoundingBox& LocalSpaceAxisAlignedBoundingBox)
{
	std::array<XMVECTOR, 8> vPoints = LocalSpaceAxisAlignedBoundingBox.GetCornerPointsV4();
	for (int i = 0; i < 8; ++i) // transform points to world space
	{
		vPoints[i] = XMVector4Transform(vPoints[i], MWorld);
	}
	return GetAxisAligned(vPoints);
}



//------------------------------------------------------------------------------------------------------------------------------
//
// SCENE BOUNDING BOX HIERARCHY
//
//------------------------------------------------------------------------------------------------------------------------------
#define BOUNDING_BOX_HIERARCHY__MULTI_THREADED_BUILD 1
void SceneBoundingBoxHierarchy::Build(const Scene* pScene, const std::vector<size_t>& vGameObjectHandles, ThreadPool& WorkerThreadPool)
{
	assert(pScene);

	constexpr size_t NumDesiredMinimumWorkItemsPerThread = 256;
	const size_t NumWorkerThreadsAvailable = WorkerThreadPool.GetThreadPoolSize();

	SCOPED_CPU_MARKER("BuildBoundingBoxHierarchy");
	this->ResizeGameObjectBoundingBoxContainer(vGameObjectHandles.size());

#if BOUNDING_BOX_HIERARCHY__MULTI_THREADED_BUILD

	std::vector<TaskSignal<void>> Signals;

	// dispatch gameobject bounding box workers
	{
		const size_t NumWorkItems = vGameObjectHandles.size();
		const size_t NumWorkItemsPerAvailableWorkerThread = DIV_AND_ROUND_UP(NumWorkItems, WorkerThreadPool.GetThreadPoolSize());
		const size_t NumWorkersToUse = CalculateNumThreadsToUse(NumWorkItems, NumWorkerThreadsAvailable, NumDesiredMinimumWorkItemsPerThread)
			+ (WorkerThreadPool.GetNumActiveTasks() == 0 ? 1 : 0);

		const std::vector<std::pair<size_t, size_t>> vRanges = PartitionWorkItemsIntoRanges(NumWorkItems, NumWorkersToUse);
		Signals.resize(vRanges.size());
		
		// dispatch worker threads
		{
			SCOPED_CPU_MARKER("DispatchWorkers");
			for(size_t iRange=1; iRange<vRanges.size(); ++iRange)
			//for (const std::pair<size_t, size_t>& Range : vRanges)
			{
				const std::pair<size_t, size_t>& Range = vRanges[iRange];
				const size_t& iBegin = Range.first;
				const size_t& iEnd = Range.second; // inclusive
				assert(iBegin <= iEnd); // ensure work context bounds

				WorkerThreadPool.AddTask([=, &vGameObjectHandles, &Signals]()
				{
					SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
					this->BuildGameObjectBoundingBoxes_Range(pScene, vGameObjectHandles, iBegin, iEnd);
					Signals[iRange].Notify();
				});
			}
		}

		// this thread
		if(!vRanges.empty())
		{
			this->BuildGameObjectBoundingBoxes_Range(pScene, vGameObjectHandles, vRanges[0].first, vRanges[0].second);
			Signals[0].Notify();
		}
	}


	// dispatch mesh bounding box workers
	
	using namespace std;
	//CountGameObjectMeshes(pObjects);
		
	// Sync point -------------------------------------------------
	{
		for (TaskSignal<void>& Signal : Signals)
			Signal.Wait();
	}
	{
		SCOPED_CPU_MARKER("CountGameObjectMeshes");
		mNumValidMeshBoundingBoxes = 0;
		for (size_t NumMeshes : mGameObjectNumMeshes)
			mNumValidMeshBoundingBoxes += NumMeshes;
	}

	ResizeGameMeshBoxContainer(mNumValidMeshBoundingBoxes);
#if 1
	std::vector<std::tuple<size_t, size_t, size_t>> vRanges;
	{
		SCOPED_CPU_MARKER("PrepareRanges");
		const size_t  NumObjs = vGameObjectHandles.size();
		const size_t& NumWorkItems = mNumValidMeshBoundingBoxes;
		const size_t  NumWorkerThreadsToUse = CalculateNumThreadsToUse(NumWorkItems, NumWorkerThreadsAvailable, NumDesiredMinimumWorkItemsPerThread);

		const size_t ThreadBBBatchCapacity = mNumValidMeshBoundingBoxes / (NumWorkerThreadsToUse+1);

		size_t iBegin = 0;       size_t iEnd = 0;
		size_t CurrBatchSize = 0; size_t iBB = 0;
		for (size_t i=0; i<NumObjs; ++i)
		{
			CurrBatchSize += mGameObjectNumMeshes[i];
			++iEnd;

			if (CurrBatchSize > ThreadBBBatchCapacity)
			{
				vRanges.push_back({ iBegin, iEnd, iBB });
				iBegin = iEnd;
				iBB += CurrBatchSize;
				CurrBatchSize = 0;
			}
		}

		vRanges.push_back({ iBegin, iEnd, iBB });
	}
	std::vector<TaskSignal<void>> SignalsMeshBB(vRanges.size());
	{
		SCOPED_CPU_MARKER("DispatchWorkers");
		for (size_t i = 1; i < vRanges.size(); ++i)
		{
			WorkerThreadPool.AddTask([=, &vGameObjectHandles, &SignalsMeshBB]()
			{
				SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
				this->BuildMeshBoundingBoxes_Range(pScene, vGameObjectHandles, get<0>(vRanges[i]), get<1>(vRanges[i]), get<2>(vRanges[i]));
				SignalsMeshBB[i].Notify();
			});
		}
	}
	{
		SCOPED_CPU_MARKER("Process_ThisThread");
		this->BuildMeshBoundingBoxes_Range(pScene, vGameObjectHandles, get<0>(vRanges.front()), get<1>(vRanges.front()), get<2>(vRanges.front()));
	}
#else
	this->BuildMeshBoundingBoxes(pObjects);
#endif
	
	// Sync point -------------------------------------------------
	{
		SCOPED_CPU_MARKER_C("WAIT_WORKERS_MeshBB", 0xFFFF0000);
		for (size_t i = 1; i < vRanges.size(); ++i)
		{
			SignalsMeshBB[i].Wait();
		}
	}

#else
	this->CountGameObjectMeshes(pObjects);
	this->ResizeGameMeshBoxContainer(mNumValidMeshBoundingBoxes);
	this->BuildGameObjectBoundingBoxes(pObjects);
	this->BuildMeshBoundingBoxes(pObjects);
#endif
}


void SceneBoundingBoxHierarchy::BuildGameObjectBoundingBox(const Scene* pScene, size_t ObjectHandle, size_t iBB)
{
	const Transform* pTF = pScene->GetGameObjectTransform(ObjectHandle);
	assert(pTF);

	const GameObject* pObj = pScene->GetGameObject(ObjectHandle);
	assert(pObj);

	// assumes static meshes: 
	// - no VB/IB change
	// - no dynamic vertex animations, morphing etc
	XMMATRIX matWorld = pTF->matWorldTransformation();
	mGameObjectBoundingBoxes[iBB] = CalculateAxisAlignedBoundingBox(matWorld, pObj->mLocalSpaceBoundingBox);
	mGameObjectHandles[iBB] = ObjectHandle;
	
	auto it = mModels.find(pObj->mModelID);
	if (it == mModels.end())
	{
		return;
	}

	const Model& model = it->second;
	mGameObjectNumMeshes[iBB] = model.mData.GetNumMeshesOfAllTypes();
}

void SceneBoundingBoxHierarchy::BuildGameObjectBoundingBoxes(const Scene* pScene, const std::vector<size_t>& GameObjectHandles)
{
	SCOPED_CPU_MARKER("BuildGameObjectBoundingBoxes");
	size_t iBB = 0;
	for (size_t Handle : GameObjectHandles)
		BuildGameObjectBoundingBox(pScene, Handle, iBB++);
}
void SceneBoundingBoxHierarchy::BuildGameObjectBoundingBoxes_Range(const Scene* pScene, const std::vector<size_t>& GameObjectHandles, size_t iBegin, size_t iEnd)
{
	SCOPED_CPU_MARKER("BuildGameObjectBoundingBoxes_Range");
	for (size_t i = iBegin; i <= iEnd; ++i)
	{
		BuildGameObjectBoundingBox(pScene, GameObjectHandles[i], i);
	}
}


void SceneBoundingBoxHierarchy::BuildMeshBoundingBox(const Scene* pScene, size_t ObjectHandle, size_t iBB_Begin, size_t iBB_End)
{
	const Transform* pTF = pScene->GetGameObjectTransform(ObjectHandle);
	assert(pTF);

	const GameObject* pObj = pScene->GetGameObject(ObjectHandle);
	assert(pObj);

	const Model& model = mModels.at(pObj->mModelID);

	const XMMATRIX matWorld = pTF->matWorldTransformation();

	// assumes static meshes: 
	// - no VB/IB change
	// - no dynamic vertex animations, morphing etc
	bool bAtLeastOneMesh = false;
	size_t iMesh = iBB_Begin;
	auto fnProcessMeshes = [&](const std::vector<std::pair<MeshID, MaterialID>>& meshIDs)
	{
		for (const std::pair<MeshID, MaterialID>& meshMaterialIDPair : meshIDs)
		{
			MeshID meshID = meshMaterialIDPair.first;
			const Mesh& mesh = mMeshes.at(meshID);
			MaterialID mat = meshMaterialIDPair.second;
			const int numMaxLODs = mesh.GetNumLODs();

			mMeshBoundingBoxes[iMesh] = CalculateAxisAlignedBoundingBox(matWorld, mesh.GetLocalSpaceBoundingBox());
			mMeshIDs[iMesh] = meshID;
			mNumMeshLODs[iMesh] = numMaxLODs;
			mMeshMaterials[iMesh] = mat;
			mMeshGameObjectHandles[iMesh] = ObjectHandle;
			mMeshTransforms[iMesh] = pTF;
			++iMesh;
			bAtLeastOneMesh = true;
		}
	};
	fnProcessMeshes(model.mData.GetMeshMaterialIDPairs(Model::Data::EMeshType::OPAQUE_MESH));
	fnProcessMeshes(model.mData.GetMeshMaterialIDPairs(Model::Data::EMeshType::TRANSPARENT_MESH));

	assert(bAtLeastOneMesh);
}
void SceneBoundingBoxHierarchy::BuildMeshBoundingBoxes(const Scene* pScene, const std::vector<size_t>& GameObjectHandles)
{

	SCOPED_CPU_MARKER("BuildMeshBoundingBoxes");
	size_t i = 0;
	for (size_t ObjectHandle : GameObjectHandles)
	{
		const GameObject* pObj = pScene->GetGameObject(ObjectHandle);
		assert(pObj);

		const Model& model = mModels.at(pObj->mModelID);
		const size_t NumMeshes = model.mData.GetNumMeshesOfAllTypes();
		BuildMeshBoundingBox(pScene, ObjectHandle, i, i + NumMeshes);
		i += NumMeshes;
	}
	
}
void SceneBoundingBoxHierarchy::BuildMeshBoundingBoxes_Range(const Scene* pScene, const std::vector<size_t>& GameObjectHandles, size_t iBegin, size_t iEnd, size_t iMeshBB)
{
	SCOPED_CPU_MARKER("BuildMeshBoundingBoxes_Range");
	size_t iMeshBBOffset = 0;
	for (size_t i = iBegin; i<iEnd; ++i)
	{
		const GameObject* pObj = pScene->GetGameObject(GameObjectHandles[i]);
		assert(pObj);

		auto it = mModels.find(pObj->mModelID);
		if (it == mModels.end())
		{
			continue;
		}

		const Model& model = it->second;
		const size_t NumMeshes = model.mData.GetNumMeshesOfAllTypes();
		BuildMeshBoundingBox(pScene, GameObjectHandles[i], iMeshBB + iMeshBBOffset, 0);
		iMeshBBOffset += NumMeshes;
	}
}

void SceneBoundingBoxHierarchy::Clear()
{
	SCOPED_CPU_MARKER("SceneBoundingBoxHierarchy::Clear()");
	mSceneBoundingBox = {};
	
	mGameObjectBoundingBoxes.clear();
	mGameObjectHandles.clear();
	
	mMeshBoundingBoxes.clear();
	mMeshIDs.clear();
	mNumMeshLODs.clear();
	mMeshMaterials.clear();
	mMeshTransforms.clear();
	mMeshGameObjectHandles.clear();
}

void SceneBoundingBoxHierarchy::ResizeGameObjectBoundingBoxContainer(size_t sz)
{
	SCOPED_CPU_MARKER("ResizeGameObjectBoundingBoxContainer");
	mGameObjectBoundingBoxes.resize(sz);
	mGameObjectHandles.resize(sz);
	mGameObjectNumMeshes.resize(sz);
}

static size_t CountGameObjectMeshes(const Scene* pScene, const std::vector<size_t>& GameObjectHandles, const ModelLookup_t& Models)
{
	SCOPED_CPU_MARKER("CountGameObjectMeshes");
	size_t count = 0;
	for (size_t ObjectHandle : GameObjectHandles) // count total number of meshes in all game objects
	{
		const GameObject* pObj = pScene->GetGameObject(ObjectHandle);
		assert(pObj);

		const Model& model = Models.at(pObj->mModelID);
		const size_t NumMeshes = model.mData.GetNumMeshesOfAllTypes();
		count += NumMeshes;
	}
	return count;
}

void SceneBoundingBoxHierarchy::ResizeGameMeshBoxContainer(size_t size)
{
	SCOPED_CPU_MARKER("BuildMeshBoundingBoxes_Rsz");
	mMeshBoundingBoxes.resize(size);
	mMeshIDs.resize(size);
	mNumMeshLODs.resize(size);
	mMeshMaterials.resize(size);
	mMeshTransforms.resize(size);
	mMeshGameObjectHandles.resize(size);
}

