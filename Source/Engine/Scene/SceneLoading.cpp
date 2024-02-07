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

#define NOMINMAX

#include "Scene.h"
#include "../Core/Window.h"
#include "../VQEngine.h"
#include "../GPUMarker.h"

#include "Libs/VQUtils/Source/utils.h"

#include <fstream>

#define LOG_CACHED_RESOURCES_ON_LOAD 0
#define LOG_RESOURCE_CREATE          1

using namespace DirectX;

MaterialID Scene::LoadMaterial(const FMaterialRepresentation& matRep, TaskID taskID)
{
	MaterialID id = this->CreateMaterial(matRep.Name);
	Material& mat = this->GetMaterial(id);

	auto fnAssignF = [](float& dest, const float& src) { if (src != MATERIAL_UNINITIALIZED_VALUE) dest = src; };
	auto fnAssignF3 = [](XMFLOAT3& dest, const XMFLOAT3& src) { if (src.x != MATERIAL_UNINITIALIZED_VALUE) dest = src; };
	auto fnEnqueueTexLoad = [&](MaterialID matID, const std::string& path, AssetLoader::ETextureType type) -> bool
	{
		if (path.empty())
			return false;

		AssetLoader::FTextureLoadParams p = {};
		p.MatID = matID;
		p.TexturePath = path;
		p.TexType = type;
		mAssetLoader.QueueTextureLoad(taskID, p);
		return true;
	};

	// immediate values
	fnAssignF(mat.alpha, matRep.Alpha);
	fnAssignF(mat.metalness, matRep.Metalness);
	fnAssignF(mat.roughness, matRep.Roughness);
	fnAssignF(mat.emissiveIntensity, matRep.EmissiveIntensity);
	fnAssignF3(mat.emissiveColor, matRep.EmissiveColor);
	fnAssignF3(mat.diffuse, matRep.DiffuseColor);

	// async data (textures)
	bool bHasTexture = false;
	bHasTexture |= fnEnqueueTexLoad(id, matRep.DiffuseMapFilePath, AssetLoader::ETextureType::DIFFUSE);
	bHasTexture |= fnEnqueueTexLoad(id, matRep.NormalMapFilePath, AssetLoader::ETextureType::NORMALS);
	bHasTexture |= fnEnqueueTexLoad(id, matRep.EmissiveMapFilePath, AssetLoader::ETextureType::EMISSIVE);
	bHasTexture |= fnEnqueueTexLoad(id, matRep.AlphaMaskMapFilePath, AssetLoader::ETextureType::ALPHA_MASK);
	bHasTexture |= fnEnqueueTexLoad(id, matRep.MetallicMapFilePath, AssetLoader::ETextureType::METALNESS);
	bHasTexture |= fnEnqueueTexLoad(id, matRep.RoughnessMapFilePath, AssetLoader::ETextureType::ROUGHNESS);
	bHasTexture |= fnEnqueueTexLoad(id, matRep.AOMapFilePath, AssetLoader::ETextureType::AMBIENT_OCCLUSION);

	AssetLoader::FMaterialTextureAssignment MatTexAssignment = {};
	MatTexAssignment.matID = id;
	if (bHasTexture)
		mMaterialAssignments.mAssignments.push_back(MatTexAssignment);
	return id;
}

void Scene::StartLoading(const BuiltinMeshArray_t& builtinMeshes, FSceneRepresentation& sceneRep, ThreadPool& UpdateWorkerThreadPool)
{
	SCOPED_CPU_MARKER("Scene::StartLoading()");

	mRenderer.WaitForLoadCompletion();

	Log::Info("[Scene] Loading Scene: %s", sceneRep.SceneName.c_str());
	const TaskID taskID = AssetLoader::GenerateModelLoadTaskID();


	mSceneRepresentation = sceneRep;

	LoadBuiltinMeshes(builtinMeshes);

	{
		SCOPED_CPU_MARKER("LoadScene()");
		this->LoadScene(sceneRep); // scene-specific load 
	}

	LoadBuiltinMaterials(taskID, sceneRep.Objects);
	LoadSceneMaterials(sceneRep.Materials, taskID);
	
	LoadGameObjects(std::move(sceneRep.Objects), UpdateWorkerThreadPool);
	LoadLights(sceneRep.Lights);
	LoadCameras(sceneRep.Cameras);
	LoadPostProcessSettings();

	{
		SCOPED_CPU_MARKER("ClearHistoryData");
		mViewProjectionMatrixHistory.clear();
	}
	mBoundingBoxHierarchy.Clear();
	{
		SCOPED_CPU_MARKER("ClearSceneViews");
		for (FSceneView& view : mFrameSceneViews)
		{
			view.MaterialMeshInstanceDataLookup.clear();
		}
	}
	{
		SCOPED_CPU_MARKER("ClearShadowViews");
		for (FSceneShadowView& view : mFrameShadowViews)
		{
			for (FSceneShadowView::FShadowView& sv : view.ShadowViews_Spot) sv.ShadowMeshInstanceDataLookup.clear();
			for (FSceneShadowView::FShadowView& sv : view.ShadowViews_Point) sv.ShadowMeshInstanceDataLookup.clear();
			view.ShadowView_Directional.ShadowMeshInstanceDataLookup.clear();
		}
	}
	mFrustumCullWorkerContext.ClearMemory();
}



