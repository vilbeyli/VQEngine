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

// TODO: move all instanced definitions into a single loation (the other is in LightConstantBufferData.h)
#define RENDER_INSTANCED_BOUNDING_BOXES 1
#define RENDER_INSTANCED_SHADOW_MESHES  1


#include "Camera.h"
#include "Mesh.h"
#include "Material.h"
#include "Model.h"
#include "Light.h"
#include "Transform.h"
#include "GameObject.h"
#include "Serialization.h"

#include "../Core/Memory.h"
#include "../Core/RenderCommands.h"
#include "../AssetLoader.h"
#include "../PostProcess/PostProcess.h"

#include <algorithm>

#if RENDER_INSTANCED_SCENE_MESHES
using MeshRenderCommand_t = FInstancedMeshRenderCommand;
#else
using MeshRenderCommand_t = FMeshRenderCommand;
#endif

#if RENDER_INSTANCED_BOUNDING_BOXES
using BoundingBoxRenderCommand_t = FInstancedBoundingBoxRenderCommand;
#else
using BoundingBoxRenderCommand_t = FBoundingBoxRenderCommand;
#endif




// fwd decl
class Input;
class ObjectIDPass;
struct Material;
struct FResourceNames;
struct FFrustumPlaneset;
struct FUIState;
class Window;

// typedefs
using MeshLookup_t     = std::unordered_map<MeshID, Mesh>;
using ModelLookup_t    = std::unordered_map<ModelID, Model>;
using MaterialLookup_t = std::unordered_map<MaterialID, Material>;


//--- Pass Parameters ---
struct FPostProcessParameters;
struct FSceneRenderParameters
{
	struct FFFX_SSSR_UIParameters
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
	FFFX_SSSR_UIParameters FFX_SSSRParameters = {};
	DirectX::XMFLOAT4 OutlineColor = DirectX::XMFLOAT4(1.0f, 0.647f, 0.1f, 1.0f);
};
//--- Pass Parameters ---

namespace InstanceBatching
{
	inline int GetLODFromProjectedScreenArea(float fArea, int NumMaxLODs)
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

}

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
	int                   SceneRTWidth = 0;
	int                   SceneRTHeight = 0;
	//bool                  bIsPBRLightingUsed;
	//bool                  bIsDeferredRendering;
	//bool                  bIsIBLEnabled;
	//Settings::SceneRender sceneRenderSettings;
	//EnvironmentMap	environmentMap;

	VQ_SHADER_DATA::SceneLighting GPULightingData;

	FSceneRenderParameters sceneParameters;
	FPostProcessParameters postProcessParameters;

	std::vector<MeshRenderCommand_t>  meshRenderCommands;
	std::vector<FLightRenderCommand> lightRenderCommands;
	std::vector<FLightRenderCommand> lightBoundsRenderCommands;
	std::vector<FOutlineRenderCommand> outlineRenderCommands;
	std::vector<MeshRenderCommand_t> debugVertexAxesRenderCommands;
	std::vector<BoundingBoxRenderCommand_t> boundingBoxRenderCommands;

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
	std::vector<FInstanceDataWriteParam> mRenderCmdInstanceDataWriteIndex; // drawParamLookup --> meshRenderCommands
	//--------------------------------------------------------------------------------------------------------------------------------------------
#endif
};
struct FSceneShadowViews
{
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
		std::vector<FInstanceDataWriteParam> mRenderCmdInstanceDataWriteIndex; // drawParamLookup --> meshRenderCommands
		////--------------------------------------------------------------------------------------------------------------------------------------------
		std::vector<FInstancedShadowMeshRenderCommand> meshRenderCommands; // per LOD mesh
#else
		std::vector<FShadowMeshRenderCommand> meshRenderCommands;
#endif
	};
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

