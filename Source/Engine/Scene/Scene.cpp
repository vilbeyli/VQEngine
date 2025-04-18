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
#include "Engine/MeshSorting.h"
#include "Engine/GPUMarker.h"

#include "Engine/Scene/SceneViews.h"
#include "Engine/Core/Window.h"
#include "Engine/VQEngine.h"
#include "Engine/Culling.h"
#include "Renderer/Rendering/RenderPass/ObjectIDPass.h"
#include "Renderer/Renderer.h"

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
#define ENABLE_WORKER_THREADS 1
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

	MaterialID id = INVALID_ID;
	// critical section
	{
		std::unique_lock<std::mutex> lk(mMtx_Materials);
		id = (MaterialID)mMaterialPool.Allocate();
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

	Material& mat = *mMaterialPool.Get(id);
	mat = Material();

	return id;
}

int Scene::CreateLight(Light::EType Type, Light::EMobility Mobility)
{
	// create light
	std::vector<Light>* pLights = nullptr;
	switch (Mobility)
	{
	case Light::STATIC    : pLights = &mLightsStatic; break;
	case Light::STATIONARY: pLights = &mLightsStationary; break;
	case Light::DYNAMIC   : pLights = &mLightsDynamic; break;
	default:
		Log::Error("CreateLight calle w/ invalid mobility");
		return -1;
	}
	assert(pLights);
	switch (Type)
	{
	case Light::POINT      : pLights->push_back(Light::MakePointLight()); break;
	case Light::SPOT       : pLights->push_back(Light::MakeSpotLight()); break;
	case Light::DIRECTIONAL: pLights->push_back(Light::MakeDirectionalLight()); break;
	default:
		Log::Error("CreateLight called w/ invalid type");
		return -1;
	}

	// validate directional light singularity
	int NumDirectional = 0;
	for (const Light& l : *pLights) NumDirectional += l.Type == Light::EType::DIRECTIONAL ? 1 : 0;
	assert(NumDirectional <= 1);
	if (NumDirectional > 1)
	{
		Log::Warning("More than 1 Directional Light created");
	}

	Light& l = pLights->back();

	// calculate light spawn location
	const float SPAWN_DISTANCE_TO_CAMERA = 3.0f;
	float3 spawnLocation = float3(0, 0, 0);
	if (mIndex_SelectedCamera >= 0)
	{
		const Camera& cam = this->GetActiveCamera();
		const XMFLOAT3 pos = cam.GetPositionF();
		XMVECTOR vPos = XMLoadFloat3(&pos);
		XMVECTOR vDir = cam.GetDirection();
		vPos += vDir * SPAWN_DISTANCE_TO_CAMERA;
		XMStoreFloat3(&spawnLocation, vPos);
	}
	l.Position = spawnLocation;

	return (int)pLights->size() - 1;
}

bool Scene::RemoveLight(Light* pLight)
{
	if (!pLight)
	{
		Log::Warning("RemoveLight received invalid light address");
		return false;
	}
	std::vector<Light>* pLights = nullptr;
	switch (pLight->Mobility)
	{
	case Light::STATIC    : pLights = &mLightsStatic; break;
	case Light::STATIONARY: pLights = &mLightsStationary; break;
	case Light::DYNAMIC   : pLights = &mLightsDynamic; break;
	default:
		Log::Error("RemoveLight called w/ invalid mobility");
		return false;
	}
	assert(pLights);

	for (auto it = pLights->begin(); it != pLights->end(); ++it)
	{
		if (&*it == pLight)
		{
			pLights->erase(it);
			return true;
		}
	}

	Log::Warning("Couldn't find light to remove");
	return false;
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
	SCOPED_CPU_MARKER("Scene.GetLightsOfType");
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
	std::vector<size_t> vHandles = mMaterialPool.GetAllAliveObjectHandles();
	std::vector<MaterialID> vIDs(vHandles.size());
	size_t i = 0;
	for (size_t h : vHandles)
		vIDs[i++] = static_cast<MaterialID>(h);
	return vIDs;
}

