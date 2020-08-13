//	VQEngine | DirectX11 Renderer
//	Copyright(C) 2018  - Volkan Ilbeyli
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
#include "Model.h"
#include "Light.h"
#include "Transform.h"
#include "GameObject.h"

class Input;

// TODO:


#if 1

struct GameObjectRepresentation
{
	Transform tf;
	std::string ModelName;
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

struct FPostProcessParameters
{
	EColorSpace   ContentColorSpace = EColorSpace::REC_709;
	EDisplayCurve OutputDisplayCurve = EDisplayCurve::sRGB;
	float         DisplayReferenceBrightnessLevel = 200.0f;
	int           ToggleGammaCorrection = 1;
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



#if 0
		// list of objects that has the renderSettings.bRender=true
	RenderList opaqueList;
	RenderList alphaList;

	// list of objects that fall within the main camera's view frustum
	RenderList culledOpaqueList;
	RenderListLookup culluedOpaqueInstancedRenderListLookup;
#endif
};

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
	///friend class VQEngine; 

//----------------------------------------------------------------------------------------------------------------
// SCENE INTERFACE
//----------------------------------------------------------------------------------------------------------------
protected:
	// Update() is called each frame before Engine::Render(). Scene-specific update logic goes here.
	//
	virtual void UpdateScene(float dt, FSceneView& view) = 0;

	// Scene-specific loading logic goes here
	//
	virtual void LoadScene(FSceneRepresentation& scene) = 0;

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
	Scene(int NumFrameBuffers, const Input& input, const std::unique_ptr<Window>& pWin)
		: mInput(input)
		, mpWindow(pWin)
		, mFrameSceneViews(NumFrameBuffers)
		, mIndex_SelectedCamera(0)
		, mIndex_ActiveEnvironmentMapPreset(0)
	{}

	void Update(float dt, int FRAME_DATA_INDEX);
	void PostUpdate(int FRAME_DATA_INDEX, int FRAME_DATA_NEXT_INDEX);
	void StartLoading(FSceneRepresentation& scene);
	void OnLoadComplete();
	void Unload(); // serial-only for now. maybe MT later.
	void RenderUI();

	inline const FSceneView& GetSceneView(int FRAME_DATA_INDEX) const { return mFrameSceneViews[FRAME_DATA_INDEX]; }
	inline       FPostProcessParameters& GetPostProcessParameters(int FRAME_DATA_INDEX)       { return mFrameSceneViews[FRAME_DATA_INDEX].postProcess; }
	inline const FPostProcessParameters& GetPostProcessParameters(int FRAME_DATA_INDEX) const { return mFrameSceneViews[FRAME_DATA_INDEX].postProcess; }
	inline const Camera& GetActiveCamera() const { return mCameras[mIndex_SelectedCamera]; }
	inline       Camera& GetActiveCamera()       { return mCameras[mIndex_SelectedCamera]; }
//----------------------------------------------------------------------------------------------------------------
// SCENE DATA
//----------------------------------------------------------------------------------------------------------------
protected:
	std::vector<FSceneView> mFrameSceneViews;

	//
	// SCENE RESOURCE CONTAINERS
	//
	std::vector<MeshID>     mMeshIDs;
	std::vector<GameObject> mObjects;
	std::vector<Transform>  mGameObjectTransforms;
	std::vector<Camera>     mCameras;

	Light                   mDirectionalLight;
	std::vector<Light>      mLightsStatic;  // stationary lights
	std::vector<Light>      mLightsDynamic; // moving lights
	//Skybox                mSkybox;


	//
	// DATA
	//
	BoundingBox              mSceneBoundingBox;
	std::vector<BoundingBox> mMeshBoundingBoxes;
	std::vector<BoundingBox> mGameObjectBoundingBoxes;

	//
	// SCENE STATE
	//
	int                     mIndex_SelectedCamera;

public:
	int                     mIndex_ActiveEnvironmentMapPreset;
	//EEnvironmentMapPresets   mActiveSkyboxPreset;
	//Settings::SceneRender  mSceneRenderSettings;


protected:
	const Input&             mInput;
	const std::unique_ptr<Window>& mpWindow;