struct FSceneStats
{
	// lights -----------------------
	uint NumDirectionalLights;
	uint NumStaticLights;
	uint NumDynamicLights;
	uint NumStationaryLights;
	uint NumSpotLights;
	uint NumPointLights;
	uint NumDisabledSpotLights;
	uint NumDisabledPointLights;
	uint NumDisabledDirectionalLights;
	uint NumShadowingPointLights;
	uint NumShadowingSpotLights;

	// render cmds ------------------
	uint NumMeshRenderCommands;
	uint NumShadowMeshRenderCommands;
	uint NumBoundingBoxRenderCommands;

	// scene ------------------------
	uint NumMeshes;
	uint NumModels;
	uint NumMaterials;
	uint NumObjects;
	uint NumCameras;
};

// For the time being, this is simply a flat list of bounding boxes -- there is not much of a hierarchy to speak of.
class SceneBoundingBoxHierarchy
{
public:
	SceneBoundingBoxHierarchy(
		const MeshLookup_t& Meshes
		, const ModelLookup_t& Models
		, const MaterialLookup_t& Materials
		, const std::vector<size_t>& TransformHandles
	)
		: mMeshes(Meshes)
		, mModels(Models)
		, mMaterials(Materials)
		, mTransformHandles(TransformHandles)
	{}
	SceneBoundingBoxHierarchy() = delete;

	void Build(const Scene* pScene, const std::vector<size_t>& GameObjectHandles, ThreadPool& UpdateWorkerThreadPool);
	void Clear();
	void ResizeGameObjectBoundingBoxContainer(size_t sz);

	const std::vector<int>& GetNumMeshesLODs() const { return mNumMeshLODs; }
	const std::vector<MeshID>& GetMeshesIDs() const { return mMeshIDs; }
	const std::vector<MaterialID>& GetMeshMaterialIDs() const { return mMeshMaterials; }
	const std::vector<const Transform*>& GetMeshTransforms() const { return mMeshTransforms; }
	const std::vector<size_t>& GetMeshGameObjectHandles() const { return mMeshGameObjectHandles; }

private:
	void ResizeGameMeshBoxContainer(size_t size);

	void BuildGameObjectBoundingSpheres(const std::vector<size_t>& GameObjectHandles);
	void BuildGameObjectBoundingSpheres_Range(const std::vector<size_t>& GameObjectHandles, size_t iBegin, size_t iEnd);

	void BuildGameObjectBoundingBox(const Scene* pScene, size_t ObjectHandle, size_t iBB);
	void BuildGameObjectBoundingBoxes(const Scene* pScene, const std::vector<size_t>& GameObjectHandles);
	void BuildGameObjectBoundingBoxes_Range(const Scene* pScene, const std::vector<size_t>& GameObjectHandles, size_t iBegin, size_t iEnd);

	void BuildMeshBoundingBox(const Scene* pScene, size_t ObjectHandle, size_t iBB_Begin, size_t iBB_End);
	void BuildMeshBoundingBoxes(const Scene* pScene, const std::vector<size_t>& GameObjectHandles);
	void BuildMeshBoundingBoxes_Range(const Scene* pScene, const std::vector<size_t>& GameObjectHandles, size_t iBegin, size_t iEnd, size_t iMeshBB);

private:
	friend class Scene;
	FBoundingBox mSceneBoundingBox;

	// list of game object bounding boxes for coarse culling
	//------------------------------------------------------
	std::vector<FBoundingBox>      mGameObjectBoundingBoxes;
	std::vector<size_t>            mGameObjectHandles;
	std::vector<size_t>            mGameObjectNumMeshes;
	//------------------------------------------------------

	// list of mesh bounding boxes for fine culling
	//------------------------------------------------------
	// these are same size containers, mapping bounding boxes to gameobjects, meshIDs, etc.
	size_t mNumValidMeshBoundingBoxes = 0;
	std::vector<FBoundingBox>      mMeshBoundingBoxes;
	std::vector<MeshID>            mMeshIDs;
	std::vector<int>               mNumMeshLODs;
	std::vector<MaterialID>        mMeshMaterials;
	std::vector<const Transform*>  mMeshTransforms;
	std::vector<size_t>            mMeshGameObjectHandles;
	//------------------------------------------------------

