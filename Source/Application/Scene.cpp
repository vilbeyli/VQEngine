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

#define NOMINMAX

#include "Scene.h"
#include "Window.h"

using namespace DirectX;

void Scene::Update(float dt, int FRAME_DATA_INDEX)
{
	assert(FRAME_DATA_INDEX < mFrameSceneViews.size());
	FSceneView& SceneView = mFrameSceneViews[FRAME_DATA_INDEX];

	this->UpdateScene(dt, SceneView);
}

void Scene::PostUpdate(int FRAME_DATA_INDEX, int FRAME_DATA_NEXT_INDEX)
{
	assert(FRAME_DATA_INDEX < mFrameSceneViews.size());
	FSceneView& SceneView = mFrameSceneViews[FRAME_DATA_INDEX];

	const Camera& cam = mCameras[mIndex_SelectedCamera];
	const XMFLOAT3 camPos = cam.GetPositionF();

	// extract scene view
	SceneView.proj           = cam.GetProjectionMatrix();
	SceneView.projInverse    = XMMatrixInverse(NULL, SceneView.proj);
	SceneView.view           = cam.GetViewMatrix();
	SceneView.viewInverse    = cam.GetViewInverseMatrix();
	SceneView.viewProj       = SceneView.view * SceneView.proj;
	SceneView.cameraPosition = XMLoadFloat3(&camPos);
}

void Scene::StartLoading(FSceneRepresentation& scene)
{
	// scene-specific load 
	this->LoadScene(scene);

	// dispatch workers

	// initialize cameras
	for (FCameraParameters& param : scene.Cameras)
	{
		param.Width = mpWindow->GetWidth();
		param.Height = mpWindow->GetHeight();

		Camera c;
		c.InitializeCamera(param);
		mCameras.push_back(c);
	}

	// assign scene rep
	mSceneRepresentation = scene;
}

void Scene::OnLoadComplete()
{
	Log::Info("[Scene] %s loaded.", mSceneRepresentation.SceneName.c_str());
	mSceneRepresentation.loadSuccess = 1;
}

void Scene::Unload()
{
	mSceneRepresentation = {};

	const size_t sz = mFrameSceneViews.size();
	mFrameSceneViews.clear();
	mFrameSceneViews.resize(sz);

	mMeshIDs.clear();
	mObjects.clear();
	mGameObjectTransforms.clear();
	mCameras.clear();

	mDirectionalLight = {};
	mLightsStatic.clear();
	mLightsDynamic.clear();

	mSceneBoundingBox = {};
	mMeshBoundingBoxes.clear();
	mGameObjectBoundingBoxes.clear();

	mIndex_SelectedCamera
		= mIndex_ActiveEnvironmentMapPreset
		= 0;
}

void Scene::RenderUI()
{
	// TODO
}


#if 0
#include "SceneResourceView.h"
#include "Engine.h"
#include "ObjectCullingSystem.h"

#include "Application/Input.h"
#include "Application/ThreadPool.h"
#include "Renderer/GeometryGenerator.h"
#include "Utilities/Log.h"

#include <numeric>
#include <set>

#define THREADED_FRUSTUM_CULL 0	// uses workers to cull the render lists (not implemented yet)

Scene::Scene(const BaseSceneParams& params)
	: mpRenderer(params.pRenderer)
	, mpTextRenderer(params.pTextRenderer)
	, mSelectedCamera(0)
	, mpCPUProfiler(params.pCPUProfiler)
	, mLODManager(this->mMeshIDs, this->mpRenderer)
	, mActiveSkyboxPreset(ENVIRONMENT_MAP_PRESET_COUNT)
{
	mModelLoader.Initialize(mpRenderer);
}


//----------------------------------------------------------------------------------------------------------------
// LOAD / UNLOAD FUNCTIONS
//----------------------------------------------------------------------------------------------------------------
void Scene::LoadScene(FSceneRepresentation& scene, const Settings::Window& windowSettings)
{
	//
	// Allocate GameObject & Material memory
	//
#ifdef _DEBUG
	mObjectPool.Initialize(4096);
	mMaterials.Initialize(4096);
#else
	mObjectPool.Initialize(4096 * 8);
	mMaterials.Initialize(4096 * 8);
#endif

	mMeshIDs.clear();
	mpObjects.clear();
	mMeshIDs.resize(mBuiltinMeshes.size());
	std::copy(RANGE(mBuiltinMeshes), mMeshIDs.begin());


	//
	// Lights
	//
	for (Light& l : scene.lights) this->AddLight(std::move(l));
	//mLights = std::move(scene.lights);

	mMaterials = std::move(scene.materials);
	mDirectionalLight = std::move(scene.directionalLight);
	mSceneRenderSettings = scene.settings;


	//
	// Cameras
	//
	for (const Settings::Camera& camSetting : scene.cameras)
	{
		Camera c;
		c.ConfigureCamera(camSetting, windowSettings, mpRenderer);
		mCameras.push_back(c);
	}


	//
	// Game Objects
	//
	for (size_t i = 0; i < scene.objects.size(); ++i)
	{
		GameObject* pObj = mObjectPool.Create(this);
		*pObj = scene.objects[i];
		pObj->mpScene = this;

		// if the game object has a model that is not loaded yet
		// call the async load function
		if (!pObj->mModel.mbLoaded && !pObj->mModel.mModelName.empty())
		{
			LoadModel_Async(pObj, pObj->mModel.mModelName);
		}
		mpObjects.push_back(pObj);
	}


	Load(scene);            // Scene-specific load
	StartLoadingModels();	// async model loading
	
	SetLightCache();

	EndLoadingModels();

	// initialize LOD manager
	{
		std::vector<GameObject*> pSceneObjects;
		for (GameObject* pObj : mpObjects)
		{
			if (pObj->mpScene == this)
			{
				pSceneObjects.push_back(pObj);
			}
		}

		mLODManager.Initialize(GetActiveCamera(), pSceneObjects);
	}
	CalculateSceneBoundingBox();	// needs to happen after models are loaded
}


void Scene::UnloadScene()
{
	//---------------------------------------------------------------------------
	// if we clear materials and don't clear the models loaded with them,
	// we'll crash in lookups. this can be improved by only reloading
	// the materials instead of the whole mesh data. #TODO: more granular reload
	mModelLoader.UnloadSceneModels(this);
	mMaterials.Clear();
	//---------------------------------------------------------------------------
	mCameras.clear();
	mObjectPool.Cleanup();
	mMeshIDs.clear();
	mObjectPool.Cleanup();
	ClearLights();
	mLODManager.Reset();
	Unload();
	mpObjects.clear();
}

void Scene::RenderUI()
{
}


MeshID Scene::AddMesh_Async(Mesh mesh)
{
	std::unique_lock<std::mutex> l(mSceneMeshMutex);
	mMeshIDs.push_back(mesh);
	return MeshID(mMeshIDs.size() - 1);
}

void Scene::StartLoadingModels()
{
	// can have multiple objects pointing to the same path
	// get all the unique paths 
	//
	std::set<std::string> uniqueModelList;

	std::for_each(RANGE(mModelLoadQueue.objectModelMap), [&](auto kvp)
	{	// 'value' (.second) of the 'key value pair' (kvp) contains the model path
		uniqueModelList.emplace(kvp.second);
	});

	if (uniqueModelList.empty()) return;

	Log::Info("Async Model Load List: ");

	// so we load the models only once
	//
	std::for_each(RANGE(uniqueModelList), [&](const std::string& modelPath)
	{
		Log::Info("\t%s", modelPath.c_str());
		mModelLoadQueue.asyncModelResults[modelPath] = mpThreadPool->AddTask([=]()
		{
			return mModelLoader.LoadModel_Async(modelPath, this);
		});
	});
}

void Scene::EndLoadingModels()
{
	std::unordered_map<std::string, Model> loadedModels;
	std::for_each(RANGE(mModelLoadQueue.objectModelMap), [&](auto kvp)
	{
		const std::string& modelPath = kvp.second;
		GameObject* pObj = kvp.first;

		// this will wait on the longest item.
		Model m = {};
		if (loadedModels.find(modelPath) == loadedModels.end())
		{
			m = mModelLoadQueue.asyncModelResults.at(modelPath).get();
			loadedModels[modelPath] = m;
		}
		else
		{
			m = loadedModels.at(modelPath);
		}

		// override material if any.
		if (!pObj->mModel.mMaterialAssignmentQueue.empty())
		{
			MaterialID matID = pObj->mModel.mMaterialAssignmentQueue.front();
			pObj->mModel.mMaterialAssignmentQueue.pop(); // we only use the first material for now...
			m.OverrideMaterials(matID);
		}

		// assign the model to object
		pObj->SetModel(m);
	});
}

