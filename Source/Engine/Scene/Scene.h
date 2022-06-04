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

#if RENDER_INSTANCED_SCENE_MESHES
using MeshRenderCommand_t = FInstancedMeshRenderCommand;
#else
using MeshRenderCommand_t = FMeshRenderCommand;
#endif


// fwd decl
class Input;
struct Material;
struct FResourceNames;
struct FFrustumPlaneset;
struct FUIState;

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

	bool bDrawLightBounds = false;
	bool bDrawMeshBoundingBoxes = false;
	bool bDrawGameObjectBoundingBoxes = false;
	bool bDrawLightMeshes = true;
	float fYawSliderValue = 0.0f;
	float fAmbientLightingFactor = 0.055f;
	bool bScreenSpaceAO = true;
	FFFX_SSSR_UIParameters FFX_SSSRParameters = {};
};
//--- Pass Parameters ---


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

#if RENDER_INSTANCED_BOUNDING_BOXES
	std::vector<FInstancedBoundingBoxRenderCommand> boundingBoxRenderCommands;
#else
	std::vector<FBoundingBoxRenderCommand> boundingBoxRenderCommands;
#endif
};
struct FSceneShadowView
{
	struct FShadowView
	{
		DirectX::XMMATRIX matViewProj;
#if RENDER_INSTANCED_SHADOW_MESHES
		//--------------------------------------------------------------------------------------------------------------------------------------------
		//	+----SHADOW_MESH0
		//	       +----ShadowInstData0
		//	       +----ShadowInstData1
		//	+----SHADOW_MESH1
		//	       +----ShadowInstData0
		//	       +----ShadowInstData1
		//	       +----ShadowInstData2
		struct FShadowInstanceData { DirectX::XMMATRIX matWorld, matWorldViewProj; };
		struct FShadowMeshInstanceData { size_t NumValidData = 0; std::vector<FShadowInstanceData> InstanceData; };
		std::unordered_map<MeshID, FShadowMeshInstanceData> ShadowMeshInstanceDataLookup;
		//--------------------------------------------------------------------------------------------------------------------------------------------
		std::vector<FInstancedShadowMeshRenderCommand> meshRenderCommands;
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
		, const std::vector<Transform*>& pTransforms
	)
		: mMeshes(Meshes)
		, mModels(Models)
		, mMaterials(Materials)
		, mpTransforms(pTransforms)
	{}
	SceneBoundingBoxHierarchy() = delete;

	void Build(const std::vector<GameObject*>& pObjects, ThreadPool& UpdateWorkerThreadPool);
	void Clear();
	void ResizeGameObjectBoundingBoxContainer(size_t sz);

private:
	void CountGameObjectMeshes(const std::vector<GameObject*>& pObjects);
	void ResizeGameMeshBoxContainer(size_t size);

	void BuildGameObjectBoundingBoxes(const std::vector<GameObject*>& pObjects);
	void BuildGameObjectBoundingBoxes_Range(const std::vector<GameObject*>& pObjects, size_t iBegin, size_t iEnd);
	void BuildMeshBoundingBoxes(const std::vector<GameObject*>& pObjects);
	void BuildMeshBoundingBoxes_Range(const std::vector<GameObject*>& pObjects, size_t iBegin, size_t iEnd, size_t iMeshBB);

	void BuildMeshBoundingBox(const GameObject* pObj, size_t iBB_Begin, size_t iBB_End);
	void BuildGameObjectBoundingBox(const GameObject* pObj, size_t iBB);

private:
	friend class Scene;
	FBoundingBox mSceneBoundingBox;

	// list of game object bounding boxes for coarse culling
	//------------------------------------------------------
	std::vector<FBoundingBox>      mGameObjectBoundingBoxes;
	std::vector<const GameObject*> mGameObjectBoundingBoxGameObjectPointerMapping;
	std::vector<size_t>            mGameObjectNumMeshes;
	//------------------------------------------------------

	// list of mesh bounding boxes for fine culling
	//------------------------------------------------------
	// these are same size containers, mapping bounding boxes to gameobjects and meshIDs
	size_t mNumValidMeshBoundingBoxes = 0;
	std::vector<FBoundingBox>      mMeshBoundingBoxes;
	std::vector<MeshID>            mMeshBoundingBoxMeshIDMapping;
	std::vector<const GameObject*> mMeshBoundingBoxGameObjectPointerMapping; 
	//------------------------------------------------------

	// scene data container references
	const MeshLookup_t& mMeshes;
	const ModelLookup_t& mModels;
	const MaterialLookup_t& mMaterials;
	const std::vector<Transform*>& mpTransforms;
};