void Scene::LoadBuiltinMaterials(TaskID taskID, const std::vector<FGameObjectRepresentation>& GameObjsToBeLoaded)
{
	SCOPED_CPU_MARKER("Scene::LoadBuiltinMaterials()");

	const char* STR_MATERIALS_FOLDER = "Data/Materials/";
	auto vMatFiles = DirectoryUtil::ListFilesInDirectory(STR_MATERIALS_FOLDER, "xml");

	// Parse builtin materials and build material map
	std::unordered_map<std::string, FMaterialRepresentation> MaterialMap;
	{
		std::vector<FMaterialRepresentation> vBuiltinMaterialReps;
		for (const std::string& filePath : vMatFiles) // for each material file
		{
			std::vector<FMaterialRepresentation> vMaterialReps = VQEngine::ParseMaterialFile(filePath); // get all materials in an xml file
			vBuiltinMaterialReps.insert(vBuiltinMaterialReps.end(), vMaterialReps.begin(), vMaterialReps.end());
		}

		// build map
		for (FMaterialRepresentation& rep : vBuiltinMaterialReps)
		{
			MaterialMap[StrUtil::GetLowercased(rep.Name)] = std::move(rep); // ensure lowercase comparison
		}
		vBuiltinMaterialReps.clear();
	}

	// look at which materials to be loaded from game objects that use builtin meshes
	std::set<std::string> vBuiltinMatsToLoad; // unique names
	for (const FGameObjectRepresentation& ObjRep : GameObjsToBeLoaded)
	{
		// Note: assumes only builtin meshes can load builtin materials.
		//       models go their own path.
		if (!ObjRep.BuiltinMeshName.empty() && !ObjRep.MaterialName.empty())
		{
			vBuiltinMatsToLoad.emplace(StrUtil::GetLowercased(ObjRep.MaterialName)); // ensure lowercase comparison
		}
	}


	// load the referenced builtin materials
	for (const std::string& matName : vBuiltinMatsToLoad)
	{
		auto it = MaterialMap.find(matName);
		const bool bMaterialFound = it != MaterialMap.end();
		if (bMaterialFound) // only the matching builtin materials, scene-specific materials won't be found here
			LoadMaterial(it->second, taskID);
	}
}

void Scene::LoadBuiltinMeshes(const BuiltinMeshArray_t& builtinMeshes)
{
	SCOPED_CPU_MARKER("Scene::LoadBuiltinMeshes()");

	// register builtin meshes to scene mesh lookup
	// @mMeshes[0-NUM_BUILTIN_MESHES] are assigned here directly while the rest
	// of the meshes used in the scene must use this->AddMesh(Mesh&&) interface;
	for (size_t i = 0; i < builtinMeshes.size(); ++i)
	{
		this->mMeshes[(int)i] = builtinMeshes[i];
	}

	// register builtin materials 
	{
		this->mDefaultMaterialID = this->CreateMaterial("DefaultMaterial");
		Material& mat = this->GetMaterial(this->mDefaultMaterialID);
	}
}


