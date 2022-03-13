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
#include "../GPUMarker.h"

#include "../Core/Window.h"
#include "../VQEngine.h"
#include "../Culling.h"

#include "Libs/VQUtils/Source/utils.h"

#include <fstream>

//-------------------------------------------------------------------------------
// LOGGING
//-------------------------------------------------------------------------------
#define LOG_CACHED_RESOURCES_ON_LOAD 0
#define LOG_RESOURCE_CREATE          1
//-------------------------------------------------------------------------------


//-------------------------------------------------------------------------------
// Culling
//-------------------------------------------------------------------------------
#define ENABLE_VIEW_FRUSTUM_CULLING 1
#define ENABLE_LIGHT_CULLING        1
//-------------------------------------------------------------------------------


//-------------------------------------------------------------------------------
// Multithreading
//-------------------------------------------------------------------------------
#define UPDATE_THREAD__ENABLE_WORKERS 1
#if UPDATE_THREAD__ENABLE_WORKERS
	#define ENABLE_THREADED_SHADOW_FRUSTUM_GATHER 0
#endif
//-------------------------------------------------------------------------------

using namespace DirectX;

static MeshID LAST_USED_MESH_ID = EBuiltInMeshes::NUM_BUILTIN_MESHES;

//-------------------------------------------------------------------------------
//
// RESOURCE MANAGEMENT
//
//-------------------------------------------------------------------------------
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

FSceneStats Scene::GetSceneRenderStats(int FRAME_DATA_INDEX) const
{
	FSceneStats stats = {};
	const FSceneView& view = mFrameSceneViews[FRAME_DATA_INDEX];
	const FSceneShadowView& shadowView = mFrameShadowViews[FRAME_DATA_INDEX];

	stats.NumStaticLights         = static_cast<uint>(mLightsStatic.size()    );
	stats.NumDynamicLights        = static_cast<uint>(mLightsDynamic.size()   );
	stats.NumStationaryLights     = static_cast<uint>(mLightsStationary.size());
	stats.NumShadowingPointLights = shadowView.NumPointShadowViews;
	stats.NumShadowingSpotLights  = shadowView.NumSpotShadowViews;
	auto fnCountLights = [&stats](const std::vector<Light>& vLights)
	{
		for (const Light& l : vLights)
		{
			switch (l.Type)
			{
			case Light::EType::DIRECTIONAL: 
				++stats.NumDirectionalLights;
				if (!l.bEnabled) ++stats.NumDisabledDirectionalLights;
				break;
			case Light::EType::POINT: 
				++stats.NumPointLights; 
				if (!l.bEnabled) ++stats.NumDisabledPointLights;
				break;
			case Light::EType::SPOT: 
				++stats.NumSpotLights; 
				if (!l.bEnabled) ++stats.NumDisabledSpotLights;
				break;

			//case Light::EType::DIRECTIONAL: break;
			default:
				//assert(false); // Area lights are WIP atm, so will hit this on Default scene, as defined in the Default.xml
				break;
			}
		}
	};
	fnCountLights(mLightsStationary);
	fnCountLights(mLightsDynamic);
	fnCountLights(mLightsStatic);


	stats.NumMeshRenderCommands        = static_cast<uint>(view.meshRenderCommands.size() + view.lightRenderCommands.size() + view.lightBoundsRenderCommands.size() /*+ view.boundingBoxRenderCommands.size()*/);
	stats.NumBoundingBoxRenderCommands = static_cast<uint>(view.boundingBoxRenderCommands.size());
	auto fnCountShadowMeshRenderCommands = [](const FSceneShadowView& shadowView) -> uint
	{
		uint NumShadowRenderCmds = 0;
		for (uint i = 0; i < shadowView.NumPointShadowViews; ++i)
		for (uint face = 0; face < 6u; ++face)
			NumShadowRenderCmds += static_cast<uint>(shadowView.ShadowViews_Point[i * 6 + face].meshRenderCommands.size());
		for (uint i = 0; i < shadowView.NumSpotShadowViews; ++i)
			NumShadowRenderCmds += static_cast<uint>(shadowView.ShadowViews_Spot[i].meshRenderCommands.size());
		NumShadowRenderCmds += static_cast<uint>(shadowView.ShadowView_Directional.meshRenderCommands.size());
		return NumShadowRenderCmds;
	};
	stats.NumShadowMeshRenderCommands = fnCountShadowMeshRenderCommands(shadowView);
	
	stats.NumMeshes    = static_cast<uint>(this->mMeshes.size());
	stats.NumModels    = static_cast<uint>(this->mModels.size());
	stats.NumMaterials = static_cast<uint>(this->mMaterials.size());
	stats.NumObjects   = static_cast<uint>(this->mpObjects.size());
	stats.NumCameras   = static_cast<uint>(this->mCameras.size());

	return stats;
}