void Scene::AddStaticLight(const Light& l)
{
	mLightsStatic.push_back(l);

	// Note: Static light cache is initialized after the loading is done.
	//       So we just push the light to the static light container here.
}

void Scene::AddDynamicLight(const Light& l)
{
	mLightsDynamic.push_back(l);
}

Model Scene::LoadModel(const std::string & modelPath)
{
	return mModelLoader.LoadModel(modelPath, this);
}

void Scene::LoadModel_Async(GameObject* pObject, const std::string& modelPath)
{
	std::unique_lock<std::mutex> lock(mModelLoadQueue.mutex);
	mModelLoadQueue.objectModelMap[pObject] = modelPath;
}


//----------------------------------------------------------------------------------------------------------------
// UPDATE FUNCTIONS
//----------------------------------------------------------------------------------------------------------------
void Scene::UpdateScene(float dt)
{
	// CYCLE THROUGH CAMERAS
	//
	if (ENGINE->INP()->IsKeyTriggered("C"))
	{
		mSelectedCamera = (mSelectedCamera + 1) % mCameras.size();
		mLODManager.SetViewer(this->GetActiveCamera());
	}


	// OPTIMIZATION SETTINGS TOGGLES
	//
//#if _DEBUG
#if 1
	if (ENGINE->INP()->IsKeyTriggered("F7"))
	{
		bool& toggle = ENGINE->INP()->IsKeyDown("Shift")
			? mSceneRenderSettings.optimization.bViewFrustumCull_LocalLights
			: mSceneRenderSettings.optimization.bViewFrustumCull_MainView;

		toggle = !toggle;
	}
	if (ENGINE->INP()->IsKeyTriggered("F8"))
	{
		mSceneRenderSettings.optimization.bViewFrustumCull_LocalLights = !mSceneRenderSettings.optimization.bViewFrustumCull_LocalLights;
	}
#endif



	// UPDATE CAMERA & WORLD
	//
	mpCPUProfiler->BeginEntry("Scene::Update()");
	mCameras[mSelectedCamera].Update(dt);
	Update(dt);
	mpCPUProfiler->EndEntry();

	// UPDATE LOD MANAGER
	//
	mpCPUProfiler->BeginEntry("LODManager::Update()");
	mLODManager.Update();
	mpCPUProfiler->EndEntry();


	// Note:
	// this could be moved outside this function to allow for more time 
	// for concurrency of lod manager update operations (if it takes that long).
	mpCPUProfiler->BeginEntry("LODManager::LateUpdate()");
	mLODManager.LateUpdate();
	mpCPUProfiler->EndEntry();
}

static void ResetSceneStatCounters(SceneStats& stats)
{
	stats.numSpots = 0;
	stats.numPoints = 0;
	//stats.numDirectionalCulledObjects = 0;
	stats.numPointsCulledObjects = 0; 
	stats.numSpotsCulledObjects = 0;
}

void Scene::PreRender(FrameStats& stats, SceneLightingConstantBuffer& outLightingData)
{
	using namespace VQEngine;

	// containers we'll work on for preparing draw lists
	std::vector<const GameObject*> mainViewRenderList; // Shadow casters + non-shadow casters
	std::vector<const GameObject*> mainViewShadowCasterRenderList;
	SceneShadowingLightIndexCollection shadowingLightIndexCollection;


	//----------------------------------------------------------------------------
	// SET SCENE VIEW / SETTINGS
	//----------------------------------------------------------------------------
	SetSceneViewData();
	ResetSceneStatCounters(stats.scene);
	

	//----------------------------------------------------------------------------
	// PREPARE RENDER LISTS
	//----------------------------------------------------------------------------
	mpCPUProfiler->BeginEntry("GatherSceneObjects");
	GatherSceneObjects(mainViewShadowCasterRenderList, stats.scene.numObjects);
	mpCPUProfiler->EndEntry();

	//----------------------------------------------------------------------------
	// CULL LIGHTS
	//----------------------------------------------------------------------------
	mpCPUProfiler->BeginEntry("Cull_Lights");
	shadowingLightIndexCollection = CullShadowingLights(stats.scene.numCulledShadowingPointLights, stats.scene.numCulledShadowingSpotLights);
	mpCPUProfiler->EndEntry(); 

	//----------------------------------------------------------------------------
	// CULL MAIN VIEW RENDER LISTS
	//----------------------------------------------------------------------------
	mpCPUProfiler->BeginEntry("Cull_MainView");
	mainViewRenderList = FrustumCullMainView(stats.scene.numMainViewCulledObjects);
	mpCPUProfiler->EndEntry(); 


	//----------------------------------------------------------------------------
	// CULL SHADOW VIEW RENDER LISTS
	//----------------------------------------------------------------------------
	//mpCPUProfiler->BeginEntry("Cull_ShadowViews"); 
	FrustumCullPointAndSpotShadowViews(mainViewShadowCasterRenderList, shadowingLightIndexCollection, stats);
	//mpCPUProfiler->EndEntry(); 


	//----------------------------------------------------------------------------
	// OCCLUSION CULL DIRECTIONAL SHADOW VIEW RENDER LISTS (not implemented yet)
	//----------------------------------------------------------------------------
	if (mSceneRenderSettings.optimization.bShadowViewCull)
	{
		mpCPUProfiler->BeginEntry("Cull_Directional_Occl");
		OcclusionCullDirectionalLightView();
		mpCPUProfiler->EndEntry();
	}

	//mpCPUProfiler->BeginEntry("Gather_FlattenedLightList");
	std::vector<const Light*> pShadowingLights = shadowingLightIndexCollection.GetFlattenedListOfLights(mLightsStatic, mLightsDynamic);
	//mpCPUProfiler->EndEntry();

	//----------------------------------------------------------------------------
	// SORT RENDER LISTS
	//----------------------------------------------------------------------------
	if (mSceneRenderSettings.optimization.bSortRenderLists)
	{
		mpCPUProfiler->BeginEntry("Sort");
		SortRenderLists(mainViewShadowCasterRenderList, pShadowingLights);
		mpCPUProfiler->EndEntry();
	}


	//----------------------------------------------------------------------------
	// BATCH SHADOW VIEW RENDER LISTS
	//----------------------------------------------------------------------------
	BatchShadowViewRenderLists(mainViewShadowCasterRenderList);
	
	
	//----------------------------------------------------------------------------
	// BATCH MAIN VIEW RENDER LISTS
	//----------------------------------------------------------------------------
	mpCPUProfiler->BeginEntry("Batch_MainView");
	BatchMainViewRenderList(mainViewRenderList);
	mpCPUProfiler->EndEntry();


#if _DEBUG
	static bool bReportedList = false;
	if (!bReportedList)
	{
		Log::Info("Mesh Render List (%s): ", mSceneRenderSettings.optimization.bSortRenderLists ? "Sorted" : "Unsorted");
		int num = 0;
		std::for_each(RANGE(mSceneView.culledOpaqueList), [&](const GameObject* pObj)
		{
			Log::Info("\tObj[%d]: ", num);

			int numMesh = 0;
			std::for_each(RANGE(pObj->GetModelData().mMeshIDs), [&](const MeshID& id)
			{
				Log::Info("\t\tMesh[%d]: %d", numMesh, id);
			});
			++num;
		});
		bReportedList = true;
	}
#endif // THREADED_FRUSTUM_CULL


	mpCPUProfiler->BeginEntry("GatherLightData");
	GatherLightData(outLightingData, pShadowingLights);
	mpCPUProfiler->EndEntry();

	//return numFrustumCulledObjs + numShadowFrustumCullObjs;
}


void Scene::ResetActiveCamera()
{
	mCameras[mSelectedCamera].Reset();
}

