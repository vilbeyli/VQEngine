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
#include "Memory.h"

class Input;
class AssetLoader;
struct Material;
struct FResourceNames;

//------------------------------------------------------
struct GameObjectRepresentation
{
	Transform tf;
	
	std::string ModelName;
	std::string ModelFilePath;
	
	std::string BuiltinMeshName;
	struct Material { float data[16]; };
};
struct FSceneRepresentation
{
	std::string SceneName;
	std::string EnvironmentMapPreset;

	std::vector<FCameraParameters>        Cameras;
	std::vector<GameObjectRepresentation> Objects;
	//std::vector<LightRepresentation> Lights;

	char loadSuccess = 0;
};
//------------------------------------------------------
struct FPostProcessParameters
{
	EColorSpace   ContentColorSpace = EColorSpace::REC_709;
	EDisplayCurve OutputDisplayCurve = EDisplayCurve::sRGB;
	float         DisplayReferenceBrightnessLevel = 200.0f;
	int           ToggleGammaCorrection = 1;
};
struct FMeshRenderCommand
{
	MeshID     meshID = INVALID_ID;
	MaterialID matID  = INVALID_ID;
	DirectX::XMMATRIX WorldTransformationMatrix;
};
struct FSceneView
{
	DirectX::XMMATRIX     view;
	DirectX::XMMATRIX     viewProj;
	DirectX::XMMATRIX     viewInverse;
	DirectX::XMMATRIX     proj;
	DirectX::XMMATRIX     projInverse;
	DirectX::XMMATRIX     directionalLightProjection;
	DirectX::XMVECTOR     cameraPosition;
	//bool                  bIsPBRLightingUsed;
	//bool                  bIsDeferredRendering;
	//bool                  bIsIBLEnabled;
	//Settings::SceneRender sceneRenderSettings;
	//EnvironmentMap	environmentMap;

	FPostProcessParameters postProcess;

	std::vector<FMeshRenderCommand> meshRenderCommands;
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
	// Scene class contains the scene data and the logic to manipulate it. 
	// Scene is essentially a small part of the Engine. Writing an entire interface
	// for Scene to query scene data would be a waste of time without added benefit.
	// Hence VQEngine is declared a friend and has easy acess to all data to 
	// effectively orchestrate communication between its multiple threads.
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
public:
	Scene(
		  VQEngine& engine
		, int NumFrameBuffers
		, const Input& input
		, const std::unique_ptr<Window>& pWin
		, VQRenderer& renderer
	);

private: // Derived Scenes shouldn't access these functions
	void Update(float dt, int FRAME_DATA_INDEX);
	void PostUpdate(int FRAME_DATA_INDEX, int FRAME_DATA_NEXT_INDEX);
	void StartLoading(FSceneRepresentation& scene);
	void OnLoadComplete();
	void Unload(); // serial-only for now. maybe MT later.
	void RenderUI();
	void HandleInput();

public:
	inline const FSceneView& GetSceneView(int FRAME_DATA_INDEX) const { return mFrameSceneViews[FRAME_DATA_INDEX]; }
	inline       FPostProcessParameters& GetPostProcessParameters(int FRAME_DATA_INDEX)       { return mFrameSceneViews[FRAME_DATA_INDEX].postProcess; }
	inline const FPostProcessParameters& GetPostProcessParameters(int FRAME_DATA_INDEX) const { return mFrameSceneViews[FRAME_DATA_INDEX].postProcess; }
	inline const Camera& GetActiveCamera() const { return mCameras[mIndex_SelectedCamera]; }
	inline       Camera& GetActiveCamera()       { return mCameras[mIndex_SelectedCamera]; }

	// Mesh, Model, GameObj management
	//TransformID CreateTransform(Transform** ppTransform);
	//GameObject* CreateObject(TransformID tfID, ModelID modelID);
	//MeshID     CreateMesh();
	MeshID     AddMesh(Mesh&& mesh);
	ModelID    CreateModel();
	MaterialID CreateMaterial(const std::string& UniqueMaterialName);

	Material&  GetMaterial(MaterialID ID);
	Model&     GetModel(ModelID);

//----------------------------------------------------------------------------------------------------------------
// SCENE DATA
//----------------------------------------------------------------------------------------------------------------
protected:
	using MeshLookup_t     = std::unordered_map<MeshID, Mesh>;
	using ModelLookup_t    = std::unordered_map<ModelID, Model>;
	using MaterialLookup_t = std::unordered_map<MaterialID, Material>;
	//--------------------------------------------------------------

	//
	// SCENE VIEWS
	//
	std::vector<FSceneView> mFrameSceneViews;

	//
	// SCENE RESOURCE CONTAINERS
	//
	MeshLookup_t             mMeshes;
	ModelLookup_t            mModels;
	MaterialLookup_t         mMaterials;
	std::vector<GameObject*> mpObjects;
	std::vector<Transform*>  mpTransforms;
	std::vector<Camera>      mCameras;

	Light                    mDirectionalLight;
	std::vector<Light>       mLightsStatic;  // stationary lights
	std::vector<Light>       mLightsDynamic; // moving lights
	//Skybox                   mSkybox;


	//
	// DATA
	//
	BoundingBox              mSceneBoundingBox;
	std::vector<BoundingBox> mMeshBoundingBoxes;
	std::vector<BoundingBox> mGameObjectBoundingBoxes;

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

	std::unordered_map<std::string, MaterialID> mLoadedMaterials;

	//CPUProfiler*    mpCPUProfiler;
	//ModelLoader     mModelLoader;
	//MaterialPool    mMaterials;
	//ModelLoadQueue  mModelLoadQueue;

	//BoundingBox     mSceneBoundingBox;
	//FSceneView       mSceneView;
	//ShadowView      mShadowView;

};