//-------------------------------------------------------------------------------
//
// STATIC HELPERS
//
//-------------------------------------------------------------------------------
// returns true if culled
static bool ShouldCullLight(const Light& l, const FFrustumPlaneset& MainViewFrustumPlanesInWorldSpace)
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
	SCOPED_CPU_MARKER("GetActiveAndCulledLightIndices()");
	constexpr bool bCULL_LIGHTS = true;

	std::vector<size_t> ActiveLightIndices;

	for (size_t i = 0; i < vLights.size(); ++i)
	{
		const Light& l = vLights[i];

		// skip disabled and non-shadow casting lights
		if (!l.bEnabled || !l.bCastingShadows)
			continue;

#if ENABLE_LIGHT_CULLING	
		if (ShouldCullLight(l, MainViewFrustumPlanesInWorldSpace))
			continue;
#endif

		ActiveLightIndices.push_back(i);
	}

	return ActiveLightIndices;
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
static void ToggleBool(bool& b) { b = !b; }


//-------------------------------------------------------------------------------
//
// SCENE
//
//-------------------------------------------------------------------------------
Scene::Scene(VQEngine& engine, int NumFrameBuffers, const Input& input, const std::unique_ptr<Window>& pWin, VQRenderer& renderer)
	: mInput(input)
	, mpWindow(pWin)
	, mEngine(engine)
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	, mFrameSceneViews(NumFrameBuffers)
	, mFrameShadowViews(NumFrameBuffers)
#else
	, mFrameSceneViews(1)
	, mFrameShadowViews(1)
#endif
	, mIndex_SelectedCamera(0)
	, mIndex_ActiveEnvironmentMapPreset(-1)
	, mGameObjectPool(NUM_GAMEOBJECT_POOL_SIZE, GAMEOBJECT_BYTE_ALIGNMENT)
	, mTransformPool(NUM_GAMEOBJECT_POOL_SIZE, GAMEOBJECT_BYTE_ALIGNMENT)
	, mResourceNames(engine.GetResourceNames())
	, mAssetLoader(engine.GetAssetLoader())
	, mRenderer(renderer)
	, mMaterialAssignments(engine.GetAssetLoader().GetThreadPool_TextureLoad())
	, mBoundingBoxHierarchy(mMeshes, mModels, mMaterials, mpTransforms)
{}


void Scene::PreUpdate(int FRAME_DATA_INDEX, int FRAME_DATA_PREV_INDEX)
{
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	if (std::max(FRAME_DATA_INDEX, FRAME_DATA_PREV_INDEX) >= mFrameSceneViews.size())
	{
		Log::Warning("Scene::PreUpdate(): Frame data is not yet allocated!");
		return;
	}
	assert(std::max(FRAME_DATA_INDEX, FRAME_DATA_PREV_INDEX) < mFrameSceneViews.size());
	FSceneView& SceneViewCurr = mFrameSceneViews[FRAME_DATA_INDEX];
	FSceneView& SceneViewPrev = mFrameSceneViews[FRAME_DATA_PREV_INDEX];
	// bring over the parameters from the last frame
	SceneViewCurr.postProcessParameters = SceneViewPrev.postProcessParameters;
	SceneViewCurr.sceneParameters = SceneViewPrev.sceneParameters;
#endif
}