const Material& Scene::GetMaterial(MaterialID ID) const
{
	const Material* pMaterial = mMaterialPool.Get(ID);
	if (pMaterial == nullptr)
	{
		Log::Error("Material not created. Did you call Scene::CreateMaterial()? (matID=%d)", ID);
		return *mMaterialPool.Get(0); // TODO: mDefaultMaterialID
	}
	return *pMaterial;
}

Material& Scene::GetMaterial(MaterialID ID)
{
	Material* pMaterial = mMaterialPool.Get(ID);
	if (pMaterial == nullptr)
	{
		Log::Error("Material not created. Did you call Scene::CreateMaterial()? (matID=%d)", ID);
		return *mMaterialPool.Get(0); // TODO: mDefaultMaterialID
	}
	return *pMaterial;
}

const Mesh& Scene::GetMesh(MeshID ID) const
{
	if (mMeshes.find(ID) == mMeshes.end())
	{
		Log::Error("Mesh not found. Did you call Scene::AddMesh()? (meshID=%d)", ID);
		assert(false);
	}
	return mMeshes.at(ID);
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

	stats.pRenderStats = &this->mRenderer.GetRenderStats();
	
	stats.NumMeshes    = static_cast<uint>(this->mMeshes.size());
	stats.NumModels    = static_cast<uint>(this->mModels.size());
	stats.NumMaterials = static_cast<uint>(this->mMaterialPool.GetAliveObjectCount());
	stats.NumObjects   = static_cast<uint>(this->mGameObjectHandles.size());
	stats.NumCameras   = static_cast<uint>(this->mCameras.size());

	return stats;
}

GameObject* Scene::GetGameObject(size_t hObject) const { return mGameObjectPool.Get(hObject); }
Transform* Scene::GetGameObjectTransform(size_t hObject) const { return mGameObjectTransformPool.Get(hObject); }

const Light* Scene::GetLight(Light::EMobility Mobility) const
{
	return nullptr;
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
	, mFrustumCullWorkerContext(mBoundingBoxHierarchy, mMeshes, mMaterialPool)
	, mIndex_SelectedCamera(0)
	, mIndex_ActiveEnvironmentMapPreset(-1)
	, mGameObjectPool(NUM_GAMEOBJECT_POOL_SIZE, GAMEOBJECT_BYTE_ALIGNMENT)
	, mGameObjectTransformPool(NUM_GAMEOBJECT_POOL_SIZE, GAMEOBJECT_BYTE_ALIGNMENT)
	, mMaterialPool(NUM_MATERIAL_POOL_SIZE, alignof(Material))
	, mResourceNames(engine.GetResourceNames())
	, mAssetLoader(engine.GetAssetLoader())
	, mRenderer(renderer)
	, mBoundingBoxHierarchy(mMeshes, mModels, mMaterialPool, mTransformHandles)
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
	SceneViewCurr.sceneRenderOptions = SceneViewPrev.sceneRenderOptions;
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

static void ExtractSceneView(FSceneView& SceneView, std::unordered_map<const Camera*, DirectX::XMMATRIX>& mViewProjectionMatrixHistory, const Camera& cam, std::pair<BufferID, BufferID> cubeVBIB)
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
	SceneView.HDRIYawOffset = SceneView.sceneRenderOptions.fYawSliderValue * XM_PI * 2.0f;

	SceneView.cubeVB = cubeVBIB.first;
	SceneView.cubeIB = cubeVBIB.second;

	{
		Camera skyCam = cam.Clone();
		FCameraParameters p = {};
		p.bInitializeCameraController = false;
		p.ProjectionParams = skyCam.GetProjectionParameters();
		p.ProjectionParams.bPerspectiveProjection = true;
		p.ProjectionParams.FieldOfView = p.ProjectionParams.FieldOfView * RAD2DEG;
		p.x = p.y = p.z = 0;
		p.Yaw = (SceneView.MainViewCameraYaw + SceneView.HDRIYawOffset) * RAD2DEG;
		p.Pitch = SceneView.MainViewCameraPitch * RAD2DEG;
		skyCam.InitializeCamera(p);
		SceneView.EnvironmentMapViewProj = skyCam.GetViewMatrix()* skyCam.GetProjectionMatrix();
	}
}