	FSceneRepresentation mSceneRepresentation;
//----------------------------------------------------------------------------------------------------------------
// INTERNAL DATA
//----------------------------------------------------------------------------------------------------------------
private:
	//CPUProfiler*    mpCPUProfiler;
	//ModelLoader     mModelLoader;
	//GameObjectPool  mObjectPool;
	//MaterialPool    mMaterials;
	//ModelLoadQueue  mModelLoadQueue;

	//BoundingBox     mSceneBoundingBox;
	//FSceneView       mSceneView;
	//ShadowView      mShadowView;

};

#else
#include "Settings.h"	// todo: is this needed?
#include "Skybox.h"
#include "GameObjectPool.h"
#include "SceneView.h"
#include "SceneLODManager.h"

#include <memory>
#include <mutex>
#include <future>


struct FSceneRepresentation;
struct FSceneView;
struct ShadowView;
struct DrawLists;

class SceneManager;
class Renderer;
class TextRenderer;
class MaterialPool;
class CPUProfiler;

#define DO_NOT_LOAD_SCENES 0

struct ModelLoadQueue
{
	std::mutex mutex;
	std::unordered_map<GameObject*, std::string> objectModelMap;
	std::unordered_map<std::string, std::future<Model>> asyncModelResults;
};

//----------------------------------------------------------------------------------------------------------------
// https://en.wikipedia.org/wiki/Template_method_pattern
// https://stackoverflow.com/questions/9724371/force-calling-base-class-virtual-function
// https://isocpp.org/wiki/faq/strange-inheritance#two-strategies-for-virtuals
// template method seems like a good idea here:
//   base class takes care of the common tasks among all scenes and calls the 
//   customized functions of the derived classes through pure virtual functions
//----------------------------------------------------------------------------------------------------------------
class Scene
{
//----------------------------------------------------------------------------------------------------------------
// INTERFACE FOR SCENE INSTANCES
//----------------------------------------------------------------------------------------------------------------
protected:

	// Update() is called each frame before Engine::Render(). Scene-specific update logic goes here.
	//
	virtual void Update(float dt) = 0;

	// Scene-specific loading logic goes here
	//
	virtual void Load(FSceneRepresentation& scene) = 0;
	
	// Scene-specific unloading logic goes here
	//
	virtual void Unload() = 0;

	// Each scene has to implement scene-specific RenderUI() function. 
	// RenderUI() is called after post processing is finished and it is 
	// the last rendering workload before presenting the frame.
	//
	virtual void RenderUI() const = 0;

	//------------------------------------------------------------------------

	//
	// SCENE RESOURCE MANAGEMENT
	//
#if 0
	//	Use this function to programmatically create new objects in the scene.
	//
	GameObject* CreateNewGameObject();

	// Updates the scene's bounding box boundaries. This has to be called after 
	// a new game object added to a scene (or after getting done adding multiple
	// game objects as this needs to be called only once).
	//
	void CalculateSceneBoundingBox();

	// Adds a light to the scene resources. Scene manager differentiates between
	// static and dynamic (moving and non-moving) lights for optimization reasons.
	//
	void AddLight(const Light& l);

	// Use this function to programmatically create new lights in the scene.
	// TODO: finalize design after light refactor
	//
	//Light* CreateNewLight();
	
	//	Loads an assimp model - blocks the thread until the model loads
	//
	Model LoadModel(const std::string& modelPath);

	// Queues a task for loading an assimp model for the GameObject* pObject
	// - ModelData will be assigned when the models finish loading which is sometime 
	//   after Load() and before Render(), it won't be immediately available.
	//
	void LoadModel_Async(GameObject* pObject, const std::string& modelPath);
#endif

//----------------------------------------------------------------------------------------------------------------
// ENGINE INTERFACE
//----------------------------------------------------------------------------------------------------------------
public:
	struct BaseSceneParams
	{
		VQRenderer*     pRenderer     = nullptr; 
		//TextRenderer* pTextRenderer = nullptr;
		//CPUProfiler*  pCPUProfiler  = nullptr;
	};
	Scene(const BaseSceneParams& params);
	~Scene() = default;