std::vector<Mesh> Scene::mBuiltinMeshes;
void Scene::InitializeBuiltinMeshes()
{

	// cylinder parameters
	const float	 cylHeight = 3.1415f;		const float	 cylTopRadius = 1.0f;
	const float	 cylBottomRadius = 1.0f;	const unsigned cylSliceCount = 70;
	const unsigned cylStackCount = 20;

	// grid parameters
	const float gridWidth = 1.0f;		const float gridDepth = 1.0f;
	const unsigned gridFinenessH = 90;	const unsigned gridFinenessV = 90;

	// sphere parameters
	const float sphRadius = 2.0f;
	const unsigned sphRingCount = 70;	const unsigned sphSliceCount = 70;

	// cone parameters
	const float coneHeight = 3.0f;
	const float coneRadius = 1.0f;

	constexpr int numDefaultLODLevels_Sphere = 5;
	constexpr int numDefaultLODLevels_Grid = 7;
	constexpr int numDefaultLODLevels_Cone = 6;
	constexpr int numDefaultLODLevels_Cylinder = 6;

	Scene::mBuiltinMeshes =	// this should match enum declaration order
	{
		GeometryGenerator::Triangle(1.0f),
		GeometryGenerator::Quad(1.0f),
		GeometryGenerator::FullScreenQuad(),
		GeometryGenerator::Cube(),
		GeometryGenerator::Cylinder(cylHeight, cylTopRadius, cylBottomRadius, cylSliceCount, cylStackCount, numDefaultLODLevels_Cylinder),
		GeometryGenerator::Sphere(sphRadius, sphRingCount, sphSliceCount, numDefaultLODLevels_Sphere),
		GeometryGenerator::Grid(gridWidth, gridDepth, gridFinenessH, gridFinenessV, numDefaultLODLevels_Grid),
		GeometryGenerator::Cone(coneHeight, coneRadius, 120, numDefaultLODLevels_Cone),
		GeometryGenerator::Cone(1.0f, 1.0f, 30),
		//GeometryGenerator::Sphere(sphRadius / 40, 10, 10),
	};

	LODManager::InitializeBuiltinMeshLODSettings();
}

void Scene::SetEnvironmentMap(EEnvironmentMapPresets preset)
{
	mActiveSkyboxPreset = preset;
	mSkybox = Skybox::s_Presets[mActiveSkyboxPreset];
}



//----------------------------------------------------------------------------------------------------------------
// PRE-RENDER FUNCTIONS
//----------------------------------------------------------------------------------------------------------------
// can't use std::array<T&, 2>, hence std::array<T*, 2> 
// array of 2: light data for non-shadowing and shadowing lights
constexpr size_t NON_SHADOWING_LIGHT_INDEX = 0;
constexpr size_t SHADOWING_LIGHT_INDEX = 1;
using pPointLightDataArray = std::array<PointLightDataArray*, 2>;
using pSpotLightDataArray = std::array<SpotLightDataArray*, 2>;

// stores the number of lights per light type (2 types : point and spot)
using pNumArray = std::array<int*, 2>;
void Scene::GatherLightData(SceneLightingConstantBuffer & outLightingData, const std::vector<const Light*>& pLightList)
{
	SceneLightingConstantBuffer::cb& cbuffer = outLightingData._cb;

	outLightingData.ResetCounts();

	cbuffer.directionalLight.depthBias = 0.0f;
	cbuffer.directionalLight.enabled = 0;
	cbuffer.directionalLight.shadowing = 0;
	cbuffer.directionalLight.brightness = 0.0f;

	pNumArray lightCounts
	{
		&outLightingData._cb.pointLightCount,
		&outLightingData._cb.spotLightCount
	};
	pNumArray casterCounts
	{
		&outLightingData._cb.pointLightCount_shadow,
		&outLightingData._cb.spotLightCount_shadow
	};

	unsigned numShdSpot = 0;
	unsigned numPtSpot = 0;

	for (const Light* l : pLightList)
	{
		//if (!l->_bEnabled) continue;	// #BreaksRelease

		pNumArray& refLightCounts = casterCounts;
		const size_t lightIndex = (*refLightCounts[l->mType])++;
		switch (l->mType)
		{
		case Light::ELightType::POINT:
		{
			PointLightGPU plData;
			l->GetGPUData(plData);

			cbuffer.pointLightsShadowing[lightIndex] = plData;
			mShadowView.points.push_back(l);
		} break;
		case Light::ELightType::SPOT:
		{
			SpotLightGPU slData;
			l->GetGPUData(slData);

			cbuffer.spotLightsShadowing[lightIndex] = slData;
			cbuffer.shadowViews[numShdSpot++] = l->GetLightSpaceMatrix();
			mShadowView.spots.push_back(l);
		} break;
		default:
			Log::Error("Engine::PreRender(): UNKNOWN LIGHT TYPE");
			continue;
		}
	}

	// iterate for non-shadowing lights (they won't be in pLightList)
	constexpr size_t NUM_LIGHT_CONTAINERS = 2;
	std::array<const std::vector<Light>*, NUM_LIGHT_CONTAINERS > lightContainers =
	{
		  &mLightsStatic
		, &mLightsDynamic
	};
	for (int i = 0; i < NUM_LIGHT_CONTAINERS; ++i)
	{
		const std::vector<Light>& mLights = *lightContainers[i];
		for (const Light& l : mLights)
		{
			if (l.mbCastingShadows) continue;
			pNumArray& refLightCounts = lightCounts;

			const size_t lightIndex = (*refLightCounts[l.mType])++;
			switch (l.mType)
			{
			case Light::ELightType::POINT:
			{
				PointLightGPU plData;
				l.GetGPUData(plData);
				cbuffer.pointLights[lightIndex] = plData;
			} break;
			case Light::ELightType::SPOT:
			{
				SpotLightGPU slData;
				l.GetGPUData(slData);
				cbuffer.spotLights[lightIndex] = slData;
			} break;
			default:
				Log::Error("Engine::PreRender(): UNKNOWN LIGHT TYPE");
				continue;
			}
		}
	}

	if (mDirectionalLight.mbEnabled)
	{
		mDirectionalLight.GetGPUData(cbuffer.directionalLight);
		cbuffer.shadowViewDirectional = mDirectionalLight.GetLightSpaceMatrix();
		mShadowView.pDirectional = &mDirectionalLight;
	}
	else
	{
		cbuffer.directionalLight = {};
	}
}

void Scene::SetSceneViewData()
{
	const Camera& viewCamera = GetActiveCamera();
	const XMMATRIX view = viewCamera.GetViewMatrix();
	const XMMATRIX viewInverse = viewCamera.GetViewInverseMatrix();
	const XMMATRIX proj = viewCamera.GetProjectionMatrix();
	XMVECTOR det = XMMatrixDeterminant(proj);
	const XMMATRIX projInv = XMMatrixInverse(&det, proj);
	det = mDirectionalLight.mbEnabled
		? XMMatrixDeterminant(mDirectionalLight.GetProjectionMatrix())
		: XMVECTOR();
	const XMMATRIX directionalLightProjection = mDirectionalLight.mbEnabled
		? mDirectionalLight.GetProjectionMatrix()
		: XMMatrixIdentity();

	// scene view matrices
	mSceneView.viewProj = view * proj;
	mSceneView.view = view;
	mSceneView.viewInverse = viewInverse;
	mSceneView.proj = proj;
	mSceneView.projInverse = projInv;
	mSceneView.directionalLightProjection = directionalLightProjection;

	// render/scene settings
	mSceneView.sceneRenderSettings = GetSceneRenderSettings();
	mSceneView.environmentMap = GetEnvironmentMap();
	mSceneView.cameraPosition = viewCamera.GetPositionF();
	mSceneView.bIsIBLEnabled = mSceneRenderSettings.bSkylightEnabled && mSceneView.bIsPBRLightingUsed && mSceneView.environmentMap.environmentMap != -1;
}

