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

using namespace DirectX;

//------------------------------------------------------------------------------------------------------------------------------
//
// CULLING FUNCTIONS
//
//------------------------------------------------------------------------------------------------------------------------------
bool IsSphereIntersectingFurstum(const FFrustumPlaneset& FrustumPlanes, const FSphere& Sphere)
{
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
	return true; // TODO: remove
	return bIntersecting; 
}

bool IsBoundingBoxIntersectingFrustum(const FFrustumPlaneset& FrustumPlanes, const FBoundingBox& BBox)
{
	constexpr float EPSILON = 0.000002f;

	// this is a hotspot: GetCornerPointsV4() creating the bounding box on the stack may slow it down.
	//                    TODO: test with a pre-generated set of corners instead of doing it on the fly.
	const std::array<XMFLOAT4, 8> vPoints = BBox.GetCornerPointsV4();

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
	assert(false); // not done yet
	return true;
}



//------------------------------------------------------------------------------------------------------------------------------
//
// THREADING
//
//------------------------------------------------------------------------------------------------------------------------------
size_t FFrustumCullWorkerContext::AddWorkerItem(FFrustumPlaneset&& FrustumPlaneSet, std::vector<FBoundingBox> vBoundingBoxList, const std::vector<const GameObject*>& pGameObjects)
{
	vFrustumPlanes.emplace_back(FrustumPlaneSet);
	vBoundingBoxLists.push_back(vBoundingBoxList);
	vGameObjectPointerLists.push_back(pGameObjects);
	assert(vFrustumPlanes.size() == vBoundingBoxLists.size());
	return vFrustumPlanes.size() - 1;
}

void FFrustumCullWorkerContext::Process(size_t iRangeBegin, size_t iRangeEnd)
{
	const size_t szFP = vFrustumPlanes.size();
	const size_t szBB = vBoundingBoxLists.size();
	assert(szFP == szBB);
	assert(iRangeBegin <= szFP);
	assert(iRangeEnd < szFP);
	assert(iRangeBegin <= iRangeEnd);

	// allocate context memory
	vCulledBoundingBoxIndexLists.resize(szFP);
	
	// process each frustum
	for (size_t iWork = iRangeBegin; iWork <= iRangeEnd; ++iWork)
	{
		// process bounding box list per frustum
		for (size_t bb = 0; bb < vBoundingBoxLists[iWork].size(); ++bb)
		{
			if (IsBoundingBoxIntersectingFrustum(vFrustumPlanes[iWork], vBoundingBoxLists[iWork][bb]))
			{
				vCulledBoundingBoxIndexLists[iWork].push_back(bb); // grows as we go (no pre-alloc)
			}
		}
	}
}

std::array<DirectX::XMFLOAT4, 8> FBoundingBox::GetCornerPointsV4() const
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

std::array<DirectX::XMFLOAT3, 8> FBoundingBox::GetCornerPointsV3() const
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



static FBoundingBox GetAxisAligned(const FBoundingBox& WorldBoundingBox)
{
	constexpr float max_f = std::numeric_limits<float>::max();
	constexpr float min_f = -(max_f - 1.0f);

	XMFLOAT3 mins = XMFLOAT3(max_f, max_f, max_f);
	XMFLOAT3 maxs = XMFLOAT3(min_f, min_f, min_f);
	XMVECTOR vMins = XMLoadFloat3(&mins);
	XMVECTOR vMaxs = XMLoadFloat3(&maxs);

	FBoundingBox AABB;
	const std::array<XMFLOAT3, 8> vPoints = WorldBoundingBox.GetCornerPointsV3();
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
static FBoundingBox CalculateAxisAlignedBoundingBox(const XMMATRIX& MWorld, const FBoundingBox& LocalSpaceAxisAlignedBoundingBox)
{
	const FBoundingBox& bb = LocalSpaceAxisAlignedBoundingBox; // shorthand

	// transformed local space bounding box, no longer necessarily axis-aligned
	FBoundingBox WorldTransformedBB;
	XMVECTOR vMin = XMLoadFloat3(&bb.ExtentMin);
	XMVECTOR vMax = XMLoadFloat3(&bb.ExtentMax);
	vMin = XMVector3Transform(vMin, MWorld);
	vMax = XMVector3Transform(vMax, MWorld);
	XMStoreFloat3(&WorldTransformedBB.ExtentMin, vMin);
	XMStoreFloat3(&WorldTransformedBB.ExtentMax, vMax);

	return GetAxisAligned(WorldTransformedBB);
}

#include "Scene/Scene.h"

void SceneBoundingBoxHierarchy::Build(const std::vector<GameObject*>& pObjects)
{
	mGameObjectBoundingBoxes.clear();
	
	mMeshBoundingBoxes.clear();
	mMeshBoundingBoxMeshIDMapping.clear();
	mMeshBoundingBoxGameObjectPointerMapping.clear();


	// Note: This'll gather all the mesh bounding boxes regardless of their
	//       model's visibilitiy. A coarse culling (gameobject-level) could be
	//       done first to determine which mesh bounding boxes should be culled 
	//       for optimizing this further.
	constexpr bool GATHER_MESH_BOUNDING_BOXES = true;

	// process each game object
	for (const GameObject* pObj : pObjects)
	{
		assert(pObj);

		Transform* const& pTF = mpTransforms.at(pObj->mTransformID);
		assert(pTF);

		const Model& model = mModels.at(pObj->mModelID);

		XMMATRIX matWorld = pTF->WorldTransformationMatrix();

		if constexpr (GATHER_MESH_BOUNDING_BOXES)
		{
			bool bAtLeastOneMesh = false;
			for (MeshID mesh : model.mData.mOpaueMeshIDs)
			{
				FBoundingBox AABB = CalculateAxisAlignedBoundingBox(matWorld, mMeshes.at(mesh).GetLocalSpaceBoundingBox());
				mMeshBoundingBoxes.push_back(AABB);
				mMeshBoundingBoxMeshIDMapping.push_back(mesh);
				mMeshBoundingBoxGameObjectPointerMapping.push_back(pObj);
				bAtLeastOneMesh = true;
			}
			for (MeshID mesh : model.mData.mTransparentMeshIDs)
			{
				FBoundingBox AABB = CalculateAxisAlignedBoundingBox(matWorld, mMeshes.at(mesh).GetLocalSpaceBoundingBox());
				mMeshBoundingBoxes.push_back(AABB);
				mMeshBoundingBoxMeshIDMapping.push_back(mesh);
				mMeshBoundingBoxGameObjectPointerMapping.push_back(pObj);
				bAtLeastOneMesh = true;
			}

			assert(bAtLeastOneMesh);
		}

		FBoundingBox AABB_Obj = CalculateAxisAlignedBoundingBox(matWorld, pObj->mLocalSpaceBoundingBox);
		mGameObjectBoundingBoxes.push_back(AABB_Obj);
		mGameObjectBoundingBoxGameObjectPointerMapping.push_back(pObj);
	}
}

void SceneBoundingBoxHierarchy::Clear()
{
	mSceneBoundingBox = {};
	mGameObjectBoundingBoxes.clear();
	mMeshBoundingBoxes.clear();
	mMeshBoundingBoxMeshIDMapping.clear();
}