	// Moves objects from FSceneRepresentation into objects vector and sets the scene pointer in objects
	//
	void LoadScene(FSceneRepresentation& scene, const Settings::Window& windowSettings);

	// Clears object/light containers and camera settings
	//
	void UnloadScene();

	// Updates selected camera and calls overridden Update from derived scene class
	//
	void UpdateScene(float dt);

#if 0
	// Prepares the scene and shadow views for culling, sorting, instanced draw lists, lights, etc.
	//
	void PreRender(FrameStats& stats, SceneLightingConstantBuffer & outLightingData);


	// Renders the meshes in the scene which have materials with alpha=1.0f
	//
	int RenderOpaque(const FSceneView& sceneView) const;

	// Renders the transparent meshes in the scene, on a separate draw pass
	//
	int RenderAlpha(const FSceneView& sceneView) const;

	// Renders debugging information such as bounding boxes, wireframe meshes, etc.
	//
	int RenderDebug(const XMMATRIX& viewProj) const;

	void	RenderLights() const;
	void	RenderSkybox(const XMMATRIX& viewProj) const;


	//	Use these functions to programmatically create material instances which you can add to game objects in the scene. 
	//
	Material* CreateNewMaterial(EMaterialType type); // <Thread safe>
	Material* CreateRandomMaterialOfType(EMaterialType type); // <Thread safe>

	// ???
	//
	MeshID AddMesh_Async(Mesh m);
#endif

	// note: this function introduces plenty of header includes in many areas.
	//		 at this point, its probably worth considering elsewhere.
	///static inline std::pair<BufferID, BufferID> GetGeometryVertexAndIndexBuffers(EGeometry GeomEnum, int lod = 0) { return mBuiltinMeshes[GeomEnum].GetIABuffers(lod); }

	// TODO: Move all this to SceneResourceView 
	// Getters
	//
	inline const EnvironmentMap&		GetEnvironmentMap() const { return mSkybox.GetEnvironmentMap(); }
	inline const Camera&				GetActiveCamera() const { return mCameras[mSelectedCamera]; }
	inline const Settings::SceneRender& GetSceneRenderSettings() const { return mSceneRenderSettings; }
	inline const std::vector<Light>&	GetDynamicLights() { return mLightsDynamic; }
	inline EEnvironmentMapPresets		GetActiveEnvironmentMapPreset() const { return mActiveSkyboxPreset; }
	inline const std::vector<Mesh>&		GetBuiltInMeshes() const { return mBuiltinMeshes; }

	inline bool							HasSkybox() const { return mSkybox.GetSkyboxTexture() != -1; }

	// Setters
	//
	void SetEnvironmentMap(EEnvironmentMapPresets preset);
	void ResetActiveCamera();


protected:
	//----------------------------------------------------------------------------------------------------------------
	// SCENE DATA
	//----------------------------------------------------------------------------------------------------------------
	friend class SceneResourceView; // using attorney method, alternatively can use friend function
	friend class ModelLoader;
	
	//
	// SCENE RESOURCE CONTAINERS
	//
	//static std::vector<Mesh> mBuiltinMeshes;
	//static void InitializeBuiltinMeshes();
	std::vector<Transform>      mTransforms;
	std::vector<MeshID>			mMeshIDs;
	std::vector<Camera>			mCameras;
	std::vector<GameObject*>	mpObjects;
	Light						mDirectionalLight;
	std::vector<Light>			mLightsStatic;  // non-moving lights
	std::vector<Light>			mLightsDynamic; // moving lights
	//Skybox					mSkybox;


	//
	// SCENE STATE
	//
	EEnvironmentMapPresets		mActiveSkyboxPreset;
	int							mSelectedCamera;
	Settings::SceneRender		mSceneRenderSettings;