	// scene data container references
	const MeshLookup_t& mMeshes;
	const ModelLookup_t& mModels;
	const MaterialLookup_t& mMaterials;
	const std::vector<size_t>& mTransformHandles;
};

//------------------------------------------------------


struct FFrustumRenderCommandRecorderContext
{
	size_t iFrustum;
	const std::vector<FFrustumCullWorkerContext::FCullResult>* pCullResults = nullptr;
	FSceneShadowViews::FShadowView* pShadowView = nullptr;
};

constexpr size_t NUM_GAMEOBJECT_POOL_SIZE = 1024 * 64;
constexpr size_t GAMEOBJECT_BYTE_ALIGNMENT = 64; // assumed typical cache-line size

//----------------------------------------------------------------------------------------------------------------
// https://en.wikipedia.org/wiki/Template_method_pattern
// https://stackoverflow.com/questions/9724371/force-calling-base-class-virtual-function
// https://isocpp.org/wiki/faq/strange-inheritance#two-strategies-for-virtuals
// template method seems like a good idea here:
//   The base class takes care of the common tasks among all scenes and calls the 
//   customized functions of the derived classes through pure virtual functions
// In other words, the particular scene implementations will have to override those functions
// so that each scene can have custom logic as desired. Similar to how you override those functions in Unity3D/C#.
//----------------------------------------------------------------------------------------------------------------
class Scene
{
	// Engine has easy access to the scene as scene is essentially a part of the engine.
	friend class VQEngine; 
	friend class AssetLoader;

//----------------------------------------------------------------------------------------------------------------
// SCENE INTERFACE
//----------------------------------------------------------------------------------------------------------------
protected:
	// Scene-specific loading logic goes here. 
	// LoadScene() is called right before loading begins.
	//
	virtual void LoadScene(FSceneRepresentation& scene) = 0;

	// InitializeScene() is called after the scene data is loaded from the disk.
	// 
	virtual void InitializeScene() = 0;

	// Update() is called each frame before Engine::Render(). Scene-specific update logic goes here.
	//
	virtual void UpdateScene(float dt, FSceneView& view) = 0;

	// Scene-specific unloading logic goes here
	//
	virtual void UnloadScene() = 0;

	// Each scene has to implement scene-specific RenderUI() function. 
	// RenderUI() is called after post processing is finished and it is 
	// the last rendering workload before presenting the frame.
	//
	virtual void RenderSceneUI() const = 0;

//----------------------------------------------------------------------------------------------------------------
// ENGINE INTERFACE
//----------------------------------------------------------------------------------------------------------------
private: // Derived Scenes shouldn't access these functions
	void PreUpdate(int FRAME_DATA_INDEX, int FRAME_DATA_PREV_INDEX);
	void Update(float dt, int FRAME_DATA_INDEX = 0);
	void PostUpdate(ThreadPool& UpdateWorkerThreadPool, const FUIState& UIState, int FRAME_DATA_INDEX = 0);
	
	void StartLoading(const BuiltinMeshArray_t& builtinMeshes, FSceneRepresentation& scene, ThreadPool& UpdateWorkerThreadPool);
	void OnLoadComplete();
	void Unload(); // serial-only for now. maybe MT later.
	
	void RenderUI(FUIState& UIState, uint32_t W, uint32_t H);
	void HandleInput(FSceneView& SceneView);
	void PickObject(const ObjectIDPass& ObjectIDRenderPass, int MouseClickPositionX, int MouseClickPositionY);

	void GatherSceneLightData(FSceneView& SceneView) const;
	void GatherShadowViewData(FSceneShadowViews& SceneShadowView
		, const std::vector<Light>& vLights
		, const std::vector<size_t>& vActiveLightIndices
	);

