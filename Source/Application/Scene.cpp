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
#include "imgui.h"

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
	Log::Info("Scene::CreateMaterial() ID=%d - %s", id,  UniqueMaterialName.c_str());
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

MaterialID Scene::LoadMaterial(const FMaterialRepresentation& matRep, TaskID taskID)
{
	MaterialID id = this->CreateMaterial(matRep.Name);
	Material& mat = this->GetMaterial(id);

	auto fnAssignF  = [](float& dest, const float& src) { if (src != MATERIAL_UNINITIALIZED_VALUE) dest = src; };
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
	bHasTexture |= fnEnqueueTexLoad(id, matRep.DiffuseMapFilePath  , AssetLoader::ETextureType::DIFFUSE);
	bHasTexture |= fnEnqueueTexLoad(id, matRep.NormalMapFilePath   , AssetLoader::ETextureType::NORMALS);
	bHasTexture |= fnEnqueueTexLoad(id, matRep.EmissiveMapFilePath , AssetLoader::ETextureType::EMISSIVE);
	bHasTexture |= fnEnqueueTexLoad(id, matRep.AlphaMaskMapFilePath, AssetLoader::ETextureType::ALPHA_MASK);
	bHasTexture |= fnEnqueueTexLoad(id, matRep.MetallicMapFilePath , AssetLoader::ETextureType::METALNESS);
	bHasTexture |= fnEnqueueTexLoad(id, matRep.RoughnessMapFilePath, AssetLoader::ETextureType::ROUGHNESS);
	bHasTexture |= fnEnqueueTexLoad(id, matRep.AOMapFilePath       , AssetLoader::ETextureType::AMBIENT_OCCLUSION);

	AssetLoader::FMaterialTextureAssignment MatTexAssignment = {};
	MatTexAssignment.matID = id;
	if(bHasTexture)
		mMaterialAssignments.mAssignments.push_back(MatTexAssignment);
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



//
//
//
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
{}

void Scene::StartLoading(const BuiltinMeshArray_t& builtinMeshes, FSceneRepresentation& sceneRep)
{
	mRenderer.WaitForLoadCompletion();
	
	Log::Info("[Scene] Loading Scene: %s", sceneRep.SceneName.c_str());
	const TaskID taskID = AssetLoader::GenerateModelLoadTaskID();


	mSceneRepresentation = sceneRep;

	// ------------------------------------------------------------------------------------------------
	// this is not very good: builtin materials will always be loaded even when they're not used!
	// TODO: load materials that are only referenced by the scene.
	if (mMaterials.empty())
		LoadBuiltinMaterials(taskID);
	// ------------------------------------------------------------------------------------------------

	LoadBuiltinMeshes(builtinMeshes);
	
	this->LoadScene(sceneRep); // scene-specific load 
	
	LoadSceneMaterials(sceneRep.Materials, taskID);

	LoadGameObjects(std::move(sceneRep.Objects));
	LoadLights(sceneRep.Lights);
	LoadCameras(sceneRep.Cameras);
	LoadPostProcessSettings();
}

void Scene::LoadBuiltinMaterials(TaskID taskID)
{
	const char* STR_MATERIALS_FOLDER = "Data/Materials/";

	auto vMatFiles = DirectoryUtil::ListFilesInDirectory(STR_MATERIALS_FOLDER, "xml");
	for (const std::string& filePath : vMatFiles)
	{
		std::vector<FMaterialRepresentation> vMaterialReps = VQEngine::ParseMaterialFile(filePath);
		for (const FMaterialRepresentation& matRep : vMaterialReps)
		{
			LoadMaterial(matRep, taskID);
		}
	}
}

void Scene::LoadBuiltinMeshes(const BuiltinMeshArray_t& builtinMeshes)
{
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

void Scene::LoadGameObjects(std::vector<FGameObjectRepresentation>&& GameObjects)
{
	constexpr bool B_LOAD_GAMEOBJECTS_SERIAL = true;

	if constexpr (B_LOAD_GAMEOBJECTS_SERIAL)
	{
		for (FGameObjectRepresentation& ObjRep : GameObjects)
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

				// material
				MaterialID matID = this->mDefaultMaterialID;
				if (!ObjRep.MaterialName.empty())
				{
					matID = this->CreateMaterial(ObjRep.MaterialName);
				}
				Material& mat = this->GetMaterial(matID);
				const bool bTransparentMesh = mat.IsTransparent();
				model.mData.mOpaqueMaterials[meshID] = matID; // todo: handle transparency

				model.mbLoaded = true;
				pObj->mModelID = mID;
			}
			else
			{
				mAssetLoader.QueueModelLoad(pObj, ObjRep.ModelFilePath, ObjRep.ModelName);
			}


			mpObjects.push_back(pObj);
		}
	}
	else // THREADED LOAD
	{
		// dispatch workers
		assert(false); // TODO: profile first
	}

	// kickoff workers for loading models
	mModelLoadResults = mAssetLoader.StartLoadingModels(this);
	Log::Info("[Scene] Start loading models...");

}

