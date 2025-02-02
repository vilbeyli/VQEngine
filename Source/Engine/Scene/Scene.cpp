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

#include "Scene.h"
#include "../GPUMarker.h"

#include "../Core/Window.h"
#include "../VQEngine.h"
#include "../Culling.h"
#include "../RenderPass/ObjectIDPass.h"

#include "Libs/VQUtils/Source/utils.h"

#include <fstream>
#include <bitset>

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
	int iMat = -1;
	for (auto it = mMaterialNames.begin(); it != mMaterialNames.end(); ++it)
	{
		if (it->second == UniqueMaterialName)
		{
			iMat = it->first;
			break;
		}
	}

	if( iMat != -1)
	{
#if LOG_CACHED_RESOURCES_ON_LOAD
		Log::Info("Material already loaded: %s", UniqueMaterialName.c_str());
#endif
		return iMat;
	}

	static MaterialID LAST_USED_MATERIAL_ID = 1;
	MaterialID id = INVALID_ID;
	// critical section
	{
		std::unique_lock<std::mutex> lk(mMtx_Materials);
		id = LAST_USED_MATERIAL_ID++;
		mMaterials[id] = Material();
		mLoadedMaterials.emplace(id);
		mMaterialNames[id] = UniqueMaterialName;
		if (UniqueMaterialName == "")
		{
#if LOG_RESOURCE_CREATE
			Log::Info("Scene::CreateMaterial() ID=%d - <empty string>", id, UniqueMaterialName.c_str());
#endif
		}
	}
#if LOG_RESOURCE_CREATE
	Log::Info("Scene::CreateMaterial() ID=%d - %s", id, UniqueMaterialName.c_str());
#endif

	Material& mat = mMaterials.at(id);
	if (mat.SRVMaterialMaps == INVALID_ID)
	{
		mat.SRVMaterialMaps = mRenderer.AllocateSRV(NUM_MATERIAL_TEXTURE_MAP_BINDINGS-1);
		mRenderer.InitializeSRV(mat.SRVMaterialMaps, 0, INVALID_ID);
		mRenderer.InitializeSRV(mat.SRVMaterialMaps, 1, INVALID_ID);
		mRenderer.InitializeSRV(mat.SRVMaterialMaps, 2, INVALID_ID);
		mRenderer.InitializeSRV(mat.SRVMaterialMaps, 3, INVALID_ID);
		mRenderer.InitializeSRV(mat.SRVMaterialMaps, 4, INVALID_ID);
		mRenderer.InitializeSRV(mat.SRVMaterialMaps, 5, INVALID_ID);
		mRenderer.InitializeSRV(mat.SRVMaterialMaps, 6, INVALID_ID);
		mRenderer.InitializeSRV(mat.SRVMaterialMaps, 7, INVALID_ID);
		mat.SRVHeightMap = mRenderer.AllocateSRV(1);
		mRenderer.InitializeSRV(mat.SRVHeightMap, 0, INVALID_ID);
	}
	return id;
}

const std::string& Scene::GetMaterialName(MaterialID ID) const
{
	auto it = mMaterialNames.find(ID);
	if (it != mMaterialNames.end())
	{
		return it->second;
	}
	return mInvalidMaterialName;
}

const std::string& Scene::GetTexturePath(TextureID ID) const
{
	auto it = mTexturePaths.find(ID);
	if (it != mTexturePaths.end())
	{
		return it->second;
	}
	return mInvalidTexturePath;
}

std::string Scene::GetTextureName(TextureID ID) const
{
	const std::string& path = GetTexturePath(ID);
	const std::string fileNameAndExtension = StrUtil::split(path, '/').back();
	return StrUtil::split(fileNameAndExtension, '.').front();
}

std::vector<const Light*> Scene::GetLightsOfType(Light::EType eType) const
{
	std::vector<const Light*> lights;
	for (const Light& l : mLightsStatic    ) if(l.Type == eType) lights.push_back(&l);
	for (const Light& l : mLightsStationary) if(l.Type == eType) lights.push_back(&l);
	for (const Light& l : mLightsDynamic   ) if(l.Type == eType) lights.push_back(&l);
	return lights;
}

std::vector<const Light*> Scene::GetLights() const
{
	std::vector<const Light*> lights;
	for (const Light& l : mLightsStatic    ) lights.push_back(&l);
	for (const Light& l : mLightsStationary) lights.push_back(&l);
	for (const Light& l : mLightsDynamic   ) lights.push_back(&l);
	return lights;
}

std::vector<Light*> Scene::GetLights()
{
	std::vector<Light*> lights;
	for (Light& l : mLightsStatic    ) lights.push_back(&l);
	for (Light& l : mLightsStationary) lights.push_back(&l);
	for (Light& l : mLightsDynamic   ) lights.push_back(&l);
	return lights;
}

std::vector<MaterialID> Scene::GetMaterialIDs() const
{
	std::vector<MaterialID> ids(mMaterials.size());
	size_t i = 0;
	for (const auto& kvp : mMaterials)
	{
		ids[i++] = kvp.first;
	}
	return ids;
}

const Material& Scene::GetMaterial(MaterialID ID) const
{
	if (mMaterials.find(ID) == mMaterials.end())
	{
		Log::Error("Material not created. Did you call Scene::CreateMaterial()? (matID=%d)", ID);
		return mMaterials.at(mDefaultMaterialID);
	}
	return mMaterials.at(ID);
}