	//
	// SYSTEMS
	//
	VQRenderer*					mpRenderer;
	//TextRenderer*				mpTextRenderer;
	ThreadPool*					mpThreadPool;	// initialized by the Engine
	LODManager					mLODManager;

private:
	//----------------------------------------------------------------------------------------------------------------
	// INTERNAL DATA
	//----------------------------------------------------------------------------------------------------------------
	//
	// LIGHTS
	//
	struct ShadowingLightIndexCollection
	{
		inline void Clear() { spotLightIndices.clear(); pointLightIndices.clear(); }
		std::vector<int> spotLightIndices;
		std::vector<int> pointLightIndices;
	};
	struct SceneShadowingLightIndexCollection
	{
		inline void Clear() { mStaticLights.Clear(); mDynamicLights.Clear(); }
		inline size_t GetLightCount(Light::ELightType type) const
		{
			switch (type)
			{
			case Light::POINT: return mStaticLights.pointLightIndices.size() + mDynamicLights.pointLightIndices.size(); break;
			case Light::SPOT : return mStaticLights.spotLightIndices.size()  + mDynamicLights.spotLightIndices.size();  break;
			default: return 0;
			}
		}
		inline std::vector<const Light*> GetFlattenedListOfLights(const std::vector<Light>& staticLights, const std::vector<Light>& dynamicLights) const
		{
			std::vector<const Light*> pLights;
			for (const int& i : mStaticLights.spotLightIndices)   pLights.push_back(&staticLights[i]);
			for (const int& i : mStaticLights.pointLightIndices)  pLights.push_back(&staticLights[i]);
			for (const int& i : mDynamicLights.spotLightIndices)  pLights.push_back(&dynamicLights[i]);
			for (const int& i : mDynamicLights.pointLightIndices) pLights.push_back(&dynamicLights[i]);
			return pLights;
		}
		ShadowingLightIndexCollection mStaticLights;
		ShadowingLightIndexCollection mDynamicLights;
	};

	// Static lights will not change position or orientation. Here, we cache
	// some light data based on this assumption, such as frustum planes.
	//
	struct StaticLightCache
	{
		std::unordered_map<const Light*, std::array<FrustumPlaneset, 6>> mStaticPointLightFrustumPlanes;
		std::unordered_map<const Light*, FrustumPlaneset               > mStaticSpotLightFrustumPlanes;
		void Clear() { mStaticPointLightFrustumPlanes.clear(); mStaticSpotLightFrustumPlanes.clear(); }
	};

	StaticLightCache			mStaticLightCache;

	friend class Engine;

	GameObjectPool	mObjectPool;
	MaterialPool	mMaterials;
	ModelLoader		mModelLoader;
	ModelLoadQueue	mModelLoadQueue;

	std::mutex		mSceneMeshMutex;


	BoundingBox		mSceneBoundingBox;


	FSceneView		mSceneView;
	ShadowView		mShadowView;

	CPUProfiler*	mpCPUProfiler;


private:

	void StartLoadingModels();
	void EndLoadingModels();

	void AddStaticLight(const Light& l);
	void AddDynamicLight(const Light& l);

	//-------------------------------
	// PreRender() ROUTINES
	//-------------------------------
	void SetSceneViewData();

	void GatherSceneObjects(std::vector <const GameObject*>& mainViewShadowCasterRenderList, int& outNumSceneObjects);
	void GatherLightData(SceneLightingConstantBuffer& outLightingData, const std::vector<const Light*>& pLightList);

	void SortRenderLists(std::vector <const GameObject*>& mainViewShadowCasterRenderList, std::vector<const Light*>& pShadowingLights);

	SceneShadowingLightIndexCollection CullShadowingLights(int& outNumCulledPoints, int& outNumCulledSpots); // culls lights against main view
	std::vector<const GameObject*> FrustumCullMainView(int& outNumCulledObjects);
	void FrustumCullPointAndSpotShadowViews(const std::vector <const GameObject*>& mainViewShadowCasterRenderList, const SceneShadowingLightIndexCollection& shadowingLightIndices, FrameStats& stats);
	void OcclusionCullDirectionalLightView();

	void BatchMainViewRenderList(const std::vector<const GameObject*> mainViewRenderList);
	void BatchShadowViewRenderLists(const std::vector <const GameObject*>& mainViewShadowCasterRenderList);
	//-------------------------------

	void SetLightCache();
	void ClearLights();
};




#endif