void Scene::LoadSceneMaterials(const std::vector<FMaterialRepresentation>& Materials, TaskID taskID)
{
	// Create scene materials before deserializing gameobjects
	uint NumMaterials = 0;
	for (const FMaterialRepresentation& matRep : Materials)
	{
		this->LoadMaterial(matRep, taskID);
		++NumMaterials;
	}
	
	if(NumMaterials > 0)
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
	for (const Light& l : SceneLights)
	{
		std::vector<Light>& LightContainer = [&]() -> std::vector<Light>& {
			switch (l.Mobility)
			{
			case Light::EMobility::DYNAMIC   : return mLightsDynamic;
			case Light::EMobility::STATIC    : return mLightsStatic;
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
			mCameras[i].InitializeController(CameraParams[i].bFirstPerson, CameraParams[i]);
		}
	}
	Log::Info("[Scene] Cameras initialized");
}


#define A_CPU 1
#include "Shaders/FidelityFX/ffx_a.h"
#include "Shaders/FidelityFX/CAS/ffx_cas.h"
void Scene::LoadPostProcessSettings(/*TODO: scene PP settings*/)
{
	// TODO: remove hardcode

	const float fWidth  = static_cast<float>(this->mpWindow->GetWidth());
	const float fHeight = static_cast<float>(this->mpWindow->GetHeight());

	// Update PostProcess Data
	for (size_t i = 0; i < mFrameSceneViews.size(); ++i)
	{
		FPostProcessParameters& PPParams = this->GetPostProcessParameters(static_cast<int>(i));
		FPostProcessParameters::FFFXCAS& CASParams = PPParams.FFXCASParams;
		CasSetup(&CASParams.CASConstantBlock[0], &CASParams.CASConstantBlock[4], CASParams.CASSharpen, fWidth, fHeight, fWidth, fHeight);

		PPParams.bEnableCAS = true; // TODO: read from scene PP settings
	}
}

void Scene::OnLoadComplete()
{
	Log::Info("[Scene] OnLoadComplete()");

	// Assign model data 
	for (auto it = mModelLoadResults.begin(); it != mModelLoadResults.end(); ++it)
	{
		GameObject* pObj = it->first;
		AssetLoader::ModelLoadResult_t res = std::move(it->second);

		assert(res.valid());
		///res.wait(); // we should already have the results ready in OnLoadComplete()

		pObj->mModelID = res.get();
	}

	mMaterialAssignments.DoAssignments(this, &mRenderer);

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
	mFrameShadowViews.clear();
	mFrameSceneViews.resize(sz);
	mFrameShadowViews.resize(sz);

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

	mLightsDynamic.clear();
	mLightsStatic.clear();
	mLightsStationary.clear();
}



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

	// TODO: compute mesh visibility 
	// TODO: cull lights
	PrepareSceneMeshRenderParams(SceneView);
	GatherSceneLightData(SceneView);
	PrepareShadowMeshRenderParams(ShadowView);
	PrepareLightMeshRenderParams(SceneView);

	// update post process settings for next frame
	SceneViewNext.postProcessParameters = SceneView.postProcessParameters;
	SceneViewNext.sceneParameters = SceneView.sceneParameters;
}

