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
#define UPDATE_THREAD__ENABLE_WORKERS 1 // TODO: fix single-threaded path
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
		mat.SRVMaterialMaps = mRenderer.AllocateSRV(NUM_MATERIAL_TEXTURE_MAP_BINDINGS);
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

	const XMMATRIX MatView = cam.GetViewMatrix();
	const XMMATRIX MatProj = cam.GetProjectionMatrix();
	const XMMATRIX MatViewProj = MatView * MatProj;
	const XMMATRIX MatViewProjPrev = mViewProjectionMatrixHistory.find(&cam) != mViewProjectionMatrixHistory.end() 
		? mViewProjectionMatrixHistory.at(&cam)
		: XMMatrixIdentity();
	mViewProjectionMatrixHistory[&cam] = MatViewProj;
	const FFrustumPlaneset ViewFrustumPlanes = FFrustumPlaneset::ExtractFromMatrix(SceneView.viewProj);
	
	// extract scene view
	SceneView.proj = MatProj;
	SceneView.projInverse = XMMatrixInverse(NULL, SceneView.proj);
	SceneView.view = MatView;
	SceneView.viewInverse = cam.GetViewInverseMatrix();
	SceneView.viewProj = MatViewProj;
	SceneView.viewProjPrev = MatViewProjPrev;
	SceneView.cameraPosition = XMLoadFloat3(&camPos);
	SceneView.MainViewCameraYaw = cam.GetYaw();
	SceneView.MainViewCameraPitch = cam.GetPitch();
	SceneView.HDRIYawOffset = SceneView.sceneParameters.fYawSliderValue * XM_PI * 2.0f;

	// reset shadow view
	ShadowView.NumPointShadowViews = 0;
	ShadowView.NumSpotShadowViews = 0;

	// distance-cull and get active shadowing lights from various light containers
	{
		SCOPED_CPU_MARKER("CullLights");
		mActiveLightIndices_Static     = GetActiveAndCulledLightIndices(mLightsStatic, ViewFrustumPlanes);
		mActiveLightIndices_Stationary = GetActiveAndCulledLightIndices(mLightsStationary, ViewFrustumPlanes);
		mActiveLightIndices_Dynamic    = GetActiveAndCulledLightIndices(mLightsDynamic, ViewFrustumPlanes);
	}

	mBoundingBoxHierarchy.Build(mpObjects, UpdateWorkerThreadPool);

	GatherSceneLightData(SceneView);
	GatherShadowViewData(ShadowView, mLightsStatic, mActiveLightIndices_Static);
	GatherShadowViewData(ShadowView, mLightsStationary, mActiveLightIndices_Stationary);
	GatherShadowViewData(ShadowView, mLightsDynamic, mActiveLightIndices_Dynamic);

	GatherFrustumCullParameters(SceneView, ShadowView, UpdateWorkerThreadPool);

	CullFrustums(SceneView, UpdateWorkerThreadPool);

	BatchInstanceData(SceneView, UpdateWorkerThreadPool);

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


//--------------------------------------------------------------------------------

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

void Scene::GatherShadowViewData(FSceneShadowView& SceneShadowView, const std::vector<Light>& vLights, const std::vector<size_t>& vActiveLightIndices)
{
	SCOPED_CPU_MARKER("GatherShadowViewData");
	for (const size_t& LightIndex : vActiveLightIndices)
	{
		const Light& l = vLights[LightIndex];

		switch (l.Type)
		{
		case Light::EType::DIRECTIONAL:
		{
			FSceneShadowView::FShadowView& ShadowView = SceneShadowView.ShadowView_Directional;
			ShadowView.matViewProj = l.GetViewProjectionMatrix();
		}	break;
		case Light::EType::SPOT:
		{
			XMMATRIX matViewProj = l.GetViewProjectionMatrix();
			FSceneShadowView::FShadowView& ShadowView = SceneShadowView.ShadowViews_Spot[SceneShadowView.NumSpotShadowViews++];
			ShadowView.matViewProj = matViewProj;
		} break;
		case Light::EType::POINT:
		{
			for (int face = 0; face < 6; ++face)
			{
				XMMATRIX matViewProj = l.GetViewProjectionMatrix(static_cast<Texture::CubemapUtility::ECubeMapLookDirections>(face));
				FSceneShadowView::FShadowView& ShadowView = SceneShadowView.ShadowViews_Point[SceneShadowView.NumPointShadowViews * 6 + face];
				ShadowView.matViewProj = matViewProj;
			}
			SceneShadowView.PointLightLinearDepthParams[SceneShadowView.NumPointShadowViews].fFarPlane = l.Range;
			SceneShadowView.PointLightLinearDepthParams[SceneShadowView.NumPointShadowViews].vWorldPos = l.Position;
			++SceneShadowView.NumPointShadowViews;
		} break;
		}
	}
}