static void CollectDebugVertexDrawParams(
	FSceneDrawData& SceneDrawData,
	const std::vector<size_t>& mSelectedObjects,
	const Scene* pScene,
	const ModelLookup_t& mModels,
	const std::unordered_map<MeshID, Mesh>& mMeshes,
	const Camera& cam
)
{
	SCOPED_CPU_MARKER("CollectDebugVertexDrawParams");

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
	SceneDrawData.debugVertexAxesRenderParams.resize(NumMeshes);
	
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

			MeshRenderData_t& cmd = SceneDrawData.debugVertexAxesRenderParams[i];
			cmd.matWorld.resize(1);
			cmd.matNormal.resize(1);

			const int lod = 0;
			cmd.numIndices = mesh.GetNumIndices(lod);
			cmd.vertexIndexBuffer = mesh.GetIABufferIDs(lod);
			cmd.matWorld[0] = pTf->matWorldTransformation();
			cmd.matNormal[0] = pTf->NormalMatrix(cmd.matWorld.back());
			++i;
		}
	}	
}


static void GatherOutlineMeshRenderData(
	std::vector<FOutlineRenderData>& cmds,
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
			//Log::Warning("pObj NULL in GatherOutlineMeshRenderData");
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

			FOutlineRenderData cmd = {};
			cmd.pMesh = &pScene->GetMesh(meshID);
			cmd.matID = matID;
			cmd.pMaterial = &pScene->GetMaterial(matID);
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

static void GatherLightBoundsRenderData(const Light& l,
	std::vector<FLightRenderData>& cmds,
	const XMVECTOR& CameraPosition,
	const Scene* pScene
)
{
	const XMVECTOR lightPosition = XMLoadFloat3(&l.Position);
	const XMVECTOR lightToCamera = CameraPosition - lightPosition;
	const XMVECTOR dot = XMVector3Dot(lightToCamera, lightToCamera);

	const float distanceSq = dot.m128_f32[0];
	const float attenuation = 1.0f / distanceSq;
	const float attenuatedBrightness = l.Brightness * attenuation;

	const float alpha = 0.45f;

	FLightRenderData cmd;
	cmd.color = XMFLOAT4(
		l.Color.x //* attenuatedBrightness
		, l.Color.y //* attenuatedBrightness
		, l.Color.z //* attenuatedBrightness
		, alpha
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
		cmd.pMesh = &pScene->GetMesh(EBuiltInMeshes::CONE);
		cmd.matWorldTransformation = scaleConeToRange * alignConeToSpotLightTransformation * tf.matWorldTransformation();
		cmds.push_back(cmd);
	}	break;

	case Light::EType::POINT:
	{
		Transform tf = l.GetTransform();
		tf._scale = XMFLOAT3(l.Range, l.Range, l.Range);

		cmd.pMesh = &pScene->GetMesh(EBuiltInMeshes::SPHERE);
		cmd.matWorldTransformation = tf.matWorldTransformation();
		cmds.push_back(cmd);
	}  break;
	} // swicth
}
void Scene::PostUpdate(ThreadPool& UpdateWorkerThreadPool, const FUIState& UIState, bool AppInSimulationState, int FRAME_DATA_INDEX)
{
	SCOPED_CPU_MARKER("Scene::PostUpdate()");
	assert(FRAME_DATA_INDEX < mFrameSceneViews.size());

	const Camera& cam = mCameras[mIndex_SelectedCamera];

	FSceneView& SceneView = mFrameSceneViews[FRAME_DATA_INDEX];
	FSceneShadowViews& ShadowView = mFrameShadowViews[FRAME_DATA_INDEX];

	mBoundingBoxHierarchy.Build(this, mGameObjectHandles, UpdateWorkerThreadPool);

	ExtractSceneView(SceneView, mViewProjectionMatrixHistory, cam, this->mMeshes.at(EBuiltInMeshes::CUBE).GetIABufferIDs());
	SceneView.pEnvironmentMapMesh        = &mMeshes.at((MeshID)EBuiltInMeshes::CUBE);
	SceneView.bAppIsInSimulationState    = AppInSimulationState;
	SceneView.NumGameObjectBBRenderCmds  = (uint)(SceneView.sceneRenderOptions.bDrawGameObjectBoundingBoxes ? DIV_AND_ROUND_UP(mBoundingBoxHierarchy.mGameObjectBoundingBoxes.size(), MAX_INSTANCE_COUNT__UNLIT_SHADER) : 0);
	SceneView.NumMeshBBRenderCmds        = (uint)(SceneView.sceneRenderOptions.bDrawMeshBoundingBoxes       ? DIV_AND_ROUND_UP(mBoundingBoxHierarchy.mMeshBoundingBoxes.size()      , MAX_INSTANCE_COUNT__UNLIT_SHADER) : 0);
	SceneView.pGameObjectBoundingBoxList = &mBoundingBoxHierarchy.mGameObjectBoundingBoxes;
	SceneView.pMeshBoundingBoxList       = &mBoundingBoxHierarchy.mMeshBoundingBoxes;

	// distance-cull and get active shadowing lights from various light containers
	{
		SCOPED_CPU_MARKER("CullLights");
		const FFrustumPlaneset ViewFrustumPlanes = FFrustumPlaneset::ExtractFromMatrix(cam.GetViewProjectionMatrix());
		mActiveLightIndices_Static     = GetActiveAndCulledLightIndices(mLightsStatic, ViewFrustumPlanes);
		mActiveLightIndices_Stationary = GetActiveAndCulledLightIndices(mLightsStationary, ViewFrustumPlanes);
		mActiveLightIndices_Dynamic    = GetActiveAndCulledLightIndices(mLightsDynamic, ViewFrustumPlanes);
	}

	GatherSceneLightData(SceneView);

	ShadowView.NumPointShadowViews = 0;
	ShadowView.NumSpotShadowViews = 0;
	ShadowView.NumDirectionalViews = 0;
	GatherShadowViewData(ShadowView, mLightsStatic, mActiveLightIndices_Static);
	GatherShadowViewData(ShadowView, mLightsStationary, mActiveLightIndices_Stationary);
	GatherShadowViewData(ShadowView, mLightsDynamic, mActiveLightIndices_Dynamic);

	GatherFrustumCullParameters(SceneView, ShadowView, UpdateWorkerThreadPool);

	CullFrustums(SceneView, UpdateWorkerThreadPool);

	// ----------------------------------------------------------------------------------------------------------------------------------------------------------------

	FSceneDrawData& DrawData = mRenderer.GetSceneDrawData(FRAME_DATA_INDEX);
	
	GatherLightMeshRenderData(SceneView);
	GatherOutlineMeshRenderData(DrawData.outlineRenderParams, mSelectedObjects, this, SceneView.view, SceneView.proj, SceneView.sceneRenderOptions.OutlineColor);
	 
	if (UIState.bDrawLightVolume)
	{
		const auto lights = this->GetLights(); // todo: this is unnecessary copying, don't do this
		const int i = UIState.SelectedEditeeIndex[FUIState::EEditorMode::LIGHTS];
		if (i >= 0 && i < lights.size())
		{
			const Light& l = *lights[i];
			if (l.bEnabled)
			{
				GatherLightBoundsRenderData(l, DrawData.lightBoundsRenderParams, SceneView.cameraPosition, this);
			}
		}
	}

	if (SceneView.sceneRenderOptions.bDrawVertexLocalAxes)
	{
		CollectDebugVertexDrawParams(DrawData, mSelectedObjects, this, mModels, mMeshes, cam);
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
		ToggleBool(SceneView.sceneRenderOptions.bDrawLightBounds);
	}
	if (mInput.IsKeyTriggered("N"))
	{
		if(bIsShiftDown) ToggleBool(SceneView.sceneRenderOptions.bDrawGameObjectBoundingBoxes);
		else             ToggleBool(SceneView.sceneRenderOptions.bDrawMeshBoundingBoxes);
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

void Scene::PickObject(int4 px)
{
	// Log::Info("Picked(%d, %d): Obj[%d] Mesh[%d] Material[%d] ProjArea[%d]", MouseClickPositionX, MouseClickPositionY, px.x, px.y, px.z, px.w);

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
			SceneShadowView.ShadowView_Directional = l.GetViewProjectionMatrix();
			++SceneShadowView.NumDirectionalViews;
		}	break;
		case Light::EType::SPOT:
		{
			SceneShadowView.ShadowViews_Spot[SceneShadowView.NumSpotShadowViews++] = l.GetViewProjectionMatrix();
		} break;
		case Light::EType::POINT:
		{
			for (int face = 0; face < 6; ++face)
			{
				SceneShadowView.ShadowViews_Point[SceneShadowView.NumPointShadowViews * 6 + face] = l.GetViewProjectionMatrix(static_cast<CubemapUtility::ECubeMapLookDirections>(face));
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

void Scene::GatherFrustumCullParameters(FSceneView& SceneView, FSceneShadowViews& SceneShadowView, ThreadPool& UpdateWorkerThreadPool)
{
	SCOPED_CPU_MARKER("GatherFrustumCullParameters");
	const SceneBoundingBoxHierarchy& BVH = mBoundingBoxHierarchy;
	const size_t NumWorkerThreadsAvailable = UpdateWorkerThreadPool.GetThreadPoolSize();

	const std::vector<const Light*> dirLights = GetLightsOfType(Light::EType::DIRECTIONAL);
	const bool bCullDirectionalLightView = !dirLights.empty() && dirLights[0]->bEnabled && dirLights[0]->bCastingShadows;

	const uint NumSceneViews = 1;
	const uint NumFrustums = NumSceneViews
		+ SceneShadowView.NumSpotShadowViews
		+ SceneShadowView.NumPointShadowViews * 6
		+ (bCullDirectionalLightView ? 1 : 0);


	mFrustumCullWorkerContext.pFrustumRenderLists = &SceneView.FrustumRenderLists;
	mFrustumCullWorkerContext.InvalidateContextData();
	mFrustumCullWorkerContext.AllocInputMemoryIfNecessary(NumFrustums);
	assert(SceneView.FrustumRenderLists.size() >= NumFrustums);

	std::vector<FFrustumPlaneset>& FrustumPlanesets = mFrustumCullWorkerContext.vFrustumPlanes;
	std::vector<XMMATRIX>& FrustumViewProjMatrix = mFrustumCullWorkerContext.vMatViewProj;

	std::vector<FFrustumRenderList>& FrustumRenderLists = (*mFrustumCullWorkerContext.pFrustumRenderLists);
	size_t iFrustum = 0;
	{
		SCOPED_CPU_MARKER("CollectFrustumPlanesets");
		FrustumViewProjMatrix[iFrustum] = SceneView.viewProj;
		FrustumPlanesets[iFrustum] = FFrustumPlaneset::ExtractFromMatrix(SceneView.viewProj);
		FrustumRenderLists[iFrustum].pViewData = &SceneView;
		FrustumRenderLists[iFrustum].ResetSignalsAndData();

		++iFrustum; // main view frustum done -- move to shadow views

		// directional
		if (bCullDirectionalLightView)
		{
			FrustumRenderLists[iFrustum].pViewData = &SceneShadowView.ShadowView_Directional;
			FrustumRenderLists[iFrustum].Type = FFrustumRenderList::EFrustumType::DirectionalShadow;
			FrustumRenderLists[iFrustum].TypeIndex = 0;
			FrustumRenderLists[iFrustum].ResetSignalsAndData();

			FrustumViewProjMatrix[iFrustum] = SceneShadowView.ShadowView_Directional;
			FrustumPlanesets[iFrustum++] = FFrustumPlaneset::ExtractFromMatrix(SceneShadowView.ShadowView_Directional);
		}

		// point
		for (uint iPoint = 0; iPoint < SceneShadowView.NumPointShadowViews; ++iPoint)
		for (uint face = 0; face < 6; ++face)
		{
			const uint iPointFace = iPoint * 6 + face;

			FrustumRenderLists[iFrustum].pViewData = &SceneShadowView.ShadowViews_Point[iPointFace];
			FrustumRenderLists[iFrustum].Type = FFrustumRenderList::EFrustumType::PointShadow;
			FrustumRenderLists[iFrustum].TypeIndex = iPointFace;
			FrustumRenderLists[iFrustum].ResetSignalsAndData();

			FrustumViewProjMatrix[iFrustum] = SceneShadowView.ShadowViews_Point[iPointFace];
			FrustumPlanesets[iFrustum++] = FFrustumPlaneset::ExtractFromMatrix(SceneShadowView.ShadowViews_Point[iPointFace]);
		}

		// spot
		for (uint iSpot = 0; iSpot < SceneShadowView.NumSpotShadowViews; ++iSpot)
		{
			FrustumRenderLists[iFrustum].pViewData = &SceneShadowView.ShadowViews_Spot[iSpot];
			FrustumRenderLists[iFrustum].Type = FFrustumRenderList::EFrustumType::SpotShadow;
			FrustumRenderLists[iFrustum].TypeIndex = iSpot;
			FrustumRenderLists[iFrustum].ResetSignalsAndData();

			FrustumViewProjMatrix[iFrustum] = SceneShadowView.ShadowViews_Spot[iSpot];
			FrustumPlanesets[iFrustum++] = FFrustumPlaneset::ExtractFromMatrix(SceneShadowView.ShadowViews_Spot[iSpot]);
		}
	}
	SceneView.NumActiveFrustumRenderLists = (uint)iFrustum;

	constexpr size_t MINIMUM_WORK_SIZE_PER_THREAD = 256;

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

#if ENABLE_WORKER_THREADS

	std::vector<TaskSignal<void>> Signals;
	{
		SCOPED_CPU_MARKER("InitFrustumCullWorkerContexts");
#if 0 // debug-single threaded
		for (size_t iKey = 0; iKey < FrustumPlanesets.size(); ++iKey)
			mFrustumCullWorkerContext.AddWorkerItem(FrustumPlanesets[iKey], BVH.mMeshBoundingBoxes, BVH.mGameObjectHandles, iKey);
#else
		const std::vector<std::pair<size_t, size_t>> vRanges = PartitionWorkItemsIntoRanges(NumFrustums, NumThreads);
		Signals.resize(vRanges.size());
		{
			mFrustumCullWorkerContext.vBoundingBoxList = BVH.mMeshBoundingBoxes;

			const bool bForceLOD0_ShadowView = SceneView.sceneRenderOptions.bForceLOD0_ShadowView;
			std::function<bool(const FVisibleMeshSortData&, const FVisibleMeshSortData&)> fnShadowViewSort = [](const FVisibleMeshSortData& l, const FVisibleMeshSortData& r)
			{
				return MeshSorting::GetShadowMeshKey(l.matID, l.meshID, l.iLOD, l.bTess) > MeshSorting::GetShadowMeshKey(r.matID, r.meshID, r.iLOD, r.bTess);
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
					UpdateWorkerThreadPool.AddTask([Range, fnShadowViewSort, &FrustumPlanesets, &BVH, this, &FrustumViewProjMatrix, bForceLOD0_ShadowView, &Signals, currRange]()
					{
						// we're doing shadow views
						
						SCOPED_CPU_MARKER_C("UpdateWorker", 0xFF0000FF);
						{
							SCOPED_CPU_MARKER(fnMark("InitWorkerContexts", Range.first, Range.second).c_str());
							for (size_t i = Range.first; i <= Range.second; ++i)
							{
								mFrustumCullWorkerContext.AddWorkerItem(
									  BVH.mMeshBoundingBoxes
									, i
									, fnShadowViewSort
									, bForceLOD0_ShadowView
								);
							}
							Signals[currRange].Notify();
						}
					});
					++currRange;
				}
			}

			// main view
			SCOPED_CPU_MARKER(fnMark("InitWorkerContexts", vRanges[0].first, vRanges[0].second).c_str());

			const bool bForceLOD0 = SceneView.sceneRenderOptions.bForceLOD0_SceneView;
			std::function<bool(const FVisibleMeshSortData&, const FVisibleMeshSortData&)> fnMainViewSort = [](const FVisibleMeshSortData& l, const FVisibleMeshSortData& r)
			{
				return MeshSorting::GetLitMeshKey(l.matID, l.meshID, l.iLOD, l.bTess) > MeshSorting::GetLitMeshKey(r.matID, r.meshID, r.iLOD, r.bTess);
			};

			for (size_t i = vRanges[0].first; i <= vRanges[0].second; ++i)
			{
				mFrustumCullWorkerContext.AddWorkerItem(
					  BVH.mMeshBoundingBoxes
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
		SCOPED_CPU_MARKER_C("WAIT_WORKERS", 0xFFFF0000);
		for (size_t i = 1; i < Signals.size(); ++i)
			Signals[i].Wait();
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


	if (ENABLE_WORKER_THREADS && bThreadTheWork)
	{
		mFrustumCullWorkerContext.ProcessWorkItems_MultiThreaded(NumThreads, UpdateWorkerThreadPool);
	}
	else
	{
		mFrustumCullWorkerContext.ProcessWorkItems_SingleThreaded();
	}
}




//-------------------------------------------------------------------------------
//
// DRAW BATCHING
//
//-------------------------------------------------------------------------------

static void GatherLightBoundsRenderData(
	const std::vector<Light>& vLights, 
	std::vector<FLightRenderData>& cmds,
	const XMVECTOR& CameraPosition,
	const Scene* pScene
)
{
	for (const Light& l : vLights)
	{
		if (!l.bEnabled)
			continue;

		GatherLightBoundsRenderData(l, cmds, CameraPosition, pScene);
	}
}

static void GatherLightMeshRenderData(
	const std::vector<Light>& vLights,
	std::vector<FLightRenderData>& cmds,
	const XMVECTOR& CameraPosition,
	const Scene* pScene,
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
		FLightRenderData cmd;
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
			break; // don't draw directional light mesh
		case Light::EType::SPOT:
		{
			cmd.pMesh = &pScene->GetMesh(EBuiltInMeshes::SPHERE);
			cmds.push_back(cmd);
		}	break;

		case Light::EType::POINT:
		{
			cmd.pMesh = &pScene->GetMesh(EBuiltInMeshes::SPHERE);
			cmds.push_back(cmd);
		}  break;
		}
	} 
}


void Scene::GatherLightMeshRenderData(const FSceneView& SceneView) const
{
	SCOPED_CPU_MARKER("GatherLightMeshRenderData");
	FSceneDrawData& SceneDrawData = mRenderer.GetSceneDrawData(0);

	SceneDrawData.lightBoundsRenderParams.clear();
	SceneDrawData.lightRenderParams.clear();
	if (SceneView.sceneRenderOptions.bDrawLightMeshes)
	{
		::GatherLightMeshRenderData(mLightsStatic    , SceneDrawData.lightRenderParams, SceneView.cameraPosition, this);
		::GatherLightMeshRenderData(mLightsDynamic   , SceneDrawData.lightRenderParams, SceneView.cameraPosition, this);
		::GatherLightMeshRenderData(mLightsStationary, SceneDrawData.lightRenderParams, SceneView.cameraPosition, this);
	}
	if (SceneView.sceneRenderOptions.bDrawLightBounds)
	{
		::GatherLightBoundsRenderData(mLightsStatic    , SceneDrawData.lightBoundsRenderParams, SceneView.cameraPosition, this);
		::GatherLightBoundsRenderData(mLightsDynamic   , SceneDrawData.lightBoundsRenderParams, SceneView.cameraPosition, this);
		::GatherLightBoundsRenderData(mLightsStationary, SceneDrawData.lightBoundsRenderParams, SceneView.cameraPosition, this);
	}
}