void Scene::Update(float dt, int FRAME_DATA_INDEX)
{
	SCOPED_CPU_MARKER("Scene::Update()");

	assert(FRAME_DATA_INDEX < mFrameSceneViews.size());
	FSceneView& SceneView = mFrameSceneViews[FRAME_DATA_INDEX];

	Camera& Cam = this->mCameras[this->mIndex_SelectedCamera];

	Cam.Update(dt, mInput);
	this->HandleInput(SceneView);
	this->UpdateScene(dt, SceneView);
}

void Scene::PostUpdate(ThreadPool& UpdateWorkerThreadPool, int FRAME_DATA_INDEX)
{
	SCOPED_CPU_MARKER("Scene::PostUpdate()");
	assert(FRAME_DATA_INDEX < mFrameSceneViews.size());
	FSceneView& SceneView = mFrameSceneViews[FRAME_DATA_INDEX];
	FSceneShadowView& ShadowView = mFrameShadowViews[FRAME_DATA_INDEX];

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
	SceneView.HDRIYawOffset = SceneView.sceneParameters.fYawSliderValue * XM_PI * 2.0f;

	const FFrustumPlaneset ViewFrustumPlanes = FFrustumPlaneset::ExtractFromMatrix(SceneView.viewProj);

	{
		SCOPED_CPU_MARKER("BuildBoundingBoxHierarchy");
		mBoundingBoxHierarchy.Clear();
		mBoundingBoxHierarchy.BuildGameObjectBoundingBoxes(mpObjects);
		mBoundingBoxHierarchy.BuildMeshBoundingBoxes(mpObjects);
	}

	if constexpr (!UPDATE_THREAD__ENABLE_WORKERS)
	{
		PrepareSceneMeshRenderParams(ViewFrustumPlanes, SceneView.meshRenderCommands);
		GatherSceneLightData(SceneView);
		PrepareShadowMeshRenderParams(ShadowView, ViewFrustumPlanes, UpdateWorkerThreadPool);
		PrepareLightMeshRenderParams(SceneView);
		PrepareBoundingBoxRenderParams(SceneView);
	}
	else
	{
		UpdateWorkerThreadPool.AddTask([=, &SceneView]()
		{
			PrepareSceneMeshRenderParams(ViewFrustumPlanes, SceneView.meshRenderCommands);
		});
		GatherSceneLightData(SceneView);
		PrepareShadowMeshRenderParams(ShadowView, ViewFrustumPlanes, UpdateWorkerThreadPool);
		PrepareLightMeshRenderParams(SceneView);
		{
			SCOPED_CPU_MARKER_C("BUSY_WAIT_WORKER", 0xFFFF0000);
			while (UpdateWorkerThreadPool.GetNumActiveTasks() != 0);
		}
		PrepareBoundingBoxRenderParams(SceneView);
	}
}



FSceneView& Scene::GetSceneView(int FRAME_DATA_INDEX)
{
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	return mFrameSceneViews[FRAME_DATA_INDEX];
#else
	return mFrameSceneViews[0];
#endif
}
const FSceneView& Scene::GetSceneView(int FRAME_DATA_INDEX) const
{
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	return mFrameSceneViews[FRAME_DATA_INDEX]; 
#else
	return mFrameSceneViews[0];
#endif
}
const FSceneShadowView& Scene::GetShadowView(int FRAME_DATA_INDEX) const 
{
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	return mFrameShadowViews[FRAME_DATA_INDEX]; 
#else
	return mFrameShadowViews[0];
#endif
}
FPostProcessParameters& Scene::GetPostProcessParameters(int FRAME_DATA_INDEX) 
{
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	return mFrameSceneViews[FRAME_DATA_INDEX].postProcessParameters; 
#else
	return mFrameSceneViews[0].postProcessParameters;
#endif
}
const FPostProcessParameters& Scene::GetPostProcessParameters(int FRAME_DATA_INDEX) const 
{
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	return mFrameSceneViews[FRAME_DATA_INDEX].postProcessParameters; 
#else
	return mFrameSceneViews[0].postProcessParameters;
#endif
}


