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

#include "Mesh.h"
#include "Material.h"
#include "Model.h"
#include "Transform.h"
#include "Libs/VQUtils/Include/Multithreading/TaskSignal.h"
#include "Engine/Core/Memory.h"

// typedefs
using MeshLookup_t = std::unordered_map<MeshID, Mesh>;
using ModelLookup_t = std::unordered_map<ModelID, Model>;
using MaterialLookup_t = std::unordered_map<MaterialID, Material>;

struct Transform;
class Scene;


struct FRenderDebugOptions
{
	bool bForceLOD0_ShadowView = false;
	bool bForceLOD0_SceneView = false;
	bool bDrawLightBounds = false;
	bool bDrawMeshBoundingBoxes = false;
	bool bDrawGameObjectBoundingBoxes = false;
	bool bDrawLightMeshes = true;
	bool bDrawVertexLocalAxes = false;
	float fVertexLocalAxisSize = 1.0f;

	struct FMagnifierOptions
	{
		bool bEnable = false;
		float fMagnifierScreenRadius = 0.35f;
		float fMagnificationAmount = 6.0f;

		int ScreenOffsetX = 0;
		int ScreenOffsetY = 0;

		// lock
		bool bLockPosition = false;
		bool bLockPositionHistory = false;
		int LockedScreenPositionX = 0;
		int LockedScreenPositionY = 0;
		float BorderColorLocked[3] = { 0.002f, 0.52f, 0.0f }; // G
		float BorderColorFree[3] = { 0.72f, 0.002f, 0.0f };   // R
		inline void GetBorderColor(float* pOut) const { memcpy(pOut, bLockPosition ? BorderColorLocked : BorderColorFree, sizeof(float) * 3); }
		inline void ToggleLock(int MousePosX, int MousePosY)
		{
			if (!bEnable)
				return;
			bLockPositionHistory = bLockPosition; // record histroy
			bLockPosition = !bLockPosition; // flip state
			const bool bLockSwitchedOn = !this->bLockPositionHistory && bLockPosition;
			const bool bLockSwitchedOff = this->bLockPositionHistory && !bLockPosition;
			if (bLockSwitchedOn)
			{
				LockedScreenPositionX = MousePosX;
				LockedScreenPositionY = MousePosY;
			}
		}
	} Magnifier;
};
struct FSceneLightingOptions
{
	float fYawSliderValue = 0.0f; // env map
	float fAmbientLightingFactor = 0.055f;
	bool bScreenSpaceAO = true;
};
struct FSceneRenderOptions // these options directly affect scene data gathering
{
	FRenderDebugOptions Debug;
	FSceneLightingOptions Lighting;
};


struct FVisibleMeshSortData
{
	int32 iBB;
	float fBBArea;
	int32 matID;
	int32 meshID;
	uint8 bTess;
	uint8 iLOD;
};
struct alignas(16) FPerInstanceData // that fits in a cache line.
{
	size_t hGameObject;
	float fBBArea;
};
struct alignas(16) FPerDrawData // that fits in a cache line.
{
	MaterialID hMaterial;
	MeshID hMesh;
	std::pair<BufferID, BufferID> VBIB;
	unsigned NumIndices;
	short SelectedLOD;
};
struct FVisibleMeshDataSoA
{
	const MemoryPool<Material>* pMaterialPool = nullptr;

	std::vector<uint64> SortKey;
	std::vector<FPerDrawData> PerDrawData;
	std::vector<Transform> Transform;
	std::vector<FPerInstanceData> PerInstanceData;
	std::vector<MaterialID> MaterialID;
	size_t NumValidElements;
	inline void Reserve(size_t sz)
	{
		if (NumValidElements < sz)
		{
			SortKey.resize(sz);
			PerDrawData.resize(sz);
			Transform.resize(sz);
			PerInstanceData.resize(sz);
			MaterialID.resize(sz);
		}
		NumValidElements = sz;
	}
	inline void Clear()
	{
		NumValidElements = 0;
		SortKey.clear();
		PerDrawData.clear();
		PerInstanceData.clear();
		Transform.clear();
		MaterialID.clear();
	}
	inline void ResetValidElements() { NumValidElements = 0; }
	size_t Size() const { return NumValidElements; }
};