void Scene::GatherFrustumCullParameters(const FSceneView& SceneView, FSceneShadowView& SceneShadowView, ThreadPool& UpdateWorkerThreadPool)
{
	SCOPED_CPU_MARKER("GatherFrustumCullParameters");
	const SceneBoundingBoxHierarchy& BVH = mBoundingBoxHierarchy;

	std::vector< FFrustumPlaneset> FrustumPlanesets(1 + SceneShadowView.NumSpotShadowViews + SceneShadowView.NumPointShadowViews * 6);
	size_t iFrustum = 0;
	{
		SCOPED_CPU_MARKER("CollectFrustumPlanesets");
		FrustumPlanesets[iFrustum++] = FFrustumPlaneset::ExtractFromMatrix(SceneView.viewProj);

		for (size_t iPoint = 0; iPoint < SceneShadowView.NumPointShadowViews; ++iPoint)
			for (size_t face = 0; face < 6; ++face)
			{
				const size_t iPointFace = iPoint * 6 + face;
				mFrustumIndex_pShadowViewLookup[iFrustum] = &SceneShadowView.ShadowViews_Point[iPointFace];
				FrustumPlanesets[iFrustum++] = FFrustumPlaneset::ExtractFromMatrix(SceneShadowView.ShadowViews_Point[iPointFace].matViewProj);

			}

		for (size_t iSpot = 0; iSpot < SceneShadowView.NumSpotShadowViews; ++iSpot)
		{
			mFrustumIndex_pShadowViewLookup[iFrustum] = &SceneShadowView.ShadowViews_Spot[iSpot];
			FrustumPlanesets[iFrustum++] = FFrustumPlaneset::ExtractFromMatrix(SceneShadowView.ShadowViews_Spot[iSpot].matViewProj);
		}
	}

	mFrustumCullWorkerContext.InvalidateContextData();

#if UPDATE_THREAD__ENABLE_WORKERS

	{
		SCOPED_CPU_MARKER("InitFrustumCullWorkerContexts");
#if 0 // debug-single threaded
		for (size_t i = 0; i < FrustumPlanesets.size(); ++i)
			mFrustumCullWorkerContext.AddWorkerItem(FrustumPlanesets[i], BVH.mMeshBoundingBoxes, BVH.mMeshBoundingBoxGameObjectPointerMapping, i);
#else
		{
			mFrustumCullWorkerContext.AllocInputMemoryIfNecessary(FrustumPlanesets.size());
			mFrustumCullWorkerContext.NumValidInputElements = FrustumPlanesets.size();

			const std::vector<std::pair<size_t, size_t>> vRanges = PartitionWorkItemsIntoRanges(FrustumPlanesets.size(), UpdateWorkerThreadPool.GetThreadPoolSize()+1);
			{
				SCOPED_CPU_MARKER("DispatchWorkers");
				size_t currRange = 0;
				for (const std::pair<size_t, size_t>& Range : vRanges)
				{
					if (currRange == 0)
					{
						++currRange; // skip the first range, and do it on this thread after dispatches
						continue;
					}
					const size_t& iBegin = Range.first;
					const size_t& iEnd = Range.second; // inclusive
					assert(iBegin <= iEnd); // ensure work context bounds
					UpdateWorkerThreadPool.AddTask([Range, &FrustumPlanesets, &BVH, this]()
						{
							SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
							for (size_t i = Range.first; i <= Range.second; ++i)
								mFrustumCullWorkerContext.AddWorkerItem(FrustumPlanesets[i]
									, BVH.mMeshBoundingBoxes
									, BVH.mMeshBoundingBoxGameObjectPointerMapping
									, BVH.mMeshBoundingBoxMaterialIDMapping
									, i
								);
						});
				}
			}

			for (size_t i = vRanges[0].first; i <= vRanges[0].second; ++i)
				mFrustumCullWorkerContext.AddWorkerItem(FrustumPlanesets[i]
					, BVH.mMeshBoundingBoxes
					, BVH.mMeshBoundingBoxGameObjectPointerMapping
					, BVH.mMeshBoundingBoxMaterialIDMapping
					, i
				);
		}
#endif
	}

	// ---------------------------------------------------SYNC ---------------------------------------------------
	{
		SCOPED_CPU_MARKER_C("BUSY_WAIT_WORKER", 0xFFFF0000);
		while (UpdateWorkerThreadPool.GetNumActiveTasks() != 0);
	}
	// --------------------------------------------------- SYNC ---------------------------------------------------
#else
	{
		SCOPED_CPU_MARKER("InitWorkerContext_MainView");
		mFrustumCullWorkerContext.AddWorkerItem(FrustumPlanesets[0], BVH.mMeshBoundingBoxes, BVH.mMeshBoundingBoxGameObjectPointerMapping);
	}
	{
		SCOPED_CPU_MARKER("InitWorkerContext_ShadowViews");
		for(size_t i=1; i<FrustumPlanesets.size(); ++i)
			mFrustumCullWorkerContext.AddWorkerItem(FrustumPlanesets[i], BVH.mMeshBoundingBoxes, BVH.mMeshBoundingBoxGameObjectPointerMapping);
	}
#endif
}