void Scene::BuildGameObject(const FGameObjectRepresentation& ObjRep, size_t iObj, size_t iTF)
{
	SCOPED_CPU_MARKER("BuildGameObject");

	// GameObject
	GameObject* pObj = mpObjects[iObj];
	pObj->mModelID = INVALID_ID;
	pObj->mTransformID = INVALID_ID;

	// Transform
	Transform* pTransform = mpTransforms[iTF];
	*pTransform = ObjRep.tf;
	mpTransforms[iTF] = pTransform;
	pObj->mTransformID = (TransformID)iTF;

	// Model
	const bool bModelIsBuiltinMesh = !ObjRep.BuiltinMeshName.empty();
	const bool bModelIsLoadedFromFile = !ObjRep.ModelFilePath.empty();
	assert(bModelIsBuiltinMesh != bModelIsLoadedFromFile);

	if (bModelIsBuiltinMesh)
	{
		ModelID mID = this->CreateModel();
		Model& model = mModels.at(mID);

		// create/get mesh
		MeshID meshID = mEngine.GetBuiltInMeshID(ObjRep.BuiltinMeshName);

		// material
		MaterialID matID = this->mDefaultMaterialID;
		if (!ObjRep.MaterialName.empty())
		{
			matID = this->CreateMaterial(ObjRep.MaterialName);
		}
		Material& mat = this->GetMaterial(matID);
		const bool bTransparentMesh = mat.IsTransparent();

		// model data
		model.mData = Model::Data(meshID, matID, Model::Data::OPAQUE_MESH);

		model.mbLoaded = true;
		pObj->mModelID = mID;
	}
	else
	{
		mAssetLoader.QueueModelLoad(pObj, ObjRep.ModelFilePath, ObjRep.ModelName);
	}

	mpObjects[iObj] = pObj;
}

void Scene::LoadGameObjects(std::vector<FGameObjectRepresentation>&& GameObjects, ThreadPool& WorkerThreadPool)
{
	const size_t NumGameObjects = GameObjects.size();

	SCOPED_CPU_MARKER_F("Scene::LoadGameObjects(N=%d)", NumGameObjects);
	constexpr bool B_LOAD_GAMEOBJECTS_SERIAL = false;
	constexpr size_t NUM_GAMEOBJECTS_THRESHOLD_FOR_THREADED_LOAD = 1024;


	size_t iTF = mpTransforms.size();
	size_t iObj = mpObjects.size();
	{
		SCOPED_CPU_MARKER("MemAlloc");
		mpTransforms.resize(mpTransforms.size() + NumGameObjects);
		mpObjects.resize   (mpObjects.size()    + NumGameObjects);
		Transform*  pTFArray  = mTransformPool.Allocate(NumGameObjects);
		GameObject* pObjArray = mGameObjectPool.Allocate(NumGameObjects);
		for (size_t i = 0; i < mpTransforms.size(); ++i)
		{
			mpTransforms[i] = &pTFArray[i];
			mpObjects[i] = &pObjArray[i];
		}
	}

	if (B_LOAD_GAMEOBJECTS_SERIAL || NumGameObjects < NUM_GAMEOBJECTS_THRESHOLD_FOR_THREADED_LOAD)
	{
		for (FGameObjectRepresentation& ObjRep : GameObjects)
		{
			BuildGameObject(ObjRep, iObj++, iTF++);
		}
	}
	else // THREADED LOAD
	{
		const size_t NumAvailableWorkers = WorkerThreadPool.GetThreadPoolSize();
		const size_t NumThreads = NumAvailableWorkers + 1;
		const std::vector<FGameObjectRepresentation>* pObjects = &GameObjects;

		std::vector<std::pair<size_t, size_t>> ranges = PartitionWorkItemsIntoRanges(NumGameObjects, NumThreads);
		{
			SCOPED_CPU_MARKER("DispatchThreads");
			for (size_t iRange = 1; iRange < ranges.size(); ++iRange)
			{
				WorkerThreadPool.AddTask([=]()
				{
					SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
					{
						SCOPED_CPU_MARKER_F("Range: [%d, %d]", ranges[iRange].first, ranges[iRange].second);
						const size_t NumItems_PrevRange = (ranges[iRange - 1].second - ranges[iRange - 1].first + 1);
						const size_t Offset = (ranges[iRange - 1].second - ranges[0].first + 1);
						size_t iTF_Thread  = iTF  + Offset;
						size_t iObj_Thread = iObj + Offset;

						for (size_t i = ranges[iRange].first; i <= ranges[iRange].second; ++i)
							BuildGameObject((*pObjects)[i], iObj_Thread++, iTF_Thread++);
					}
				});
			}
		}
		{
			SCOPED_CPU_MARKER_F("Range[%d,%d] ", ranges[0].first, ranges[0].second);
			for (size_t i = ranges.front().first; i <= ranges.front().second; ++i)
			{
				BuildGameObject(GameObjects[i], iObj++, iTF++);
			}
		}
		{
			SCOPED_CPU_MARKER_C("BUSY_WAIT_WORKER", 0xFFFF0000);
			while (WorkerThreadPool.GetNumActiveTasks() != 0);
		}
	}

	// kickoff workers for loading models
	mModelLoadResults = mAssetLoader.StartLoadingModels(this);
	Log::Info("[Scene] Start loading models...");

}