void Scene::RenderUI(FUIState& UIState, uint32_t W, uint32_t H)
{
	this->RenderSceneUI();
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
		ToggleBool(SceneView.sceneParameters.bDrawLightBounds);
	}
	if (mInput.IsKeyTriggered("N"))
	{
		if(bIsShiftDown) ToggleBool(SceneView.sceneParameters.bDrawGameObjectBoundingBoxes);
		else             ToggleBool(SceneView.sceneParameters.bDrawMeshBoundingBoxes);
	}

	
	// if there's no EnvMap selected and the user wants the change the env map,
	// temporarily assign 0 so that Circular*crement() can work
	if ((mInput.IsKeyTriggered("PageUp")|| mInput.IsKeyTriggered("PageDown")) && mIndex_ActiveEnvironmentMapPreset == -1)
	{
		mIndex_ActiveEnvironmentMapPreset = 0;
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
	SCOPED_CPU_MARKER("Scene::GatherSceneLightData()");
	SceneView.lightBoundsRenderCommands.clear();
	SceneView.lightRenderCommands.clear();

	VQ_SHADER_DATA::SceneLighting& data = SceneView.GPULightingData;

	int iGPUSpot = 0;  int iGPUSpotShadow = 0;
	int iGPUPoint = 0; int iGPUPointShadow = 0;
	auto fnGatherLightData = [&](const std::vector<Light>& vLights, Light::EMobility eLightMobility)
	{
		for (const Light& l : vLights)
		{
			if (!l.bEnabled) continue;
			if (l.Mobility != eLightMobility) continue;

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
	fnGatherLightData(mLightsStatic, Light::EMobility::STATIC);
	fnGatherLightData(mLightsStationary, Light::EMobility::STATIONARY);
	fnGatherLightData(mLightsDynamic, Light::EMobility::DYNAMIC);

	data.numPointCasters = iGPUPointShadow;
	data.numPointLights = iGPUPoint;
	data.numSpotCasters = iGPUSpotShadow;
	data.numSpotLights = iGPUSpot;
}

void Scene::PrepareLightMeshRenderParams(FSceneView& SceneView) const
{
	SCOPED_CPU_MARKER("Scene::PrepareLightMeshRenderParams()");
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
			cmd.matWorldTransformation = l.GetWorldTransformationMatrix();

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
					cmd.matWorldTransformation = scaleConeToRange * alignConeToSpotLightTransformation * tf.matWorldTransformation();
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
					cmd.matWorldTransformation = tf.matWorldTransformation();
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


void Scene::PrepareSceneMeshRenderParams(const FFrustumPlaneset& MainViewFrustumPlanesInWorldSpace, std::vector<FMeshRenderCommand>& MeshRenderCommands) const
{
	SCOPED_CPU_MARKER("Scene::PrepareSceneMeshRenderParams()");

#if ENABLE_VIEW_FRUSTUM_CULLING

	FFrustumCullWorkerContext GameObjectFrustumCullWorkerContext;
	
	GameObjectFrustumCullWorkerContext.AddWorkerItem(MainViewFrustumPlanesInWorldSpace
		, mBoundingBoxHierarchy.mGameObjectBoundingBoxes
		, mBoundingBoxHierarchy.mGameObjectBoundingBoxGameObjectPointerMapping
	);

	FFrustumCullWorkerContext MeshFrustumCullWorkerContext; // TODO: populate after culling game objects?
	MeshFrustumCullWorkerContext.AddWorkerItem(MainViewFrustumPlanesInWorldSpace
		, mBoundingBoxHierarchy.mMeshBoundingBoxes
		, mBoundingBoxHierarchy.mMeshBoundingBoxGameObjectPointerMapping
	);

	constexpr bool SINGLE_THREADED_CULL = true; // !UPDATE_THREAD__ENABLE_WORKERS;
	//-----------------------------------------------------------------------------------------
	{
		SCOPED_CPU_MARKER("CullMainViewFrustum");
		if constexpr (SINGLE_THREADED_CULL)
		{
			GameObjectFrustumCullWorkerContext.ProcessWorkItems_SingleThreaded();
			MeshFrustumCullWorkerContext.ProcessWorkItems_SingleThreaded();
		}
		else
		{
			const size_t NumThreadsToDistributeIncludingThisThread = ThreadPool::sHardwareThreadCount - 1; // -1 to leave RenderThread a physical core

			// The current FFrustumCullWorkerContext doesn't support threading a single list
			// TODO: keep an eye on the perf here, find a way to thread of necessary
			assert(false);
#if 0
			GameObjectFrustumCullWorkerContext.ProcessWorkItems_MultiThreaded(NumThreadsIncludingThisThread, UpdateWorkerThreadPool);
			MeshFrustumCullWorkerContext.ProcessWorkItems_MultiThreaded(NumThreadsIncludingThisThread, UpdateWorkerThreadPool);
#endif
		}
	}
	//-----------------------------------------------------------------------------------------
	
	// TODO: we have a culled list of game object boundin box indices
	//for (size_t iFrustum = 0; iFrustum < NumFrustumsToCull; ++iFrustum)
	{
		SCOPED_CPU_MARKER("RecordShadowMeshRenderCommand");
		//const std::vector<size_t>& CulledBoundingBoxIndexList_Obj = GameObjectFrustumCullWorkerContext.vCulledBoundingBoxIndexLists[iFrustum];

		MeshRenderCommands.clear();

		const std::vector<size_t>& CulledBoundingBoxIndexList_Msh = MeshFrustumCullWorkerContext.vCulledBoundingBoxIndexListPerView[0];
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
			meshRenderCmd.matWorldTransformation = pTF->matWorldTransformation();
			meshRenderCmd.matNormalTransformation = pTF->NormalMatrix(meshRenderCmd.matWorldTransformation);
			meshRenderCmd.matID = model.mData.mOpaqueMaterials.at(meshID);
			MeshRenderCommands.push_back(meshRenderCmd);
		}
	}

#else // no culling, render all game objects
	
	MeshRenderCommands.clear();
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
			meshRenderCmd.matWorldTransformation = pTF->matWorldTransformation();
			meshRenderCmd.matNormalTransformation = pTF->NormalMatrix(meshRenderCmd.matWorldTransformation);
			meshRenderCmd.matID = model.mData.mOpaqueMaterials.at(id);

			meshRenderCmd.ModelName = model.mModelName;
			meshRenderCmd.MaterialName = ""; // TODO

			MeshRenderCommands.push_back(meshRenderCmd);
		}
	}
#endif // ENABLE_VIEW_FRUSTUM_CULLING


}