void Scene::CullFrustums(const FSceneView& SceneView, ThreadPool& UpdateWorkerThreadPool)
{
	SCOPED_CPU_MARKER("CullFrustums");

#if UPDATE_THREAD__ENABLE_WORKERS 

	mFrustumCullWorkerContext.ProcessWorkItems_MultiThreaded(UpdateWorkerThreadPool.GetThreadPoolSize()+1, UpdateWorkerThreadPool);

	// ---------------------------------------------------SYNC ---------------------------------------------------
	{
		SCOPED_CPU_MARKER_C("BUSY_WAIT_WORKER", 0xFFFF0000);
		while (UpdateWorkerThreadPool.GetNumActiveTasks() != 0);
	}
	// --------------------------------------------------- SYNC ---------------------------------------------------

#else
	mFrustumCullWorkerContext.ProcessWorkItems_SingleThreaded();
#endif
}



static const XMMATRIX IdentityMat = XMMatrixIdentity();
static const XMVECTOR IdentityQuaternion = XMQuaternionIdentity();
static const XMVECTOR ZeroVector = XMVectorZero();
static XMMATRIX GetBBTransformMatrix(const FBoundingBox& BB)
{
	XMVECTOR BBMax = XMLoadFloat3(&BB.ExtentMax);
	XMVECTOR BBMin = XMLoadFloat3(&BB.ExtentMin);
	XMVECTOR ScaleVec = (BBMax - BBMin) * 0.5f;
	XMVECTOR BBOrigin = (BBMax + BBMin) * 0.5f;
	XMMATRIX MatTransform = XMMatrixTransformation(ZeroVector, IdentityQuaternion, ScaleVec, ZeroVector, IdentityQuaternion, BBOrigin);
	return MatTransform;
}
static void BatchBoundingBoxRenderCommandData(
	std::vector<FInstancedBoundingBoxRenderCommand>& cmds
	, const std::vector<FBoundingBox>& BBs
	, const XMMATRIX viewProj
	, const XMFLOAT3 Color
	, size_t iBegin
)
{
	SCOPED_CPU_MARKER("BatchBoundingBoxRenderCommandData");
	int NumBBsToProcess = (int)BBs.size();
	size_t i = 0;
	int iBB = 0;
	while (NumBBsToProcess > 0)
	{
		FInstancedBoundingBoxRenderCommand& cmd = cmds[iBegin + i];
		cmd.matWorldViewProj.resize(std::min(MAX_INSTANCE_COUNT__UNLIT_SHADER, (size_t)NumBBsToProcess));
		cmd.meshID = EBuiltInMeshes::CUBE;
		cmd.color = Color;

		int iBatch = 0;
		while (iBatch < MAX_INSTANCE_COUNT__UNLIT_SHADER && iBB < BBs.size())
		{
			cmd.matWorldViewProj[iBatch] = GetBBTransformMatrix(BBs[iBB]) * viewProj;
			++iBatch;
			++iBB;
		}

		NumBBsToProcess -= iBatch;
		++i;
	}
}
void Scene::BatchInstanceData_BoundingBox(FSceneView& SceneView
	, ThreadPool& UpdateWorkerThreadPool
	, const DirectX::XMMATRIX matViewProj) const
{
	SCOPED_CPU_MARKER("BoundingBox");
	const bool bDrawGameObjectBBs = SceneView.sceneParameters.bDrawGameObjectBoundingBoxes;
	const bool bDrawMeshBBs = SceneView.sceneParameters.bDrawMeshBoundingBoxes;

	const XMFLOAT3 BBColor_GameObj = XMFLOAT3(0.0f, 0.2f, 0.8f);
	const XMFLOAT3 BBColor_Mesh = XMFLOAT3(0.0f, 0.8f, 0.2f);


	auto fnBatch = [&UpdateWorkerThreadPool](
		std::vector<FInstancedBoundingBoxRenderCommand>& cmds
		, const std::vector<FBoundingBox>& BBs
		, size_t iBoundingBox
		, const XMFLOAT3 BBColor
		, const XMMATRIX matViewProj
		, const char* strMarker = ""
	)
	{
		SCOPED_CPU_MARKER(strMarker);
		constexpr size_t MIN_NUM_BOUNDING_BOX_FOR_THREADING = 128;
		if (BBs.size() < MIN_NUM_BOUNDING_BOX_FOR_THREADING)
		{
			BatchBoundingBoxRenderCommandData(cmds
				, BBs
				, matViewProj
				, BBColor
				, iBoundingBox
			);
		}
		else
		{
			SCOPED_CPU_MARKER("Dispatch");
			UpdateWorkerThreadPool.AddTask([=, &BBs, &cmds]()
			{
				SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
				BatchBoundingBoxRenderCommandData(cmds
					, BBs
					, matViewProj
					, BBColor
					, iBoundingBox
				);
			});
		}
	};


	{
		SCOPED_CPU_MARKER("AllocMem");
		SceneView.boundingBoxRenderCommands.resize(
			(bDrawGameObjectBBs ? mBoundingBoxHierarchy.mGameObjectBoundingBoxes.size() : 0)
			+ (bDrawMeshBBs ? mBoundingBoxHierarchy.mMeshBoundingBoxes.size() : 0)
		);
	}
	// --------------------------------------------------------------
	// Game Object Bounding Boxes
	// --------------------------------------------------------------
	size_t iBoundingBox = 0;
	if (SceneView.sceneParameters.bDrawGameObjectBoundingBoxes)
	{
#if RENDER_INSTANCED_BOUNDING_BOXES 
		fnBatch(SceneView.boundingBoxRenderCommands
			, mBoundingBoxHierarchy.mGameObjectBoundingBoxes
			, iBoundingBox
			, BBColor_GameObj
			, matViewProj
			, "GameObj"
		);
#else
	BatchBoundingBoxRenderCommandData(SceneView.boundingBoxRenderCommands, SceneView, mBoundingBoxHierarchy.mGameObjectBoundingBoxes, BBColor_GameObj, 0);
#endif
	}


	// --------------------------------------------------------------
	// Mesh Bounding Boxes
	// --------------------------------------------------------------
	if (SceneView.sceneParameters.bDrawMeshBoundingBoxes)
	{
		iBoundingBox += (SceneView.sceneParameters.bDrawGameObjectBoundingBoxes ? mBoundingBoxHierarchy.mGameObjectBoundingBoxes.size() : 0);
#if RENDER_INSTANCED_BOUNDING_BOXES 
		fnBatch(SceneView.boundingBoxRenderCommands
			, mBoundingBoxHierarchy.mMeshBoundingBoxes
			, iBoundingBox
			, BBColor_Mesh
			, matViewProj
			, "Meshes"
		);
#else
		BatchBoundingBoxRenderCommandData(SceneView.boundingBoxRenderCommands, SceneView, mBoundingBoxHierarchy.mGameObjectBoundingBoxes, BBColor_GameObj, 0);
#endif
	}
}