void Scene::GatherSceneObjects(std::vector <const GameObject*>& mainViewShadowCasterRenderList, int& outNumSceneObjects)
{
	// CLEAN UP RENDER LISTS
	//
	//pCPUProfiler->BeginEntry("CleanUp");
	// scene view
	mSceneView.opaqueList.clear();
	mSceneView.culledOpaqueList.clear();
	mSceneView.culluedOpaqueInstancedRenderListLookup.clear();
	mSceneView.alphaList.clear();

	// shadow views
	mShadowView.Clear();
	mShadowView.RenderListsPerMeshType.clear();
	mShadowView.casters.clear();
	mShadowView.shadowMapRenderListLookUp.clear();
	mShadowView.shadowMapInstancedRenderListLookUp.clear();
	//pCPUProfiler->EndEntry();

	// POPULATE RENDER LISTS WITH SCENE OBJECTS
	//
	outNumSceneObjects = 0;
	for (GameObject& obj : mObjectPool.mObjects) 
	{
		// only gather the game objects that are to be rendered in 'this' scene
		if (obj.mpScene == this && obj.mRenderSettings.bRender)
		{
			const bool bMeshListEmpty = obj.mModel.mData.mMeshIDs.empty();
			const bool bTransparentMeshListEmpty = obj.mModel.mData.mTransparentMeshIDs.empty();
			const bool mbCastingShadows = obj.mRenderSettings.bCastShadow && !bMeshListEmpty;

			// populate render lists
			if (!bMeshListEmpty) 
			{ 
				mSceneView.opaqueList.push_back(&obj); 
			}
			if (!bTransparentMeshListEmpty) 
			{ 
				mSceneView.alphaList.push_back(&obj); 
			}
			if (mbCastingShadows) 
			{ 
				mainViewShadowCasterRenderList.push_back(&obj); 
			}

#if _DEBUG
			if (bMeshListEmpty && bTransparentMeshListEmpty)
			{
				Log::Warning("GameObject with no Mesh Data, turning bRender off");
				obj.mRenderSettings.bRender = false;
			}
#endif
			++outNumSceneObjects;
		}
	}
}


void Scene::SortRenderLists(std::vector <const GameObject*>& mainViewShadowCasterRenderList, std::vector<const Light*>& pShadowingLights)
{
	// LAMBDA DEFINITIONS
	//---------------------------------------------------------------------------------------------
	// Meshes are sorted according to BUILT_IN_TYPE < CUSTOM, 
	// and BUILT_IN_TYPEs are sorted in themselves
	auto SortByMeshType = [&](const GameObject* pObj0, const GameObject* pObj1)
	{
		const ModelData& model0 = pObj0->GetModelData();
		const ModelData& model1 = pObj1->GetModelData();

		const MeshID mID0 = model0.mMeshIDs.empty() ? -1 : model0.mMeshIDs.back();
		const MeshID mID1 = model1.mMeshIDs.empty() ? -1 : model1.mMeshIDs.back();

		assert(mID0 != -1 && mID1 != -1);

		// case: one of the objects have a custom mesh
		if (mID0 >= EGeometry::MESH_TYPE_COUNT || mID1 >= EGeometry::MESH_TYPE_COUNT)
		{
			if (mID0 < EGeometry::MESH_TYPE_COUNT)
				return true;

			if (mID1 < EGeometry::MESH_TYPE_COUNT)
				return false;

			return false;
		}

		// case: both objects are built-in types
		else
		{
			return mID0 < mID1;
		}
	};
	auto SortByMaterialID = [](const GameObject* pObj0, const GameObject* pObj1)
	{
		// TODO:
		return true;
	};
	auto SortByViewSpaceDepth = [](const GameObject* pObj0, const GameObject* pObj1)
	{
		// TODO:
		return true;
	};
	//---------------------------------------------------------------------------------------------

	std::sort(RANGE(mSceneView.culledOpaqueList), SortByMeshType);
	std::sort(RANGE(mainViewShadowCasterRenderList), SortByMeshType);
	for (const Light* pLight : pShadowingLights)
	{
		switch (pLight->mType)
		{

		case Light::ELightType::POINT:
		{
			for (int i = 0; i < 6; ++i)
			{
				;
			}
			break;
		}
		case Light::ELightType::SPOT:
		{
			RenderList& lightRenderList = mShadowView.shadowMapRenderListLookUp.at(pLight);
			std::sort(RANGE(lightRenderList), SortByMeshType);
			break;
		}
		}

	}
}

Scene::SceneShadowingLightIndexCollection Scene::CullShadowingLights(int& outNumCulledPoints, int& outNumCulledSpots)
{
	using namespace VQEngine;
	
	SceneShadowingLightIndexCollection sceneShadowingLightIndexCollection;

	outNumCulledPoints = 0;
	outNumCulledSpots = 0;

	auto fnCullLights = [&](const std::vector<Light>& lights) -> ShadowingLightIndexCollection
	{
		ShadowingLightIndexCollection outLightIndices;

		for (int i = 0; i < lights.size(); ++i)
		{
			if (!lights[i].mbCastingShadows) continue;
			
			const Light& l = lights[i];
			switch (l.mType)
			{
			case Light::ELightType::SPOT:
			{
				// TODO: frustum - cone check
				const bool bCullLight = false;
				if (!bCullLight /* == IsVisible() */)
				{
					outLightIndices.spotLightIndices.push_back(i);
				}
				else
				{
					++outNumCulledSpots;
				}
			}	break;

			case Light::ELightType::POINT:
			{
				vec3 vecCamera = GetActiveCamera().GetPositionF() - l.mTransform._position;
				const float dstSqrCameraToLight = XMVector3Dot(vecCamera, vecCamera).m128_f32[0];
				const float rangeSqr = l.mRange * l.mRange;

				const bool bIsCameraInPointLightSphereOfIncluence = dstSqrCameraToLight < rangeSqr;
				const bool bSphereInFrustum = IsSphereInFrustum(GetActiveCamera().GetViewFrustumPlanes(), Sphere(l.mTransform._position, l.mRange));

				if (bIsCameraInPointLightSphereOfIncluence || bSphereInFrustum)
				{
					outLightIndices.pointLightIndices.push_back(i);
				}
				else
				{
					++outNumCulledPoints;
				}
			} break;
			} // light type

		}

		return outLightIndices;
	};

	sceneShadowingLightIndexCollection.mStaticLights  = fnCullLights(mLightsStatic);
	sceneShadowingLightIndexCollection.mDynamicLights = fnCullLights(mLightsDynamic);
	return sceneShadowingLightIndexCollection;
}

std::vector<const GameObject*> Scene::FrustumCullMainView(int& outNumCulledObjects)
{
	using namespace VQEngine;

	const bool& bCullMainView = mSceneRenderSettings.optimization.bViewFrustumCull_MainView;

	std::vector<const GameObject*> mainViewRenderList;

	//mpCPUProfiler->BeginEntry("[Cull Main SceneView]");
	if (bCullMainView)
	{
		outNumCulledObjects = static_cast<int>(CullGameObjects(
			FrustumPlaneset::ExtractFromMatrix(mSceneView.viewProj)
			, mSceneView.opaqueList
			, mainViewRenderList));
	}
	else
	{
		mainViewRenderList.resize(mSceneView.opaqueList.size());
		std::copy(RANGE(mSceneView.opaqueList), mainViewRenderList.begin());
		outNumCulledObjects = 0;
	}
	//mpCPUProfiler->EndEntry();

	return mainViewRenderList;
}


