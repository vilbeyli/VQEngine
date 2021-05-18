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

#include "Libs/VQUtils/Source/utils.h"

#include <fstream>

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
	Log::Info("Scene::CreateMaterial() ID=%d - %s", id, UniqueMaterialName.c_str());
#endif

	Material& mat = mMaterials.at(id);
	if (mat.SRVMaterialMaps == INVALID_ID)
	{
		mat.SRVMaterialMaps = mRenderer.CreateSRV(NUM_MATERIAL_TEXTURE_MAP_BINDINGS);
		mRenderer.InitializeSRV(mat.SRVMaterialMaps, 0, INVALID_ID);
		mRenderer.InitializeSRV(mat.SRVMaterialMaps, 1, INVALID_ID);
		mRenderer.InitializeSRV(mat.SRVMaterialMaps, 2, INVALID_ID);
		mRenderer.InitializeSRV(mat.SRVMaterialMaps, 3, INVALID_ID);
		mRenderer.InitializeSRV(mat.SRVMaterialMaps, 4, INVALID_ID);
		mRenderer.InitializeSRV(mat.SRVMaterialMaps, 5, INVALID_ID);
		mRenderer.InitializeSRV(mat.SRVMaterialMaps, 6, INVALID_ID);
		mRenderer.InitializeSRV(mat.SRVMaterialMaps, 7, INVALID_ID);
	}
	return id;
}

Material& Scene::GetMaterial(MaterialID ID)
{
	if (mMaterials.find(ID) == mMaterials.end())
	{
		Log::Error("Material not created. Did you call Scene::CreateMaterial()? (matID=%d)", ID);
		assert(false);
	}
	return mMaterials.at(ID);
}

Model& Scene::GetModel(ModelID id)
{
	if (mModels.find(id) == mModels.end())
	{
		Log::Error("Model not created. Did you call Scene::CreateModel()? (modelID=%d)", id);
		assert(false);
	}
	return mModels.at(id);
}


Scene::Scene(VQEngine& engine, int NumFrameBuffers, const Input& input, const std::unique_ptr<Window>& pWin, VQRenderer& renderer)
	: mInput(input)
	, mpWindow(pWin)
	, mEngine(engine)
	, mFrameSceneViews(NumFrameBuffers)
	, mFrameShadowViews(NumFrameBuffers)
	, mIndex_SelectedCamera(0)
	, mIndex_ActiveEnvironmentMapPreset(0)
	, mGameObjectPool(NUM_GAMEOBJECT_POOL_SIZE, GAMEOBJECT_BYTE_ALIGNMENT)
	, mTransformPool(NUM_GAMEOBJECT_POOL_SIZE, GAMEOBJECT_BYTE_ALIGNMENT)
	, mResourceNames(engine.GetResourceNames())
	, mAssetLoader(engine.GetAssetLoader())
	, mRenderer(renderer)
	, mMaterialAssignments(engine.GetAssetLoader().GetThreadPool_TextureLoad())
	, mBoundingBoxHierarchy(mMeshes, mModels, mMaterials, mpTransforms)
{}


void Scene::Update(float dt, int FRAME_DATA_INDEX)
{
	assert(FRAME_DATA_INDEX < mFrameSceneViews.size());
	FSceneView& SceneView = mFrameSceneViews[FRAME_DATA_INDEX];
	Camera& Cam = this->mCameras[this->mIndex_SelectedCamera];

	Cam.Update(dt, mInput);
	this->HandleInput(SceneView);
	this->UpdateScene(dt, SceneView);
}