Material& Scene::GetMaterial(MaterialID ID)
{
	if (mMaterials.find(ID) == mMaterials.end())
	{
		Log::Error("Material not created. Did you call Scene::CreateMaterial()? (matID=%d)", ID);
		return mMaterials.at(mDefaultMaterialID);
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

const Model& Scene::GetModel(ModelID id) const
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
	const FSceneShadowViews& shadowView = mFrameShadowViews[FRAME_DATA_INDEX];

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
	auto fnCountShadowMeshRenderCommands = [](const FSceneShadowViews& shadowView) -> uint
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
	stats.NumObjects   = static_cast<uint>(this->mGameObjectHandles.size());
	stats.NumCameras   = static_cast<uint>(this->mCameras.size());

	return stats;
}

GameObject* Scene::GetGameObject(size_t hObject) const { return mGameObjectPool.Get(hObject); }
Transform* Scene::GetGameObjectTransform(size_t hObject) const { return mGameObjectTransformPool.Get(hObject); }

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


static void RecordRenderLightBoundsCommand(const Light& l,
	std::vector<FLightRenderCommand>& cmds,
	const XMVECTOR& CameraPosition
)
{
	const XMVECTOR lightPosition = XMLoadFloat3(&l.Position);
	const XMVECTOR lightToCamera = CameraPosition - lightPosition;
	const XMVECTOR dot = XMVector3Dot(lightToCamera, lightToCamera);

	const float distanceSq = dot.m128_f32[0];
	const float attenuation = 1.0f / distanceSq;
	const float attenuatedBrightness = l.Brightness * attenuation;

	FLightRenderCommand cmd;
	cmd.color = XMFLOAT4(
		l.Color.x //* attenuatedBrightness
		, l.Color.y //* attenuatedBrightness
		, l.Color.z //* attenuatedBrightness
		, 0.45f
	);

	switch (l.Type)
	{
	case Light::EType::DIRECTIONAL:
		break; // don't draw directional light mesh

	case Light::EType::SPOT:
	{
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
		cmd.meshID = EBuiltInMeshes::CONE;
		cmd.matWorldTransformation = scaleConeToRange * alignConeToSpotLightTransformation * tf.matWorldTransformation();
		cmds.push_back(cmd);
	}	break;

	case Light::EType::POINT:
	{
		Transform tf = l.GetTransform();
		tf._scale = XMFLOAT3(l.Range, l.Range, l.Range);
		
		cmd.meshID = EBuiltInMeshes::SPHERE;
		cmd.matWorldTransformation = tf.matWorldTransformation();
		cmds.push_back(cmd);
	}  break;
	} // swicth
}

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
	, mFrustumCullWorkerContext(mBoundingBoxHierarchy, mMeshes, mMaterials)
	, mIndex_SelectedCamera(0)
	, mIndex_ActiveEnvironmentMapPreset(-1)
	, mGameObjectPool(NUM_GAMEOBJECT_POOL_SIZE, GAMEOBJECT_BYTE_ALIGNMENT)
	, mGameObjectTransformPool(NUM_GAMEOBJECT_POOL_SIZE, GAMEOBJECT_BYTE_ALIGNMENT)
	, mResourceNames(engine.GetResourceNames())
	, mAssetLoader(engine.GetAssetLoader())
	, mRenderer(renderer)
	, mMaterialAssignments(engine.GetAssetLoader().GetThreadPool_TextureLoad())
	, mBoundingBoxHierarchy(mMeshes, mModels, mMaterials, mTransformHandles)
	, mInvalidMaterialName("INVALID MATERIAL")
	, mInvalidTexturePath("INVALID PATH")
{}

void Scene::PreUpdate(int FRAME_DATA_INDEX, int FRAME_DATA_PREV_INDEX)
{
	SCOPED_CPU_MARKER("Scene::PreUpdate()");
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


	{
		SCOPED_CPU_MARKER("UpdateTransforms");
		for (size_t Handle : mTransformHandles)
		{
			Transform* pTF = mGameObjectTransformPool.Get(Handle);
			pTF->_positionPrev = pTF->_position;
		}
	}
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

static void RecordOutlineRenderCommands(
	std::vector<FOutlineRenderCommand>& cmds,
	const std::vector<size_t>& objHandles,
	const Scene* pScene,
	const XMMATRIX& matView,
	const XMMATRIX& matProj,
	const XMFLOAT4& SelectionColor
)
{
	cmds.clear();
	for (size_t hObj : objHandles)
	{
		const GameObject* pObj = pScene->GetGameObject(hObj);
		if (!pObj)
		{
			//Log::Warning("pObj NULL in RecordOutlineRenderCommands");
			continue;
		}

		const Transform& tf = *pScene->GetGameObjectTransform(hObj);
		const XMMATRIX matWorld = tf.matWorldTransformation();
		const XMMATRIX matNormal = tf.NormalMatrix(matWorld);
		XMVECTOR det = XMMatrixDeterminant(matView);
		const XMMATRIX matViewInverse = XMMatrixInverse(&det, matView);;
		const Model& model = pScene->GetModel(pObj->mModelID);
		for (const auto& pair : model.mData.GetMeshMaterialIDPairs(Model::Data::EMeshType::OPAQUE_MESH))
		{
			MeshID meshID = pair.first;
			MaterialID matID = pair.second;
			const Material& mat = pScene->GetMaterial(matID);
			
			FOutlineRenderCommand cmd = {};
			cmd.meshID = meshID;
			cmd.matID = matID;
			cmd.cb.matWorld = matWorld;
			cmd.cb.matWorldView = matWorld * matView;
			cmd.cb.matNormalView = matNormal * matView;
			cmd.cb.matProj = matProj;
			cmd.cb.matViewInverse = matViewInverse;
			cmd.cb.color = SelectionColor;
			cmd.cb.uvScaleBias = float4(mat.tiling.x, mat.tiling.y, mat.uv_bias.x, mat.uv_bias.y);
			cmd.cb.scale = 1.0f;
			cmd.cb.heightDisplacement = mat.displacement;
			cmds.push_back(cmd);
		}
	}
}

static void ExtractSceneView(FSceneView& SceneView, std::unordered_map<const Camera*, DirectX::XMMATRIX>& mViewProjectionMatrixHistory, const Camera& cam)
{
	SCOPED_CPU_MARKER("ExtractSceneView");

	const XMMATRIX MatView = cam.GetViewMatrix();
	const XMMATRIX MatProj = cam.GetProjectionMatrix();
	const XMFLOAT3 camPos = cam.GetPositionF();
	const XMMATRIX MatViewProj = MatView * MatProj;

	const XMMATRIX MatViewProjPrev = mViewProjectionMatrixHistory.find(&cam) != mViewProjectionMatrixHistory.end()
		? mViewProjectionMatrixHistory.at(&cam)
		: XMMatrixIdentity();
	mViewProjectionMatrixHistory[&cam] = MatViewProj;

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

}

static void CollectDebugVertexDrawParams(FSceneView& SceneView,
	const std::vector<size_t>& mSelectedObjects,
	const Scene* pScene,
	const ModelLookup_t& mModels,
	const std::unordered_map<MeshID, Mesh>& mMeshes,
	const Camera& cam
)
{
	SCOPED_CPU_MARKER("CollectDebugVertexDrawParams");
	if (!SceneView.sceneParameters.bDrawVertexLocalAxes)
	{
		return;
	}

	// count num meshes
	size_t NumMeshes = 0;
	for (size_t hObj : mSelectedObjects)
	{
		const GameObject* pObj = pScene->GetGameObject(hObj);
		if (!pObj)
			continue;

		const Model& m = mModels.at(pObj->mModelID);
		NumMeshes += m.mData.GetNumMeshesOfAllTypes();
	}
	SceneView.debugVertexAxesRenderCommands.resize(NumMeshes);
	
	int i = 0; // no instancing, draw meshes one by one for now
	for (size_t hObj : mSelectedObjects)
	{
		const GameObject* pObj = pScene->GetGameObject(hObj);
		if (!pObj)
			continue;
		const Transform* pTf = pScene->GetGameObjectTransform(hObj);
		assert(pTf);

		const Model& m = mModels.at(pObj->mModelID);
		for (const auto& pair : m.mData.GetMeshMaterialIDPairs(Model::Data::EMeshType::OPAQUE_MESH))
		{
			MeshID meshID = pair.first;
			const Mesh& mesh = mMeshes.at(meshID);

			MeshRenderCommand_t& cmd = SceneView.debugVertexAxesRenderCommands[i];
			cmd.matWorld.resize(1);
			cmd.matNormal.resize(1);

			const int lod = 0; // GetLODFromProjectedScreenArea(2.0f, mesh.GetNumLODs());
			cmd.numIndices = mesh.GetNumIndices(lod);
			cmd.vertexIndexBuffer = mesh.GetIABufferIDs(lod);
			cmd.matWorld[0] = pTf->matWorldTransformation();
			cmd.matNormal[0] = pTf->NormalMatrix(cmd.matWorld.back());
			++i;
		}
	}	
}

static void ResetInstanceCounts(size_t NumFrustums, FSceneView& SceneView, std::unordered_map<size_t, FSceneShadowViews::FShadowView*>& mFrustumIndex_pShadowViewLookup)
{
	SCOPED_CPU_MARKER_C("ResetInstanceCounts", 0xFF0000FF);
	for (auto& it : ((FSceneView&)SceneView).drawParamLookup)
	{
		FSceneView::FMeshInstanceDataArray& LODArrays = it.second;
		LODArrays.NumValidData = 0;
	}

	for (int iFrustum = 1; iFrustum < NumFrustums; ++iFrustum)
	{
		FSceneShadowViews::FShadowView* pShadowView = mFrustumIndex_pShadowViewLookup.at(iFrustum);
		for (auto& it : ((FSceneShadowViews::FShadowView&)*pShadowView).drawParamLookup)
		{
			FSceneShadowViews::FShadowView::FInstanceDataArray& LODArrays = it.second;
			LODArrays.NumValidData = 0;
		}
	}
}

void Scene::PostUpdate(ThreadPool& UpdateWorkerThreadPool, const FUIState& UIState, int FRAME_DATA_INDEX)
{
	SCOPED_CPU_MARKER("Scene::PostUpdate()");
	assert(FRAME_DATA_INDEX < mFrameSceneViews.size());
	FSceneView& SceneView = mFrameSceneViews[FRAME_DATA_INDEX];
	FSceneShadowViews& ShadowView = mFrameShadowViews[FRAME_DATA_INDEX];

	const Camera& cam = mCameras[mIndex_SelectedCamera];
	
	ExtractSceneView(SceneView, mViewProjectionMatrixHistory, cam);

	// reset shadow view
	ShadowView.NumPointShadowViews = 0;
	ShadowView.NumSpotShadowViews = 0;

	// distance-cull and get active shadowing lights from various light containers
	{
		SCOPED_CPU_MARKER("CullLights");
		const FFrustumPlaneset ViewFrustumPlanes = FFrustumPlaneset::ExtractFromMatrix(cam.GetViewProjectionMatrix());
		mActiveLightIndices_Static     = GetActiveAndCulledLightIndices(mLightsStatic, ViewFrustumPlanes);
		mActiveLightIndices_Stationary = GetActiveAndCulledLightIndices(mLightsStationary, ViewFrustumPlanes);
		mActiveLightIndices_Dynamic    = GetActiveAndCulledLightIndices(mLightsDynamic, ViewFrustumPlanes);
	}

	mBoundingBoxHierarchy.Build(this, mGameObjectHandles, UpdateWorkerThreadPool);

	GatherSceneLightData(SceneView);
	GatherShadowViewData(ShadowView, mLightsStatic, mActiveLightIndices_Static);
	GatherShadowViewData(ShadowView, mLightsStationary, mActiveLightIndices_Stationary);
	GatherShadowViewData(ShadowView, mLightsDynamic, mActiveLightIndices_Dynamic);

	GatherFrustumCullParameters(SceneView, ShadowView, UpdateWorkerThreadPool);

	CullFrustums(SceneView, UpdateWorkerThreadPool);

	BatchInstanceData(SceneView, UpdateWorkerThreadPool);
	
	RecordRenderLightMeshCommands(SceneView);
	RecordOutlineRenderCommands(SceneView.outlineRenderCommands, mSelectedObjects, this, SceneView.view, SceneView.proj, SceneView.sceneParameters.OutlineColor);

	if (UIState.bDrawLightVolume)
	{
		const auto lights = this->GetLights(); // todo: this is unnecessary copying, don't do this
		const int i = UIState.SelectedEditeeIndex[FUIState::EEditorMode::LIGHTS];
		if (i >= 0 && i < lights.size())
		{
			const Light& l = *lights[i];
			if (l.bEnabled)
			{
				RecordRenderLightBoundsCommand(l, SceneView.lightBoundsRenderCommands, SceneView.cameraPosition);
			}
		}
	}

	CollectDebugVertexDrawParams(SceneView, mSelectedObjects, this, mModels, mMeshes, cam);
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
const FSceneShadowViews& Scene::GetShadowView(int FRAME_DATA_INDEX) const 
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

void Scene::PickObject(const ObjectIDPass& ObjectIDRenderPass, int MouseClickPositionX, int MouseClickPositionY)
{
	int4 px = ObjectIDRenderPass.ReadBackPixel(MouseClickPositionX, MouseClickPositionY);
	Log::Info("Picked(%d, %d): Obj[%d] Mesh[%d] Material[%d] ProjArea[%d]", MouseClickPositionX, MouseClickPositionY, px.x, px.y, px.z, px.w);
	
	// if the mouse clicked on another monitor and main window lost focus, 
	// we will potentially get negative coordinates when the mouse is clicked
	// again on the main window. px will have negative values in that case.
	// we discard this case.
	if (px.x < 0)
	{
		return;
	}

	// objectID starts from 0, shader writes with an offset of 1.
	// we remove the offset here so that we can clear RT to 0 and 
	// not select object when clicked on area with no rendered pixels (no obj id).
	const int hObj = px.x - 1;

	// handle click on background (no object)
	if (hObj == -1)
	{
		this->mSelectedObjects.clear();
		return;
	}

	assert(px.x >= 1);

	auto it = std::find(this->mSelectedObjects.begin(), this->mSelectedObjects.end(), hObj);
	const bool bClickOnAlreadySelected = it != this->mSelectedObjects.end();
	const bool bIsShiftDown = mInput.IsKeyDown("Shift");
	
	if (bClickOnAlreadySelected)
	{
		if (bIsShiftDown) // de-select
		{
			this->mSelectedObjects.erase(it);
		}
	}
	else // clicked on new object
	{
		if(!bIsShiftDown) // multi-select w/ shift, single select otherwise
		{
			this->mSelectedObjects.clear();
		}
		this->mSelectedObjects.push_back(hObj);
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
			if (!l.bEnabled && l.Type != Light::EType::DIRECTIONAL) continue;
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

void Scene::GatherShadowViewData(FSceneShadowViews& SceneShadowView, const std::vector<Light>& vLights, const std::vector<size_t>& vActiveLightIndices)
{
	SCOPED_CPU_MARKER("GatherShadowViewData");
	for (const size_t& LightIndex : vActiveLightIndices)
	{
		const Light& l = vLights[LightIndex];

		switch (l.Type)
		{
		case Light::EType::DIRECTIONAL:
		{
			FSceneShadowViews::FShadowView& ShadowView = SceneShadowView.ShadowView_Directional;
			ShadowView.matViewProj = l.GetViewProjectionMatrix();
		}	break;
		case Light::EType::SPOT:
		{
			XMMATRIX matViewProj = l.GetViewProjectionMatrix();
			FSceneShadowViews::FShadowView& ShadowView = SceneShadowView.ShadowViews_Spot[SceneShadowView.NumSpotShadowViews++];
			ShadowView.matViewProj = matViewProj;
		} break;
		case Light::EType::POINT:
		{
			for (int face = 0; face < 6; ++face)
			{
				XMMATRIX matViewProj = l.GetViewProjectionMatrix(static_cast<Texture::CubemapUtility::ECubeMapLookDirections>(face));
				FSceneShadowViews::FShadowView& ShadowView = SceneShadowView.ShadowViews_Point[SceneShadowView.NumPointShadowViews * 6 + face];
				ShadowView.matViewProj = matViewProj;
			}
			SceneShadowView.PointLightLinearDepthParams[SceneShadowView.NumPointShadowViews].fFarPlane = l.Range;
			SceneShadowView.PointLightLinearDepthParams[SceneShadowView.NumPointShadowViews].vWorldPos = l.Position;
			++SceneShadowView.NumPointShadowViews;
		} break;
		}
	}
}


static std::string fnMark(const std::string& marker, size_t iB, size_t iE)
{
	const size_t num = iE - iB;
	std::stringstream ss;
	ss << marker << "[" << iB << ", " << iE << "]: " << num;
	return ss.str();
};

void Scene::GatherFrustumCullParameters(const FSceneView& SceneView, FSceneShadowViews& SceneShadowView, ThreadPool& UpdateWorkerThreadPool)
{
	SCOPED_CPU_MARKER("GatherFrustumCullParameters");
	const SceneBoundingBoxHierarchy& BVH = mBoundingBoxHierarchy;
	const size_t NumWorkerThreadsAvailable = UpdateWorkerThreadPool.GetThreadPoolSize();

	const std::vector<const Light*> dirLights = GetLightsOfType(Light::EType::DIRECTIONAL);
	const bool bCullDirectionalLightView = !dirLights.empty() && dirLights[0]->bEnabled && dirLights[0]->bCastingShadows;

	std::vector< FFrustumPlaneset> FrustumPlanesets(1 + SceneShadowView.NumSpotShadowViews + SceneShadowView.NumPointShadowViews * 6 + (bCullDirectionalLightView ? 1 : 0));
	std::vector<XMMATRIX> FrustumViewProjMatrix(FrustumPlanesets.size());
	size_t iFrustum = 0;
	{
		SCOPED_CPU_MARKER("CollectFrustumPlanesets");
		FrustumViewProjMatrix[iFrustum] = SceneView.viewProj;
		FrustumPlanesets[iFrustum++] = FFrustumPlaneset::ExtractFromMatrix(SceneView.viewProj);
		
		// directional
		if (bCullDirectionalLightView)
		{
			mFrustumIndex_pShadowViewLookup[iFrustum] = &SceneShadowView.ShadowView_Directional;
			FrustumViewProjMatrix[iFrustum] = SceneShadowView.ShadowView_Directional.matViewProj;
			FrustumPlanesets[iFrustum++] = FFrustumPlaneset::ExtractFromMatrix(SceneShadowView.ShadowView_Directional.matViewProj);
		}
		else
		{
			SceneShadowView.ShadowView_Directional.drawParamLookup.clear();
			SceneShadowView.ShadowView_Directional.mRenderCmdInstanceDataWriteIndex.clear();
			SceneShadowView.ShadowView_Directional.meshRenderCommands.clear();
		}

		// point
		for (size_t iPoint = 0; iPoint < SceneShadowView.NumPointShadowViews; ++iPoint)
		for (size_t face = 0; face < 6; ++face)
		{
			const size_t iPointFace = iPoint * 6 + face;
			mFrustumIndex_pShadowViewLookup[iFrustum] = &SceneShadowView.ShadowViews_Point[iPointFace];
			FrustumViewProjMatrix[iFrustum] = SceneShadowView.ShadowViews_Point[iPointFace].matViewProj;
			FrustumPlanesets[iFrustum++] = FFrustumPlaneset::ExtractFromMatrix(SceneShadowView.ShadowViews_Point[iPointFace].matViewProj);
		}

		// spot
		for (size_t iSpot = 0; iSpot < SceneShadowView.NumSpotShadowViews; ++iSpot)
		{
			mFrustumIndex_pShadowViewLookup[iFrustum] = &SceneShadowView.ShadowViews_Spot[iSpot];
			FrustumViewProjMatrix[iFrustum] = SceneShadowView.ShadowViews_Spot[iSpot].matViewProj;
			FrustumPlanesets[iFrustum++] = FFrustumPlaneset::ExtractFromMatrix(SceneShadowView.ShadowViews_Spot[iSpot].matViewProj);
		}
	}

	mFrustumCullWorkerContext.InvalidateContextData();
	mFrustumCullWorkerContext.AllocInputMemoryIfNecessary(FrustumPlanesets.size());
	mFrustumCullWorkerContext.NumValidInputElements = FrustumPlanesets.size();


	constexpr size_t MINIMUM_WORK_SIZE_PER_THREAD = 256;
	const size_t NumFrustums = FrustumPlanesets.size();

	size_t NumFrustums_Threaded = 0;
	for (size_t i = 0; i < NumFrustums; ++i)
	{
		if (BVH.mMeshBoundingBoxes.size() >= MINIMUM_WORK_SIZE_PER_THREAD)
		{
			++NumFrustums_Threaded;
		}
	}
	const size_t NumWorkerThreads = CalculateNumThreadsToUse(NumFrustums_Threaded, NumWorkerThreadsAvailable, 1);

	const size_t NumThreads = 1 + NumWorkerThreads;

#if UPDATE_THREAD__ENABLE_WORKERS
	
	{
		SCOPED_CPU_MARKER("InitFrustumCullWorkerContexts");
#if 0 // debug-single threaded
		for (size_t iKey = 0; iKey < FrustumPlanesets.size(); ++iKey)
			mFrustumCullWorkerContext.AddWorkerItem(FrustumPlanesets[iKey], BVH.mMeshBoundingBoxes, BVH.mGameObjectHandles, iKey);
#else
		const std::vector<std::pair<size_t, size_t>> vRanges = PartitionWorkItemsIntoRanges(NumFrustums, NumThreads);
		{
			const std::vector<MeshID>& MeshBB_MeshID = BVH.GetMeshesIDs();
			const std::vector<MaterialID>& MeshBB_MatID = BVH.GetMeshMaterialIDs();
			const std::vector<int>& MeshBB_NumMeshLODs = BVH.GetNumMeshesLODs();

			mFrustumCullWorkerContext.vBoundingBoxList = BVH.mMeshBoundingBoxes;
			const bool bForceLOD0_ShadowView = SceneView.sceneParameters.bForceLOD0_ShadowView;
			std::function<bool(const FFrustumCullWorkerContext::FCullResult&, const FFrustumCullWorkerContext::FCullResult&)> fnShadowViewSort = 
				[&MeshBB_MeshID, &MeshBB_MatID, &MeshBB_NumMeshLODs, bForceLOD0_ShadowView]
				(const FFrustumCullWorkerContext::FCullResult& l, const FFrustumCullWorkerContext::FCullResult& r)
			{
				const uint64 keyL = FSceneShadowViews::FShadowView::GetKey(MeshBB_MatID[l.iBB], MeshBB_MeshID[l.iBB], l.SelectedLOD, l.bTessellated);
				const uint64 keyR = FSceneShadowViews::FShadowView::GetKey(MeshBB_MatID[r.iBB], MeshBB_MeshID[r.iBB], r.SelectedLOD, r.bTessellated);
				return keyL > keyR;
			};
			size_t currRange = 0;
			{
				SCOPED_CPU_MARKER("DispatchWorkers");
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
					UpdateWorkerThreadPool.AddTask([Range, fnShadowViewSort, &FrustumPlanesets, &BVH, this, &FrustumViewProjMatrix, bForceLOD0_ShadowView]()
					{
						// we're doing shadow views
						
						SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
						{
							SCOPED_CPU_MARKER(fnMark("InitWorkerContexts", Range.first, Range.second).c_str());
							for (size_t i = Range.first; i <= Range.second; ++i)
								mFrustumCullWorkerContext.AddWorkerItem(FrustumPlanesets[i]
									, FrustumViewProjMatrix[i]
									, BVH.mMeshBoundingBoxes
									, BVH.mGameObjectHandles
									, BVH.mMeshMaterials
									, i
									, fnShadowViewSort
									, bForceLOD0_ShadowView
								);
						}
					});
				}
			}

			// main view
			SCOPED_CPU_MARKER(fnMark("InitWorkerContexts", vRanges[0].first, vRanges[0].second).c_str());

			const bool bForceLOD0 = SceneView.sceneParameters.bForceLOD0_SceneView;
			std::function<bool(const FFrustumCullWorkerContext::FCullResult&, const FFrustumCullWorkerContext::FCullResult&)> fnMainViewSort = 
				[&MeshBB_MeshID, &MeshBB_MatID, bForceLOD0]
				(const FFrustumCullWorkerContext::FCullResult& l, const FFrustumCullWorkerContext::FCullResult& r)
			{
				const uint64 keyL = FSceneView::GetKey(MeshBB_MatID[l.iBB], MeshBB_MeshID[l.iBB], l.SelectedLOD, l.bTessellated);
				const uint64 keyR = FSceneView::GetKey(MeshBB_MatID[r.iBB], MeshBB_MeshID[r.iBB], r.SelectedLOD, r.bTessellated);
				return keyL > keyR;
			};

			for (size_t i = vRanges[0].first; i <= vRanges[0].second; ++i)
			{
				mFrustumCullWorkerContext.AddWorkerItem(FrustumPlanesets[i]
					, FrustumViewProjMatrix[i]
					, BVH.mMeshBoundingBoxes
					, BVH.mGameObjectHandles
					, BVH.mMeshMaterials
					, i
					, (i == 0 ? fnMainViewSort : fnShadowViewSort)
					, bForceLOD0
				);
			}
		}
#endif
	}

	// ---------------------------------------------------SYNC ---------------------------------------------------
	if(NumThreads>1)
	{
		SCOPED_CPU_MARKER_C("BUSY_WAIT_WORKER", 0xFFFF0000);
		while (UpdateWorkerThreadPool.GetNumActiveTasks() != 0);
	}
	// --------------------------------------------------- SYNC ---------------------------------------------------

#else
	{
		SCOPED_CPU_MARKER("InitWorkerContext_MainView");
		mFrustumCullWorkerContext.AddWorkerItem(
			FrustumPlanesets[0]
			, BVH.mMeshBoundingBoxes
			, BVH.mGameObjectHandles
			, BVH.mMeshBoundingBoxMaterialIDMapping
			, 0
		);
	}
	{
		SCOPED_CPU_MARKER("InitWorkerContext_ShadowViews");
		for(size_t i=1; i<FrustumPlanesets.size(); ++i)
			mFrustumCullWorkerContext.AddWorkerItem(
				FrustumPlanesets[i]
				, BVH.mMeshBoundingBoxes
				, BVH.mGameObjectHandles
				, BVH.mMeshBoundingBoxMaterialIDMapping
				, i
			);
	}
#endif
}

void Scene::CullFrustums(const FSceneView& SceneView, ThreadPool& UpdateWorkerThreadPool)
{
	SCOPED_CPU_MARKER("CullFrustums");
	const size_t  NumThreadsAvailable = UpdateWorkerThreadPool.GetThreadPoolSize();

	constexpr size_t MIN_NUM_MESHES_FOR_THREADING_FRUSTUM = 128;

	const size_t& NumFrustums = mFrustumCullWorkerContext.NumValidInputElements;
	
	size_t NumAllBBs = 0;
	size_t NumBBs_Threaded = 0;
	size_t NumFrustums_Threaded = 0;
	for (size_t i=0; i< NumFrustums; ++i)
	{
		const size_t NumBBs = mFrustumCullWorkerContext.vBoundingBoxList.size();
		NumAllBBs += NumBBs;
		if (MIN_NUM_MESHES_FOR_THREADING_FRUSTUM <= NumBBs)
		{
			NumBBs_Threaded += NumBBs;
			NumFrustums_Threaded += 1;
		}
	}

	const size_t NumWorkerThreadsToUse = NumFrustums_Threaded > 0 ? NumThreadsAvailable : 0;
	const size_t  NumThreads = NumWorkerThreadsToUse + 1;
	const bool bThreadTheWork = NumWorkerThreadsToUse > 0;


	if (UPDATE_THREAD__ENABLE_WORKERS && bThreadTheWork)
	{
		mFrustumCullWorkerContext.ProcessWorkItems_MultiThreaded(NumThreads, UpdateWorkerThreadPool);
	}
	else
	{
		mFrustumCullWorkerContext.ProcessWorkItems_SingleThreaded();
	}
}



static void BatchBoundingBoxRenderCommandData(
	std::vector<FInstancedBoundingBoxRenderCommand>& cmds
	, const std::vector<FBoundingBox>& BBs
	, const XMMATRIX viewProj
	, const XMFLOAT4 Color
	, size_t iBegin
	, const std::pair<BufferID, BufferID>& VBIB
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
		cmd.vertexIndexBuffer = VBIB;
		cmd.numIndices = 36; // TODO: remove magic number
		cmd.color = Color;

		int iBatch = 0;
		while (iBatch < MAX_INSTANCE_COUNT__UNLIT_SHADER && iBB < BBs.size())
		{
			cmd.matWorldViewProj[iBatch] = BBs[iBB].GetWorldTransformMatrix() * viewProj;
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

	const float Transparency = 0.75f;
	const XMFLOAT4 BBColor_GameObj = XMFLOAT4(0.0f, 0.2f, 0.8f, Transparency);
	const XMFLOAT4 BBColor_Mesh = XMFLOAT4(0.0f, 0.8f, 0.2f, Transparency);
	
	const auto VBIB = this->mMeshes.at(EBuiltInMeshes::CUBE).GetIABufferIDs();
	auto fnBatch = [&UpdateWorkerThreadPool, &VBIB](
		std::vector<FInstancedBoundingBoxRenderCommand>& cmds
		, const std::vector<FBoundingBox>& BBs
		, size_t iBoundingBox
		, const XMFLOAT4 BBColor
		, const XMMATRIX matViewProj
		, const char* strMarker = ""
	)
	{
		SCOPED_CPU_MARKER(strMarker);

		constexpr size_t MIN_NUM_BOUNDING_BOX_FOR_THREADING = 128;
		if (BBs.size() < MIN_NUM_BOUNDING_BOX_FOR_THREADING || !UPDATE_THREAD__ENABLE_WORKERS)
		{
			BatchBoundingBoxRenderCommandData(cmds
				, BBs
				, matViewProj
				, BBColor
				, iBoundingBox
				, VBIB
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
					, VBIB
				);
			});
		}
	};

	const size_t NumGameObjectBBRenderCmds = (bDrawGameObjectBBs ? DIV_AND_ROUND_UP(mBoundingBoxHierarchy.mGameObjectBoundingBoxes.size(), MAX_INSTANCE_COUNT__UNLIT_SHADER) : 0);
	const size_t NumMeshBBRenderCmds       = (bDrawMeshBBs       ? DIV_AND_ROUND_UP(mBoundingBoxHierarchy.mMeshBoundingBoxes.size(), MAX_INSTANCE_COUNT__UNLIT_SHADER) : 0);

	{
		SCOPED_CPU_MARKER("AllocMem");
		SceneView.boundingBoxRenderCommands.resize(NumGameObjectBBRenderCmds + NumMeshBBRenderCmds);
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
		iBoundingBox = NumGameObjectBBRenderCmds;

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

using namespace InstanceBatching;
static void ResizeDrawInstanceArrays(FInstancedMeshRenderCommand& cmd, size_t sz, size_t iCmd)
{
	//Log::Info("ResizeDrawParamInstanceArrays(cmds[%d], %d);", sz, iCmd);
	cmd.matWorld.resize(sz);
	cmd.matWorldViewProj.resize(sz);
	cmd.matWorldViewProjPrev.resize(sz);
	cmd.matNormal.resize(sz);
	cmd.objectID.resize(sz);
	cmd.projectedArea.resize(sz);
}
static void ResizeDrawInstanceArrays(FInstancedShadowMeshRenderCommand& cmd, size_t sz, size_t iCmd)
{
	//Log::Info("ResizeDrawParamInstanceArrays(cmds[%d], %d);", sz, iCmd);
	cmd.matWorld.resize(sz);
	cmd.matWorldViewProj.resize(sz);
}



template<class TMeshInstanceDataArray>
static void CountInstanceData(
	  const SceneBoundingBoxHierarchy& BBH
	, std::unordered_map<uint64, TMeshInstanceDataArray>& drawParamLookup
	, const std::vector<FFrustumCullWorkerContext::FCullResult>& vViewCullResults
	, std::function<uint64(MaterialID, MeshID, int, bool)> FnGetKey
)
{
	SCOPED_CPU_MARKER("CountInstanceData");

	const std::vector<MeshID>& MeshBB_MeshID    = BBH.GetMeshesIDs();
	const std::vector<MaterialID>& MeshBB_MatID = BBH.GetMeshMaterialIDs();
	for (int i = 0; i < vViewCullResults.size(); ++i)
	{
		const size_t iBB = vViewCullResults[i].iBB;
		const MaterialID matID = MeshBB_MatID[iBB];
		const MeshID meshID = MeshBB_MeshID[iBB];
		TMeshInstanceDataArray& d = drawParamLookup[FnGetKey(matID, meshID, vViewCullResults[i].SelectedLOD, vViewCullResults[i].bTessellated)];
		d.NumValidData++;
	}
}

template<class TMeshInstanceDataArray, class TMeshRenderCommand>
static void CountNResizeRenderCmds(
	std::unordered_map<uint64, TMeshInstanceDataArray>& drawParamLookup,
	std::vector<TMeshRenderCommand>& MeshRenderCommands,
	int MAX_INSTANCE_COUNT
)
{
	SCOPED_CPU_MARKER("CountNResizeDrawParams");
	int NumInstancedRenderCommands = 0;
	{
		SCOPED_CPU_MARKER("Count");
		for (auto it = drawParamLookup.begin(); it != drawParamLookup.end(); ++it)
		{
			TMeshInstanceDataArray& MeshInstanceData = it->second;
			int NumInstancesToProces = (int)MeshInstanceData.NumValidData;
			int iInst = 0;
			while (NumInstancesToProces > 0)
			{
				const int ThisBatchSize = std::min(MAX_INSTANCE_COUNT, NumInstancesToProces);
				int iBatch = 0;
				while (iBatch < MAX_INSTANCE_COUNT && iInst < MeshInstanceData.NumValidData)
				{
					++iBatch;
					++iInst;
				}
				NumInstancesToProces -= iBatch;
				++NumInstancedRenderCommands;
			}
		}
	}
	{
		SCOPED_CPU_MARKER("Resize");
		MeshRenderCommands.resize(NumInstancedRenderCommands);
	}
}


template<class TMeshInstanceDataArray>
static void AllocInstanceData(std::unordered_map<uint64, TMeshInstanceDataArray>& drawParamLookup)
{
	SCOPED_CPU_MARKER("AllocInstData");
	std::vector< std::unordered_map<uint64, TMeshInstanceDataArray>::iterator> deleteList;
	for (auto it = drawParamLookup.begin(); it != drawParamLookup.end(); ++it)
	{
		TMeshInstanceDataArray& a = it->second;
		if (a.NumValidData == 0)
		{
			deleteList.push_back(it);
			continue;
		}
		a.Data.resize(a.NumValidData);
		//Log::Info("AllocInstanceData: %llu -> resize(%d)", it->first, a.NumValidData);
		a.NumValidData = 0; // we'll use NumValidData for indexing after this call.
	}

	for (auto it : deleteList)
	{
		drawParamLookup.erase(it);
	}
}

static void SetParamData(MeshRenderCommand_t& cmd,
	int iInst,
	const Transform& tf,
	const XMMATRIX& viewProj,
	const XMMATRIX& viewProjPrev,
	const FFrustumCullWorkerContext::FCullResult& cullResult,
	const MaterialID matID,
	size_t objID
)
{
	const XMMATRIX matWorld = tf.matWorldTransformation();
	const XMMATRIX matWorldHistory = tf.matWorldTransformationPrev();
	const XMMATRIX matNormal = tf.NormalMatrix(matWorld);
	const XMMATRIX matWVP = matWorld * viewProj;

	assert(iInst >= 0 && cmd.matWorld.size() > iInst);
	assert(cmd.matWorld.size() <= MAX_INSTANCE_COUNT__SCENE_MESHES);

	cmd.matWorld[iInst] = matWorld;
	cmd.matWorldViewProj[iInst] = matWVP;
	cmd.matWorldViewProjPrev[iInst] = matWorldHistory * viewProjPrev;
	cmd.matNormal[iInst] = matNormal;
	cmd.objectID[iInst] = (int)objID + 1; // 0 means empty. offset by 1 now, and undo it when reading back
	cmd.projectedArea[iInst] = cullResult.fBBArea;
	cmd.vertexIndexBuffer = cullResult.VBIB;
	cmd.numIndices = cullResult.NumIndices;
	cmd.matID = matID;

#if 0
	auto fnAllZero = [](const XMMATRIX& m)
		{
			for (int hObj = 0; hObj < 4; ++hObj)
				for (int j = 0; j < 4; ++j)
					if (m.r[hObj].m128_f32[j] != 0.0f)
						return false;
			return true;
		};
	if (fnAllZero(matWorld))
	{
		Log::Warning("All zero matrix");
	}
#endif
}
static void SetParamData(FInstancedShadowMeshRenderCommand& cmd,
	int iInst,
	const Transform& tf,
	const XMMATRIX& viewProj,
	const XMMATRIX& viewProjPrev,
	const FFrustumCullWorkerContext::FCullResult& cullResult,
	const MaterialID matID,
	size_t objID
)
{
	const XMMATRIX matWorld = tf.matWorldTransformation();
	const XMMATRIX matWVP = matWorld * viewProj;

	assert(iInst >= 0 && cmd.matWorldViewProj.size() > iInst);
	assert(iInst >= 0 && cmd.matWorld.size() > iInst);
	assert(cmd.matWorldViewProj.size() <= MAX_INSTANCE_COUNT__SHADOW_MESHES);
	assert(cmd.matWorld.size() <= MAX_INSTANCE_COUNT__SHADOW_MESHES);

	cmd.matWorld[iInst] = matWorld;
	cmd.matWorldViewProj[iInst] = matWVP;
	cmd.vertexIndexBuffer = cullResult.VBIB;
	cmd.numIndices = cullResult.NumIndices;
	cmd.matID = matID;
}

template<class TMeshRenderCmd>
static void CollectViewInstanceData(
	  const SceneBoundingBoxHierarchy& BBH
	, const std::vector<FFrustumCullWorkerContext::FCullResult>& vViewCullResults
	, size_t iBegin
	, size_t iEnd
	, const XMMATRIX& viewProj
	, const XMMATRIX& viewProjPrev
	, const std::vector<FInstanceDataWriteParam>& vOutputWriteParams // {iDraw, iInst}
	, std::vector<TMeshRenderCmd>& meshRenderCommands
)
{
	SCOPED_CPU_MARKER("CollectViewInstanceData");
	const std::vector<MeshID>&           MeshBB_MeshID         = BBH.GetMeshesIDs();
	const std::vector<MaterialID>&       MeshBB_MatID          = BBH.GetMeshMaterialIDs();
	const std::vector<const Transform*>& MeshBB_Transforms     = BBH.GetMeshTransforms();
	const std::vector<size_t>&           MeshBB_GameObjHandles = BBH.GetMeshGameObjectHandles();
	
	for (size_t i = iBegin; i <= iEnd; ++i)
	{
		const size_t iBB  = vViewCullResults[i].iBB;

		const MaterialID matID = MeshBB_MatID[iBB];
		const MeshID meshID    = MeshBB_MeshID[iBB];
		const size_t objID     = MeshBB_GameObjHandles[iBB];
		const Transform& tf    = *MeshBB_Transforms[iBB];

		const int iDraw = vOutputWriteParams[i].iDraw;
		const int iInst = vOutputWriteParams[i].iInst;

		assert(meshRenderCommands.size() > iDraw);
		TMeshRenderCmd& cmd = meshRenderCommands[iDraw];
		//Log::Info("meshRenderCommands[%d] NumInst=%d , iFirst=%d", iCmdParam, NumInstances, range.first);
		SetParamData(cmd
			, iInst
			, tf
			, viewProj
			, viewProjPrev
			, vViewCullResults[i]
			, matID
			, objID
		);
	}
}


template<class TMeshRenderCmd>
static void CountNResizeRenderCmdInstanceArrays(
	const SceneBoundingBoxHierarchy& BBH
	, const std::vector<FFrustumCullWorkerContext::FCullResult>& vCullResults
	, std::vector<TMeshRenderCmd>& meshRenderCommands
	, int MAX_INSTANCE_COUNT
	, std::function<uint64(MaterialID, MeshID, int, bool)> FnGetKey
)
{
	SCOPED_CPU_MARKER("CountNResizeRenderCmdInstanceArrays");
	const std::vector<MeshID>&           MeshBB_MeshID         = BBH.GetMeshesIDs();
	const std::vector<MaterialID>&       MeshBB_MatID          = BBH.GetMeshMaterialIDs();
	const std::vector<const Transform*>& MeshBB_Transforms     = BBH.GetMeshTransforms();
	const std::vector<size_t>&           MeshBB_GameObjHandles = BBH.GetMeshGameObjectHandles();

	int iDraw = -1;
	int iInst = -1;
	uint64 keyPrev = -1;
	for (int i = 0; i < vCullResults.size(); ++i)
	{
		const size_t iBB = vCullResults[i].iBB;
		const MaterialID matID = MeshBB_MatID[iBB];
		const MeshID meshID = MeshBB_MeshID[iBB];

		const uint64 key = FnGetKey(matID, meshID, vCullResults[i].SelectedLOD, vCullResults[i].bTessellated);
		if (keyPrev != key)
		{
			if (iDraw != -1)
			{
				ResizeDrawInstanceArrays(meshRenderCommands[iDraw], iInst, iDraw);
			}
			keyPrev = key;
			++iDraw;
			iInst = 0;
		}
		if (iInst == MAX_INSTANCE_COUNT)
		{
			ResizeDrawInstanceArrays(meshRenderCommands[iDraw], iInst, iDraw);
			++iDraw;
			iInst = 0;
		}
		++iInst;
	}
	if (iDraw != -1 && iInst != -1)
	{
		ResizeDrawInstanceArrays(meshRenderCommands[iDraw], iInst, iDraw);
	}
}

static void CalculateInstanceDataWriteIndices(
	  const SceneBoundingBoxHierarchy& BBH
	, const std::vector<FFrustumCullWorkerContext::FCullResult>& vViewCullResults
	, std::vector<FInstanceDataWriteParam>& vOutputWriteParams // {iDraw, iInst}
	, std::function<uint64(MaterialID, MeshID, int, bool)> FnGetKey
	, int MAX_INSTANCE_COUNT
)
{
	const std::vector<MeshID>&           MeshBB_MeshID         = BBH.GetMeshesIDs();
	const std::vector<MaterialID>&       MeshBB_MatID          = BBH.GetMeshMaterialIDs();
	{
		SCOPED_CPU_MARKER("AllocInstanceParams");
		vOutputWriteParams.resize(vViewCullResults.size());
	}
	{
		SCOPED_CPU_MARKER("WriteOutputIndices");
		int iDraw = -1;
		int iInst = -1;
		uint64 keyPrev = -1;
		for (size_t i = 0; i < vViewCullResults.size(); ++i)
		{
			const int lod = vViewCullResults[i].SelectedLOD;
			const size_t iBB = vViewCullResults[i].iBB;
			const MaterialID matID = MeshBB_MatID[iBB];
			const MeshID meshID = MeshBB_MeshID[iBB];

			const uint64 key = FnGetKey(matID, meshID, lod, vViewCullResults[i].bTessellated);
			if (keyPrev != key)
			{
				keyPrev = key;
				++iDraw;
				iInst = 0;
			}
			if (iInst == MAX_INSTANCE_COUNT)
			{
				++iDraw;
				iInst = 0;
			}

			// i,iBB --> key --> { iDraw, iInst }
			// Log::Info("[%-4d] %-6d --> %-13llu --> { %d , %d }", i, iBB, key, iDraw, iInst);

			vOutputWriteParams[i] = FInstanceDataWriteParam{ iDraw, iInst };
			++iInst;
		}
	}
}

template<class TMeshInstanceDataArray, class TMeshRenderCmd>
static void ReserveWorkerMemory(
	  const SceneBoundingBoxHierarchy& BBH
	, const std::vector<FFrustumCullWorkerContext::FCullResult>& vViewCullResults
	, std::vector<TMeshRenderCmd>& meshRenderCommands
	, std::unordered_map<uint64, TMeshInstanceDataArray>& drawParamLookup
	, std::function<uint64(MaterialID, MeshID, int, bool)> FnGetKey
	, std::vector<FInstanceDataWriteParam>& outWriteParams
	, int MAX_INSTANCE_COUNT
)
{
	SCOPED_CPU_MARKER("ReserveWorkerMemory");
	CountInstanceData(BBH, drawParamLookup, vViewCullResults, FnGetKey);
	CountNResizeRenderCmds(drawParamLookup, meshRenderCommands, MAX_INSTANCE_COUNT);
	CountNResizeRenderCmdInstanceArrays(BBH, vViewCullResults, meshRenderCommands, MAX_INSTANCE_COUNT, FnGetKey);
	AllocInstanceData(drawParamLookup);
	CalculateInstanceDataWriteIndices(BBH, vViewCullResults, outWriteParams, FnGetKey, MAX_INSTANCE_COUNT);
}


static void DispatchMainViewInstanceDataWorkers(
	  const SceneBoundingBoxHierarchy& BBH
	, FSceneView& SceneView
	, const std::vector<FFrustumCullWorkerContext::FCullResult>& vMainViewCullResults
	, const MeshLookup_t& mMeshes
	, ThreadPool& UpdateWorkerThreadPool
	, std::atomic<int>& MainViewThreadDone
	, std::vector<FInstanceDataWriteParam>& vOutputWriteParams // {iDraw, iInst}
)
{
	SCOPED_CPU_MARKER("DispatchWorker_MainView");
	const size_t NumWorkerThreads = UpdateWorkerThreadPool.GetThreadPoolSize();
	const size_t NumThreads = NumWorkerThreads + 1;
	const std::vector<std::pair<size_t, size_t>> vRanges = PartitionWorkItemsIntoRanges(vMainViewCullResults.size(), NumThreads);

	ReserveWorkerMemory(BBH,
		vMainViewCullResults,
		SceneView.meshRenderCommands,
		SceneView.drawParamLookup,
		FSceneView::GetKey,
		SceneView.mRenderCmdInstanceDataWriteIndex,
		MAX_INSTANCE_COUNT__SCENE_MESHES
	);

	for (int iR = 0; !vRanges.empty() && iR < vRanges.size(); ++iR) // offload all tasks to workers
	{
		UpdateWorkerThreadPool.AddTask([=, &SceneView, &vMainViewCullResults, &MainViewThreadDone, &BBH, &mMeshes]()
		{
			SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
			CollectViewInstanceData(BBH,
				vMainViewCullResults,
				vRanges[iR].first,
				vRanges[iR].second,
				SceneView.viewProj,
				SceneView.viewProjPrev,
				vOutputWriteParams,
				SceneView.meshRenderCommands
			);
			MainViewThreadDone++;
		});
	}
}


static void DispatchWorkers_ShadowViews(
	size_t NumShadowMeshFrustums
	, std::vector<FFrustumRenderCommandRecorderContext>& WorkerContexts
	, const bool bForceLOD0
	, ThreadPool& UpdateWorkerThreadPool
	, FFrustumCullWorkerContext& mFrustumCullWorkerContext
	, std::unordered_map<size_t, FSceneShadowViews::FShadowView*>& mFrustumIndex_pShadowViewLookup
	, const SceneBoundingBoxHierarchy& BBH
	, const MeshLookup_t& mMeshes
	, const MaterialLookup_t& mMaterials
)
{
	SCOPED_CPU_MARKER("DispatchWorkers_ShadowViews");
	constexpr size_t NUM_MIN_SHADOW_MESHES_FOR_THREADING = 1; // TODO: tweak this when thread work count is divided equally instead of per frustum

	size_t NumShadowFrustumsWithNumMeshesLargerThanMinNumMeshesPerThread = 0;
	size_t NumShadowMeshes = 0;
	size_t NumShadowMeshes_Threaded = 0;
	{
		SCOPED_CPU_MARKER("PrepareShadowViewWorkerContexts");
		WorkerContexts.resize(NumShadowMeshFrustums);
		for (size_t iFrustum = 1; iFrustum <= NumShadowMeshFrustums; ++iFrustum) // iFrustum==0 is for mainView, start from 1
		{
			FSceneShadowViews::FShadowView* pShadowView = mFrustumIndex_pShadowViewLookup.at(iFrustum);
			const std::vector<FFrustumCullWorkerContext::FCullResult>& ViewCullResults = mFrustumCullWorkerContext.vCullResultsPerView[iFrustum];
			WorkerContexts[iFrustum - 1] = { iFrustum, &ViewCullResults, pShadowView };

			const size_t NumMeshes = ViewCullResults.size();
			NumShadowMeshes += NumMeshes;
			NumShadowFrustumsWithNumMeshesLargerThanMinNumMeshesPerThread += NumMeshes >= NUM_MIN_SHADOW_MESHES_FOR_THREADING ? 1 : 0;
			NumShadowMeshes_Threaded += NumMeshes >= NUM_MIN_SHADOW_MESHES_FOR_THREADING ? NumMeshes : 0;
		}
	}
	const size_t NumShadowMeshesRemaining = NumShadowMeshes - NumShadowMeshes_Threaded;
	const size_t NumWorkersForFrustumsBelowThreadingThreshold = DIV_AND_ROUND_UP(NumShadowMeshesRemaining, NUM_MIN_SHADOW_MESHES_FOR_THREADING);
	const size_t NumShadowFrustumBatchWorkers = NumShadowFrustumsWithNumMeshesLargerThanMinNumMeshesPerThread;
	const bool bUseWorkerThreadsForShadowViews = NumShadowFrustumBatchWorkers >= 1;
	const size_t NumShadowFrustumsThisThread = bUseWorkerThreadsForShadowViews 
		? std::max((size_t)0, NumShadowMeshFrustums - NumShadowFrustumBatchWorkers) 
		: NumShadowMeshFrustums;
	if (bUseWorkerThreadsForShadowViews)
	{
		for (size_t iFrustum = 1 + NumShadowFrustumsThisThread; iFrustum <= NumShadowMeshFrustums; ++iFrustum)
		{
			SCOPED_CPU_MARKER("Dispatch");
			UpdateWorkerThreadPool.AddTask([=, &BBH, &mMeshes, &mMaterials]() // dispatch workers
			{
				SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
				FFrustumRenderCommandRecorderContext ctx = WorkerContexts[iFrustum - 1]; // operate on a copy
				if (ctx.pCullResults->empty())
					return;

				ReserveWorkerMemory(BBH,
					*ctx.pCullResults,
					ctx.pShadowView->meshRenderCommands,
					ctx.pShadowView->drawParamLookup,
					FSceneShadowViews::FShadowView::GetKey,
					ctx.pShadowView->mRenderCmdInstanceDataWriteIndex,
					MAX_INSTANCE_COUNT__SHADOW_MESHES
				);
				
				CollectViewInstanceData(BBH,
					*ctx.pCullResults,
					0,
					ctx.pCullResults->size()-1,
					ctx.pShadowView->matViewProj,
					ctx.pShadowView->matViewProj,
					ctx.pShadowView->mRenderCmdInstanceDataWriteIndex,
					ctx.pShadowView->meshRenderCommands
				);
			});
		}
	}

	//{
	//	SCOPED_CPU_MARKER("ThisThread");

	//	// handle frustums in this thread
	//	for (size_t iFrustum = 1; iFrustum <= std::min(1+ NumShadowFrustumsThisThread, NumShadowMeshFrustums); ++iFrustum)
	//	{
	//		FFrustumRenderCommandRecorderContext ctx = WorkerContexts[iFrustum - 1];
	//		assert(!ctx.pCullResults->empty());
	//		ReserveWorkerMemory(BBH,
	//			*ctx.pCullResults,
	//			ctx.pShadowView->meshRenderCommands,
	//			ctx.pShadowView->drawParamLookup,
	//			FSceneShadowViews::FShadowView::GetKey,
	//			ctx.pShadowView->mRenderCmdInstanceDataWriteIndex,
	//			MAX_INSTANCE_COUNT__SHADOW_MESHES
	//		);
	//		CollectViewInstanceData(BBH,
	//			*ctx.pCullResults,
	//			0,
	//			ctx.pCullResults->size() - 1,
	//			ctx.pShadowView->matViewProj,
	//			ctx.pShadowView->matViewProj,
	//			ctx.pShadowView->mRenderCmdInstanceDataWriteIndex,
	//			ctx.pShadowView->meshRenderCommands
	//		);
	//	}
	//}

	return;
}

void Scene::BatchInstanceData(FSceneView& SceneView, ThreadPool& UpdateWorkerThreadPool)
{
	SCOPED_CPU_MARKER("BatchInstanceData");
	const SceneBoundingBoxHierarchy& BBH = mBoundingBoxHierarchy;
	FFrustumCullWorkerContext& ctx = mFrustumCullWorkerContext;

	constexpr size_t NUM_MIN_SCENE_MESHES_FOR_THREADING = 128;
	const size_t NumWorkerThreads = UpdateWorkerThreadPool.GetThreadPoolSize();

#if UPDATE_THREAD__ENABLE_WORKERS
	// ---------------------------------------------------SYNC ---------------------------------------------------
	{
		SCOPED_CPU_MARKER_C("BUSY_WAIT_WORKER_CULL", 0xFFFF0000); // wait for frustum cull workers to finish
		while (UpdateWorkerThreadPool.GetNumActiveTasks() != 0);
	}
	// --------------------------------------------------- SYNC ---------------------------------------------------
	std::vector<FFrustumCullWorkerContext::FCullResult>& vMainViewCullResults = mFrustumCullWorkerContext.vCullResultsPerView[0];

	const size_t NumSceneViewMeshes = vMainViewCullResults.size();
	const bool bUseWorkerThreadForMainView = NUM_MIN_SCENE_MESHES_FOR_THREADING <= NumSceneViewMeshes;
	const bool bForceLOD0_SceneView = SceneView.sceneParameters.bForceLOD0_SceneView;
	const bool bForceLOD0_ShadowView = SceneView.sceneParameters.bForceLOD0_ShadowView;

	std::vector< FFrustumRenderCommandRecorderContext> WorkerContexts;
	const size_t NumShadowMeshFrustums = ctx.NumValidInputElements - 1; // exclude main view

	// shadow views
	DispatchWorkers_ShadowViews(
		NumShadowMeshFrustums, 
		WorkerContexts, 
		bForceLOD0_ShadowView, 
		UpdateWorkerThreadPool, 
		mFrustumCullWorkerContext, 
		mFrustumIndex_pShadowViewLookup, 
		BBH, 
		mMeshes, 
		mMaterials
	);

	// main view
	std::atomic<int> MainViewThreadDone = 0;
	DispatchMainViewInstanceDataWorkers(
		mBoundingBoxHierarchy,
		SceneView,
		vMainViewCullResults,
		mMeshes,
		UpdateWorkerThreadPool,
		MainViewThreadDone,
		SceneView.mRenderCmdInstanceDataWriteIndex
	);

	// collect isntance data on this thread
	{
		const size_t NumThreads = NumWorkerThreads + 1;
		auto vRanges = PartitionWorkItemsIntoRanges(vMainViewCullResults.size(), NumThreads);
		if (!vRanges.empty())
		{
			const std::pair<size_t, size_t> vRange_ThisThread = vRanges.back();
			CollectViewInstanceData(BBH
				, vMainViewCullResults
				, vRange_ThisThread.first
				, vRange_ThisThread.second
				, SceneView.viewProj
				, SceneView.viewProjPrev
				, SceneView.mRenderCmdInstanceDataWriteIndex
				, SceneView.meshRenderCommands
			);
		}
	}

	// -------------------------------------------------------------------------------------------------------------------

	BatchInstanceData_BoundingBox(SceneView, UpdateWorkerThreadPool, SceneView.viewProj);

	{
		SCOPED_CPU_MARKER_C("ClearLocalContext", 0xFF880000);
		WorkerContexts.clear();
	}
	
	UpdateWorkerThreadPool.RunRemainingTasksOnThisThread();
	{
		SCOPED_CPU_MARKER_C("BUSY_WAIT_WORKER", 0xFFFF0000);
		while (UpdateWorkerThreadPool.GetNumActiveTasks() != 0);
	}

#else
	BatchInstanceData_SceneMeshes(&SceneView.meshRenderCommands
		, SceneView.MaterialMeshLODInstanceDataLookup
		, mFrustumCullWorkerContext.vCulledBoundingBoxIndexListPerView[0]
		, SceneView.viewProj
		, SceneView.viewProjPrev
	);
	for (size_t iFrustum = 1; iFrustum <= NumShadowMeshFrustums; ++iFrustum)
	{
		const FFrustumRenderCommandRecorderContext& ctx = WorkerContexts[iFrustum - 1];
		BatchInstanceData_ShadowMeshes(ctx.iFrustum, ctx.pShadowView, ctx.pObjIndices, ctx.pShadowView->matViewProj);
	}
	BatchInstanceData_BoundingBox(SceneView, UpdateWorkerThreadPool, SceneView.viewProj);
	RecordRenderLightMeshCommands(SceneView);

#endif
}

static void RecordRenderLightBoundsCommands(
	const std::vector<Light>& vLights, 
	std::vector<FLightRenderCommand>& cmds,
	const XMVECTOR& CameraPosition
)
{
	for (const Light& l : vLights)
	{
		if (!l.bEnabled)
			continue;

		RecordRenderLightBoundsCommand(l, cmds, CameraPosition);
	}
}

static void RecordRenderLightMeshCommands(
	const std::vector<Light>& vLights,
	std::vector<FLightRenderCommand>& cmds,
	const XMVECTOR& CameraPosition,
	bool bAttenuateLight = true
)
{
	for (const Light& l : vLights)
	{
		if (!l.bEnabled)
			continue;

		XMVECTOR lightPosition = XMLoadFloat3(&l.Position);
		XMVECTOR lightToCamera = CameraPosition - lightPosition;
		XMVECTOR dot = XMVector3Dot(lightToCamera, lightToCamera);

		const float distanceSq = dot.m128_f32[0];
		const float attenuation = 1.0f / distanceSq;
		const float attenuatedBrightness = l.Brightness * attenuation;
		FLightRenderCommand cmd;
		cmd.color = XMFLOAT4(
			  l.Color.x * bAttenuateLight ? attenuatedBrightness : 1.0f
			, l.Color.y * bAttenuateLight ? attenuatedBrightness : 1.0f
			, l.Color.z * bAttenuateLight ? attenuatedBrightness : 1.0f
			, 1.0f
		);
		cmd.matWorldTransformation = l.GetWorldTransformationMatrix();

		switch (l.Type)
		{
		case Light::EType::DIRECTIONAL:
			continue; // don't draw directional light mesh
			break;
		case Light::EType::SPOT:
		{
			cmd.meshID = EBuiltInMeshes::SPHERE;
			cmds.push_back(cmd);
		}	break;

		case Light::EType::POINT:
		{
			cmd.meshID = EBuiltInMeshes::SPHERE;
			cmds.push_back(cmd);
		}  break;
		}
	} 
}


void Scene::RecordRenderLightMeshCommands(FSceneView& SceneView) const
{
	SCOPED_CPU_MARKER("Scene::RecordRenderLightMeshCommands()");
	if (SceneView.sceneParameters.bDrawLightMeshes)
	{
		::RecordRenderLightMeshCommands(mLightsStatic    , SceneView.lightRenderCommands, SceneView.cameraPosition);
		::RecordRenderLightMeshCommands(mLightsDynamic   , SceneView.lightRenderCommands, SceneView.cameraPosition);
		::RecordRenderLightMeshCommands(mLightsStationary, SceneView.lightRenderCommands, SceneView.cameraPosition);
	}
	if (SceneView.sceneParameters.bDrawLightBounds)
	{
		::RecordRenderLightBoundsCommands(mLightsStatic    , SceneView.lightBoundsRenderCommands, SceneView.cameraPosition);
		::RecordRenderLightBoundsCommands(mLightsDynamic   , SceneView.lightBoundsRenderCommands, SceneView.cameraPosition);
		::RecordRenderLightBoundsCommands(mLightsStationary, SceneView.lightBoundsRenderCommands, SceneView.cameraPosition);
	}
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