static void MarkInstanceDataStale(std::unordered_map < MaterialID, std::unordered_map<MeshID, FSceneView::FMeshInstanceData>>& MaterialMeshInstanceDataLookup)
{
	SCOPED_CPU_MARKER("MarkInstanceDataStale");
	// for-each material
	for (auto itMat = MaterialMeshInstanceDataLookup.begin(); itMat != MaterialMeshInstanceDataLookup.end(); ++itMat)
	{
		const MaterialID matID = itMat->first;
		std::unordered_map<MeshID, FSceneView::FMeshInstanceData>& meshInstanceDataLookup = itMat->second;

		// for-each mesh
		for (auto itMesh = meshInstanceDataLookup.begin(); itMesh != meshInstanceDataLookup.end(); ++itMesh)
		{
			meshInstanceDataLookup[itMesh->first].NumValidData = 0;
		}
	}
}

void Scene::BatchInstanceData_SceneMeshes(
	  std::vector<MeshRenderCommand_t>* pMeshRenderCommands
	, std::unordered_map < MaterialID, std::unordered_map<MeshID, FSceneView::FMeshInstanceData>>& MaterialMeshInstanceDataLookup
	, const std::vector<size_t>& CulledBoundingBoxIndexList_Msh
	, const DirectX::XMMATRIX& matViewProj
	, const DirectX::XMMATRIX& matViewProjHistory
)
{
	SCOPED_CPU_MARKER("BatchInstanceData_SceneMeshes");

#if RENDER_INSTANCED_SCENE_MESHES
	MarkInstanceDataStale(MaterialMeshInstanceDataLookup);

	{
		SCOPED_CPU_MARKER("CollectInstanceData");
		const std::vector<MeshID>& MeshBB_MeshID = mBoundingBoxHierarchy.mMeshBoundingBoxMeshIDMapping;
		const std::vector<MaterialID>& MeshBB_MatID = mBoundingBoxHierarchy.mMeshBoundingBoxMaterialIDMapping;
		const std::vector<Transform>& MeshBB_Transforms = mBoundingBoxHierarchy.mMeshTransforms;

		const size_t iBegin = 0;
		const size_t iEnd = CulledBoundingBoxIndexList_Msh.size();
		for (size_t i = iBegin; i < iEnd; ++i)
		{
			const size_t BBIndex = CulledBoundingBoxIndexList_Msh[i];
			assert(BBIndex < MeshBB_MeshID.size());
			MeshID meshID = MeshBB_MeshID[BBIndex];

			// read game object data
			const Transform& tf = MeshBB_Transforms[BBIndex];
			const XMMATRIX matWorld = tf.matWorldTransformation();
			const XMMATRIX matWorldHistory = tf.matWorldTransformationPrev();
			const XMMATRIX matNormal = tf.NormalMatrix(matWorld);

			MaterialID matID = MeshBB_MatID[BBIndex];
			{
				// record instance data
				FSceneView::FMeshInstanceData& d = MaterialMeshInstanceDataLookup[matID][meshID];

				// if we're seeing this materia/mesh combo the first time, 
				// allocate some memory for instance data, enough for 1 batch
				if (d.InstanceData.empty() || d.InstanceData.size() == d.NumValidData)
				{
					SCOPED_CPU_MARKER("MemAlloc");
					d.InstanceData.resize(d.InstanceData.empty() ? MAX_INSTANCE_COUNT__SCENE_MESHES : d.InstanceData.size() * 2);
				}

				d.InstanceData[d.NumValidData++] = { matWorld, matWorld * matViewProj, matWorldHistory * matViewProjHistory, matNormal };
			}
		}
	}

	int NumInstancedRenderCommands = 0;
	{
		SCOPED_CPU_MARKER("CountBatches");
		for (auto itMat = MaterialMeshInstanceDataLookup.begin(); itMat != MaterialMeshInstanceDataLookup.end(); ++itMat)
		{
			const MaterialID matID = itMat->first;
			const std::unordered_map<MeshID, FSceneView::FMeshInstanceData>& meshInstanceDataLookup = itMat->second;
			for (auto itMesh = meshInstanceDataLookup.begin(); itMesh != meshInstanceDataLookup.end(); ++itMesh)
			{
				const MeshID meshID = itMesh->first;
				const FSceneView::FMeshInstanceData& instData = itMesh->second;

				int NumInstancesToProces = (int)instData.NumValidData;
				int iInst = 0;
				while (NumInstancesToProces > 0)
				{
					const int ThisBatchSize = std::min(MAX_INSTANCE_COUNT__SCENE_MESHES, NumInstancesToProces);
					int iBatch = 0;
					while (iBatch < MAX_INSTANCE_COUNT__SCENE_MESHES && iInst < instData.NumValidData)
					{
						++iBatch;
						++iInst;
					}
					NumInstancesToProces -= iBatch;
					++NumInstancedRenderCommands;
				}
			}
		}
	}
	{
		SCOPED_CPU_MARKER("ResizeCommands");
		pMeshRenderCommands->resize(NumInstancedRenderCommands);
	}

	// chunk-up instance and record commands
	{
		NumInstancedRenderCommands = 0;
		SCOPED_CPU_MARKER("ChunkDataUp");

		// for-each material
		for (auto itMat = MaterialMeshInstanceDataLookup.begin(); itMat != MaterialMeshInstanceDataLookup.end(); ++itMat)
		{
			const MaterialID matID = itMat->first;
			const std::unordered_map<MeshID, FSceneView::FMeshInstanceData>& meshInstanceDataLookup = itMat->second;

			// for-each mesh
			for (auto itMesh = meshInstanceDataLookup.begin(); itMesh != meshInstanceDataLookup.end(); ++itMesh)
			{
				const MeshID meshID = itMesh->first;
				const FSceneView::FMeshInstanceData& MeshInstanceData = itMesh->second;

				int NumInstancesToProces = (int)MeshInstanceData.NumValidData;
				int iInst = 0;
				while (NumInstancesToProces > 0)
				{
					const int ThisBatchSize = std::min(MAX_INSTANCE_COUNT__SCENE_MESHES, NumInstancesToProces);
					FInstancedMeshRenderCommand& cmd = (*pMeshRenderCommands)[NumInstancedRenderCommands];
					cmd.meshID = meshID;
					cmd.matID = matID;
					cmd.matWorld.resize(ThisBatchSize);
					cmd.matWorldViewProj.resize(ThisBatchSize);
					cmd.matWorldViewProjPrev.resize(ThisBatchSize);
					cmd.matNormal.resize(ThisBatchSize);

					int iBatch = 0;
					while (iBatch < MAX_INSTANCE_COUNT__SCENE_MESHES && iInst < MeshInstanceData.NumValidData)
					{
						cmd.matWorld            [iBatch] = MeshInstanceData.InstanceData[iInst].mWorld;
						cmd.matWorldViewProj    [iBatch] = MeshInstanceData.InstanceData[iInst].mWorldViewProj;
						cmd.matWorldViewProjPrev[iBatch] = MeshInstanceData.InstanceData[iInst].mWorldViewProjPrev;
						cmd.matNormal           [iBatch] = MeshInstanceData.InstanceData[iInst].mNormal;
						++iBatch;
						++iInst;
					}

					NumInstancesToProces -= iBatch;
					++NumInstancedRenderCommands;
				}
			}
		}
	}


#else
	MeshRenderCommands.clear();
	for (const size_t& BBIndex : CulledBoundingBoxIndexList_Msh)
	{
		assert(BBIndex < mBoundingBoxHierarchy.mMeshBoundingBoxMeshIDMapping.size());
		MeshID meshID = mBoundingBoxHierarchy.mMeshBoundingBoxMeshIDMapping[BBIndex];

		const GameObject* pGameObject = MeshFrustumCullWorkerContext[WORKER_CONTEXT_INDEX].vGameObjectPointerLists[0][BBIndex];

		Transform* const& pTF = mpTransforms.at(pGameObject->mTransformID);
		const Model& model = mModels.at(pGameObject->mModelID);

		const XMMATRIX matWorld = pTF->matWorldTransformation();
		XMMATRIX matWorldHistory = matWorld;
		if (mTransformWorldMatrixHistory.find(pTF) != mTransformWorldMatrixHistory.end())
		{
			matWorldHistory = mTransformWorldMatrixHistory.at(pTF);
		}
		mTransformWorldMatrixHistory[pTF] = matWorld;

		// record ShadowMeshRenderCommand
		FMeshRenderCommand meshRenderCmd;
		meshRenderCmd.meshID = meshID;
		meshRenderCmd.matWorldTransformation = matWorld;
		meshRenderCmd.matNormalTransformation = pTF->NormalMatrix(meshRenderCmd.matWorldTransformation);
		meshRenderCmd.matID = model.mData.mOpaqueMaterials.at(meshID);
		meshRenderCmd.matWorldTransformationPrev = matWorldHistory;
		MeshRenderCommands.push_back(meshRenderCmd);
	}
#endif
}

