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
#include "Math.h"
#include "Scene/Scene.h"
#include "Libs/VQUtils/Source/Multithreading.h"

#include <algorithm>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
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

bool IsBoundingBoxIntersectingFrustum(const FFrustumPlaneset& FrustumPlanes, const FBoundingBox& BBox)
{
	constexpr float EPSILON = 0.000002f;

	// this is a hotspot: GetCornerPointsV4() creating the bounding box on the stack may slow it down.
	//                    TODO: test with a pre-generated set of corners instead of doing it on the fly.
	const std::array<XMFLOAT4, 8> vPoints = BBox.GetCornerPointsF4(); // TODO: optimize XMLoadFloat4

	for (int p = 0; p < 6; ++p)	// for each plane
	{
		bool bInside = false;
		for (const XMFLOAT4& f4Point : vPoints)
		{
			XMVECTOR vPoint = XMLoadFloat4(&f4Point);
			XMVECTOR vPlane = XMLoadFloat4(&FrustumPlanes.abcd[p]);
			if (XMVector4Dot(vPoint, vPlane).m128_f32[0] > EPSILON)
			{
				bInside = true;
				break;
			}
		}
		if (!bInside) // if all the BB points are outside the frustum plane
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



//------------------------------------------------------------------------------------------------------------------------------
//
// THREADING
//
//------------------------------------------------------------------------------------------------------------------------------
void FFrustumCullWorkerContext::AllocInputMemoryIfNecessary(size_t sz)
{
	std::unique_lock<std::mutex> lk(mMutex);
	if (vFrustumPlanes.size() < sz)
	{
		SCOPED_CPU_MARKER("AllocMem");
		vFrustumPlanes.resize(sz);
		vBoundingBoxLists.resize(sz);
		vGameObjectPointerLists.resize(sz);
	}
}
void FFrustumCullWorkerContext::ClearMemory()
{
	vFrustumPlanes.clear();
	vBoundingBoxLists.clear();
	vGameObjectPointerLists.clear();
	vCulledBoundingBoxIndexListPerView.clear();
	NumValidInputElements = 0;
}


size_t FFrustumCullWorkerContext::AddWorkerItem(FFrustumPlaneset&& FrustumPlaneSet, const std::vector<FBoundingBox>& vBoundingBoxList, const std::vector<const GameObject*>& pGameObjects)
{
	assert(false);
	SCOPED_CPU_MARKER("FFrustumCullWorkerContext::AddWorkerItem()");
	const size_t i = NumValidInputElements++;
	AllocInputMemoryIfNecessary(i);

	vFrustumPlanes[i] = FrustumPlaneSet;
	vBoundingBoxLists[i] = vBoundingBoxList;
	vGameObjectPointerLists[i] = pGameObjects;
	return i;
}
size_t FFrustumCullWorkerContext::AddWorkerItem(const FFrustumPlaneset& FrustumPlaneSet, const std::vector<FBoundingBox>& vBoundingBoxList, const std::vector<const GameObject*>& pGameObjects)
{
	SCOPED_CPU_MARKER("FFrustumCullWorkerContext::AddWorkerItem()");

	const size_t i = NumValidInputElements++;
	//AllocInputMemoryIfNecessary(i);

	vFrustumPlanes[i] = FrustumPlaneSet;
	vBoundingBoxLists[i] = vBoundingBoxList;
	vGameObjectPointerLists[i] = pGameObjects;

	return i;
}

void FFrustumCullWorkerContext::ProcessWorkItems_SingleThreaded()
{
	const size_t szFP = vFrustumPlanes.size();
	const size_t szBB = vBoundingBoxLists.size();
	assert(szFP == szBB); // ensure matching input vector length
	
	const size_t& NumWorkItems = szFP;
	if (NumWorkItems == 0)
	{
		// LogWarning?
		return;
	}

	// allocate context memory
	vCulledBoundingBoxIndexListPerView.resize(szFP);

	// process all items on this thread
	this->Process(0, szFP - 1);
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

		WorkerThreadPool.AddTask([=]()
		{
			SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
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

	// allocate context memory
	vCulledBoundingBoxIndexListPerView.resize(NumValidInputElements); // prepare worker output memory, each worker will then populate the vector

	// distribute ranges of work into worker threads
	const std::vector<std::pair<size_t, size_t>> vRanges = PartitionWorkItemsIntoRanges(NumWorkItems, NumThreadsIncludingThisThread);
	
	// dispatch worker threads
	// DispatchWorkers(WorkerThreadPool, NumWorkItems, Process); // TODO: make this a single funciton
	{
		SCOPED_CPU_MARKER("Process_DispatchWorkers");
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

			WorkerThreadPool.AddTask([=]() 
			{
				SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
				this->Process(iBegin, iEnd); 
			});
		}
	}

	// process the remaining work on this thread
	{
		SCOPED_CPU_MARKER("Process_ThisThread");
		const size_t& iBegin = vRanges.begin()->first;
		const size_t& iEnd   = vRanges.begin()->second; // inclusive
		this->Process(iBegin, iEnd);
	}

	// Sync point -------------------------------------------------
	{
		SCOPED_CPU_MARKER_C("BUSY_WAIT_WORKERS", 0xFFFF0000);
		while (WorkerThreadPool.GetNumActiveTasks() != 0); // busy-wait is bad...
	}
	// Sync point -------------------------------------------------

	return;
}

void FFrustumCullWorkerContext::Process(size_t iRangeBegin, size_t iRangeEnd)
{
	const size_t szFP = vFrustumPlanes.size();
	const size_t szBB = vBoundingBoxLists.size();
	assert(iRangeBegin <= szFP); // ensure work context bounds
	assert(iRangeEnd < szFP); // ensure work context bounds
	assert(iRangeBegin <= iRangeEnd); // ensure work context bounds
	
	// process each frustum
	for (size_t iWork = iRangeBegin; iWork <= iRangeEnd; ++iWork)
	{
		{
			{
				SCOPED_CPU_MARKER("Clear");
				vCulledBoundingBoxIndexListPerView[iWork].clear();
			}

			SCOPED_CPU_MARKER("CullFrustum");
			// process bounding box list per frustum
			for (size_t bb = 0; bb < vBoundingBoxLists[iWork].size(); ++bb)
			{
				if (IsBoundingBoxIntersectingFrustum(vFrustumPlanes[iWork], vBoundingBoxLists[iWork][bb]))
				{
					vCulledBoundingBoxIndexListPerView[iWork].push_back(bb); // grows as we go (no pre-alloc)
				}
			}
		}
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
void SceneBoundingBoxHierarchy::Build(const std::vector<GameObject*>& pObjects, ThreadPool& WorkerThreadPool)
{
	SCOPED_CPU_MARKER("BuildBoundingBoxHierarchy");
	this->ResizeGameObjectBoundingBoxContainer(pObjects.size());

#if BOUNDING_BOX_HIERARCHY__MULTI_THREADED_BUILD

	// dispatch gameobject bounding box workers
	{
		const size_t NumWorkItems = pObjects.size();
		const size_t NumWorkItemsPerAvailableWorkerThread = DIV_AND_ROUND_UP(NumWorkItems, WorkerThreadPool.GetThreadPoolSize());
		const size_t NumDesiredMinimumWorkItemsPerThread = 256;
		size_t NumWorkersToUse = WorkerThreadPool.GetThreadPoolSize(); // start with all threads

		// if per-thread work amount is too little, then use less threads
		if (NumWorkItemsPerAvailableWorkerThread < NumDesiredMinimumWorkItemsPerThread)
		{
			const float OffRatio = float(NumDesiredMinimumWorkItemsPerThread) / float(NumWorkItemsPerAvailableWorkerThread);
			NumWorkersToUse = static_cast<size_t>(NumWorkersToUse / OffRatio); // clamp down
			NumWorkersToUse = std::max((size_t)1, NumWorkersToUse);
		}

		const std::vector<std::pair<size_t, size_t>> vRanges = PartitionWorkItemsIntoRanges(NumWorkItems, NumWorkersToUse+1);
		
		// dispatch worker threads
		{
			SCOPED_CPU_MARKER("DispatchWorkers");
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

				WorkerThreadPool.AddTask([=]()
				{
					SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
					this->BuildGameObjectBoundingBoxes_Range(pObjects, iBegin, iEnd);
				});
			}
		}

		// this thread
		{
			this->BuildGameObjectBoundingBoxes_Range(pObjects, vRanges[0].first, vRanges[0].second);
		}
	}


	// dispatch mesh bounding box workers
	{
		using namespace std;
		//CountGameObjectMeshes(pObjects);
		
		// Sync point -------------------------------------------------
		{
			SCOPED_CPU_MARKER_C("BUSY_WAIT_WORKERS", 0xFFFF0000);
			while (WorkerThreadPool.GetNumActiveTasks() != 0); // busy-wait is bad...
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
			const size_t NumObjs = pObjects.size();
			const size_t ThreadBBBatchCapacity = mNumValidMeshBoundingBoxes / WorkerThreadPool.GetThreadPoolSize();

			size_t iBegin = 0;
			size_t iEnd = 0;
			size_t CurrBatchSize = 0;
			size_t iBB = 0;
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
		{
			SCOPED_CPU_MARKER("DispatchWorkers");
			for (size_t i = 0; i < vRanges.size() - 1; ++i)
			{
				WorkerThreadPool.AddTask([=]()
				{
					SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
					this->BuildMeshBoundingBoxes_Range(pObjects, get<0>(vRanges[i]), get<1>(vRanges[i]), get<2>(vRanges[i]));
				});
			}

		}
		{
			SCOPED_CPU_MARKER("Process_ThisThread");
			this->BuildMeshBoundingBoxes_Range(pObjects, get<0>(vRanges.back()), get<1>(vRanges.back()), get<2>(vRanges.back()));
		}
#else
		this->BuildMeshBoundingBoxes(pObjects);
#endif
	}
	
	// Sync point -------------------------------------------------
	{
		SCOPED_CPU_MARKER_C("BUSY_WAIT_WORKERS", 0xFFFF0000);
		while (WorkerThreadPool.GetNumActiveTasks() != 0); // busy-wait is bad...
	}

#else
	this->CountGameObjectMeshes(pObjects);
	this->ResizeGameMeshBoxContainer(mNumValidMeshBoundingBoxes);
	this->BuildGameObjectBoundingBoxes(pObjects);
	this->BuildMeshBoundingBoxes(pObjects);
#endif
}


void SceneBoundingBoxHierarchy::BuildGameObjectBoundingBox(const GameObject* pObj, size_t iBB)
{
	assert(pObj);
	Transform* const& pTF = mpTransforms.at(pObj->mTransformID);
	assert(pTF);


	// assumes static meshes: 
	// - no VB/IB change
	// - no dynamic vertex animations, morphing etc
	XMMATRIX matWorld = pTF->matWorldTransformation();
	mGameObjectBoundingBoxes[iBB] = CalculateAxisAlignedBoundingBox(matWorld, pObj->mLocalSpaceBoundingBox);
	mGameObjectBoundingBoxGameObjectPointerMapping[iBB] = pObj;
	
	const Model& model = mModels.at(pObj->mModelID);
	mGameObjectNumMeshes[iBB] = model.mData.mOpaueMeshIDs.size() + model.mData.mTransparentMeshIDs.size();
}
void SceneBoundingBoxHierarchy::BuildGameObjectBoundingBoxes(const std::vector<GameObject*>& pObjects)
{
	SCOPED_CPU_MARKER("BuildGameObjectBoundingBoxes");
	size_t iBB = 0;
	for (const GameObject* pObj : pObjects)
		BuildGameObjectBoundingBox(pObj, iBB++);
}
void SceneBoundingBoxHierarchy::BuildGameObjectBoundingBoxes_Range(const std::vector<GameObject*>& pObjects, size_t iBegin, size_t iEnd)
{
	SCOPED_CPU_MARKER("BuildGameObjectBoundingBoxes_Range");
	for (size_t i = iBegin; i <= iEnd; ++i)
	{
		BuildGameObjectBoundingBox(pObjects[i], i);
	}
}


void SceneBoundingBoxHierarchy::BuildMeshBoundingBox(const GameObject* pObj, size_t iBB_Begin, size_t iBB_End)
{
	assert(pObj);
	Transform* const& pTF = mpTransforms.at(pObj->mTransformID);
	assert(pTF);

	const Model& model = mModels.at(pObj->mModelID);

	XMMATRIX matWorld = pTF->matWorldTransformation();

	// assumes static meshes: 
	// - no VB/IB change
	// - no dynamic vertex animations, morphing etc
	bool bAtLeastOneMesh = false;
	size_t iMesh = iBB_Begin;
	auto fnProcessMeshes = [&](const std::vector<MeshID>& meshIDs)
	{
		for (MeshID mesh : meshIDs)
		{
			FBoundingBox AABB = CalculateAxisAlignedBoundingBox(matWorld, mMeshes.at(mesh).GetLocalSpaceBoundingBox());

			mMeshBoundingBoxes[iMesh] = std::move(AABB);
			mMeshBoundingBoxMeshIDMapping[iMesh] = mesh;
			mMeshBoundingBoxGameObjectPointerMapping[iMesh] = pObj;
			++iMesh;
			bAtLeastOneMesh = true;
		}
	};
	fnProcessMeshes(model.mData.mOpaueMeshIDs);
	fnProcessMeshes(model.mData.mTransparentMeshIDs);

	assert(bAtLeastOneMesh);
}
void SceneBoundingBoxHierarchy::BuildMeshBoundingBoxes(const std::vector<GameObject*>& pObjects)
{
	SCOPED_CPU_MARKER("BuildMeshBoundingBoxes");
	size_t i = 0;
	for (const GameObject* pObj : pObjects)
	{
		const Model& model = mModels.at(pObj->mModelID);
		const size_t NumMeshes = model.mData.mOpaueMeshIDs.size() + model.mData.mTransparentMeshIDs.size();
		BuildMeshBoundingBox(pObj, i, i + NumMeshes);
		i += NumMeshes;
	}
	
}
void SceneBoundingBoxHierarchy::BuildMeshBoundingBoxes_Range(const std::vector<GameObject*>& pObjects, size_t iBegin, size_t iEnd, size_t iMeshBB)
{
	SCOPED_CPU_MARKER("BuildMeshBoundingBoxes_Range");
	size_t iMeshBBOffset = 0;
	for (size_t i = iBegin; i<iEnd; ++i)
	{
		const Model& model = mModels.at(pObjects[i]->mModelID);
		const size_t NumMeshes = model.mData.mOpaueMeshIDs.size() + model.mData.mTransparentMeshIDs.size();
		BuildMeshBoundingBox(pObjects[i], iMeshBB + iMeshBBOffset, 0);
		iMeshBBOffset += NumMeshes;
	}
}

void SceneBoundingBoxHierarchy::Clear()
{
	mSceneBoundingBox = {};
	mGameObjectBoundingBoxes.clear();
	mMeshBoundingBoxes.clear();
	mMeshBoundingBoxMeshIDMapping.clear();
	mMeshBoundingBoxGameObjectPointerMapping.clear();
	mGameObjectBoundingBoxGameObjectPointerMapping.clear();
}

void SceneBoundingBoxHierarchy::ResizeGameObjectBoundingBoxContainer(size_t sz)
{
	SCOPED_CPU_MARKER("ResizeGameObjectBoundingBoxContainer");
	mGameObjectBoundingBoxes.resize(sz);
	mGameObjectBoundingBoxGameObjectPointerMapping.resize(sz);
	mGameObjectNumMeshes.resize(sz);
}

void SceneBoundingBoxHierarchy::CountGameObjectMeshes(const std::vector<GameObject*>& pObjects)
{
	SCOPED_CPU_MARKER("CountGameObjectMeshes");
	mNumValidMeshBoundingBoxes = 0;

	// count total number of meshes in all game objects
	for (const GameObject* pObj : pObjects)
	{
		const Model& model = mModels.at(pObj->mModelID);
		const size_t NumMeshes = model.mData.mOpaueMeshIDs.size() + model.mData.mTransparentMeshIDs.size();
		mNumValidMeshBoundingBoxes += NumMeshes;
	}
}

void SceneBoundingBoxHierarchy::ResizeGameMeshBoxContainer(size_t size)
{
	SCOPED_CPU_MARKER("BuildMeshBoundingBoxes_Rsz");
	mMeshBoundingBoxes.resize(size);
	mMeshBoundingBoxMeshIDMapping.resize(size);
	mMeshBoundingBoxGameObjectPointerMapping.resize(size);
}

