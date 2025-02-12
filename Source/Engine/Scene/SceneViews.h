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
#include "../PostProcess/PostProcess.h"
#include "../Core/RenderCommands.h"

#include "../Culling.h" // FFrustumCullWorkerContext


#define RENDER_INSTANCED_BOUNDING_BOXES 1
#define RENDER_INSTANCED_SHADOW_MESHES  1

// typedefs
using MeshLookup_t = std::unordered_map<MeshID, Mesh>;
using ModelLookup_t = std::unordered_map<ModelID, Model>;
using MaterialLookup_t = std::unordered_map<MaterialID, Material>;
#if RENDER_INSTANCED_SCENE_MESHES
using MeshRenderData_t = FInstancedMeshRenderData;
#else
using MeshRenderData_t = FMeshRenderData;
#endif

#if RENDER_INSTANCED_BOUNDING_BOXES
using BoundingBoxRenderData_t = FInstancedBoundingBoxRenderData;
#else
using BoundingBoxRenderData_t = FBoundingBoxRenderData;
#endif
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

struct FInstanceDataWriteParam { int iDraw, iInst; };
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

	VQ_SHADER_DATA::SceneLighting GPULightingData;

	FSceneRenderOptions sceneRenderOptions;
	FPostProcessParameters postProcessParameters;

	std::vector<MeshRenderData_t>  meshRenderParams;
	std::vector<FLightRenderData> lightRenderParams;
	std::vector<FLightRenderData> lightBoundsRenderParams;
	std::vector<FOutlineRenderData> outlineRenderParams;
	std::vector<MeshRenderData_t> debugVertexAxesRenderParams;
	std::vector<BoundingBoxRenderData_t> boundingBoxRenderParams;
	
#if RENDER_INSTANCED_SCENE_MESHES
	//--------------------------------------------------------------------------------------------------------------------------------------------
	// collect instance data based on Material, and then Mesh.
	// In order to avoid clear, we also keep track of the number 
	// of valid instance data.
	//--------------------------------------------------------------------------------------------------------------------------------------------
	struct FInstanceData { DirectX::XMMATRIX mWorld, mWorldViewProj, mWorldViewProjPrev, mNormal; int mObjID; float mProjectedArea; }; // transformation matrixes used in the shader
	struct FMeshInstanceDataArray { size_t NumValidData = 0; std::vector<FInstanceData> Data; };
	// MAT0
	// +---- PSO0                       MAT1       
	//     +----MESH0                 +----MESH37             
	//         +----LOD0                  +----LOD0                
	//             +----InstData0             +----InstData0                        
	//             +----InstData1             +----InstData1                        
	//          +----LOD1                     +----InstData2                
	//             +----InstData0      +----MESH225                        
	//             +----InstData1          +----LOD0                        
	// +---- PSO1
	//     +----MESH1                        +----InstData0             
	//         +----LOD0                     +----InstData1                
	//            +----InstData0          +----LOD1                        
	//            +----InstData1             +----InstData0                        
	//            +----InstData2             +----InstData1                        
	//     +----MESH2                              
	//         +----LOD0
	//            +----InstData0
	//

	// Bits[0  -  3] : LOD
	// Bits[4  - 33] : MeshID
	// Bits[34 - 34] : IsAlphaMasked (or opaque)
	// Bits[35 - 35] : IsTessellated
	static inline uint64 GetKey(MaterialID matID, MeshID meshID, int lod, /*UNUSED*/bool bTessellated)
	{
		assert(matID != -1);
		assert(meshID != -1);
		assert(lod >= 0 && lod < 16);
		constexpr int mask = 0x3FFFFFFF; // __11 1111 1111 1111 ... | use the first 30 bits of IDs
		uint64 hash = std::max(0, std::min(1 << 4, lod));
		hash |= ((uint64)(meshID & mask)) << 4;
		hash |= ((uint64)(matID  & mask)) << 34;
		return hash;
	}
	static inline MaterialID GetMatIDFromKey(uint64 key) { return MaterialID(key >> 34); }
	static inline MeshID     GetMeshIDFromKey(uint64 key) { return MeshID((key >> 4) & 0x3FFFFFFF); }
	static inline int        GetLODFromKey(uint64 key) { return int(key & 0xF); }
	std::unordered_map<uint64, FSceneView::FMeshInstanceDataArray> drawParamLookup;
	std::vector<FInstanceDataWriteParam> mRenderCmdInstanceDataWriteIndex; // drawParamLookup --> meshRenderParams
	//--------------------------------------------------------------------------------------------------------------------------------------------
#endif
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
	const std::vector<FFrustumCullWorkerContext::FCullResult>* pCullResults = nullptr;
	FShadowView* pShadowView = nullptr;
};