void Scene::BatchInstanceData_ShadowMeshes(size_t iFrustum, FSceneShadowView::FShadowView* pShadowView, const std::vector<size_t>* pCulledBoundingBoxIndexList_Msh, XMMATRIX matViewProj) const
{
	SCOPED_CPU_MARKER("ProcessShadowFrustumRenderCommands");

	{
		SCOPED_CPU_MARKER("MarkInstanceDataStale");
		// for-each mesh
		for (auto itMesh = pShadowView->ShadowMeshInstanceDataLookup.begin(); itMesh != pShadowView->ShadowMeshInstanceDataLookup.end(); ++itMesh)
		{
			itMesh->second.NumValidData = 0;
		}
	}

	// record instance data
	{
		SCOPED_CPU_MARKER("RecordInstanceData");
		for (const size_t& BBIndex : *pCulledBoundingBoxIndexList_Msh)
		{
			assert(BBIndex < mBoundingBoxHierarchy.mMeshBoundingBoxMeshIDMapping.size());
			MeshID meshID = mBoundingBoxHierarchy.mMeshBoundingBoxMeshIDMapping[BBIndex];

			
			const GameObject* pGameObject = mBoundingBoxHierarchy.mMeshBoundingBoxGameObjectPointerMapping[BBIndex];
			
			Transform const TF = mBoundingBoxHierarchy.mMeshTransforms[BBIndex];
			XMMATRIX matWorld = TF.matWorldTransformation();

			FSceneShadowView::FShadowView::FShadowMeshInstanceData& d = pShadowView->ShadowMeshInstanceDataLookup[meshID];

			// allocate some memory if we're just creating this instance data entry.
			// a minimum of BATCH_SIZE is a good initial size for the vector
			if (d.InstanceData.empty() || d.InstanceData.size() == d.NumValidData)
			{
				SCOPED_CPU_MARKER("MemAlloc");
				d.InstanceData.resize(d.InstanceData.empty() ? MAX_INSTANCE_COUNT__SHADOW_MESHES : d.InstanceData.size() * 2);
			}

			d.InstanceData[d.NumValidData++] = { matWorld, matWorld * matViewProj };
		}
	}

	int NumInstancedRenderCommands = 0;
	{
		SCOPED_CPU_MARKER("CountBatches");
		for (auto itMesh = pShadowView->ShadowMeshInstanceDataLookup.begin(); itMesh != pShadowView->ShadowMeshInstanceDataLookup.end(); ++itMesh)
		{
			const FSceneShadowView::FShadowView::FShadowMeshInstanceData& instData = itMesh->second;
			size_t NumInstancesToProces = instData.NumValidData;
			int iInst = 0;
			while (NumInstancesToProces > 0)
			{
				const int ThisBatchSize = std::min(MAX_INSTANCE_COUNT__SHADOW_MESHES, NumInstancesToProces);
				int iBatch = 0;
				while (iBatch < MAX_INSTANCE_COUNT__SHADOW_MESHES && iInst < instData.NumValidData)
				{
					++iBatch;
					++iInst;
				}
				NumInstancesToProces -= iBatch;
				++NumInstancedRenderCommands;
			}

		}
	}

	// resize the render command list
	{
		SCOPED_CPU_MARKER("Resize");
		pShadowView->meshRenderCommands.resize(NumInstancedRenderCommands);
	}

	// chunk-up instance data per-mesh and record commands
	{
		SCOPED_CPU_MARKER("ChunkUpData");

		NumInstancedRenderCommands = 0;

		// for-each mesh
		for (auto it = pShadowView->ShadowMeshInstanceDataLookup.begin(); it != pShadowView->ShadowMeshInstanceDataLookup.end(); ++it)
		{
			SCOPED_CPU_MARKER("Mesh");
			const MeshID meshID = it->first;
			const FSceneShadowView::FShadowView::FShadowMeshInstanceData& instData = it->second;


			int NumInstancesToProces = (int)instData.NumValidData;
			int iInst = 0;
			while (NumInstancesToProces > 0)
			{
				const int ThisBatchSize = std::min(MAX_INSTANCE_COUNT__SCENE_MESHES, NumInstancesToProces);
				FInstancedShadowMeshRenderCommand& cmd = pShadowView->meshRenderCommands[NumInstancedRenderCommands];
				cmd.meshID = meshID;
				cmd.matWorldViewProj.resize(ThisBatchSize);
				cmd.matWorldViewProjTransformations.resize(ThisBatchSize);

				int iBatch = 0;
				while (iBatch < MAX_INSTANCE_COUNT__SHADOW_MESHES && iInst < instData.NumValidData)
				{
					cmd.matWorldViewProj.push_back(instData.InstanceData[iInst].matWorld);
					cmd.matWorldViewProjTransformations.push_back(instData.InstanceData[iInst].matWorldViewProj);

					++iBatch;
					++iInst;
				}

				NumInstancesToProces -= iBatch;
				++NumInstancedRenderCommands;
			}
		}
	}
}