void Scene::LoadSceneMaterials(const std::vector<FMaterialRepresentation>& Materials, TaskID taskID)
{
	SCOPED_CPU_MARKER("Scene::LoadSceneMaterials()");
	// Create scene materials before deserializing gameobjects
	uint NumMaterials = 0;
	for (const FMaterialRepresentation& matRep : Materials)
	{
		this->LoadMaterial(matRep, taskID);
		++NumMaterials;
	}

	if (NumMaterials > 0)
		Log::Info("[Scene] Materials Created (%u)", NumMaterials);

	// kickoff background workers for texture loading
	if (!mMaterialAssignments.mAssignments.empty())
	{
		mMaterialAssignments.mTextureLoadResults = mAssetLoader.StartLoadingTextures(taskID);
		Log::Info("[Scene] Start loading textures... (%u)", mMaterialAssignments.mTextureLoadResults.size());
	}
}

void Scene::LoadLights(const std::vector<Light>& SceneLights)
{
	SCOPED_CPU_MARKER("Scene::LoadLights()");
	for (const Light& l : SceneLights)
	{
		std::vector<Light>& LightContainer = [&]() -> std::vector<Light>&{
			switch (l.Mobility)
			{
			case Light::EMobility::DYNAMIC: return mLightsDynamic;
			case Light::EMobility::STATIC: return mLightsStatic;
			case Light::EMobility::STATIONARY: return mLightsStationary;
			default:
				Log::Warning("Invalid light mobility!");
				break;
			}
			return mLightsStationary;
		}();

		LightContainer.push_back(l);
	}
}

void Scene::LoadCameras(std::vector<FCameraParameters>& CameraParams)
{
	SCOPED_CPU_MARKER("Scene::LoadCameras()");
	for (FCameraParameters& param : CameraParams)
	{
		param.ProjectionParams.ViewportWidth = static_cast<float>(mpWindow->GetWidth());
		param.ProjectionParams.ViewportHeight = static_cast<float>(mpWindow->GetHeight());

		Camera c;
		c.InitializeCamera(param);
		mCameras.emplace_back(std::move(c));
	}

	// CAMERA CONTROLLERS
	// controllers need to be initialized after mCameras are populated in order
	// to prevent dangling pointers in @pController->mpCamera (circular ptrs)
	for (size_t i = 0; i < mCameras.size(); ++i)
	{
		if (CameraParams[i].bInitializeCameraController)
		{
			mCameras[i].InitializeController(CameraParams[i]);
		}
	}
	Log::Info("[Scene] Cameras initialized");
}

void Scene::LoadPostProcessSettings(/*TODO: scene PP settings*/)
{
	SCOPED_CPU_MARKER("Scene::LoadPostProcessSettings()");
	// TODO: remove hardcode

	const uint fWidth  = this->mpWindow->GetWidth();
	const uint fHeight = this->mpWindow->GetHeight();

	// Update PostProcess Data
	for (size_t i = 0; i < mFrameSceneViews.size(); ++i)
	{
		FPostProcessParameters& PPParams = this->GetPostProcessParameters(static_cast<int>(i));

		// Update FidelityFX constant blocks
#if !DISABLE_FIDELITYFX_CAS
		PPParams.bEnableCAS = true; // TODO: read from scene PP settings
		if (PPParams.IsFFXCASEnabled())
		{
			PPParams.FFXCASParams.UpdateCASConstantBlock(fWidth, fHeight, fWidth, fHeight);
		}
#endif

		if (PPParams.IsFSREnabled())
		{
			const uint InputWidth = static_cast<uint> (PPParams.ResolutionScale * fWidth);
			const uint InputHeight = static_cast<uint>(PPParams.ResolutionScale * fHeight);
			PPParams.FSR_EASUParams.UpdateEASUConstantBlock(InputWidth, InputHeight, InputWidth, InputHeight, fWidth, fHeight);
			PPParams.FSR_RCASParams.UpdateRCASConstantBlock();
		}
	}
}