void Scene::PostUpdate(int FRAME_DATA_INDEX, int FRAME_DATA_NEXT_INDEX)
{
	assert(FRAME_DATA_INDEX < mFrameSceneViews.size());
	FSceneView& SceneView = mFrameSceneViews[FRAME_DATA_INDEX];
	FSceneShadowView& ShadowView = mFrameShadowViews[FRAME_DATA_INDEX];
	FSceneView& SceneViewNext = mFrameSceneViews[FRAME_DATA_NEXT_INDEX];

	const Camera& cam = mCameras[mIndex_SelectedCamera];
	const XMFLOAT3 camPos = cam.GetPositionF();

	// extract scene view
	SceneView.proj = cam.GetProjectionMatrix();
	SceneView.projInverse = XMMatrixInverse(NULL, SceneView.proj);
	SceneView.view = cam.GetViewMatrix();
	SceneView.viewInverse = cam.GetViewInverseMatrix();
	SceneView.viewProj = SceneView.view * SceneView.proj;
	SceneView.cameraPosition = XMLoadFloat3(&camPos);
	SceneView.MainViewCameraYaw = cam.GetYaw();
	SceneView.MainViewCameraPitch = cam.GetPitch();

	mBoundingBoxHierarchy.Build(mpObjects);
	PrepareSceneMeshRenderParams(SceneView);
	GatherSceneLightData(SceneView);
	PrepareShadowMeshRenderParams(ShadowView, cam.GetViewFrustumPlanesInWorldSpace());
	PrepareLightMeshRenderParams(SceneView);
	// TODO: Prepare BoundingBoxRenderParams

	// update post process settings for next frame
	SceneViewNext.postProcessParameters = SceneView.postProcessParameters;
	SceneViewNext.sceneParameters = SceneView.sceneParameters;
}


void Scene::RenderUI()
{
	// TODO
}

static std::string DumpCameraInfo(int index, const Camera& cam)
{
	const XMFLOAT3 pos = cam.GetPositionF();
	const float pitch = cam.GetPitch();
	const float yaw = cam.GetYaw();

	std::string info = std::string("[CAMERA INFO]\n")
		+ "mIndex_SelectedCamera=" + std::to_string(index) + "\n"
		+ "  Pos         : " + std::to_string(pos.x) + " " + std::to_string(pos.y) + " " + std::to_string(pos.z) + "\n"
		+ "  Yaw (Deg)   : " + std::to_string(yaw * RAD2DEG) + "\n"
		+ "  Pitch (Deg) : " + std::to_string(pitch * RAD2DEG) + "\n";
	;

	return info;
}

