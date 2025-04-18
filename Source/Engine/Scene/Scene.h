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

#include "Camera.h"
#include "Light.h"
#include "Transform.h"
#include "GameObject.h"
#include "Serialization.h"
#include "SceneBoundingBoxHierarchy.h"

#include "../Core/Memory.h"
#include "../AssetLoader.h"
#include "../PostProcess/PostProcess.h"

#include <algorithm>


using MeshLookup_t = std::unordered_map<MeshID, Mesh>;
using ModelLookup_t = std::unordered_map<ModelID, Model>;

// fwd decl
class Input;
struct Material;
struct FResourceNames;
struct FFrustumPlaneset;
struct FUIState;
class Window;
struct FSceneView;
struct FSceneShadowViews;
struct FShadowView;
struct FRenderStats;
class ThreadPool;

struct FSceneStats
{
	// lights -----------------------
	uint NumDirectionalLights = 0;
	uint NumStaticLights = 0;
	uint NumDynamicLights = 0;
	uint NumStationaryLights = 0;
	uint NumSpotLights = 0;
	uint NumPointLights = 0;
	uint NumDisabledSpotLights = 0;
	uint NumDisabledPointLights = 0;
	uint NumDisabledDirectionalLights = 0;
	uint NumShadowingPointLights = 0;
	uint NumShadowingSpotLights = 0;

	// render cmds ------------------
	const FRenderStats* pRenderStats = nullptr;

	// scene ------------------------
	uint NumMeshes = 0;
	uint NumModels = 0;
	uint NumMaterials = 0;
	uint NumObjects = 0;
	uint NumCameras = 0;
};

constexpr size_t NUM_MATERIAL_POOL_SIZE = 1024 * 64;
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
	void PostUpdate(ThreadPool& UpdateWorkerThreadPool, const FUIState& UIState, bool bAppInSimulationState, int FRAME_DATA_INDEX = 0);
	
	void StartLoading(const BuiltinMeshArray_t& builtinMeshes, FSceneRepresentation& scene, ThreadPool& UpdateWorkerThreadPool);
	void OnLoadComplete();
	void Unload(); // serial-only for now. maybe MT later.
	
	void RenderUI(FUIState& UIState, uint32_t W, uint32_t H);
	void HandleInput(FSceneView& SceneView);
	void PickObject(int4 ObjectIDPixelValue);

	void GatherSceneLightData(FSceneView& SceneView) const;
	void GatherShadowViewData(FSceneShadowViews& SceneShadowView
		, const std::vector<Light>& vLights
		, const std::vector<size_t>& vActiveLightIndices
	);

	void GatherLightMeshRenderData(const FSceneView& SceneView) const;

	void GatherFrustumCullParameters(FSceneView& SceneView, FSceneShadowViews& SceneShadowView, ThreadPool& UpdateWorkerThreadPool);
	void CullFrustums(const FSceneView& SceneView, ThreadPool& UpdateWorkerThreadPool);

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
	virtual ~Scene() = default;

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

	//----------------------------------------------------------------------------------------------------------------
	// SCENE ELEMENT MANAGEMENT
	//----------------------------------------------------------------------------------------------------------------
	//TransformID CreateTransform(Transform** ppTransform);
	//GameObject* CreateObject(TransformID tfID, ModelID modelID);
	MeshID      AddMesh(Mesh&& mesh);
	MeshID      AddMesh(const Mesh& mesh);
	ModelID     CreateModel();
	MaterialID  CreateMaterial(const std::string& UniqueMaterialName);
	MaterialID  LoadMaterial(const FMaterialRepresentation& matRep, TaskID taskID);
	int         CreateLight(Light::EType eType = Light::EType::POINT, Light::EMobility Mobility = Light::EMobility::DYNAMIC);
	bool        RemoveLight(Light* pLight);

	//----------------------------------------------------------------------------------------------------------------
	// GETTERS
	//----------------------------------------------------------------------------------------------------------------
	// Materials
	const std::vector<FMaterialRepresentation>& GetMaterialRepresentations() const { return mSceneRepresentation.Materials; }
	const std::string& GetMaterialName(MaterialID ID) const;
	std::vector<MaterialID> GetMaterialIDs() const;
	const Material& GetMaterial(MaterialID ID) const;
	Material& GetMaterial(MaterialID ID);
	
	// Meshes
	const Mesh& GetMesh(MeshID ID) const;

	// Textures
	const std::string& GetTexturePath(TextureID) const;
	std::string GetTextureName(TextureID) const;

	// Lights
	std::vector<const Light*> GetLightsOfType(Light::EType eType) const;
	std::vector<const Light*> GetLights() const;
	std::vector<Light*> GetLights();
	
	// Models
	Model&      GetModel(ModelID);
	const Model& GetModel(ModelID) const;
	FSceneStats GetSceneRenderStats(int FRAME_DATA_INDEX) const;
	
	// Game Objects
	GameObject* GetGameObject(size_t hObject) const;
	Transform* GetGameObjectTransform(size_t hObject) const;

	// Lights
	const Light* GetLight(Light::EMobility Mobility) const;
	      Light* GetLight(Light::EMobility Mobility);

//----------------------------------------------------------------------------------------------------------------
// SCENE DATA
//----------------------------------------------------------------------------------------------------------------
protected:

	//
	// VIEWS
	//
	std::vector<FSceneView>        mFrameSceneViews ; // per-frame data (usually 3 if Render & Update threads are separate)
	std::vector<FSceneShadowViews> mFrameShadowViews; // per-frame data (usually 3 if Render & Update threads are separate)

	//
	// SCENE ELEMENT CONTAINERS
	//
	std::unordered_map<MeshID, Mesh>         mMeshes;
	std::unordered_map<ModelID, Model>       mModels;
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

	std::vector<size_t> mActiveLightIndices_Static;
	std::vector<size_t> mActiveLightIndices_Stationary;
	std::vector<size_t> mActiveLightIndices_Dynamic;


	//
	// MATERIAL DATA
	//
	MaterialID mDefaultMaterialID = INVALID_ID;


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
	MemoryPool<Material>   mMaterialPool;

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