void Scene::FrustumCullPointAndSpotShadowViews(
	  const std::vector <const GameObject*>&	mainViewShadowCasterRenderList
	, const SceneShadowingLightIndexCollection& shadowingLightIndices
	, FrameStats&								stats
)
{
	using namespace VQEngine;


	auto fnCullPointLightView = [&](const Light* l, const std::array<FrustumPlaneset, 6>& frustumPlaneSetPerFace)
	{
#if SHADOW_PASS_USE_INSTANCED_DRAW_DATA
		std::array< MeshDrawData, 6>& meshDrawDataPerFace = mShadowView.shadowCubeMapMeshDrawListLookup[l];
		for (int i = 0; i < 6; ++i)
			meshDrawDataPerFace[i].meshTransformListLookup.clear();
#else
		std::array< MeshDrawList, 6>& meshListForPoints = mShadowView.shadowCubeMapMeshDrawListLookup[l];
		for (int i = 0; i < 6; ++i)
			meshListForPoints[i].clear();
#endif

		// cull for far distance
		std::vector<const GameObject*> filteredMainViewShadowCasterList(mainViewShadowCasterRenderList.size(), nullptr);
		int numObjs = 0;
		for (const GameObject* pObj : mainViewShadowCasterRenderList)
		{
			const XMMATRIX matWorld = pObj->GetTransform().WorldTransformationMatrix();
			BoundingBox BB = pObj->GetAABB(); // local space AABB (this is wrong, AABB should be calculated on update()).
			BB.low = XMVector3Transform(BB.low, matWorld);
			BB.hi  = XMVector3Transform(BB.hi , matWorld); // world space BB
			if (IsBoundingBoxInsideSphere_Approx(BB, Sphere(l->GetTransform()._position, l->mRange)))
				filteredMainViewShadowCasterList[numObjs++] = pObj;
		}
		if (numObjs == 0)
		{
			return;
		}

		// cull for visibility per face
		for (int face = 0; face < 6; ++face)
		{
			const Texture::CubemapUtility::ECubeMapLookDirections CubemapFaceDirectionEnum = static_cast<Texture::CubemapUtility::ECubeMapLookDirections>(face);
			for (const GameObject* pObj : filteredMainViewShadowCasterList)
			{
				if (!pObj) // stop at first null game object because we resize @filteredMainViewShadowCasterList when creating it.
					break;

#if SHADOW_PASS_USE_INSTANCED_DRAW_DATA
				stats.scene.numPointsCulledObjects += static_cast<int>(CullMeshes
				(
					frustumPlaneSetPerFace[face],
					pObj,
					meshDrawDataPerFace[face]
				));
#else
				meshDrawData.meshIDs.clear();
				stats.scene.numPointsCulledObjects += static_cast<int>(CullMeshes
				(
					frustumPlaneSetPerFace[face],
					pObj,
					meshDrawData
				));
				meshListForPoints[face].push_back(meshDrawData);
#endif
			}
		}
	};
	auto fnCullSpotLightView = [&](const Light* l, const FrustumPlaneset& frustumPlaneSet)
	{
		RenderList& renderList = mShadowView.shadowMapRenderListLookUp[l];
		
		renderList.clear();
		stats.scene.numSpotsCulledObjects += static_cast<int>(
			CullGameObjects(
				  frustumPlaneSet 
				, mainViewShadowCasterRenderList
				, renderList
			));
	};


	RenderList objList;

	// Culling Disabled: just copy the mainViewShadowCasterRenderList
	//                   to the render lists of lights without any culling.
	//
	if (!mSceneRenderSettings.optimization.bViewFrustumCull_LocalLights)
	{
		// spots
		for (int i = 0; i < shadowingLightIndices.mStaticLights.spotLightIndices.size(); ++i)
		{
			const Light* l = &mLightsStatic[shadowingLightIndices.mStaticLights.spotLightIndices[i]];

			RenderList& renderList = mShadowView.shadowMapRenderListLookUp[l];
			renderList.resize(mainViewShadowCasterRenderList.size());
			std::copy(RANGE(mainViewShadowCasterRenderList), renderList.begin());
		}
		for (int i = 0; i < shadowingLightIndices.mDynamicLights.spotLightIndices.size(); ++i)
		{
			const Light* l = &mLightsStatic[shadowingLightIndices.mDynamicLights.spotLightIndices[i]];

			RenderList& renderList = mShadowView.shadowMapRenderListLookUp[l];
			renderList.resize(mainViewShadowCasterRenderList.size());
			std::copy(RANGE(mainViewShadowCasterRenderList), renderList.begin());
		}


		// points
		auto fnCopyPointLightRenderLists = [&](const std::vector<Light>& lightContainer, const std::vector<int>& lightIndices)
		{
			for (int i = 0; i < lightIndices.size(); ++i)
			{
				const Light* l = &lightContainer[lightIndices[i]];

#if SHADOW_PASS_USE_INSTANCED_DRAW_DATA
				std::array< MeshDrawData, 6>& meshDrawDataPerFace = mShadowView.shadowCubeMapMeshDrawListLookup[l];
#else
				std::array< MeshDrawList, 6>& meshListForPoints = mShadowView.shadowCubeMapMeshDrawListLookup[l];
#endif

				for (int face = 0; face < 6; ++face)
				{
					for (const GameObject* pObj : mainViewShadowCasterRenderList)
					{
#if SHADOW_PASS_USE_INSTANCED_DRAW_DATA
						const XMMATRIX matWorld = pObj->GetTransform().WorldTransformationMatrix();
						for (MeshID meshID : pObj->GetModelData().mMeshIDs)
							meshDrawDataPerFace[face].AddMeshTransformation(meshID, matWorld);
#else
						meshListForPoints[face].push_back(pObj);
#endif
					}
				}
			}
		};

		fnCopyPointLightRenderLists(mLightsStatic , shadowingLightIndices.mStaticLights.pointLightIndices);
		fnCopyPointLightRenderLists(mLightsDynamic, shadowingLightIndices.mDynamicLights.pointLightIndices);
		return;
	}



	// Culling Enabled : cull mainViewShadowCasterRenderList against 
	//                   light frustums.
	//
	// record the light stats
	stats.scene.numSpots  = static_cast<int>(shadowingLightIndices.GetLightCount(Light::ELightType::SPOT) );
	stats.scene.numPoints = static_cast<int>(shadowingLightIndices.GetLightCount(Light::ELightType::POINT));


	// Cull Static Lights
	mpCPUProfiler->BeginEntry("Cull_ShadowView_Point_s");
	{
		const std::vector<int>& lightIndexContainer = shadowingLightIndices.mStaticLights.pointLightIndices;
		for (int i = 0; i < lightIndexContainer.size(); ++i)	// point lights
		{
			int lightIndex = lightIndexContainer[i];
			const Light* l = &mLightsStatic[lightIndex];
			fnCullPointLightView(l, mStaticLightCache.mStaticPointLightFrustumPlanes.at(l));
		}
	}
	mpCPUProfiler->EndEntry();

	mpCPUProfiler->BeginEntry("Cull_ShadowView_Spot_s");
	{
		const std::vector<int>& lightIndexContainer = shadowingLightIndices.mStaticLights.spotLightIndices;
		for (int i = 0; i < lightIndexContainer.size(); ++i) // spot lights
		{
			int lightIndex = lightIndexContainer[i];
			const Light* l = &mLightsStatic[lightIndex];
			fnCullSpotLightView(&mLightsStatic[lightIndex], mStaticLightCache.mStaticSpotLightFrustumPlanes.at(l));
		}
	}
	mpCPUProfiler->EndEntry();

	// Cull Dynamic Lights
	mpCPUProfiler->BeginEntry("Cull_ShadowView_Point_d");
	for (int lightIndex : shadowingLightIndices.mDynamicLights.pointLightIndices)
	{
		std::array<FrustumPlaneset, 6> frustumPlanesPerFace =
		{
			  mLightsDynamic[lightIndex].GetViewFrustumPlanes(static_cast<Texture::CubemapUtility::ECubeMapLookDirections>(0))
			, mLightsDynamic[lightIndex].GetViewFrustumPlanes(static_cast<Texture::CubemapUtility::ECubeMapLookDirections>(1))
			, mLightsDynamic[lightIndex].GetViewFrustumPlanes(static_cast<Texture::CubemapUtility::ECubeMapLookDirections>(2))
			, mLightsDynamic[lightIndex].GetViewFrustumPlanes(static_cast<Texture::CubemapUtility::ECubeMapLookDirections>(3))
			, mLightsDynamic[lightIndex].GetViewFrustumPlanes(static_cast<Texture::CubemapUtility::ECubeMapLookDirections>(4))
			, mLightsDynamic[lightIndex].GetViewFrustumPlanes(static_cast<Texture::CubemapUtility::ECubeMapLookDirections>(5))
		};
		fnCullPointLightView(&mLightsDynamic[lightIndex], frustumPlanesPerFace);
	}
	mpCPUProfiler->EndEntry();

	mpCPUProfiler->BeginEntry("Cull_ShadowView_Spot_d");
	for (int lightIndex : shadowingLightIndices.mDynamicLights.spotLightIndices)
	{
		const Light* l = &mLightsDynamic[lightIndex];
		fnCullSpotLightView(l, l->GetViewFrustumPlanes());
	}
	mpCPUProfiler->EndEntry();
}

void Scene::OcclusionCullDirectionalLightView()
{
	// TODO: consider this for directionals: http://stefan-s.net/?p=92 or umbra paper
}

void Scene::BatchMainViewRenderList(const std::vector<const GameObject*> mainViewRenderList)
{
	for (int i = 0; i < mainViewRenderList.size(); ++i)
	{
		const GameObject* pObj = mainViewRenderList[i];
		const ModelData& model = pObj->GetModelData();
		const MeshID meshID = model.mMeshIDs.empty() ? -1 : model.mMeshIDs.front();

		// instanced is only for built-in meshes (for now)
		if (meshID >= EGeometry::MESH_TYPE_COUNT || SceneResourceView::GetMeshRenderMode(this, pObj, meshID) == MeshRenderSettings::WIREFRAME)
		{
			mSceneView.culledOpaqueList.push_back(std::move(mainViewRenderList[i]));
			continue;
		}

		const bool bMeshHasMaterial = model.mMaterialLookupPerMesh.find(meshID) != model.mMaterialLookupPerMesh.end();
		if (bMeshHasMaterial)
		{
			const MaterialID materialID = model.mMaterialLookupPerMesh.at(meshID);
			const Material* pMat = mMaterials.GetMaterial_const(materialID);
			if (pMat->HasTexture())
			{
				mSceneView.culledOpaqueList.push_back(std::move(mainViewRenderList[i]));
				continue;
			}
		}

		RenderListLookup& instancedRenderLists = mSceneView.culluedOpaqueInstancedRenderListLookup;
		if (instancedRenderLists.find(meshID) == instancedRenderLists.end())
		{
			instancedRenderLists[meshID] = std::vector<const GameObject*>();
		}

		std::vector<const GameObject*>& renderList = instancedRenderLists.at(meshID);
		renderList.push_back(std::move(mainViewRenderList[i]));
	}
}