struct FFrustumRenderCommandRecorderContext
{
	size_t iFrustum;
	const std::vector<size_t>* pObjIndices = nullptr;
	FSceneShadowView::FShadowView* pShadowView = nullptr;
};

void Scene::BatchInstanceData(FSceneView& SceneView, ThreadPool& UpdateWorkerThreadPool)
{
	SCOPED_CPU_MARKER("BatchInstanceData");
	FFrustumCullWorkerContext& ctx = mFrustumCullWorkerContext;
	const int NumShadowMeshFrustums = ctx.NumValidInputElements - 1; // exclude main view
	const int NumPoolWorkers = UpdateWorkerThreadPool.GetThreadPoolSize();
	const int NumFrustumsThisThread = DIV_AND_ROUND_UP(NumShadowMeshFrustums, NumPoolWorkers) / 2;

	// TODO: move outside of this function scope to save alloc-realloc time
	std::vector< FFrustumRenderCommandRecorderContext> WorkerContexts(NumShadowMeshFrustums); 

	{
		SCOPED_CPU_MARKER("DispatchWorker_MainView");
		UpdateWorkerThreadPool.AddTask([&]()
		{
			SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
			BatchInstanceData_SceneMeshes(&SceneView.meshRenderCommands
				, SceneView.MaterialMeshInstanceDataLookup
				, mFrustumCullWorkerContext.vCulledBoundingBoxIndexListPerView[0]
				, SceneView.viewProj
				, SceneView.viewProjPrev
			);
		});
	}
	{
		SCOPED_CPU_MARKER("PrepareShadowViewWorkerContexts");
		for (size_t iFrustum = 1; iFrustum <= NumShadowMeshFrustums; ++iFrustum) // iFrustum==0 is for mainView, start from 1
		{
			FSceneShadowView::FShadowView* pShadowView = mFrustumIndex_pShadowViewLookup.at(iFrustum);
			const std::vector<size_t>& CulledBoundingBoxIndexList_Msh = mFrustumCullWorkerContext.vCulledBoundingBoxIndexListPerView[iFrustum];
			WorkerContexts[iFrustum-1] = { iFrustum, &CulledBoundingBoxIndexList_Msh, pShadowView };
		}
	}
	{
		SCOPED_CPU_MARKER("DispatchWorkers_ShadowViews");
		for (size_t iFrustum = 1+NumFrustumsThisThread; iFrustum <= NumShadowMeshFrustums; ++iFrustum)
		{
			UpdateWorkerThreadPool.AddTask([=]()
			{
				SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
				FFrustumRenderCommandRecorderContext ctx = WorkerContexts[iFrustum-1];
				BatchInstanceData_ShadowMeshes(ctx.iFrustum, ctx.pShadowView, ctx.pObjIndices, ctx.pShadowView->matViewProj);
			});
		}
	}
	
	BatchInstanceData_BoundingBox(SceneView, UpdateWorkerThreadPool, SceneView.viewProj);
	PrepareLightMeshRenderParams(SceneView);
	
	{
		SCOPED_CPU_MARKER("ThisThread_ShadowViews");
		for (size_t iFrustum = 1; iFrustum <= NumFrustumsThisThread; ++iFrustum)
		{
			FFrustumRenderCommandRecorderContext& ctx = WorkerContexts[iFrustum-1];
			BatchInstanceData_ShadowMeshes(ctx.iFrustum, ctx.pShadowView, ctx.pObjIndices, ctx.pShadowView->matViewProj);
		}
	}
	{
		SCOPED_CPU_MARKER_C("BUSY_WAIT_WORKER", 0xFFFF0000);
		while (UpdateWorkerThreadPool.GetNumActiveTasks() != 0);
	}
	
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

//-------------------------------------------------------------------------------
//
// MATERIAL REPRESENTATION
//
//-------------------------------------------------------------------------------
FMaterialRepresentation::FMaterialRepresentation()
	: DiffuseColor(MATERIAL_UNINITIALIZED_VALUE, MATERIAL_UNINITIALIZED_VALUE, MATERIAL_UNINITIALIZED_VALUE)
	, EmissiveColor(MATERIAL_UNINITIALIZED_VALUE, MATERIAL_UNINITIALIZED_VALUE, MATERIAL_UNINITIALIZED_VALUE)
{}

