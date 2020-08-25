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
#include "Window.h"
#include "VQEngine.h"

#define LOG_CACHED_RESOURCES_ON_LOAD 0
#define LOG_RESOURCE_CREATE          1

using namespace DirectX;

static MeshID LAST_USED_MESH_ID = EBuiltInMeshes::NUM_BUILTIN_MESHES;

//MeshID Scene::CreateMesh()
//{
//	ModelID id = LAST_USED_MESH_ID++;
//
//	mMeshes[id] = Mesh();
//	return id;
//}

MeshID Scene::AddMesh(Mesh&& mesh)
{
	std::lock_guard<std::mutex> lk(mMtx_Meshes);
	MeshID id = LAST_USED_MESH_ID++;
	mMeshes[id] = mesh;
	return id;
}

MeshID Scene::AddMesh(const Mesh& mesh)
{
	std::lock_guard<std::mutex> lk(mMtx_Meshes);
	MeshID id = LAST_USED_MESH_ID++;
	mMeshes[id] = mesh;
	return id;
}

ModelID Scene::CreateModel()
{
	std::unique_lock<std::mutex> lk(mMtx_Models);
	static ModelID LAST_USED_MODEL_ID = 0;
	ModelID id = LAST_USED_MODEL_ID++;
	mModels[id] = Model();
	return id;
}

MaterialID Scene::CreateMaterial(const std::string& UniqueMaterialName)
{
	auto it = mLoadedMaterials.find(UniqueMaterialName);
	if (it != mLoadedMaterials.end())
	{
#if LOG_CACHED_RESOURCES_ON_LOAD
		Log::Info("Material already loaded: %s", UniqueMaterialName.c_str());
#endif
		return it->second;
	}

	static MaterialID LAST_USED_MATERIAL_ID = 0;
	MaterialID id = INVALID_ID;
	// critical section
	{
		std::unique_lock<std::mutex> lk(mMtx_Materials);
		id = LAST_USED_MATERIAL_ID++;
	}
	mMaterials[id] = Material();
	mLoadedMaterials[UniqueMaterialName] = id;
#if LOG_RESOURCE_CREATE
	Log::Info("Scene::CreateMaterial(): %s", UniqueMaterialName.c_str());
#endif
	return id;
}

Material& Scene::GetMaterial(MaterialID ID)
{
	// TODO: err handle
	return mMaterials.at(ID);
}

Model& Scene::GetModel(ModelID id)
{
	// TODO: err handle
	return mModels.at(id);
}


Scene::Scene(VQEngine& engine, int NumFrameBuffers, const Input& input, const std::unique_ptr<Window>& pWin, VQRenderer& renderer)
	: mInput(input)
	, mpWindow(pWin)
	, mEngine(engine)
	, mFrameSceneViews(NumFrameBuffers)
	, mIndex_SelectedCamera(0)
	, mIndex_ActiveEnvironmentMapPreset(0)
	, mGameObjectPool(NUM_GAMEOBJECT_POOL_SIZE, GAMEOBJECT_BYTE_ALIGNMENT)
	, mTransformPool(NUM_GAMEOBJECT_POOL_SIZE, GAMEOBJECT_BYTE_ALIGNMENT)
	, mResourceNames(engine.GetResourceNames())
	, mAssetLoader(engine.GetAssetLoader())
	, mRenderer(renderer)
{}

void Scene::Update(float dt, int FRAME_DATA_INDEX)
{
	assert(FRAME_DATA_INDEX < mFrameSceneViews.size());
	FSceneView& SceneView = mFrameSceneViews[FRAME_DATA_INDEX];
	Camera& Cam = this->mCameras[this->mIndex_SelectedCamera];
	
	Cam.Update(dt, mInput);
	this->HandleInput();
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


	// TODO: compute visibility 

	SceneView.meshRenderCommands.clear();
	for (const GameObject* pObj : mpObjects)
	{
		const XMMATRIX matWorldTransform = mpTransforms.at(pObj->mTransformID)->WorldTransformationMatrix();
		
		assert(pObj->mModelID != INVALID_ID);
		for (const MeshID id : mModels.at(pObj->mModelID).mData.mOpaueMeshIDs)
		{
			FMeshRenderCommand meshRenderCmd;
			meshRenderCmd.meshID = id;
			meshRenderCmd.WorldTransformationMatrix = matWorldTransform;
			SceneView.meshRenderCommands.push_back(meshRenderCmd);
		}
	}
}