// To use the 'disabled UI state' functionality (ImGuiItemFlags_Disabled), include internal header
// https://github.com/ocornut/imgui/issues/211#issuecomment-339241929
#include "imgui_internal.h"
void Scene::RenderUI(FWindowRenderContext& ctx)
{
	auto fnDisableUIStateBegin = [](const bool& bEnable)
	{
		if (!bEnable)
		{
			ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
		}
	};
	auto fnDisableUIStateEnd = [](const bool& bEnable)
	{
		if (!bEnable)
		{
			ImGui::PopItemFlag();
			ImGui::PopStyleVar();
		}
	};

	ImGui::NewFrame();
	ImGuiIO& io = ImGui::GetIO();
	ImGuiStyle& style = ImGui::GetStyle();
	style.FrameBorderSize = 1.0f;

	
	const uint32_t W = ctx.MainRTResolutionX;
	const uint32_t H = ctx.MainRTResolutionY;

	const uint32_t PROFILER_WINDOW_PADDIG_X = 10;
	const uint32_t PROFILER_WINDOW_PADDIG_Y = 10;
	const uint32_t PROFILER_WINDOW_SIZE_X = 330;
	const uint32_t PROFILER_WINDOW_SIZE_Y = 400;
	const uint32_t PROFILER_WINDOW_POS_X = W - PROFILER_WINDOW_PADDIG_X - PROFILER_WINDOW_SIZE_X;
	const uint32_t PROFILER_WINDOW_POS_Y = PROFILER_WINDOW_PADDIG_Y;

	const uint32_t CONTROLS_WINDOW_POS_X = 10;
	const uint32_t CONTROLS_WINDOW_POS_Y = 10;
	const uint32_t CONTROLW_WINDOW_SIZE_X = 350;
	const uint32_t CONTROLW_WINDOW_SIZE_Y = 650;

	ImGui::SetNextWindowPos(ImVec2((float)PROFILER_WINDOW_POS_X, (float)PROFILER_WINDOW_POS_Y), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(PROFILER_WINDOW_SIZE_X, PROFILER_WINDOW_SIZE_Y), ImGuiCond_FirstUseEver);

	static bool bShowProfilerWindow = true;
	ImGui::Begin("PROFILER (F2)", &bShowProfilerWindow);

	ImGui::Text("Resolution : %ix%i", W, H);
	ImGui::Text("API        : %s", "DirectX 12");
	//ImGui::Text("GPU        : %s", );
	//ImGui::Text("CPU        : %s", m_systemInfo.mCPUName.c_str());
	//ImGui::Text("FPS        : %d (%.2f ms)", fps, frameTime_ms);


	this->RenderSceneUI();



	ImGui::End();
}

static std::string DumpCameraInfo(int index, const Camera& cam)
{
	const XMFLOAT3 pos = cam.GetPositionF();
	const float pitch  = cam.GetPitch();
	const float yaw    = cam.GetYaw();

	std::string info = std::string("[CAMERA INFO]\n")
		+ "mIndex_SelectedCamera=" + std::to_string(index) + "\n"
		+ "  Pos         : " + std::to_string(pos.x) + " " + std::to_string(pos.y) + " " + std::to_string(pos.z) + "\n"
		+ "  Yaw (Deg)   : " + std::to_string(yaw*RAD2DEG) + "\n"
		+ "  Pitch (Deg) : " + std::to_string(pitch*RAD2DEG) + "\n";
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
			case Light::EType::SPOT       : 
				l.GetGPUData(l.bCastingShadows ? &data.spot_casters[iGPUSpotShadow++]   : &data.spot_lights[iGPUSpot++]  ); 
				if (l.bCastingShadows)
				{
					data.shadowViews[iGPUSpotShadow - 1] = l.GetViewProjectionMatrix();
				}
				break;
			case Light::EType::POINT      : 
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

			case Light::EType::SPOT       :
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

			case Light::EType::POINT      : 
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

void Scene::PrepareSceneMeshRenderParams(FSceneView& SceneView) const
{
	SceneView.meshRenderCommands.clear();
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
}

void Scene::PrepareShadowMeshRenderParams(FSceneShadowView& SceneShadowView) const
{
	int iSpot  = 0;
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
			case Light::EType::SPOT       : 
			{
				FSceneShadowView::FShadowView& ShadowView = SceneShadowView.ShadowViews_Spot[iSpot++];
				ShadowView.matViewProj = l.GetViewProjectionMatrix();
				fnGatherMeshRenderParamsForLight(l, ShadowView);
			} break;
			case Light::EType::POINT      : 
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

}

FMaterialRepresentation::FMaterialRepresentation()
	: DiffuseColor(MATERIAL_UNINITIALIZED_VALUE, MATERIAL_UNINITIALIZED_VALUE, MATERIAL_UNINITIALIZED_VALUE)
	, EmissiveColor(MATERIAL_UNINITIALIZED_VALUE, MATERIAL_UNINITIALIZED_VALUE, MATERIAL_UNINITIALIZED_VALUE)
{}