void Scene::GatherSpotLightFrustumParameters(
	FSceneShadowView& SceneShadowView
	, size_t iShadowView
	, const Light& l
)
{
	assert(false); // todo
	FSceneShadowView::FShadowView& ShadowView = SceneShadowView.ShadowViews_Spot[iShadowView];

	XMMATRIX matViewProj = l.GetViewProjectionMatrix();

	//const size_t FrustumIndex = DispatchContext.AddWorkerItem(FFrustumPlaneset::ExtractFromMatrix(matViewProj), BoundingBoxList, pGameObjects);
	//
	//FSceneShadowView::FShadowView& ShadowView = SceneShadowView.ShadowViews_Spot[iSpot];
	//ShadowView.matViewProj = matViewProj;
	//FrustumIndex_pShadowViewLookup[FrustumIndex] = &ShadowView;
	//SceneShadowView.NumSpotShadowViews = iSpot;
}

void Scene::PrepareShadowMeshRenderParams(FSceneShadowView& SceneShadowView, const FFrustumPlaneset& MainViewFrustumPlanesInWorldSpace, ThreadPool& UpdateWorkerThreadPool) const
{
	SCOPED_CPU_MARKER("Scene::PrepareShadowMeshRenderParams()");
#if ENABLE_VIEW_FRUSTUM_CULLING
	constexpr bool bCULL_LIGHT_VIEWS     = false;
	constexpr bool bSINGLE_THREADED_CULL = !UPDATE_THREAD__ENABLE_WORKERS;

	#if ENABLE_THREADED_SHADOW_FRUSTUM_GATHER

	// TODO: gathering point light faces takes long and can be threaded
	assert(false);

	#else

	int iLight = 0;
	int iPoint = 0;
	int iSpot = 0;
	auto fnGatherShadowingLightFrustumCullParameters = [&](
		const std::vector<Light>& vLights
		, const std::vector<size_t>& vActiveLightIndices
		, FFrustumCullWorkerContext& DispatchContext
		, const std::vector<FBoundingBox>& BoundingBoxList
		, const std::vector<const GameObject*>& pGameObjects
		, std::unordered_map<size_t, FSceneShadowView::FShadowView*>& FrustumIndex_pShadowViewLookup
	)
	{
		SCOPED_CPU_MARKER("GatherLightFrustumCullParams");
		// prepare frustum cull work context
		for(const size_t& LightIndex : vActiveLightIndices)
		{
			const Light& l = vLights[LightIndex];
			const bool bIsLastLight = LightIndex == vActiveLightIndices.back();

			switch (l.Type)
			{
			case Light::EType::DIRECTIONAL:
			{
				FSceneShadowView::FShadowView& ShadowView = SceneShadowView.ShadowView_Directional;
				ShadowView.matViewProj = l.GetViewProjectionMatrix();
				const size_t FrustumIndex = DispatchContext.AddWorkerItem(FFrustumPlaneset::ExtractFromMatrix(ShadowView.matViewProj), BoundingBoxList, pGameObjects);
				FrustumIndex_pShadowViewLookup[FrustumIndex] = &ShadowView;
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
	#endif // ENABLE_THREADED_SHADOW_FRUSTUM_GATHER

	static const size_t HW_CORE_COUNT = ThreadPool::sHardwareThreadCount / 2;
	const size_t NumThreadsIncludingThisThread = HW_CORE_COUNT - 1; // -1 to leave RenderThread a physical core

	// distance-cull and get active shadowing lights from various light containers
	const std::vector<size_t> vActiveLightIndices_Static     = GetActiveAndCulledLightIndices(mLightsStatic, MainViewFrustumPlanesInWorldSpace);
	const std::vector<size_t> vActiveLightIndices_Stationary = GetActiveAndCulledLightIndices(mLightsStationary, MainViewFrustumPlanesInWorldSpace);
	const std::vector<size_t> vActiveLightIndices_Dynamic    = GetActiveAndCulledLightIndices(mLightsDynamic, MainViewFrustumPlanesInWorldSpace);
	
	// frustum cull memory containers
	std::unordered_map<size_t, FSceneShadowView::FShadowView*> FrustumIndex_pShadowViewLookup;
	FFrustumCullWorkerContext MeshFrustumCullWorkerContext;

	FFrustumCullWorkerContext GameObjectFrustumCullWorkerContext;

#if 0
	//------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	//
	// Coarse Culling : cull the game object bounding boxes against view frustums
	//
	fnGatherShadowingLightFrustumCullParameters(mLightsStatic    , vActiveLightIndices_Static    , GameObjectFrustumCullWorkerContext, mBoundingBoxHierarchy.mGameObjectBoundingBoxes, mBoundingBoxHierarchy.mGameObjectBoundingBoxGameObjectPointerMapping, FrustumIndex_pShadowViewLookup);
	fnGatherShadowingLightFrustumCullParameters(mLightsStationary, vActiveLightIndices_Stationary, GameObjectFrustumCullWorkerContext, mBoundingBoxHierarchy.mGameObjectBoundingBoxes, mBoundingBoxHierarchy.mGameObjectBoundingBoxGameObjectPointerMapping, FrustumIndex_pShadowViewLookup);
	fnGatherShadowingLightFrustumCullParameters(mLightsDynamic   , vActiveLightIndices_Dynamic   , GameObjectFrustumCullWorkerContext, mBoundingBoxHierarchy.mGameObjectBoundingBoxes, mBoundingBoxHierarchy.mGameObjectBoundingBoxGameObjectPointerMapping, FrustumIndex_pShadowViewLookup);
	if constexpr (bSINGLE_THREADED_CULL) GameObjectFrustumCullWorkerContext.ProcessWorkItems_SingleThreaded();
	else                                 GameObjectFrustumCullWorkerContext.ProcessWorkItems_MultiThreaded(NumThreadsIncludingThisThread, UpdateWorkerThreadPool);
	//------------------------------------------------------------------------------------------------------------------------------------------------------------------------

	const_cast<SceneBoundingBoxHierarchy&>(mBoundingBoxHierarchy).BuildMeshBoundingBoxes()

	//
	// TODO: Determine meshes to cull
	//
	const size_t NumGameObjectFrustums = GameObjectFrustumCullWorkerContext.vCulledBoundingBoxIndexListPerView.size();
	for (size_t iFrustum = 0; iFrustum < NumGameObjectFrustums; ++iFrustum)
	{
		const std::vector<size_t>& CulledBoundingBoxIndexList_Obj = GameObjectFrustumCullWorkerContext.vCulledBoundingBoxIndexListPerView[iFrustum];

		
	}
#endif

	//------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	//
	// Fine Culling : cull the mesh bounding boxes against view frustums
	//
#if ENABLE_THREADED_SHADOW_FRUSTUM_GATHER

#else
	fnGatherShadowingLightFrustumCullParameters(mLightsStatic    , vActiveLightIndices_Static    , MeshFrustumCullWorkerContext, mBoundingBoxHierarchy.mMeshBoundingBoxes, mBoundingBoxHierarchy.mMeshBoundingBoxGameObjectPointerMapping, FrustumIndex_pShadowViewLookup);
	fnGatherShadowingLightFrustumCullParameters(mLightsStationary, vActiveLightIndices_Stationary, MeshFrustumCullWorkerContext, mBoundingBoxHierarchy.mMeshBoundingBoxes, mBoundingBoxHierarchy.mMeshBoundingBoxGameObjectPointerMapping, FrustumIndex_pShadowViewLookup);
	fnGatherShadowingLightFrustumCullParameters(mLightsDynamic   , vActiveLightIndices_Dynamic   , MeshFrustumCullWorkerContext, mBoundingBoxHierarchy.mMeshBoundingBoxes, mBoundingBoxHierarchy.mMeshBoundingBoxGameObjectPointerMapping, FrustumIndex_pShadowViewLookup);
#endif
	{
		SCOPED_CPU_MARKER("Cull Frustums");
		if constexpr (bSINGLE_THREADED_CULL) MeshFrustumCullWorkerContext.ProcessWorkItems_SingleThreaded();
		else                                 MeshFrustumCullWorkerContext.ProcessWorkItems_MultiThreaded(NumThreadsIncludingThisThread, UpdateWorkerThreadPool);
	}
	//------------------------------------------------------------------------------------------------------------------------------------------------------------------------


	//
	//  Record Mesh Render Commands
	//
	{
		SCOPED_CPU_MARKER("RecordMeshRenderCommands");
		const size_t NumMeshFrustums = MeshFrustumCullWorkerContext.vCulledBoundingBoxIndexListPerView.size();
		for (size_t iFrustum = 0; iFrustum < NumMeshFrustums; ++iFrustum)
		{
			FSceneShadowView::FShadowView*& pShadowView = FrustumIndex_pShadowViewLookup.at(iFrustum);
			std::vector<FShadowMeshRenderCommand>& vMeshRenderList = pShadowView->meshRenderCommands;
			vMeshRenderList.clear();

			const std::vector<size_t>& CulledBoundingBoxIndexList_Msh = MeshFrustumCullWorkerContext.vCulledBoundingBoxIndexListPerView[iFrustum];
			for (const size_t& BBIndex : CulledBoundingBoxIndexList_Msh)
			{
				assert(BBIndex < mBoundingBoxHierarchy.mMeshBoundingBoxMeshIDMapping.size());
				MeshID meshID = mBoundingBoxHierarchy.mMeshBoundingBoxMeshIDMapping[BBIndex];

				const GameObject* pGameObject = MeshFrustumCullWorkerContext.vGameObjectPointerLists[iFrustum][BBIndex];
				Transform* const& pTF = mpTransforms.at(pGameObject->mTransformID);

				// record ShadowMeshRenderCommand
				FShadowMeshRenderCommand meshRenderCmd;
				meshRenderCmd.meshID = meshID;
				meshRenderCmd.matWorldTransformation = pTF->matWorldTransformation();
				meshRenderCmd.matWorldViewProj = meshRenderCmd.matWorldTransformation * pShadowView->matViewProj;
				vMeshRenderList.push_back(meshRenderCmd);
			}
		}
	}
#else // ENABLE_VIEW_FRUSTUM_CULLING
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
				meshRenderCmd.matWorldTransformation = pTF->matWorldTransformation();
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
#endif // ENABLE_VIEW_FRUSTUM_CULLING
}


void Scene::PrepareBoundingBoxRenderParams(FSceneView& SceneView) const
{
	SceneView.boundingBoxRenderCommands.clear();

	
	const XMFLOAT3 BBColor_GameObj = XMFLOAT3(0.0f, 0.2f, 0.8f);
	const XMFLOAT3 BBColor_Mesh    = XMFLOAT3(0.0f, 0.8f, 0.2f);

	const XMMATRIX IdentityMat = XMMatrixIdentity();
	const XMVECTOR IdentityQuaternion = XMQuaternionIdentity();
	const XMVECTOR ZeroVector = XMVectorZero();

	auto fnCreateBoundingBoxRenderCommand = [&ZeroVector, &IdentityQuaternion, &IdentityMat](const FBoundingBox& BB, const XMFLOAT3& Color)
	{

		XMVECTOR BBMax = XMLoadFloat3(&BB.ExtentMax);
		XMVECTOR BBMin = XMLoadFloat3(&BB.ExtentMin);
		XMVECTOR ScaleVec = (BBMax - BBMin) * 0.5f;
		XMVECTOR BBOrigin = (BBMax + BBMin) * 0.5f;
		XMMATRIX MatTransform = XMMatrixTransformation(ZeroVector, IdentityQuaternion, ScaleVec, ZeroVector, IdentityQuaternion, BBOrigin);

		FBoundingBoxRenderCommand cmd = {};
		cmd.color = Color;
		cmd.matWorldTransformation = MatTransform;
		cmd.meshID = EBuiltInMeshes::CUBE;
		return cmd;
	};


	if (SceneView.sceneParameters.bDrawGameObjectBoundingBoxes)
	{
		for (const FBoundingBox& BB : mBoundingBoxHierarchy.mGameObjectBoundingBoxes)
		{
			SceneView.boundingBoxRenderCommands.push_back(fnCreateBoundingBoxRenderCommand(BB, BBColor_GameObj));
		}
	}

	if (SceneView.sceneParameters.bDrawMeshBoundingBoxes)
	{
		for (const FBoundingBox& BB : mBoundingBoxHierarchy.mMeshBoundingBoxes)
		{
			SceneView.boundingBoxRenderCommands.push_back(fnCreateBoundingBoxRenderCommand(BB, BBColor_Mesh));
		}
	}


	// Light View Bounding Boxes 
	// TODO
	
}


//-------------------------------------------------------------------------------
//
// MATERIAL REPRESENTATION
//
//-------------------------------------------------------------------------------
FMaterialRepresentation::FMaterialRepresentation()
	: DiffuseColor(MATERIAL_UNINITIALIZED_VALUE, MATERIAL_UNINITIALIZED_VALUE, MATERIAL_UNINITIALIZED_VALUE)
	, EmissiveColor(MATERIAL_UNINITIALIZED_VALUE, MATERIAL_UNINITIALIZED_VALUE, MATERIAL_UNINITIALIZED_VALUE)
{}