	void RecordRenderLightMeshCommands(FSceneView& SceneView) const;
	void BatchInstanceData_BoundingBox(FSceneView& SceneView
		, ThreadPool& UpdateWorkerThreadPool
		, const DirectX::XMMATRIX matViewProj
	) const;

	void GatherFrustumCullParameters(const FSceneView& SceneView, FSceneShadowViews& SceneShadowView, ThreadPool& UpdateWorkerThreadPool);
	void CullFrustums(const FSceneView& SceneView, ThreadPool& UpdateWorkerThreadPool);
	void BatchInstanceData(FSceneView& SceneView, ThreadPool& UpdateWorkerThreadPool);

	void BuildGameObject(const FGameObjectRepresentation& rep, size_t iObj);
	
	void LoadBuiltinMaterials(TaskID taskID, const std::vector<FGameObjectRepresentation>& GameObjsToBeLoaded);
	void LoadBuiltinMeshes(const BuiltinMeshArray_t& builtinMeshes);
	void LoadGameObjects(std::vector<FGameObjectRepresentation>&& GameObjects, ThreadPool& WorkerThreadPool); // TODO: consider using FSceneRepresentation as the parameter and read the corresponding member
	void LoadSceneMaterials(const std::vector<FMaterialRepresentation>& Materials, TaskID taskID);
	void LoadLights(const std::vector<Light>& SceneLights);
	void LoadCameras(std::vector<FCameraParameters>& CameraParams);
	void LoadPostProcessSettings();

	void CalculateGameObjectLocalSpaceBoundingBoxes();

public:
	Scene(VQEngine& engine
		, int NumFrameBuffers
		, const Input& input
		, const std::unique_ptr<Window>& pWin
		, VQRenderer& renderer
	);

	      FSceneView&       GetSceneView (int FRAME_DATA_INDEX);
	const FSceneView&       GetSceneView (int FRAME_DATA_INDEX) const;
	const FSceneShadowViews& GetShadowView(int FRAME_DATA_INDEX) const;
	      FPostProcessParameters& GetPostProcessParameters(int FRAME_DATA_INDEX);
	const FPostProcessParameters& GetPostProcessParameters(int FRAME_DATA_INDEX) const ;

	inline const Camera& GetActiveCamera() const { return mCameras[mIndex_SelectedCamera]; }
	inline       Camera& GetActiveCamera() { return mCameras[mIndex_SelectedCamera]; }
	inline       size_t  GetNumSceneCameras() const { return mCameras.size(); }

	inline       int&    GetActiveCameraIndex() { return mIndex_SelectedCamera; }
	inline       int&    GetActiveEnvironmentMapPresetIndex() { return mIndex_ActiveEnvironmentMapPreset; }

	// Mesh, Model, GameObj management
	//TransformID CreateTransform(Transform** ppTransform);
	//GameObject* CreateObject(TransformID tfID, ModelID modelID);
	MeshID      AddMesh(Mesh&& mesh);
	MeshID      AddMesh(const Mesh& mesh);
	ModelID     CreateModel();
	MaterialID  CreateMaterial(const std::string& UniqueMaterialName);
	MaterialID  LoadMaterial(const FMaterialRepresentation& matRep, TaskID taskID);

	const std::vector<FMaterialRepresentation>& GetMaterialRepresentations() const { return mSceneRepresentation.Materials; }
	const std::string& GetMaterialName(MaterialID ID) const;
	std::vector<MaterialID> GetMaterialIDs() const;
	const Material& GetMaterial(MaterialID ID) const;
	Material& GetMaterial(MaterialID ID);

	const std::string& GetTexturePath(TextureID) const;
	std::string GetTextureName(TextureID) const;

	std::vector<const Light*> GetLightsOfType(Light::EType eType) const;
	std::vector<const Light*> GetLights() const;
	std::vector<Light*> GetLights();
	
