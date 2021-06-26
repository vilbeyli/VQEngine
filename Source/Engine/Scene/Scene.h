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
#include "Mesh.h"
#include "Material.h"
#include "Model.h"
#include "Light.h"
#include "Transform.h"
#include "GameObject.h"
#include "../Core/Memory.h"
#include "../AssetLoader.h"

class Input;
struct Material;
struct FResourceNames;
struct FFrustumPlaneset;

//------------------------------------------------------
#define MATERIAL_UNINITIALIZED_VALUE -1.0f
struct FMaterialRepresentation
{
	std::string Name;
	DirectX::XMFLOAT3 DiffuseColor;
	float Alpha             = MATERIAL_UNINITIALIZED_VALUE;
	DirectX::XMFLOAT3 EmissiveColor;
	float EmissiveIntensity = MATERIAL_UNINITIALIZED_VALUE;
	float Metalness         = MATERIAL_UNINITIALIZED_VALUE;
	float Roughness         = MATERIAL_UNINITIALIZED_VALUE;
	std::string DiffuseMapFilePath  ;
	std::string NormalMapFilePath   ;
	std::string EmissiveMapFilePath ;
	std::string AlphaMaskMapFilePath;
	std::string MetallicMapFilePath ;
	std::string RoughnessMapFilePath;
	std::string AOMapFilePath;

	FMaterialRepresentation();
};
struct FGameObjectRepresentation
{
	Transform tf;
	
	std::string ModelName;
	std::string ModelFilePath;
	
	std::string BuiltinMeshName;
	std::string MaterialName;
};
struct FSceneRepresentation
{
	std::string SceneName;
	std::string EnvironmentMapPreset;

	std::vector<FMaterialRepresentation>   Materials;
	std::vector<FCameraParameters>         Cameras;
	std::vector<FGameObjectRepresentation> Objects;
	std::vector<Light>                     Lights;

	char loadSuccess = 0;
};
//------------------------------------------------------
struct FPostProcessParameters
{
	struct FTonemapper
	{
		EColorSpace   ContentColorSpace = EColorSpace::REC_709;
		EDisplayCurve OutputDisplayCurve = EDisplayCurve::sRGB;
		float         DisplayReferenceBrightnessLevel = 200.0f;
		int           ToggleGammaCorrection = 1;
	};
	struct FFFXCAS
	{
		unsigned CASConstantBlock[8];
		float CASSharpen = 0.8f;
		FFFXCAS() = default;
		FFFXCAS(const FFFXCAS& other) : CASSharpen(other.CASSharpen) { memcpy(CASConstantBlock, other.CASConstantBlock, sizeof(CASConstantBlock)); }
	};
	struct FBlurParams // Gaussian Blur Pass
	{ 
		int iImageSizeX;
		int iImageSizeY;
	};

	inline bool IsFFXCASEnabled() const { return this->bEnableCAS && FFXCASParams.CASSharpen > 0.0f; }

	int SceneRTWidth = 0;
	int SceneRTHeight = 0;
	int DisplayResolutionWidth = 0;
	int DisplayResolutionHeight = 0;

	FTonemapper TonemapperParams;
	FBlurParams BlurParams;
	FFFXCAS     FFXCASParams;

	bool bEnableCAS;
	bool bEnableGaussianBlur;
};
struct FSceneRenderParameters
{
	bool bDrawLightBounds = false;
	bool bDrawMeshBoundingBoxes = false;
	bool bDrawGameObjectBoundingBoxes = false;
	bool bDrawLightMeshes = true;
	float fAmbientLightingFactor = 0.055f;
	bool bScreenSpaceAO = true;
};
struct FMeshRenderCommandBase
{
	MeshID meshID = INVALID_ID;
	DirectX::XMMATRIX matWorldTransformation;
};
struct FMeshRenderCommand : public FMeshRenderCommandBase
{
	MaterialID matID  = INVALID_ID;
	DirectX::XMMATRIX matNormalTransformation; //ID ?
	std::string ModelName;
	std::string MaterialName;
};
struct FShadowMeshRenderCommand : public FMeshRenderCommandBase
{
	DirectX::XMMATRIX matWorldViewProj;
	MaterialID matID = INVALID_ID;
	std::string ModelName;
};
struct FWireframeRenderCommand : public FMeshRenderCommandBase
{
	DirectX::XMFLOAT3 color;
};
using FLightRenderCommand = FWireframeRenderCommand;
using FBoundingBoxRenderCommand = FWireframeRenderCommand;

struct FSceneView
{
	DirectX::XMMATRIX     view;
	DirectX::XMMATRIX     viewProj;
	DirectX::XMMATRIX     viewInverse;
	DirectX::XMMATRIX     proj;
	DirectX::XMMATRIX     projInverse;
	DirectX::XMMATRIX     directionalLightProjection;
	DirectX::XMVECTOR     cameraPosition;
	float                 MainViewCameraYaw = 0.0f;
	float                 MainViewCameraPitch = 0.0f;
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