struct FFrustumRenderList
{
	mutable TaskSignal<void> BatchDoneSignal;
	mutable TaskSignal<void> DataReadySignal;
	mutable TaskSignal<size_t> DataCountReadySignal;
	FVisibleMeshDataSoA Data;

	enum class EFrustumType { MainView, SpotShadow, PointShadow, DirectionalShadow };
	EFrustumType Type = EFrustumType::MainView;
	uint TypeIndex = 0; // e.g., spot light index, point light index * 6 + face, etc.
	const void* pViewData = nullptr; // references SceneView or ShadowView(=matShadowViewProj) based on EFrustumType

	inline void ResetSignalsAndData()
	{
		Data.ResetValidElements();
		DataReadySignal.Reset();
		DataCountReadySignal.Reset();
		BatchDoneSignal.Reset();
	}
};

struct FSceneView
{
	DirectX::XMMATRIX     viewProj;
	DirectX::XMMATRIX     viewProjPrev;
	DirectX::XMMATRIX     view;
	DirectX::XMMATRIX     viewInverse;
	DirectX::XMMATRIX     proj;
	DirectX::XMMATRIX     projInverse;
	DirectX::XMMATRIX     directionalLightProjection;
	DirectX::XMVECTOR     cameraPosition;
	float                 MainViewCameraYaw = 0.0f;
	float                 MainViewCameraPitch = 0.0f;
	float                 HDRIYawOffset = 0.0f;
	DirectX::XMMATRIX     EnvironmentMapViewProj;
	const Mesh*           pEnvironmentMapMesh = nullptr;
	float                 DeltaTimeInSeconds = 0.0f;

	uint NumGameObjectBBRenderCmds = 0;
	uint NumMeshBBRenderCmds = 0;
	BufferID cubeVB = INVALID_ID;
	BufferID cubeIB = INVALID_ID;
	const std::vector<FBoundingBox>* pGameObjectBoundingBoxList = nullptr;
	const std::vector<FBoundingBox>* pMeshBoundingBoxList = nullptr;

	// Sent to renderer for instance data batching.
	// Renderer uses FSceneDrawData in DrawData.h to fill in batched draw parameters.
	std::vector<FFrustumRenderList> FrustumRenderLists;
	// Culled frustums are not removed from the vector so we track the active ones here
	uint NumActiveFrustumRenderLists = 0; 

	VQ_SHADER_DATA::SceneLighting GPULightingData;

	FSceneRenderOptions sceneRenderOptions;
	int iMousePosX = 0;
	int iMousePosY = 0;
};

struct FSceneDebugView
{

};

struct FSceneShadowViews
{
	struct FPointLightLinearDepthParams
	{
		DirectX::XMFLOAT3 vWorldPos;
		float fFarPlane;
	};

	std::array<DirectX::XMMATRIX, NUM_SHADOWING_LIGHTS__SPOT>                   ShadowViews_Spot;
	std::array<DirectX::XMMATRIX, NUM_SHADOWING_LIGHTS__POINT * 6>              ShadowViews_Point;
	std::array<FPointLightLinearDepthParams, NUM_SHADOWING_LIGHTS__POINT> PointLightLinearDepthParams;
	DirectX::XMMATRIX ShadowView_Directional;

	uint NumSpotShadowViews;
	uint NumPointShadowViews;
	uint NumDirectionalViews;
};

struct FFrustumRenderCommandRecorderContext
{
	const FFrustumRenderList* pFrustumRenderList = nullptr;
	const DirectX::XMMATRIX* pMatShadowViewProj = nullptr;
};