	Model&      GetModel(ModelID);
	const Model& GetModel(ModelID) const;
	FSceneStats GetSceneRenderStats(int FRAME_DATA_INDEX) const;
	
	GameObject* GetGameObject(size_t hObject) const;
	Transform* GetGameObjectTransform(size_t hObject) const;

//----------------------------------------------------------------------------------------------------------------
// SCENE DATA
//----------------------------------------------------------------------------------------------------------------
protected:

	//
	// VIEWS
	//
	std::vector<FSceneView>       mFrameSceneViews ; // per-frame data (usually 3 if Render & Update threads are separate)
	std::vector<FSceneShadowViews> mFrameShadowViews; // per-frame data (usually 3 if Render & Update threads are separate)

	//
	// SCENE ELEMENT CONTAINERS
	//
	std::unordered_map<MeshID, Mesh>         mMeshes;
	std::unordered_map<ModelID, Model>       mModels;
	std::unordered_map<MaterialID, Material> mMaterials;
	std::vector<size_t>                      mGameObjectHandles;
	std::vector<size_t>                      mTransformHandles;
	std::vector<Camera>                      mCameras;
	
	// See Light::EMobility enum for details
	std::vector<Light> mLightsStatic;
	std::vector<Light> mLightsStationary;
	std::vector<Light> mLightsDynamic;
	//Skybox             mSkybox;

	//
	// AUX DATA
	//
	std::unordered_map<const Camera*, DirectX::XMMATRIX> mViewProjectionMatrixHistory; // history for motion vectors

	//
	// CULLING DATA
	//
	SceneBoundingBoxHierarchy mBoundingBoxHierarchy;
	mutable FFrustumCullWorkerContext mFrustumCullWorkerContext;
	std::unordered_map<size_t, FSceneShadowViews::FShadowView*> mFrustumIndex_pShadowViewLookup;

	std::vector<size_t> mActiveLightIndices_Static;
	std::vector<size_t> mActiveLightIndices_Stationary;
	std::vector<size_t> mActiveLightIndices_Dynamic;


	//
	// MATERIAL DATA
	//
	MaterialID                mDefaultMaterialID = INVALID_ID;


	//
	// SCENE STATE
	//
	int                      mIndex_SelectedCamera;
	std::vector<size_t>      mSelectedObjects;

public:
	int                      mIndex_ActiveEnvironmentMapPreset;
	//EEnvironmentMapPresets  mActiveSkyboxPreset;
	//Settings::SceneRender   mSceneRenderSettings;


protected:
	const Input&                   mInput;
	const std::unique_ptr<Window>& mpWindow;
	VQEngine&                      mEngine;
	const FResourceNames&          mResourceNames;
	AssetLoader&                   mAssetLoader;
	VQRenderer&                    mRenderer;

	FSceneRepresentation mSceneRepresentation;

//----------------------------------------------------------------------------------------------------------------
// INTERNAL DATA
//----------------------------------------------------------------------------------------------------------------
private:
	MemoryPool<GameObject> mGameObjectPool;
	MemoryPool<Transform>  mGameObjectTransformPool;

	std::mutex mMtx_GameObjects;
	std::mutex mMtx_GameObjectTransforms;
	std::mutex mMtx_Meshes;
	std::mutex mMtx_Models;
	std::mutex mMtx_Materials;

	AssetLoader::ModelLoadResults_t          mModelLoadResults;
	AssetLoader::FMaterialTextureAssignments mMaterialAssignments;
	
	// cache
	std::unordered_set<MaterialID> mLoadedMaterials;
	std::unordered_map<MaterialID, std::string> mMaterialNames;
	std::unordered_map<TextureID, std::string> mTexturePaths;
	std::mutex mMtxTexturePaths;

	const std::string mInvalidMaterialName;
	const std::string mInvalidTexturePath;
};