	std::vector<FMeshRenderCommand>  meshRenderCommands;
	std::vector<FLightRenderCommand> lightRenderCommands;
	std::vector<FLightRenderCommand> lightBoundsRenderCommands;
	std::vector<FBoundingBoxRenderCommand> boundingBoxRenderCommands;

};
struct FSceneShadowView
{
	struct FShadowView
	{
		DirectX::XMMATRIX matViewProj;
		std::vector<FShadowMeshRenderCommand> meshRenderCommands;
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

	int NumSpotShadowViews;
	int NumPointShadowViews;
};

using MeshLookup_t = std::unordered_map<MeshID, Mesh>;
using ModelLookup_t = std::unordered_map<ModelID, Model>;
using MaterialLookup_t = std::unordered_map<MaterialID, Material>;

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


	void BuildGameObjectBoundingBoxes(const std::vector<GameObject*>& pObjects);
	void BuildGameObjectBoundingBoxes(const std::vector<GameObject*>& pObjects, const std::vector<size_t>& Indices);
	void BuildMeshBoundingBoxes(const std::vector<GameObject*>& pObjects);
	void BuildMeshBoundingBoxes(const std::vector<GameObject*>& pObjects, const std::vector<size_t>& Indices);
	void Clear();

private:
	void BuildMeshBoundingBox(const GameObject* pObj);
	void BuildGameObjectBoundingBox(const GameObject* pObj);

private:
	friend class Scene;
	FBoundingBox mSceneBoundingBox;

	// list of game object bounding boxes for coarse culling
	//------------------------------------------------------
	std::vector<FBoundingBox>      mGameObjectBoundingBoxes;
	std::vector<const GameObject*> mGameObjectBoundingBoxGameObjectPointerMapping;
	//------------------------------------------------------

	// list of mesh bounding boxes for fine culling
	//------------------------------------------------------
	// these are same size containers, mapping bounding boxes to gameobjects and meshIDs
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

constexpr size_t NUM_GAMEOBJECT_POOL_SIZE = 4096;
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
	void Update(float dt, int FRAME_DATA_INDEX);
	void PostUpdate(int FRAME_DATA_INDEX, ThreadPool& UpdateWorkerThreadPool);
	void StartLoading(const BuiltinMeshArray_t& builtinMeshes, FSceneRepresentation& scene);
	void OnLoadComplete();
	void Unload(); // serial-only for now. maybe MT later.
	void RenderUI();
	void HandleInput(FSceneView& SceneView);

	void GatherSceneLightData(FSceneView& SceneView) const;

	void PrepareLightMeshRenderParams(FSceneView& SceneView) const;
	void PrepareSceneMeshRenderParams(const FFrustumPlaneset& MainViewFrustumPlanesInWorldSpace, std::vector<FMeshRenderCommand>& MeshRenderCommands) const;
	void PrepareShadowMeshRenderParams(FSceneShadowView& ShadowView, const FFrustumPlaneset& ViewFrustumPlanesInWorldSpace, ThreadPool& UpdateWorkerThreadPool) const;
	void PrepareBoundingBoxRenderParams(FSceneView& SceneView) const;
	
	// WIP----
	void GatherSpotLightFrustumParameters(FSceneShadowView& SceneShadowView, size_t iShadowView, const Light& l);
	void GatherPointLightFrustumParameters();
	// WIP----

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

	inline const FSceneView&       GetSceneView (int FRAME_DATA_INDEX) const { return mFrameSceneViews[FRAME_DATA_INDEX]; }
	inline const FSceneShadowView& GetShadowView(int FRAME_DATA_INDEX) const { return mFrameShadowViews[FRAME_DATA_INDEX]; }
	inline       FPostProcessParameters& GetPostProcessParameters(int FRAME_DATA_INDEX)       { return mFrameSceneViews[FRAME_DATA_INDEX].postProcessParameters; }
	inline const FPostProcessParameters& GetPostProcessParameters(int FRAME_DATA_INDEX) const { return mFrameSceneViews[FRAME_DATA_INDEX].postProcessParameters; }
	inline const Camera& GetActiveCamera() const { return mCameras[mIndex_SelectedCamera]; }
	inline       Camera& GetActiveCamera()       { return mCameras[mIndex_SelectedCamera]; }


	// Mesh, Model, GameObj management
	//TransformID CreateTransform(Transform** ppTransform);
	//GameObject* CreateObject(TransformID tfID, ModelID modelID);
	MeshID     AddMesh(Mesh&& mesh);
	MeshID     AddMesh(const Mesh& mesh);
	ModelID    CreateModel();
	MaterialID CreateMaterial(const std::string& UniqueMaterialName);
	MaterialID LoadMaterial(const FMaterialRepresentation& matRep, TaskID taskID);

	Material&  GetMaterial(MaterialID ID);
	Model&     GetModel(ModelID);

//----------------------------------------------------------------------------------------------------------------
// SCENE DATA
//----------------------------------------------------------------------------------------------------------------
protected:

	//
	// SCENE VIEWS PER FRAME
	//
	std::vector<FSceneView>       mFrameSceneViews;
	std::vector<FSceneShadowView> mFrameShadowViews;

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
	// CULLING DATA
	//
	SceneBoundingBoxHierarchy mBoundingBoxHierarchy;

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