void Scene::StartLoading(const BuiltinMeshArray_t& builtinMeshes, FSceneRepresentation& scene)
{
	constexpr bool B_LOAD_SERIAL = true;

	// register builtin meshes to scene mesh lookup
	// @mMeshes[0-NUM_BUILTIN_MESHES] are assigned here directly while the rest
	// of the meshes used in the scene must use this->AddMesh(Mesh&&) interface;
	for (size_t i = 0; i < builtinMeshes.size(); ++i)
	{
		this->mMeshes[i] = builtinMeshes[i];
	}

	// scene-specific load 
	this->LoadScene(scene);

	auto fnDeserializeGameObject = [&](GameObjectRepresentation& ObjRep)
	{
		// GameObject
		GameObject* pObj = mGameObjectPool.Allocate(1);
		pObj->mModelID = INVALID_ID;
		pObj->mTransformID = INVALID_ID;

		// Transform
		Transform* pTransform = mTransformPool.Allocate(1);
		*pTransform = std::move(ObjRep.tf);
		mpTransforms.push_back(pTransform);

		TransformID tID = static_cast<TransformID>(mpTransforms.size() - 1);
		pObj->mTransformID = tID;

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
			model.mData.mOpaueMeshIDs.push_back(meshID);

			// TODO: material

			model.mbLoaded = true;
			pObj->mModelID = mID;
		}
		else
		{
			mAssetLoader.QueueModelLoad(pObj, ObjRep.ModelFilePath, ObjRep.ModelName);
		}
		

		mpObjects.push_back(pObj);
	};

	if constexpr (B_LOAD_SERIAL)
	{
		// GAME OBJECTS
		for (GameObjectRepresentation& ObjRep : scene.Objects)
		{
			fnDeserializeGameObject(ObjRep);
		}
	}
	else // THREADED LOAD
	{
		// dispatch workers
		assert(false); // TODO
	}

	mModelLoadResults = mAssetLoader.StartLoadingModels(this);

	// CAMERAS
	for (FCameraParameters& param : scene.Cameras)
	{
		param.ProjectionParams.ViewportWidth  = static_cast<float>( mpWindow->GetWidth()  );
		param.ProjectionParams.ViewportHeight = static_cast<float>( mpWindow->GetHeight() );

		Camera c;
		c.InitializeCamera(param);
		mCameras.emplace_back(std::move(c));
	}

	// CAMERA CONTROLLERS
	// controllers need to be initialized after mCameras are populated in order
	// to prevent dangling pointers in @pController->mpCamera (circular ptrs)
	for (size_t i = 0; i < mCameras.size(); ++i)
	{
		if (scene.Cameras[i].bInitializeCameraController)
		{
			mCameras[i].InitializeController(scene.Cameras[i].bFirstPerson, scene.Cameras[i]);
		}
	}


	// assign scene rep
	mSceneRepresentation = scene;
}

void Scene::OnLoadComplete()
{

	for (auto it = mModelLoadResults.begin(); it != mModelLoadResults.end(); ++it)
	{
		GameObject* pObj = it->first;
		AssetLoader::ModelLoadResult_t res = std::move(it->second);

		assert(res.valid());
		///res.wait(); // we should already have the results ready in OnLoadComplete()

		pObj->mModelID = res.get();
	}

	Log::Info("[Scene] %s loaded.", mSceneRepresentation.SceneName.c_str());
	mSceneRepresentation.loadSuccess = 1;
	this->InitializeScene();
}

void Scene::Unload()
{
	this->UnloadScene();

	mSceneRepresentation = {};

	const size_t sz = mFrameSceneViews.size();
	mFrameSceneViews.clear();
	mFrameSceneViews.resize(sz);

	//mMeshes.clear(); // TODO
	
	for (Transform* pTf : mpTransforms) mTransformPool.Free(pTf);
	mpTransforms.clear();

	for (GameObject* pObj : mpObjects) mGameObjectPool.Free(pObj);
	mpObjects.clear();

	mCameras.clear();

	mDirectionalLight = {};
	mLightsStatic.clear();
	mLightsDynamic.clear();

	mSceneBoundingBox = {};
	mMeshBoundingBoxes.clear();
	mGameObjectBoundingBoxes.clear();

	mIndex_SelectedCamera = 0;
	mIndex_ActiveEnvironmentMapPreset = -1;
	mEngine.UnloadEnvironmentMap();
}

void Scene::RenderUI()
{
	// TODO
}

void Scene::HandleInput()
{
	const bool bIsShiftDown = mInput.IsKeyDown("Shift");
	const int NumEnvMaps = static_cast<int>(mResourceNames.mEnvironmentMapPresetNames.size());

	if (mInput.IsKeyTriggered("C"))
	{
		const int NumCameras = static_cast<int>(mCameras.size());
		mIndex_SelectedCamera = bIsShiftDown 
			? CircularDecrement(mIndex_SelectedCamera, NumCameras) 
			: CircularIncrement(mIndex_SelectedCamera, NumCameras);
	}

	if (mInput.IsKeyTriggered("PageUp"))
	{
		mIndex_ActiveEnvironmentMapPreset = CircularIncrement(mIndex_ActiveEnvironmentMapPreset, NumEnvMaps);
		mEngine.StartLoadingEnvironmentMap(mIndex_ActiveEnvironmentMapPreset);
	}
	if (mInput.IsKeyTriggered("PageDown"))
	{
		mIndex_ActiveEnvironmentMapPreset = CircularDecrement(mIndex_ActiveEnvironmentMapPreset, NumEnvMaps - 1);
		mEngine.StartLoadingEnvironmentMap(mIndex_ActiveEnvironmentMapPreset);
	}
}