void Scene::OnLoadComplete()
{
	SCOPED_CPU_MARKER("Scene::OnLoadComplete()");
	Log::Info("[Scene] OnLoadComplete()");

	// Assign model data to game objects
	for (auto it = mModelLoadResults.begin(); it != mModelLoadResults.end(); ++it)
	{
		GameObject* pObj = it->first;
		AssetLoader::ModelLoadResult_t res = std::move(it->second);

		assert(res.valid());
		///res.wait(); // we should already have the results ready in OnLoadComplete()

		pObj->mModelID = res.get();
	}

	// assign material data
	mMaterialAssignments.DoAssignments(this, this->mTexturePaths, &mRenderer);

	// calculate local-space game object AABBs
	CalculateGameObjectLocalSpaceBoundingBoxes();

	Log::Info("[Scene] %s loaded.", mSceneRepresentation.SceneName.c_str());
	mSceneRepresentation.loadSuccess = 1;
	this->InitializeScene();
}

void Scene::Unload()
{
	SCOPED_CPU_MARKER("Scene::Unload()");

	this->UnloadScene();

	mSceneRepresentation = {};

	const size_t sz = mFrameSceneViews.size();
	mFrameSceneViews.clear();
	mFrameShadowViews.clear();
	mFrameSceneViews.resize(sz);
	mFrameShadowViews.resize(sz);

	//mMeshes.clear(); // TODO

	for (Transform* pTf : mpTransforms) mTransformPool.Free(pTf);
	mpTransforms.clear();

	for (GameObject* pObj : mpObjects) mGameObjectPool.Free(pObj);
	mpObjects.clear();

	mCameras.clear();
	mMaterials.clear();
	mMaterialNames.clear();
	mLoadedMaterials.clear();

	mDirectionalLight = {};
	mLightsStatic.clear();
	mLightsDynamic.clear();

	mBoundingBoxHierarchy.Clear();

	mIndex_SelectedCamera = 0;
	mIndex_ActiveEnvironmentMapPreset = -1;
	mEngine.UnloadEnvironmentMap();

	mLightsDynamic.clear();
	mLightsStatic.clear();
	mLightsStationary.clear();
}


void Scene::CalculateGameObjectLocalSpaceBoundingBoxes()
{
	constexpr float max_f = std::numeric_limits<float>::max();
	constexpr float min_f = -(max_f - 1.0f);
	
	size_t i = 0;
	for (GameObject* pGameObj : mpObjects)
	{
		if (!pGameObj)
		{
			Log::Warning("nullptr gameobj[%d]", i);
			++i;
			continue;
		}
		++i;
		assert(pGameObj);
		FBoundingBox& AABB = pGameObj->mLocalSpaceBoundingBox;

		// reset AABB
		AABB.ExtentMin = XMFLOAT3(max_f, max_f, max_f);
		AABB.ExtentMax = XMFLOAT3(min_f, min_f, min_f);

		// load
		XMVECTOR vMins = XMLoadFloat3(&AABB.ExtentMin);
		XMVECTOR vMaxs = XMLoadFloat3(&AABB.ExtentMax);

		// go through all meshes and generate the AABB
		if (pGameObj->mModelID == -1)
		{
			Log::Warning("Game object doesn't have a valid model ID!");
			continue;
		}
		const Model& model = mModels.at(pGameObj->mModelID);
		auto fnProcessMeshAABB = [&vMins, &vMaxs](const FBoundingBox& AABB_Mesh)
		{
			XMVECTOR vMinMesh = XMLoadFloat3(&AABB_Mesh.ExtentMin);
			XMVECTOR vMaxMesh = XMLoadFloat3(&AABB_Mesh.ExtentMax);

			vMins = XMVectorMin(vMins, vMinMesh);
			vMins = XMVectorMin(vMins, vMaxMesh);
			vMaxs = XMVectorMax(vMaxs, vMinMesh);
			vMaxs = XMVectorMax(vMaxs, vMaxMesh);
		};
		for (std::pair<MeshID, MaterialID> meshMaterialIDPair : model.mData.GetMeshMaterialIDPairs(Model::Data::EMeshType::OPAQUE_MESH))
		{
			MeshID mesh = meshMaterialIDPair.first;
			const FBoundingBox& AABB_Mesh = mMeshes.at(mesh).GetLocalSpaceBoundingBox();
			fnProcessMeshAABB(AABB_Mesh);
		}
		for (std::pair<MeshID, MaterialID> meshMaterialIDPair : model.mData.GetMeshMaterialIDPairs(Model::Data::EMeshType::TRANSPARENT_MESH))
		{
			MeshID mesh = meshMaterialIDPair.first;
			const FBoundingBox& AABB_Mesh = mMeshes.at(mesh).GetLocalSpaceBoundingBox();
			fnProcessMeshAABB(AABB_Mesh);
		}

		// store 
		XMStoreFloat3(&AABB.ExtentMin, vMins);
		XMStoreFloat3(&AABB.ExtentMax, vMaxs);
	}
}