void Scene::BatchShadowViewRenderLists(const std::vector <const GameObject*>& mainViewShadowCasterRenderList)
{
	std::unordered_map<MeshID, std::vector<const GameObject*>>& instancedCasterLists = mShadowView.RenderListsPerMeshType;

	mpCPUProfiler->BeginEntry("Batch_DirectionalView");
	for (int i = 0; i < mainViewShadowCasterRenderList.size(); ++i)
	{
		const GameObject* pCaster = mainViewShadowCasterRenderList[i];
		const ModelData&  model   = pCaster->GetModelData();
		const MeshID      meshID  = model.mMeshIDs.empty() ? -1 : model.mMeshIDs.front();
		if (meshID >= EGeometry::MESH_TYPE_COUNT)
		{
			mShadowView.casters.push_back(std::move(mainViewShadowCasterRenderList[i]));
			continue;
		}

		if (instancedCasterLists.find(meshID) == instancedCasterLists.end())
		{
			instancedCasterLists[meshID] = std::vector<const GameObject*>();
		}
		std::vector<const GameObject*>& renderList = instancedCasterLists.at(meshID);
		renderList.push_back(std::move(mainViewShadowCasterRenderList[i]));
	}
	mpCPUProfiler->EndEntry();

}

void Scene::SetLightCache()
{
	mStaticLightCache.Clear();

	// cache/set static light data
	//
	// static lights are assumed to not change position and rotations,
	// hence, we can cache their frustum planes which are extracted 
	// from the light-space matrix. This cache would save us matrix 
	// multiplication and frustum extraction instructions during 
	// PreRender() - frustum cull lights phase.
	//
	for (Light& l : mLightsStatic)
	{
		l.SetMatrices();
		switch (l.mType)
		{
		case Light::ELightType::SPOT:
			mStaticLightCache.mStaticSpotLightFrustumPlanes[&l] = l.GetViewFrustumPlanes();
			break;
		case Light::ELightType::POINT:
		{
			std::array< FrustumPlaneset, 6> planesetPerFace;
			for (int i = 0; i < 6; ++i)
				planesetPerFace[i] = l.GetViewFrustumPlanes(static_cast<Texture::CubemapUtility::ECubeMapLookDirections>(i));
			mStaticLightCache.mStaticPointLightFrustumPlanes[&l] = planesetPerFace;
		}	break;
		} // l.type
	}

	// cache/set dynamic light data
	for (Light& l : mLightsDynamic)
		l.SetMatrices();


	// special case for directional lights for now...
	// this should eventually be processed with the 
	// containers above, potentially in the static one.
	if (mDirectionalLight.mbEnabled)
	{
		mDirectionalLight.SetMatrices();
	}
}

void Scene::ClearLights()
{
	mLightsDynamic.clear();
	mLightsStatic.clear();
	mStaticLightCache.Clear();
}

//static void CalculateSceneBoundingBox(Scene* pScene, )
void Scene::CalculateSceneBoundingBox()
{
	// get the objects for the scene
	std::vector<GameObject*> pObjects;
	for (GameObject& obj : mObjectPool.mObjects)
	{
		if (obj.mpScene == this && obj.mRenderSettings.bRender)
		{
			pObjects.push_back(&obj);
		}
	}

	constexpr float max_f = std::numeric_limits<float>::max();
	vec3 mins(max_f);
	vec3 maxs(-(max_f - 1.0f));
	PerfTimer timer;
	timer.Start();
	std::for_each(RANGE(pObjects), [&](GameObject* pObj)
	{
		XMMATRIX worldMatrix = pObj->GetTransform().WorldTransformationMatrix();

		vec3 mins_obj(max_f);
		vec3 maxs_obj(-(max_f - 1.0f));

		const ModelData& modelData = pObj->GetModelData();
		pObj->mMeshBoundingBoxes.clear();
		std::for_each(RANGE(modelData.mMeshIDs), [&](const MeshID& meshID)
		{
			const BufferID VertexBufferID = mMeshIDs[meshID].GetIABuffers().first;
			const Buffer& VertexBuffer = mpRenderer->GetVertexBuffer(VertexBufferID);
			const size_t numVerts = VertexBuffer.mDesc.mElementCount;
			const size_t stride = VertexBuffer.mDesc.mStride;

			constexpr size_t defaultSz = sizeof(DefaultVertexBufferData);
			constexpr float DegenerateMeshPositionChannelValueMax = 15000.0f; // make sure no vertex.xyz is > 15,000.0f

			// #SHADER REFACTOR:
			//
			// currently all the shader input is using default vertex buffer data.
			// we just make sure that we can interpret the position data properly here
			// by ensuring the vertex buffer stride for a given mesh matches
			// the default vertex buffer.
			//
			// TODO:
			// Type information is not preserved once the vertex/index buffer is created.
			// need to figure out a way to interpret the position data in a given buffer
			//
			if (stride == defaultSz)
			{
				const DefaultVertexBufferData* pData = reinterpret_cast<const DefaultVertexBufferData*>(VertexBuffer.mpCPUData);
				if (pData == nullptr)
				{
					Log::Info("Nope: %d", int(stride));
					return;
				}

				vec3 mins_mesh(max_f);
				vec3 maxs_mesh(-(max_f - 1.0f));
				for (int i = 0; i < numVerts; ++i)
				{
					const float x_mesh_local = pData[i].position.x();
					const float y_mesh_local = pData[i].position.y();
					const float z_mesh_local = pData[i].position.z();

					mins_mesh = vec3
					(
						std::min(x_mesh_local, mins_mesh.x()),
						std::min(y_mesh_local, mins_mesh.y()),
						std::min(z_mesh_local, mins_mesh.z())
					);
					maxs_mesh = vec3
					(
						std::max(x_mesh_local, maxs_mesh.x()),
						std::max(y_mesh_local, maxs_mesh.y()),
						std::max(z_mesh_local, maxs_mesh.z())
					);


					const vec3 worldPos = vec3(XMVector4Transform(vec4(pData[i].position, 1.0f), worldMatrix));
					const float x_mesh = std::min(worldPos.x(), DegenerateMeshPositionChannelValueMax);
					const float y_mesh = std::min(worldPos.y(), DegenerateMeshPositionChannelValueMax);
					const float z_mesh = std::min(worldPos.z(), DegenerateMeshPositionChannelValueMax);


					// scene bounding box - world space
					mins = vec3
					(
						std::min(x_mesh, mins.x()),
						std::min(y_mesh, mins.y()),
						std::min(z_mesh, mins.z())
					);
					maxs = vec3
					(
						std::max(x_mesh, maxs.x()),
						std::max(y_mesh, maxs.y()),
						std::max(z_mesh, maxs.z())
					);

					// object bounding box - model space
					mins_obj = vec3
					(
						std::min(x_mesh_local, mins_obj.x()),
						std::min(y_mesh_local, mins_obj.y()),
						std::min(z_mesh_local, mins_obj.z())
					);
					maxs_obj = vec3
					(
						std::max(x_mesh_local, maxs_obj.x()),
						std::max(y_mesh_local, maxs_obj.y()),
						std::max(z_mesh_local, maxs_obj.z())
					);
				}

				pObj->mMeshBoundingBoxes.push_back(BoundingBox({ mins_mesh, maxs_mesh }));
			}
			else
			{
				Log::Warning("Unsupported vertex stride for mesh.");
			}
		});

		pObj->mBoundingBox.hi = maxs_obj;
		pObj->mBoundingBox.low = mins_obj;
	});

	timer.Stop();
	Log::Info("SceneBoundingBox:lo=(%.2f, %.2f, %.2f)\thi=(%.2f, %.2f, %.2f) in %.2fs"
		, mins.x() , mins.y() , mins.z()
		, maxs.x() , maxs.y() , maxs.z()
		, timer.DeltaTime()
	);
	this->mSceneBoundingBox.hi = maxs;
	this->mSceneBoundingBox.low = mins;
}