void Scene::HandleInput(FSceneView& SceneView)
{
	const bool bIsShiftDown = mInput.IsKeyDown("Shift");
	const bool bIsCtrlDown = mInput.IsKeyDown("Ctrl");
	const int NumEnvMaps = static_cast<int>(mResourceNames.mEnvironmentMapPresetNames.size());

	if (mInput.IsKeyTriggered("C"))
	{
		if (bIsCtrlDown) // CTRL + C : Dump Camera Info
		{
			std::string camInfo = DumpCameraInfo(mIndex_SelectedCamera, mCameras[mIndex_SelectedCamera]);
			Log::Info(camInfo.c_str());
		}
		else
		{
			const int NumCameras = static_cast<int>(mCameras.size());
			mIndex_SelectedCamera = bIsShiftDown
				? CircularDecrement(mIndex_SelectedCamera, NumCameras)
				: CircularIncrement(mIndex_SelectedCamera, NumCameras);
		}
	}

	if (mInput.IsKeyTriggered("L"))
	{
		SceneView.sceneParameters.bDrawLightBounds = !SceneView.sceneParameters.bDrawLightBounds;
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


void Scene::GatherSceneLightData(FSceneView& SceneView) const
{
	SceneView.lightBoundsRenderCommands.clear();
	SceneView.lightRenderCommands.clear();

	VQ_SHADER_DATA::SceneLighting& data = SceneView.GPULightingData;

	int iGPUSpot = 0;  int iGPUSpotShadow = 0;
	int iGPUPoint = 0; int iGPUPointShadow = 0;
	auto fnGatherLightData = [&](const std::vector<Light>& vLights)
	{
		for (const Light& l : vLights)
		{
			if (!l.bEnabled) continue;
			switch (l.Type)
			{
			case Light::EType::DIRECTIONAL:
				l.GetGPUData(&data.directional);
				if (l.bCastingShadows)
				{
					data.shadowViewDirectional = l.GetViewProjectionMatrix();
				}
				break;
			case Light::EType::SPOT:
				l.GetGPUData(l.bCastingShadows ? &data.spot_casters[iGPUSpotShadow++] : &data.spot_lights[iGPUSpot++]);
				if (l.bCastingShadows)
				{
					data.shadowViews[iGPUSpotShadow - 1] = l.GetViewProjectionMatrix();
				}
				break;
			case Light::EType::POINT:
				l.GetGPUData(l.bCastingShadows ? &data.point_casters[iGPUPointShadow++] : &data.point_lights[iGPUPoint++]);
				// TODO: 
				break;
			default:
				break;
			}

		}
	};
	fnGatherLightData(mLightsStatic);
	fnGatherLightData(mLightsStationary);
	fnGatherLightData(mLightsDynamic);

	data.numPointCasters = iGPUPointShadow;
	data.numPointLights = iGPUPoint;
	data.numSpotCasters = iGPUSpotShadow;
	data.numSpotLights = iGPUSpot;
}

void Scene::PrepareLightMeshRenderParams(FSceneView& SceneView) const
{
	if (!SceneView.sceneParameters.bDrawLightBounds && !SceneView.sceneParameters.bDrawLightMeshes)
		return;

	auto fnGatherLightRenderData = [&](const std::vector<Light>& vLights)
	{
		for (const Light& l : vLights)
		{
			if (!l.bEnabled)
				continue;

			FLightRenderCommand cmd;
			cmd.color = XMFLOAT3(l.Color.x * l.Brightness, l.Color.y * l.Brightness, l.Color.z * l.Brightness);
			cmd.WorldTransformationMatrix = l.GetWorldTransformationMatrix();

			switch (l.Type)
			{
			case Light::EType::DIRECTIONAL:
				continue; // don't draw directional light mesh
				break;

			case Light::EType::SPOT:
			{
				// light mesh
				if (SceneView.sceneParameters.bDrawLightMeshes)
				{
					cmd.meshID = EBuiltInMeshes::SPHERE;
					SceneView.lightRenderCommands.push_back(cmd);
				}

				// light bounds
				if (SceneView.sceneParameters.bDrawLightBounds)
				{
					cmd.meshID = EBuiltInMeshes::CONE;
					Transform tf = l.GetTransform();
					tf.SetScale(1, 1, 1); // reset scale as it holds the scale value for light's render mesh
					tf.RotateAroundLocalXAxisDegrees(-90.0f); // align with spot light's local space

					XMMATRIX alignConeToSpotLightTransformation = XMMatrixIdentity();
					alignConeToSpotLightTransformation.r[3].m128_f32[0] = 0.0f;
					alignConeToSpotLightTransformation.r[3].m128_f32[1] = -l.Range;
					alignConeToSpotLightTransformation.r[3].m128_f32[2] = 0.0f;

					const float coneBaseRadius = std::tanf(l.SpotOuterConeAngleDegrees * DEG2RAD) * l.Range;
					XMMATRIX scaleConeToRange = XMMatrixIdentity();
					scaleConeToRange.r[0].m128_f32[0] = coneBaseRadius;
					scaleConeToRange.r[1].m128_f32[1] = l.Range;
					scaleConeToRange.r[2].m128_f32[2] = coneBaseRadius;

					//wvp = alignConeToSpotLightTransformation * tf.WorldTransformationMatrix() * viewProj;
					cmd.WorldTransformationMatrix = scaleConeToRange * alignConeToSpotLightTransformation * tf.WorldTransformationMatrix();
					cmd.color = l.Color;  // drop the brightness multiplier for bounds rendering
					SceneView.lightBoundsRenderCommands.push_back(cmd);
				}
			}	break;

			case Light::EType::POINT:
			{
				// light mesh
				if (SceneView.sceneParameters.bDrawLightMeshes)
				{
					cmd.meshID = EBuiltInMeshes::SPHERE;
					SceneView.lightRenderCommands.push_back(cmd);
				}

				// light bounds
				if (SceneView.sceneParameters.bDrawLightBounds)
				{
					Transform tf = l.GetTransform();
					tf._scale = XMFLOAT3(l.Range, l.Range, l.Range);
					cmd.WorldTransformationMatrix = tf.WorldTransformationMatrix();
					cmd.color = l.Color; // drop the brightness multiplier for bounds rendering
					SceneView.lightBoundsRenderCommands.push_back(cmd);
				}
			}  break;
			} // swicth
		} // for: Lights
	};


	fnGatherLightRenderData(mLightsStatic);
	fnGatherLightRenderData(mLightsStationary);
	fnGatherLightRenderData(mLightsDynamic);
}

#define ENABLE_VIEW_FRUSTUM_CULLING 1
void Scene::PrepareSceneMeshRenderParams(FSceneView& SceneView) const
{
	SceneView.meshRenderCommands.clear();

#if ENABLE_VIEW_FRUSTUM_CULLING

	FFrustumCullWorkerContext GameObjectFrustumCullWorkerContext;
	
	for (const GameObject* pObj : mpObjects)
	{
		
	}
	
	const size_t GameObjectWorkSize = 1; // would need a new Process() function for: mpObjects.size(); 
	GameObjectFrustumCullWorkerContext.AddWorkerItem(FFrustumPlaneset::ExtractFromMatrix(SceneView.viewProj)
		, mBoundingBoxHierarchy.mGameObjectBoundingBoxes
		, mBoundingBoxHierarchy.mGameObjectBoundingBoxGameObjectPointerMapping
	);

	const size_t MeshWorkSize = 1; // would need a new Process() function for: mBoundingBoxHierarchy.mMeshBoundingBoxes.size();
	FFrustumCullWorkerContext MeshFrustumCullWorkerContext; // TODO: populate after culling game objects?
	MeshFrustumCullWorkerContext.AddWorkerItem(FFrustumPlaneset::ExtractFromMatrix(SceneView.viewProj)
		, mBoundingBoxHierarchy.mMeshBoundingBoxes
		, mBoundingBoxHierarchy.mMeshBoundingBoxGameObjectPointerMapping
	);

	constexpr bool SINGLE_THREADED_CULL = true;
	//-----------------------------------------------------------------------------------------
	//
	// SINGLE THREAD CULL
	//
	if constexpr (SINGLE_THREADED_CULL)
	{
		GameObjectFrustumCullWorkerContext.Process(0, GameObjectWorkSize - 1);
		MeshFrustumCullWorkerContext.Process(0, MeshWorkSize - 1);
	}
	//
	// THREADED CULL
	//
	else
	{
		assert(false); // TODO
	}
	//-----------------------------------------------------------------------------------------
	
	// TODO: we have a culled list of game object boundin box indices
	//for (size_t iFrustum = 0; iFrustum < NumFrustumsToCull; ++iFrustum)
	{
		//const std::vector<size_t>& CulledBoundingBoxIndexList_Obj = GameObjectFrustumCullWorkerContext.vCulledBoundingBoxIndexLists[iFrustum];

		std::vector<FMeshRenderCommand>& vMeshRenderList = SceneView.meshRenderCommands;
		vMeshRenderList.clear();

		const std::vector<size_t>& CulledBoundingBoxIndexList_Msh = MeshFrustumCullWorkerContext.vCulledBoundingBoxIndexLists[0];
		for (const size_t& BBIndex : CulledBoundingBoxIndexList_Msh)
		{
			assert(BBIndex < mBoundingBoxHierarchy.mMeshBoundingBoxMeshIDMapping.size());
			MeshID meshID = mBoundingBoxHierarchy.mMeshBoundingBoxMeshIDMapping[BBIndex];

			const GameObject* pGameObject = MeshFrustumCullWorkerContext.vGameObjectPointerLists[0][BBIndex];
			
			Transform* const& pTF = mpTransforms.at(pGameObject->mTransformID);
			const Model& model = mModels.at(pGameObject->mModelID);

			// record ShadowMeshRenderCommand
			FMeshRenderCommand meshRenderCmd;
			meshRenderCmd.meshID = meshID;
			meshRenderCmd.WorldTransformationMatrix = pTF->WorldTransformationMatrix();
			meshRenderCmd.NormalTransformationMatrix = pTF->NormalMatrix(meshRenderCmd.WorldTransformationMatrix);
			meshRenderCmd.matID = model.mData.mOpaqueMaterials.at(meshID);
			vMeshRenderList.push_back(meshRenderCmd);
		}
	}

#else // no culling, render all game objects
	
	for (const GameObject* pObj : mpObjects)
	{
		Transform* const& pTF = mpTransforms.at(pObj->mTransformID);

		const bool bModelNotFound = mModels.find(pObj->mModelID) == mModels.end();
		if (bModelNotFound)
		{
			Log::Warning("[Scene] Model not found: ID=%d", pObj->mModelID);
			continue; // skip rendering object if there's no model
		}

		const Model& model = mModels.at(pObj->mModelID);

		assert(pObj->mModelID != INVALID_ID);
		for (const MeshID id : model.mData.mOpaueMeshIDs)
		{
			FMeshRenderCommand meshRenderCmd;
			meshRenderCmd.meshID = id;
			meshRenderCmd.WorldTransformationMatrix = pTF->WorldTransformationMatrix();
			meshRenderCmd.NormalTransformationMatrix = pTF->NormalMatrix(meshRenderCmd.WorldTransformationMatrix);
			meshRenderCmd.matID = model.mData.mOpaqueMaterials.at(id);

			meshRenderCmd.ModelName = model.mModelName;
			meshRenderCmd.MaterialName = ""; // TODO

			SceneView.meshRenderCommands.push_back(meshRenderCmd);
		}
	}
#endif


}

#include "../Culling.h"
// returns true if culled
static bool DistanceCullLight(const Light& l, const FFrustumPlaneset& MainViewFrustumPlanesInWorldSpace)
{
	bool bCulled = false;
	switch (l.Type)
	{
	case Light::EType::DIRECTIONAL: break; // no culling for directional lights
	case Light::EType::SPOT: 
		bCulled = !IsFrustumIntersectingFrustum(MainViewFrustumPlanesInWorldSpace, FFrustumPlaneset::ExtractFromMatrix(l.GetViewProjectionMatrix()));
		break; 
	case Light::EType::POINT:
		bCulled = !IsSphereIntersectingFurstum(MainViewFrustumPlanesInWorldSpace, FSphere(l.GetTransform()._position, l.Range));
		break;
	default: assert(false); break; // unknown light type for culling
	}
	return bCulled;
}

static std::vector<size_t> GetActiveAndCulledLightIndices(const std::vector<Light> vLights, const FFrustumPlaneset& MainViewFrustumPlanesInWorldSpace)
{
	constexpr bool bCULL_LIGHTS = false; // TODO: finish Intersection Test implementations

	std::vector<size_t> ActiveLightIndices;

	for (size_t i = 0; i < vLights.size(); ++i)
	{
		const Light& l = vLights[i];

		// skip disabled and non-shadow casting lights
		if (!l.bEnabled || !l.bCastingShadows)
			continue;

		// skip culled lights if culling is enabled
		if constexpr (bCULL_LIGHTS)
		{
			if (DistanceCullLight(l, MainViewFrustumPlanesInWorldSpace))
				continue;
		}

		ActiveLightIndices.push_back(i);
	}

	return ActiveLightIndices;
}

void Scene::PrepareShadowMeshRenderParams(FSceneShadowView& SceneShadowView, const FFrustumPlaneset& MainViewFrustumPlanesInWorldSpace) const
{
#if ENABLE_VIEW_FRUSTUM_CULLING
	constexpr bool bCULL_LIGHT_VIEWS = false;

	auto fnGatherShadowingLightFrustumCullParameters = [&](
		const std::vector<Light>& vLights
		, const std::vector<size_t>& vActiveLightIndices
		, FFrustumCullWorkerContext& DispatchContext
		, const std::vector<FBoundingBox>& BoundingBoxList
		, const std::vector<const GameObject*>& pGameObjects
		, std::unordered_map<size_t, FSceneShadowView::FShadowView*>& FrustumIndex_pShadowViewLookup
	)
	{
		// prepare frustum cull work context
		int iLight = 0;
		int iPoint = 0;
		int iSpot = 0;
		for(const size_t& LightIndex : vActiveLightIndices)
		{
			const Light& l = vLights[LightIndex];
			switch (l.Type)
			{
			case Light::EType::DIRECTIONAL: // TODO: calculate frustum cull parameters
			{
				FSceneShadowView::FShadowView& ShadowView = SceneShadowView.ShadowView_Directional;
				ShadowView.matViewProj = l.GetViewProjectionMatrix();
				// TODO: gather params
			}	break;
			case Light::EType::SPOT:
			{
				XMMATRIX matViewProj = l.GetViewProjectionMatrix();
				const size_t FrustumIndex = DispatchContext.AddWorkerItem(FFrustumPlaneset::ExtractFromMatrix(matViewProj), BoundingBoxList, pGameObjects);

				FSceneShadowView::FShadowView& ShadowView = SceneShadowView.ShadowViews_Spot[iSpot];
				ShadowView.matViewProj = matViewProj;
				FrustumIndex_pShadowViewLookup[FrustumIndex] = &ShadowView;
				++iSpot;
				SceneShadowView.NumSpotShadowViews = iSpot;
			} break;
			case Light::EType::POINT:
			{
				for (int face = 0; face < 6; ++face)
				{
					XMMATRIX matViewProj = l.GetViewProjectionMatrix(static_cast<Texture::CubemapUtility::ECubeMapLookDirections>(face));
					const size_t FrustumIndex = DispatchContext.AddWorkerItem(FFrustumPlaneset::ExtractFromMatrix(matViewProj), BoundingBoxList, pGameObjects);
					FSceneShadowView::FShadowView& ShadowView = SceneShadowView.ShadowViews_Point[iPoint * 6 + face];
					ShadowView.matViewProj = matViewProj;
					FrustumIndex_pShadowViewLookup[FrustumIndex] = &ShadowView;
				}
				SceneShadowView.PointLightLinearDepthParams[iPoint].fFarPlane = l.Range;
				SceneShadowView.PointLightLinearDepthParams[iPoint].vWorldPos = l.Position;
				++iPoint;
				SceneShadowView.NumPointShadowViews = iPoint;
			} break;
			}
			++iLight;
		}
	};

	const std::vector<size_t> vActiveLightIndices_Static     = GetActiveAndCulledLightIndices(mLightsStatic, MainViewFrustumPlanesInWorldSpace);
	const std::vector<size_t> vActiveLightIndices_Stationary = GetActiveAndCulledLightIndices(mLightsStationary, MainViewFrustumPlanesInWorldSpace);
	const std::vector<size_t> vActiveLightIndices_Dynamic    = GetActiveAndCulledLightIndices(mLightsDynamic, MainViewFrustumPlanesInWorldSpace);
	
	std::unordered_map<size_t, FSceneShadowView::FShadowView*> FrustumIndex_pShadowViewLookup;
	
	FFrustumCullWorkerContext GameObjectFrustumCullWorkerContext;
	fnGatherShadowingLightFrustumCullParameters(mLightsStatic    , vActiveLightIndices_Static    , GameObjectFrustumCullWorkerContext, mBoundingBoxHierarchy.mGameObjectBoundingBoxes, mBoundingBoxHierarchy.mGameObjectBoundingBoxGameObjectPointerMapping, FrustumIndex_pShadowViewLookup);
	fnGatherShadowingLightFrustumCullParameters(mLightsStationary, vActiveLightIndices_Stationary, GameObjectFrustumCullWorkerContext, mBoundingBoxHierarchy.mGameObjectBoundingBoxes, mBoundingBoxHierarchy.mGameObjectBoundingBoxGameObjectPointerMapping, FrustumIndex_pShadowViewLookup);
	fnGatherShadowingLightFrustumCullParameters(mLightsDynamic   , vActiveLightIndices_Dynamic   , GameObjectFrustumCullWorkerContext, mBoundingBoxHierarchy.mGameObjectBoundingBoxes, mBoundingBoxHierarchy.mGameObjectBoundingBoxGameObjectPointerMapping, FrustumIndex_pShadowViewLookup);
	
	FFrustumCullWorkerContext MeshFrustumCullWorkerContext; // TODO: populate after culling game objects?
	fnGatherShadowingLightFrustumCullParameters(mLightsStatic    , vActiveLightIndices_Static    , MeshFrustumCullWorkerContext, mBoundingBoxHierarchy.mMeshBoundingBoxes, mBoundingBoxHierarchy.mMeshBoundingBoxGameObjectPointerMapping, FrustumIndex_pShadowViewLookup);
	fnGatherShadowingLightFrustumCullParameters(mLightsStationary, vActiveLightIndices_Stationary, MeshFrustumCullWorkerContext, mBoundingBoxHierarchy.mMeshBoundingBoxes, mBoundingBoxHierarchy.mMeshBoundingBoxGameObjectPointerMapping, FrustumIndex_pShadowViewLookup);
	fnGatherShadowingLightFrustumCullParameters(mLightsDynamic   , vActiveLightIndices_Dynamic   , MeshFrustumCullWorkerContext, mBoundingBoxHierarchy.mMeshBoundingBoxes, mBoundingBoxHierarchy.mMeshBoundingBoxGameObjectPointerMapping, FrustumIndex_pShadowViewLookup);

	const size_t NumFrustumsToCull = GameObjectFrustumCullWorkerContext.vBoundingBoxLists.size() == 0 
		? MeshFrustumCullWorkerContext.vBoundingBoxLists.size()
		: GameObjectFrustumCullWorkerContext.vBoundingBoxLists.size();
	if (NumFrustumsToCull == 0)
	{
		return;
	}

	assert(NumFrustumsToCull != 0);
	constexpr bool SINGLE_THREADED_CULL = true;

	//-----------------------------------------------------------------------------------------
	//
	// SINGLE THREAD CULL
	//
	if constexpr (SINGLE_THREADED_CULL)
	{
		GameObjectFrustumCullWorkerContext.Process(0, NumFrustumsToCull - 1);
		MeshFrustumCullWorkerContext.Process(0, NumFrustumsToCull - 1);
	}
	//
	// THREADED CULL
	//
	else
	{
		assert(false); // TODO
	}
	//-----------------------------------------------------------------------------------------


	// TODO: we have a culled list of game object boundin box indices
	for (size_t iFrustum = 0; iFrustum < NumFrustumsToCull; ++iFrustum)
	{
		//const std::vector<size_t>& CulledBoundingBoxIndexList_Obj = GameObjectFrustumCullWorkerContext.vCulledBoundingBoxIndexLists[iFrustum];
		
		std::vector<FShadowMeshRenderCommand>& vMeshRenderList = FrustumIndex_pShadowViewLookup.at(iFrustum)->meshRenderCommands;
		vMeshRenderList.clear();

		const std::vector<size_t>& CulledBoundingBoxIndexList_Msh = MeshFrustumCullWorkerContext.vCulledBoundingBoxIndexLists[iFrustum];
		for (const size_t& BBIndex : CulledBoundingBoxIndexList_Msh)
		{
			assert(BBIndex < mBoundingBoxHierarchy.mMeshBoundingBoxMeshIDMapping.size());
			MeshID meshID = mBoundingBoxHierarchy.mMeshBoundingBoxMeshIDMapping[BBIndex];

			const GameObject* pGameObject = MeshFrustumCullWorkerContext.vGameObjectPointerLists[iFrustum][BBIndex];
			Transform* const& pTF = mpTransforms.at(pGameObject->mTransformID);

			// record ShadowMeshRenderCommand
			FShadowMeshRenderCommand meshRenderCmd;
			meshRenderCmd.meshID = meshID;
			meshRenderCmd.WorldTransformationMatrix = pTF->WorldTransformationMatrix();
			vMeshRenderList.push_back(meshRenderCmd);
		}
		
	}

#else
	int iSpot = 0;
	int iPoint = 0;

	auto fnGatherMeshRenderParamsForLight = [&](const Light& l, FSceneShadowView::FShadowView& ShadowView)
	{
		std::vector<FShadowMeshRenderCommand>& vMeshRenderList = ShadowView.meshRenderCommands;
		vMeshRenderList.clear();
		for (const GameObject* pObj : mpObjects)
		{
			Transform* const& pTF = mpTransforms.at(pObj->mTransformID);

			const bool bModelNotFound = mModels.find(pObj->mModelID) == mModels.end();
			if (bModelNotFound)
			{
				Log::Warning("[Scene] Model not found: ID=%d", pObj->mModelID);
				continue; // skip rendering object if there's no model
			}

			const Model& model = mModels.at(pObj->mModelID);

			assert(pObj->mModelID != INVALID_ID);
			for (const MeshID id : model.mData.mOpaueMeshIDs)
			{
				FShadowMeshRenderCommand meshRenderCmd;
				meshRenderCmd.meshID = id;
				meshRenderCmd.WorldTransformationMatrix = pTF->WorldTransformationMatrix();
				vMeshRenderList.push_back(meshRenderCmd);
			}
		}
	};
	auto fnGatherShadowingLightData = [&](const std::vector<Light>& vLights)
	{
		for (const Light& l : vLights)
		{
			if (!l.bEnabled || !l.bCastingShadows)
				continue;

			switch (l.Type)
			{
			case Light::EType::DIRECTIONAL:
			{
				FSceneShadowView::FShadowView& ShadowView = SceneShadowView.ShadowView_Directional;
				ShadowView.matViewProj = l.GetViewProjectionMatrix();
				fnGatherMeshRenderParamsForLight(l, ShadowView);
			}	break;
			case Light::EType::SPOT:
			{
				FSceneShadowView::FShadowView& ShadowView = SceneShadowView.ShadowViews_Spot[iSpot++];
				ShadowView.matViewProj = l.GetViewProjectionMatrix();
				fnGatherMeshRenderParamsForLight(l, ShadowView);
			} break;
			case Light::EType::POINT:
			{

				for (int face = 0; face < 6; ++face)
				{
					FSceneShadowView::FShadowView& ShadowView = SceneShadowView.ShadowViews_Point[(size_t)iPoint * 6 + face];
					ShadowView.matViewProj = l.GetViewProjectionMatrix(static_cast<Texture::CubemapUtility::ECubeMapLookDirections>(face));
					fnGatherMeshRenderParamsForLight(l, ShadowView);
				}

				SceneShadowView.PointLightLinearDepthParams[iPoint].fFarPlane = l.Range;
				SceneShadowView.PointLightLinearDepthParams[iPoint].vWorldPos = l.Position;
				++iPoint;
			} break;
			}
		}
	};

	fnGatherShadowingLightData(mLightsStatic);
	fnGatherShadowingLightData(mLightsStationary);
	fnGatherShadowingLightData(mLightsDynamic);
	SceneShadowView.NumPointShadowViews = iPoint;
	SceneShadowView.NumSpotShadowViews = iSpot;
#endif
}

FMaterialRepresentation::FMaterialRepresentation()
	: DiffuseColor(MATERIAL_UNINITIALIZED_VALUE, MATERIAL_UNINITIALIZED_VALUE, MATERIAL_UNINITIALIZED_VALUE)
	, EmissiveColor(MATERIAL_UNINITIALIZED_VALUE, MATERIAL_UNINITIALIZED_VALUE, MATERIAL_UNINITIALIZED_VALUE)
{}
