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
#include "Libs/VQUtils/Source/Multithreading.h"
#include "Renderer/Rendering/DrawData.h" // TODO: remove after shadow mesh drawing is migrated to renderer


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

struct FVisibleMeshData
{
	Transform Transform;// store a copy
	Material Material;  // store a copy
	MeshID hMesh;
	MaterialID hMaterial;
	size_t hGameObject;
	float fBBArea;
	int SelectedLOD;
	std::pair<BufferID, BufferID> VBIB;
	unsigned NumIndices;
	char bTessellated;
};

struct FViewRef
{
	enum EViewType { Scene, Shadow };
	void* pViewData;
	EViewType eViewType;
};

struct FFrustumRenderList
{
	TaskSignal<void> DataReadySignal;
	std::vector<FVisibleMeshData> Data;
	FViewRef ViewRef; // references SceneView or ShadowView
	inline void Reset()
	{
		Data.clear();
		DataReadySignal.Reset();
	}
};

struct FSceneView
{
	DirectX::XMMATRIX     view;
	DirectX::XMMATRIX     viewProj;
	DirectX::XMMATRIX     viewProjPrev;
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
	//bool                  bIsPBRLightingUsed;
	//bool                  bIsDeferredRendering;
	//bool                  bIsIBLEnabled;
	//Settings::SceneRender sceneRenderSettings;
	//EnvironmentMap	environmentMap;
	bool                  bAppIsInSimulationState = false;
	std::vector<FFrustumRenderList> FrustumRenderLists;

	VQ_SHADER_DATA::SceneLighting GPULightingData;

	FSceneRenderOptions sceneRenderOptions;
	FPostProcessParameters postProcessParameters;
};



struct FShadowView
{
	DirectX::XMMATRIX matViewProj;
#if RENDER_INSTANCED_SHADOW_MESHES
	//--------------------------------------------------------------------------------------------------------------------------------------------
	//  +----SHADOW_MESH0             +----SHADOW_MESH1             
	//     +----LOD0                     +----LOD0        
	//         +----ShadowInstData0          +----ShadowInstData0                       
	//         +----ShadowInstData1          +----ShadowInstData1                       
	//     +----LOD1                         +----ShadowInstData2        
	//         +----ShadowInstData0                        
	//         +----ShadowInstData1                        
	struct FInstanceData { DirectX::XMMATRIX matWorld, matWorldViewProj; float fDisplacement; };
	struct FInstanceDataArray { size_t NumValidData = 0; std::vector<FInstanceData> Data; };
		
	// Bits[0  -  3] : LOD
	// Bits[4  - 33] : MeshID
	// Bits[34 - 63] : MaterialID
	static inline uint64 GetKey(MaterialID matID, MeshID meshID, int lod, bool bTessellated)
	{
		assert(matID != -1); assert(meshID != -1); assert(lod >= 0 && lod < 16);

		constexpr int mask = 0x3FFFFFFF; // __11 1111 1111 1111 ...| use the first 30 bits of IDs
		uint64 hash = std::max(0, std::min(1 << 4, lod));
		hash |= ((uint64)(meshID & mask)) << 4;
		if (bTessellated)
		{
			hash |= ((uint64)(matID & mask)) << 34;
		}
		return hash;
	}
	static inline MaterialID GetMatIDFromKey(uint64 key) { return MaterialID(key >> 34); }
	static inline MeshID     GetMeshIDFromKey(uint64 key) { return MeshID((key >> 4) & 0x3FFFFFFF); }
	static inline int        GetLODFromKey(uint64 key) { return int(key & 0xF); }
	std::unordered_map<uint64, FInstanceDataArray> drawParamLookup;
	std::vector<FInstanceDataWriteParam> mRenderCmdInstanceDataWriteIndex; // drawParamLookup --> meshRenderParams
	////--------------------------------------------------------------------------------------------------------------------------------------------
	std::vector<FInstancedShadowMeshRenderData> meshRenderParams; // per LOD mesh
#else
	std::vector<FShadowMeshRenderData> meshRenderParams;
#endif
};

struct FSceneShadowViews
{
	struct FPointLightLinearDepthParams
	{
		float fFarPlane;
		DirectX::XMFLOAT3 vWorldPos;
	};

	std::array<FShadowView, NUM_SHADOWING_LIGHTS__SPOT>                   ShadowViews_Spot;
	std::array<FShadowView, NUM_SHADOWING_LIGHTS__POINT * 6>              ShadowViews_Point;
	std::array<FPointLightLinearDepthParams, NUM_SHADOWING_LIGHTS__POINT> PointLightLinearDepthParams;
	FShadowView ShadowView_Directional;

	uint NumSpotShadowViews;
	uint NumPointShadowViews;
};

struct FFrustumRenderCommandRecorderContext
{
	size_t iFrustum;
	const std::vector<FVisibleMeshData>* pCullResults = nullptr;
	FShadowView* pShadowView = nullptr;
};