//----------------------------------------------------------------------------------------------------------------
// RESOURCE MANAGEMENT FUNCTIONS
//----------------------------------------------------------------------------------------------------------------
GameObject* Scene::CreateNewGameObject(){ mpObjects.push_back(mObjectPool.Create(this)); return mpObjects.back(); }

void Scene::AddLight(const Light& l)
{
	void (Scene::*pfnAddLight)(const Light&) = /* l.mbStatic */ true
		// TODO: add boolean to Transform for specifying if its static or not
		? &Scene::AddStaticLight
		: &Scene::AddDynamicLight;
	(this->*pfnAddLight)(l);
}

Material* Scene::CreateNewMaterial(EMaterialType type){ return static_cast<Material*>(mMaterials.CreateAndGetMaterial(type)); }
Material* Scene::CreateRandomMaterialOfType(EMaterialType type) { return static_cast<Material*>(mMaterials.CreateAndGetRandomMaterial(type)); }


//----------------------------------------------------------------------------------------------------------------
// RENDER FUNCTIONS
//----------------------------------------------------------------------------------------------------------------
int Scene::RenderOpaque(const FSceneView& sceneView) const
{
	//-----------------------------------------------------------------------------------------------

	struct ObjectMatrices_WorldSpace
	{
		XMMATRIX wvp;
		XMMATRIX w;
		XMMATRIX n;
	};
	auto ShouldSendMaterial = [](EShaders shader)
	{
		return (shader == EShaders::FORWARD_PHONG
			|| shader == EShaders::UNLIT
			|| shader == EShaders::NORMAL
			|| shader == EShaders::FORWARD_BRDF);
	};
	auto RenderObject = [&](const GameObject* pObj)
	{
		const Transform& tf = pObj->GetTransform();
		const ModelData& model = pObj->GetModelData();

		const EShaders shader = static_cast<EShaders>(mpRenderer->GetActiveShader());
		const XMMATRIX world = tf.WorldTransformationMatrix();
		const XMMATRIX wvp = world * sceneView.viewProj;

		switch (shader)
		{
		case EShaders::TBN:
			mpRenderer->SetConstant4x4f("world", world);
			mpRenderer->SetConstant4x4f("viewProj", sceneView.viewProj);
			mpRenderer->SetConstant4x4f("normalMatrix", tf.NormalMatrix(world));
			break;
		case EShaders::NORMAL:
			mpRenderer->SetConstant4x4f("normalMatrix", tf.NormalMatrix(world));
		case EShaders::UNLIT:
		case EShaders::TEXTURE_COORDINATES:
			mpRenderer->SetConstant4x4f("worldViewProj", wvp);
			break;
		default:	// lighting shaders
		{
			const ObjectMatrices_WorldSpace mats =
			{
				wvp,
				world,
				tf.NormalMatrix(world)
			};
			mpRenderer->SetConstantStruct("ObjMatrices", &mats);
			break;
		}
		}

		// SET GEOMETRY & MATERIAL, THEN DRAW
		mpRenderer->SetRasterizerState(EDefaultRasterizerState::CULL_BACK);
		for (MeshID id : model.mMeshIDs)
		{
			const auto IABuffer = mMeshIDs[id].GetIABuffers(mLODManager.GetLODValue(pObj, id));

			// SET MATERIAL CONSTANTS
			if (ShouldSendMaterial(shader))
			{
				const bool bMeshHasMaterial = model.mMaterialLookupPerMesh.find(id) != model.mMaterialLookupPerMesh.end();
				if (bMeshHasMaterial)
				{
					const MaterialID materialID = model.mMaterialLookupPerMesh.at(id);
					const Material* pMat = mMaterials.GetMaterial_const(materialID);
					// #TODO: uncomment below when transparency is implemented.
					//if (pMat->IsTransparent())	// avoidable branching - perhaps keeping opaque and transparent meshes on separate vectors is better.
					//	return;
					pMat->SetMaterialConstants(mpRenderer, shader, sceneView.bIsDeferredRendering);
				}
				else
				{
					mMaterials.GetDefaultMaterial(GGX_BRDF)->SetMaterialConstants(mpRenderer, shader, sceneView.bIsDeferredRendering);
				}
			}

			mpRenderer->SetVertexBuffer(IABuffer.first);
			mpRenderer->SetIndexBuffer(IABuffer.second);
			mpRenderer->Apply();
			mpRenderer->DrawIndexed();
		};
	};
	//-----------------------------------------------------------------------------------------------

	// RENDER NON-INSTANCED SCENE OBJECTS
	//
	int numObj = 0;
	for (const auto* obj : mSceneView.culledOpaqueList)
	{
		RenderObject(obj);
		++numObj;
	}



	return numObj;
}

int Scene::RenderAlpha(const FSceneView & sceneView) const
{
	const ShaderID selectedShader = ENGINE->GetSelectedShader();
	const bool bSendMaterialData = (
		selectedShader == EShaders::FORWARD_PHONG
		|| selectedShader == EShaders::UNLIT
		|| selectedShader == EShaders::NORMAL
		|| selectedShader == EShaders::FORWARD_BRDF
		);

	int numObj = 0;
	for (const auto* obj : mSceneView.alphaList)
	{
		obj->RenderTransparent(mpRenderer, sceneView, bSendMaterialData, mMaterials);
		++numObj;
	}
	return numObj;
}

