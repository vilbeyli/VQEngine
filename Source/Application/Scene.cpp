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
#include "VQEngine.h"

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


	// TODO: compute visibility 

	SceneView.meshRenderCommands.clear();
	for (const GameObject* pObj : mpObjects)
	{
		const XMMATRIX matWorldTransform = mpTransforms.at(pObj->mTransformID)->WorldTransformationMatrix();

		for (const MeshID id : mEngine.GetModel(pObj->mModelID).mData.mMeshIDs)
		{
			FMeshRenderCommand meshRenderCmd;
			meshRenderCmd.meshID = id;
			meshRenderCmd.WorldTransformationMatrix = matWorldTransform;
			SceneView.meshRenderCommands.push_back(meshRenderCmd);
		}
	}
}

void Scene::StartLoading(FSceneRepresentation& scene)
{
	constexpr bool B_LOAD_SERIAL = true;

	// scene-specific load 
	this->LoadScene(scene);

	// GAME OBJECTS
	auto fnDeserializeGameObject = [&](GameObjectRepresentation& ObjRep)
	{
		// Transform
		Transform* pTransform = mTransformPool.Allocate(1);
		*pTransform = std::move(ObjRep.tf);
		mpTransforms.push_back(pTransform);

		TransformID tID = static_cast<TransformID>(mpTransforms.size() - 1);

		// Model
		const bool bModelIsBuiltinMesh = !ObjRep.BuiltinMeshName.empty();
		const bool bModelIsLoadedFromFile = !ObjRep.ModelFilePath.empty();
		assert(bModelIsBuiltinMesh != bModelIsLoadedFromFile);

		ModelID mID = INVALID_ID;
		if (bModelIsBuiltinMesh)
		{
			// create model
			mID = mEngine.CreateModel();
			Model& model = mEngine.GetModel(mID);
			
			// create/get mesh
			MeshID meshID = mEngine.GetBuiltInMeshID(ObjRep.BuiltinMeshName);
			model.mData.mMeshIDs.push_back(meshID);

			// TODO: material
		}
		else
		{
			// TODO: load/create model?
			//ObjRep.ModelName; // TODO
		}
		

		// GameObject
		GameObject* pObj = mGameObjectPool.Allocate(1);
		pObj->mTransformID = tID;
		pObj->mModelID = mID;
		mpObjects.push_back(pObj);
	};

	if constexpr (B_LOAD_SERIAL)
	{
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

	// CAMERAS
	for (FCameraParameters& param : scene.Cameras)
	{
		param.Width  = static_cast<float>( mpWindow->GetWidth()  );
		param.Height = static_cast<float>( mpWindow->GetHeight() );

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

	//mMeshIDs.clear();
	
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

	mIndex_SelectedCamera
		= mIndex_ActiveEnvironmentMapPreset
		= 0;
}

void Scene::RenderUI()
{
	// TODO
}

