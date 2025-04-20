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
#include "Engine/PostProcess/PostProcess.h"
#include "Libs/VQUtils/Source/Multithreading.h" // TaskSignal
#include "Engine/Core/Memory.h"

// typedefs
using MeshLookup_t = std::unordered_map<MeshID, Mesh>;
using ModelLookup_t = std::unordered_map<ModelID, Model>;
using MaterialLookup_t = std::unordered_map<MaterialID, Material>;

struct Transform;
class Scene;


struct FSceneRenderOptions
{
	struct FFFX_SSSR_UIOptions
	{
		bool    bEnableTemporalVarianceGuidedTracing = true;
		int     maxTraversalIterations = 128;
		int     mostDetailedDepthHierarchyMipLevel = 0;
		int     minTraversalOccupancy = 4;
		float   depthBufferThickness = 0.45f;
		float   roughnessThreshold = 0.2f;
		float   temporalStability = 0.25f;
		float   temporalVarianceThreshold = 0.0f;
		int     samplesPerQuad = 1;
	};

	bool bForceLOD0_ShadowView = false;
	bool bForceLOD0_SceneView = false;
	bool bDrawLightBounds = false;
	bool bDrawMeshBoundingBoxes = false;
	bool bDrawGameObjectBoundingBoxes = false;
	bool bDrawLightMeshes = true;
	bool bDrawVertexLocalAxes = false;
	float fVertexLocalAxixSize = 1.0f;
	float fYawSliderValue = 0.0f;
	float fAmbientLightingFactor = 0.055f;
	bool bScreenSpaceAO = true;
	FFFX_SSSR_UIOptions FFX_SSSRParameters = {};
	DirectX::XMFLOAT4 OutlineColor = DirectX::XMFLOAT4(1.0f, 0.647f, 0.1f, 1.0f);
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
	int                   SceneRTWidth = 0;
	int                   SceneRTHeight = 0;

	size_t NumGameObjectBBRenderCmds = 0;
	size_t NumMeshBBRenderCmds = 0;
	BufferID cubeVB = INVALID_ID;
	BufferID cubeIB = INVALID_ID;
	const std::vector<FBoundingBox>* pGameObjectBoundingBoxList = nullptr;
	const std::vector<FBoundingBox>* pMeshBoundingBoxList = nullptr;

	// Sent to renderer for instance data batching.
	// Renderer uses FSceneDrawData in DrawData.h to fill in batched draw parameters.
	std::vector<FFrustumRenderList> FrustumRenderLists;
	// Culled frustums are not removed from the vector so we track the active ones here
	size_t NumActiveFrustumRenderLists = 0; 


	VQ_SHADER_DATA::SceneLighting GPULightingData;

	FSceneRenderOptions sceneRenderOptions;
	FPostProcessParameters postProcessParameters;
	bool bAppIsInSimulationState = false;
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