int Scene::RenderDebug(const XMMATRIX& viewProj) const
{
	const bool bRenderPointLightCues = true;
	const bool bRenderSpotLightCues = true;
	const bool bRenderObjectBoundingBoxes = false;
	const bool bRenderMeshBoundingBoxes = false;
	const bool bRenderSceneBoundingBox = true;

	const auto IABuffersCube = mMeshIDs[EGeometry::CUBE].GetIABuffers();
	const auto IABuffersSphere = mMeshIDs[EGeometry::SPHERE].GetIABuffers();
	const auto IABuffersCone = mMeshIDs[EGeometry::LIGHT_CUE_CONE].GetIABuffers();

	XMMATRIX wvp;

	// set debug render states
	mpRenderer->SetShader(EShaders::UNLIT);
	mpRenderer->SetConstant3f("diffuse", LinearColor::yellow);
	mpRenderer->SetConstant1f("isDiffuseMap", 0.0f);
	mpRenderer->BindDepthTarget(ENGINE->GetWorldDepthTarget());
	mpRenderer->SetDepthStencilState(EDefaultDepthStencilState::DEPTH_TEST_ONLY);
	mpRenderer->SetBlendState(EDefaultBlendState::DISABLED);
	mpRenderer->SetRasterizerState(EDefaultRasterizerState::WIREFRAME);
	mpRenderer->SetVertexBuffer(IABuffersCube.first);
	mpRenderer->SetIndexBuffer(IABuffersCube.second);


	// SCENE BOUNDING BOX
	//
	if (bRenderSceneBoundingBox)
	{
		wvp = mSceneBoundingBox.GetWorldTransformationMatrix() * viewProj;
		mpRenderer->SetConstant4x4f("worldViewProj", wvp);
		mpRenderer->Apply();
		mpRenderer->DrawIndexed();
	}

	// GAME OBJECT OBB & MESH BOUNDING BOXES
	//
	int numRenderedObjects = 0;
	if (bRenderObjectBoundingBoxes)
	{
		std::vector<const GameObject*> pObjects(
			mSceneView.opaqueList.size() + mSceneView.alphaList.size()
			, nullptr
		);
		std::copy(RANGE(mSceneView.opaqueList), pObjects.begin());
		std::copy(RANGE(mSceneView.alphaList), pObjects.begin() + mSceneView.opaqueList.size());
		for (const GameObject* pObj : pObjects)
		{
			const XMMATRIX matWorld = pObj->GetTransform().WorldTransformationMatrix();
			wvp = pObj->mBoundingBox.GetWorldTransformationMatrix() * matWorld * viewProj;
#if 1
			mpRenderer->SetConstant3f("diffuse", LinearColor::cyan);
			mpRenderer->SetConstant4x4f("worldViewProj", wvp);
			mpRenderer->Apply();
			mpRenderer->DrawIndexed();
#endif

			// mesh bounding boxes (currently broken...)
			if (bRenderMeshBoundingBoxes)
			{
				mpRenderer->SetConstant3f("diffuse", LinearColor::orange);
#if 0
				for (const MeshID meshID : pObj->mModel.mData.mMeshIDs)
				{
					wvp = pObj->mMeshBoundingBoxes[meshID].GetWorldTransformationMatrix() * matWorld * viewProj;
					mpRenderer->SetConstant4x4f("worldViewProj", wvp);
					mpRenderer->Apply();
					mpRenderer->DrawIndexed();
				}
#endif
			}
		}
		numRenderedObjects = (int)pObjects.size();
	}

	// LIGHT VOLUMES
	//
	mpRenderer->SetConstant3f("diffuse", LinearColor::yellow);

	// light's transform hold Translation and Rotation data,
	// Scale is used for rendering. Hence, we use another transform
	// here to use mRange as the lights render scale signifying its 
	// radius of influence.
	Transform tf;
	constexpr size_t NUM_LIGHT_CONTAINERS = 2;
	std::array<const std::vector<Light>*, NUM_LIGHT_CONTAINERS > lightContainers =
	{
		  &mLightsStatic
		, &mLightsDynamic
	};
	for (int i = 0; i < NUM_LIGHT_CONTAINERS; ++i)
	{
		const std::vector<Light>& mLights = *lightContainers[i];
		// point lights
		if (bRenderPointLightCues)
		{
			mpRenderer->SetVertexBuffer(IABuffersSphere.first);
			mpRenderer->SetIndexBuffer(IABuffersSphere.second);
			for (const Light& l : mLights)
			{
				if (l.mType == Light::ELightType::POINT)
				{
					mpRenderer->SetConstant3f("diffuse", l.mColor);

					tf.SetPosition(l.mTransform._position);
					tf.SetScale(l.mRange * 0.5f); // Mesh's model space R = 2.0f, hence scale it by 0.5f...
					wvp = tf.WorldTransformationMatrix() * viewProj;
					mpRenderer->SetConstant4x4f("worldViewProj", wvp);
					mpRenderer->Apply();
					mpRenderer->DrawIndexed();
				}
			}
		}

		// spot lights 
		if (bRenderSpotLightCues)
		{
			mpRenderer->SetVertexBuffer(IABuffersCone.first);
			mpRenderer->SetIndexBuffer(IABuffersCone.second);
			for (const Light& l : mLights)
			{
				if (l.mType == Light::ELightType::SPOT)
				{
					mpRenderer->SetConstant3f("diffuse", l.mColor);

					tf = l.mTransform;

					// reset scale as it holds the scale value for light's render mesh
					tf.SetScale(1, 1, 1);

					// align with spot light's local space
					tf.RotateAroundLocalXAxisDegrees(-90.0f);


					XMMATRIX alignConeToSpotLightTransformation = XMMatrixIdentity();
					alignConeToSpotLightTransformation.r[3].m128_f32[0] = 0.0f;
					alignConeToSpotLightTransformation.r[3].m128_f32[1] = -l.mRange;
					alignConeToSpotLightTransformation.r[3].m128_f32[2] = 0.0f;
					//tf.SetScale(1, 20, 1);

					const float coneBaseRadius = std::tanf(l.mSpotOuterConeAngleDegrees * DEG2RAD) * l.mRange;
					XMMATRIX scaleConeToRange = XMMatrixIdentity();
					scaleConeToRange.r[0].m128_f32[0] = coneBaseRadius;
					scaleConeToRange.r[1].m128_f32[1] = l.mRange;
					scaleConeToRange.r[2].m128_f32[2] = coneBaseRadius;

					//wvp = alignConeToSpotLightTransformation * tf.WorldTransformationMatrix() * viewProj;
					wvp = scaleConeToRange * alignConeToSpotLightTransformation * tf.WorldTransformationMatrix() * viewProj;
					mpRenderer->SetConstant4x4f("worldViewProj", wvp);
					mpRenderer->Apply();
					mpRenderer->DrawIndexed();
				}
			}
		}
	}


	// TODO: CAMERA FRUSTUM
	//
	if (mCameras.size() > 1 && mSelectedCamera != 0 && false)
	{
		// render camera[0]'s frustum
		const XMMATRIX viewProj = mCameras[mSelectedCamera].GetViewMatrix() * mCameras[mSelectedCamera].GetProjectionMatrix();

		auto a = FrustumPlaneset::ExtractFromMatrix(viewProj); // world space frustum plane equations

		// IA: model space camera frustum 
		// world matrix from camera[0] view ray
		// viewProj from camera[selected]
		mpRenderer->SetConstant3f("diffuse", LinearColor::orange);


		Transform tf;
		//const vec3 diag = this->hi - this->low;
		//const vec3 pos = (this->hi + this->low) * 0.5f;
		//tf.SetScale(diag * 0.5f);
		//tf.SetPosition(pos);
		XMMATRIX wvp = tf.WorldTransformationMatrix() * viewProj;
		//mpRenderer->SetConstant4x4f("worldViewProj", wvp);
		mpRenderer->Apply();
		mpRenderer->DrawIndexed();
	}

	mpRenderer->UnbindDepthTarget();
	mpRenderer->SetRasterizerState(EDefaultRasterizerState::CULL_NONE);
	mpRenderer->Apply();


	return numRenderedObjects; // objects rendered
}


void Scene::RenderLights() const
{
	if (!this->mSceneView.bIsIBLEnabled)
		return;

	mpRenderer->BeginEvent("Render Lights Pass");
	mpRenderer->SetShader(EShaders::UNLIT);
	mpRenderer->SetDepthStencilState(EDefaultDepthStencilState::DEPTH_TEST_ONLY);
	constexpr size_t NUM_LIGHT_CONTAINERS = 2;
	std::array<const std::vector<Light>*, NUM_LIGHT_CONTAINERS > lightContainers =
	{
		  &mLightsStatic
		, &mLightsDynamic
	};

	for (int i = 0; i < NUM_LIGHT_CONTAINERS; ++i)
	{
		const std::vector<Light>& mLights = *lightContainers[i];
		for (const Light& light : mLights)
		{
			//if (!light._bEnabled) continue; // #BreaksRelease

			if (light.mType == Light::ELightType::DIRECTIONAL)
				continue;	// do not render directional lights

			const auto IABuffers = mBuiltinMeshes[light.mMeshID].GetIABuffers();
			const XMMATRIX world = light.mTransform.WorldTransformationMatrix();
			const XMMATRIX worldViewProj = world * this->mSceneView.viewProj;
			const vec3 color = light.mColor.Value() * light.mBrightness;

			mpRenderer->SetVertexBuffer(IABuffers.first);
			mpRenderer->SetIndexBuffer(IABuffers.second);
			mpRenderer->SetConstant4x4f("worldViewProj", worldViewProj);
			mpRenderer->SetConstant3f("diffuse", color);
			mpRenderer->SetConstant1f("isDiffuseMap", 0.0f);
			mpRenderer->Apply();
			mpRenderer->DrawIndexed();
		}
	}
	mpRenderer->EndEvent();
}


void Scene::RenderSkybox(const XMMATRIX& viewProj) const
{
	const XMMATRIX& wvp = viewProj;
	const auto IABuffers = Scene::GetGeometryVertexAndIndexBuffers(EGeometry::CUBE);

	mpRenderer->BeginEvent("Skybox Pass");
	mpRenderer->SetViewport(mpRenderer->FrameRenderTargetDimensionsAsFloat2());
	mpRenderer->SetShader(mSkybox.GetShader());
	mpRenderer->SetDepthStencilState(EDefaultDepthStencilState::DEPTH_TEST_ONLY);
	mpRenderer->SetRasterizerState(EDefaultRasterizerState::CULL_NONE);
	mpRenderer->SetConstant4x4f("worldViewProj", wvp);
	mpRenderer->SetTexture("texSkybox", mSkybox.GetSkyboxTexture());
	//mpRenderer->SetSamplerState("samWrap", EDefaultSamplerState::WRAP_SAMPLER);
	mpRenderer->SetSamplerState("samWrap", EDefaultSamplerState::LINEAR_FILTER_SAMPLER_WRAP_UVW);
	mpRenderer->SetVertexBuffer(IABuffers.first);
	mpRenderer->SetIndexBuffer(IABuffers.second);
	mpRenderer->Apply();
	mpRenderer->DrawIndexed();
	mpRenderer->EndEvent();
}


#endif