//------------------------------------------------------

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
	void PostUpdate(ThreadPool& UpdateWorkerThreadPool, int FRAME_DATA_INDEX = 0);
	void StartLoading(const BuiltinMeshArray_t& builtinMeshes, FSceneRepresentation& scene);
	void OnLoadComplete();
	void Unload(); // serial-only for now. maybe MT later.
	void RenderUI(FUIState& UIState, uint32_t W, uint32_t H);
	void HandleInput(FSceneView& SceneView);

	void GatherSceneLightData(FSceneView& SceneView) const;
	void GatherShadowViewData(FSceneShadowView& SceneShadowView
		, const std::vector<Light>& vLights
		, const std::vector<size_t>& vActiveLightIndices
	);

	void PrepareLightMeshRenderParams(FSceneView& SceneView) const;
	void BatchInstanceData_BoundingBox(FSceneView& SceneView
		, ThreadPool& UpdateWorkerThreadPool
		, const DirectX::XMMATRIX matViewProj
	) const;
	void BatchInstanceData_SceneMeshes(
		  std::vector<MeshRenderCommand_t>* pMeshRenderCommands
		, const std::vector<const GameObject*>& MeshBoundingBoxGameObjectPointers
		, const std::vector<size_t>& CulledBoundingBoxIndexList_Msh
		, const DirectX::XMMATRIX matViewProj
		, const DirectX::XMMATRIX matViewProjHistory
	);
	void BatchInstanceData_ShadowMeshes(
		  size_t iFrustum
		, FSceneShadowView::FShadowView* pShadowView
		, const std::vector<size_t>* pCulledBoundingBoxIndexList_Msh
		, DirectX::XMMATRIX matViewProj
	) const;


	void GatherFrustumCullParameters(const FSceneView& SceneView, FSceneShadowView& SceneShadowView, ThreadPool& UpdateWorkerThreadPool);
	void CullFrustums(const FSceneView& SceneView, ThreadPool& UpdateWorkerThreadPool);
	void BatchInstanceData(FSceneView& SceneView, ThreadPool& UpdateWorkerThreadPool);

	void LoadBuiltinMaterials(TaskID taskID, const std::vector<FGameObjectRepresentation>& GameObjsToBeLoaded);
	void LoadBuiltinMeshes(const BuiltinMeshArray_t& builtinMeshes);
	void LoadGameObjects(std::vector<FGameObjectRepresentation>&& GameObjects); // TODO: consider using FSceneRepresentation as the parameter and read the corresponding member
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
	const FSceneShadowView& GetShadowView(int FRAME_DATA_INDEX) const;
	      FPostProcessParameters& GetPostProcessParameters(int FRAME_DATA_INDEX)       ;
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

	Material&   GetMaterial(MaterialID ID);
	Model&      GetModel(ModelID);
	FSceneStats GetSceneRenderStats(int FRAME_DATA_INDEX) const;

//----------------------------------------------------------------------------------------------------------------
// SCENE DATA
//----------------------------------------------------------------------------------------------------------------
protected:

	//
	// VIEWS
	//
	std::vector<FSceneView>       mFrameSceneViews ; // per-frame data (usually 3 if Render & Update threads are separate)
	std::vector<FSceneShadowView> mFrameShadowViews; // per-frame data (usually 3 if Render & Update threads are separate)

	//
	// SCENE ELEMENT CONTAINERS
	//
	MeshLookup_t             mMeshes;
	ModelLookup_t            mModels;
	MaterialLookup_t         mMaterials;
	std::vector<GameObject*> mpObjects;
	std::vector<Transform*>  mpTransforms;
	std::vector<Camera>      mCameras;
	
	Light                    mDirectionalLight;

	std::vector<Light>       mLightsStatic;      //     static lights (See Light::EMobility enum for details)
	std::vector<Light>       mLightsStationary;  // stationary lights (See Light::EMobility enum for details)
	std::vector<Light>       mLightsDynamic;     //     moving lights (See Light::EMobility enum for details)
	//Skybox                   mSkybox;

	//
	// AUX DATA
	//
	std::unordered_map<const Transform*, DirectX::XMMATRIX> mTransformWorldMatrixHistory; // history for motion vectors
	std::unordered_map<const Camera*   , DirectX::XMMATRIX> mViewProjectionMatrixHistory; // history for motion vectors

#if RENDER_INSTANCED_SCENE_MESHES
	//--------------------------------------------------------------------------------------------------------------------------------------------
	// collect instance data based on Material, and then Mesh.
	// In order to avoid clear/resize, we will track if the data is @bStale
	// and if it is not stale, we also keep track of the number of valid instance data.
	//--------------------------------------------------------------------------------------------------------------------------------------------
	// MAT0
	//	+----MESH0
	//	       +----InstData0
	//	       +----InstData1
	//	+----MESH1
	//	       +----InstData0
	//	       +----InstData1
	//	       +----InstData2
	//	+----MESH2
	//	       +----InstData0
	//
	// MAT1
	//	+----MESH37
	//	       +----InstData0
	//	       +----InstData1
	//	       +----InstData2
	//	+----MESH225
	//	       +----InstData0
	//	       +----InstData1
	struct FInstanceData { DirectX::XMMATRIX mWorld, mWorldViewProj, mWorldViewProjPrev, mNormal; }; // transformation matrixes used in the shader
	struct FMeshInstanceData { size_t NumValidData = 0; std::vector<FInstanceData> InstanceData; };
	std::unordered_map < MaterialID, std::unordered_map<MeshID, FMeshInstanceData>> MaterialMeshInstanceDataLookup;
	//--------------------------------------------------------------------------------------------------------------------------------------------
#endif

	//
	// CULLING DATA
	//
	SceneBoundingBoxHierarchy mBoundingBoxHierarchy;
	mutable FFrustumCullWorkerContext mFrustumCullWorkerContext;
	std::unordered_map<size_t, FSceneShadowView::FShadowView*> mFrustumIndex_pShadowViewLookup;

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
	MemoryPool<Transform>  mTransformPool;

	std::mutex mMtx_Meshes;
	std::mutex mMtx_Models;
	std::mutex mMtx_Materials;

	AssetLoader::ModelLoadResults_t          mModelLoadResults;
	AssetLoader::FMaterialTextureAssignments mMaterialAssignments;
	
	// cache
	std::unordered_map<std::string, MaterialID> mLoadedMaterials;
	
	//CPUProfiler*    mpCPUProfiler;
	//FBoundingBox     mSceneBoundingBox;
};
