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

#include "AssetLoader.h"

#include "GPUMarker.h"

#include "Scene/Mesh.h"
#include "Scene/Material.h"
#include "Scene/Scene.h"

#include "../Renderer/Renderer.h"

#include "Libs/VQUtils/Source/Multithreading.h"
#include "Libs/VQUtils/Source/utils.h"
#include "Libs/VQUtils/Source/Image.h"
#include "Libs/VQUtils/Source/Timer.h"
#include "Libs/VQUtils/Source/Log.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

using namespace Assimp;
using namespace DirectX;

TaskID AssetLoader::GenerateModelLoadTaskID()
{
	static std::atomic<TaskID> LOAD_TASK_ID = 0;
	TaskID id = LOAD_TASK_ID.fetch_add(1);
	return id;
}

AssetLoader::AssetLoader(ThreadPool& WorkerThreads_Model, ThreadPool& WorkerThreads_Mesh, VQRenderer& renderer)
	: mWorkers_ModelLoad(WorkerThreads_Model)
	, mWorkers_MeshLoad(WorkerThreads_Mesh)
	, mRenderer(renderer)
{}

//----------------------------------------------------------------------------------------------------------------
// MODEL LOADER
//----------------------------------------------------------------------------------------------------------------
void AssetLoader::QueueModelLoad(GameObject* pObject, const std::string& ModelPath, const std::string& ModelName)
{
	const std::string FileExtension = DirectoryUtil::GetFileExtension(ModelPath);

	std::unique_lock<std::mutex> lk(mMtxQueue_ModelLoad);
	mModelLoadQueue.push({pObject, ModelPath, ModelName, AssetLoader::ImportModel });
}

AssetLoader::ModelLoadResults_t AssetLoader::StartLoadingModels(Scene* pScene)
{
	SCOPED_CPU_MARKER("AssetLoader::StartLoadingModels()");
	VQRenderer* pRenderer = &mRenderer;
	ModelLoadResults_t ModelLoadResults;

	if (mModelLoadQueue.empty())
	{
		Log::Warning("AssetLoader::StartLoadingModels(): no models to load");
		return ModelLoadResults;
	}

	// process model load queue
	std::unordered_map<std::string, std::shared_future<ModelID>> ModelLoadResultMap;
	std::unique_lock<std::mutex> lk(mMtxQueue_ModelLoad);
	do
	{
		SCOPED_CPU_MARKER("ProcessQueueItem");

		FModelLoadParams ModelLoadParams = std::move(mModelLoadQueue.front());
		const std::string& ModelPath = ModelLoadParams.ModelPath;
		mModelLoadQueue.pop();

		// queue unique model paths for loading
		std::shared_future<ModelID> modelLoadResult;
		if (mUniqueModelPaths.find(ModelPath) == mUniqueModelPaths.end())
		{
			mUniqueModelPaths.insert(ModelPath);

			// check whether Exit signal is given to the app before dispatching workers
			if (mWorkers_ModelLoad.IsExiting())
			{
				break;
			}

			// start loading the model
			modelLoadResult = std::move(mWorkers_ModelLoad.AddTask([=]()
			{
				SCOPED_CPU_MARKER_C("ModelLoadWorker", 0xFFDDAA00);
				return ModelLoadParams.pfnImportModel(pScene, this, pRenderer, ModelLoadParams.ModelPath, ModelLoadParams.ModelName);
			}));
			ModelLoadResultMap[ModelLoadParams.ModelPath] = modelLoadResult;
		}
		else
		{
			modelLoadResult = ModelLoadResultMap.at(ModelLoadParams.ModelPath);
		}

		ModelLoadResults.emplace(std::make_pair(ModelLoadParams.pObject, modelLoadResult));

	} while (!mModelLoadQueue.empty());

	mUniqueModelPaths.clear();
	return ModelLoadResults;
}

//----------------------------------------------------------------------------------------------------------------
// TEXTURE LOADER
//----------------------------------------------------------------------------------------------------------------
void AssetLoader::QueueTextureLoad(TaskID taskID, const FTextureLoadParams& TexLoadParam)
{
	FLoadTaskContext<FTextureLoadParams>& ctx = mLookup_TextureLoadContext[taskID];
	ctx.LoadQueue.push(TexLoadParam);
}

static AssetLoader::ECustomMapType DetermineCustomMapType(const std::string& FilePath)
{
	SCOPED_CPU_MARKER("DetermineCustomMapType()");
	AssetLoader::ECustomMapType t = AssetLoader::ECustomMapType::UNKNOWN;

	// parse @CustomMapFileName 
	std::string fileName = StrUtil::split(FilePath, { '/', '\\' }).back();
	StrUtil::MakeLowercase(fileName);

	// e.g. fileName=metal_tiles_02_2k_diffuse.png
	//      case: FileName has word 'metal' in it, but isn't actually a metalness texture
	// we're interested in this region: before extension, after last occurance of '_'
	//   fileName=metal_tiles_02_2k_diffuse.png
	//                              ^^^^^^^
	// 
	auto vTokens = StrUtil::split(fileName, { '.', '_' });
	if (vTokens.size() < 3) // we expect at least 3 tokens: MAT_NAME, TEXTYPE, EXTENSION
		return t; // not a proper texture type string token, such as procedural texture names

	const std::string& texTypeToken = vTokens[vTokens.size()-2];

	if (texTypeToken.empty())
		return t;

	auto itOccl = texTypeToken.find("occlu");
	auto itRghn = texTypeToken.find("roughness");
	auto itRgh  = texTypeToken.find("rough");
	auto itMtlc = texTypeToken.find("metallic");
	auto itMtln = texTypeToken.find("metalness");
	auto itMtl  = texTypeToken.find("metal");

	bool bContainsAO = itOccl != std::string::npos;
	bool bContainsRoughness = itRghn != std::string::npos || itRgh != std::string::npos;
	bool bCountainsMetalness = itMtlc != std::string::npos 
		|| itMtln != std::string::npos
		|| itMtl  != std::string::npos;

	bool bRoughMetal = false;
	bool bMetalRough = false;

	// determine type
	if (bContainsAO && bContainsRoughness && bCountainsMetalness)
	{
		// TODO: ensure order
		t = AssetLoader::ECustomMapType::OCCLUSION_ROUGHNESS_METALNESS;
	}
	else if (!bContainsAO && bContainsRoughness && bCountainsMetalness)
	{
		// determine rough/metal order
		auto& itMtl_valid = itMtlc != std::string::npos 
			? itMtlc 
			: (itMtln != std::string::npos ? itMtln : itMtl);
		auto& itRgh_valid = itRghn != std::string::npos 
			? itRghn 
			: itRgh;
		bRoughMetal = itRgh_valid < itMtl_valid;
		bMetalRough = itMtl_valid < itRgh_valid;

		assert(bRoughMetal || bMetalRough);
		if(bRoughMetal) t = AssetLoader::ECustomMapType::ROUGHNESS_METALNESS;
		if(bMetalRough) t = AssetLoader::ECustomMapType::METALNESS_ROUGHNESS;
	}

	return t;
}

AssetLoader::TextureLoadResults_t AssetLoader::StartLoadingTextures(TaskID taskID)
{
	SCOPED_CPU_MARKER("AssetLoader.StartLoadingTextures()");
	TextureLoadResults_t TextureLoadResults;
	FLoadTaskContext<FTextureLoadParams>& ctx = mLookup_TextureLoadContext.at(taskID);

	if (ctx.LoadQueue.empty())
	{
		Log::Warning("AssetLoader::StartLoadingTextures(taskID=%d): no Textures to load", taskID);
		return TextureLoadResults;
	}
	
	std::unordered_map<std::string, TextureID> Lookup_TextureLoadResult;

	// process texture load queue
	{
		SCOPED_CPU_MARKER("DispatchTextureWorkers");
		while (!ctx.LoadQueue.empty())
		{
			FTextureLoadParams TexLoadParams = std::move(ctx.LoadQueue.front());
			ctx.LoadQueue.pop();

			// handle duplicate texture load paths
			if (ctx.UniquePaths.find(TexLoadParams.TexturePath) == ctx.UniquePaths.end())
			{
				ctx.UniquePaths.insert(TexLoadParams.TexturePath);

				// determine whether we'll load file OR use a procedurally generated texture
				auto vPathTokens = StrUtil::split(TexLoadParams.TexturePath, '/');
				assert(!vPathTokens.empty());
				const bool bProceduralTexture = vPathTokens[0] == "Procedural";

				TextureID texID = INVALID_ID;
				TextureManager& mTextureManager = mRenderer.GetTextureManager();
				if (bProceduralTexture)
				{
					texID = mRenderer.GetProceduralTexture(VQRenderer::GetProceduralTextureEnumFromName(vPathTokens[1]));
				}
				else
				{
					FTextureRequest Request;
					Request.Name = DirectoryUtil::GetFileNameFromPath(TexLoadParams.TexturePath);
					Request.FilePath = TexLoadParams.TexturePath;
					Request.bGenerateMips = true;
					Request.bCPUReadback = false;
					Request.InitialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
					Request.SRVFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // Adjust per TexType

					const bool bCheckAlphaMask = (TexLoadParams.TexType == ETextureType::DIFFUSE) || TexLoadParams.TexType == ETextureType::ALPHA_MASK;
					texID = mTextureManager.CreateTexture(Request, bCheckAlphaMask);
				}

				// update results lookup for the shared textures (among different materials)
				Lookup_TextureLoadResult[TexLoadParams.TexturePath] = texID;

				// record load results
				TextureLoadResults.emplace(std::make_pair(TexLoadParams.MatID, FTextureLoadResult{ TexLoadParams.TexType, TexLoadParams.TexturePath, texID }));
			}

			// shared textures among materials
			else
			{
				TextureLoadResults.emplace(std::make_pair(TexLoadParams.MatID, FTextureLoadResult{ TexLoadParams.TexType, TexLoadParams.TexturePath, Lookup_TextureLoadResult.at(TexLoadParams.TexturePath) }));
			}
		}
	}

	ctx.UniquePaths.clear();

	mLookup_TextureLoadContext.erase(taskID);

	return std::move(TextureLoadResults);
}

static AssetLoader::ETextureType GetTextureType(aiTextureType aiType)
{
	switch (aiType)
	{
	case aiTextureType_NONE:      return AssetLoader::ETextureType::NUM_TEXTURE_TYPES; break;
	case aiTextureType_DIFFUSE:   return AssetLoader::ETextureType::DIFFUSE; break;
	case aiTextureType_SPECULAR:  return AssetLoader::ETextureType::SPECULAR; break;
	case aiTextureType_EMISSIVE:  return AssetLoader::ETextureType::EMISSIVE; break;
	case aiTextureType_HEIGHT:    return AssetLoader::ETextureType::HEIGHT; break;
	case aiTextureType_NORMALS:   return AssetLoader::ETextureType::NORMALS; break;
	case aiTextureType_OPACITY:   return AssetLoader::ETextureType::ALPHA_MASK; break;
	case aiTextureType_METALNESS: return AssetLoader::ETextureType::METALNESS; break;
	case aiTextureType_AMBIENT:   
		assert(false);
		break;
	case aiTextureType_SHININESS: break;
	case aiTextureType_DISPLACEMENT:
		break;
	case aiTextureType_LIGHTMAP:
		break;
	case aiTextureType_REFLECTION:
		break;
	case aiTextureType_BASE_COLOR:
		break;
	case aiTextureType_NORMAL_CAMERA:
		break;
	case aiTextureType_EMISSION_COLOR:
		assert(false);
		break;
	case aiTextureType_DIFFUSE_ROUGHNESS:
		break;
	case aiTextureType_AMBIENT_OCCLUSION: return AssetLoader::ETextureType::AMBIENT_OCCLUSION; 
	case aiTextureType_UNKNOWN:
		// packed textures are unknown type for assimp, hence the engine assumes occl/rough/metal packed texture type
		return AssetLoader::ETextureType::CUSTOM_MAP; break; 
		break;
	case _aiTextureType_Force32Bit:
		break;
	default:
		break;
	}
	return AssetLoader::ETextureType::NUM_TEXTURE_TYPES;
}

void AssetLoader::FMaterialTextureAssignments::DoAssignments(Scene* pScene, std::mutex& mtxTexturePaths, std::unordered_map<TextureID, std::string>& TexturePaths, VQRenderer* pRenderer)
{
	SCOPED_CPU_MARKER("FMaterialTextureAssignments.DoAssignments()");
	for (FMaterialTextureAssignment& assignment : mAssignments)
	{
		Material& mat = pScene->GetMaterial(assignment.matID);

		bool bFound = mTextureLoadResults.find(assignment.matID) != mTextureLoadResults.end();
		if (!bFound)
		{
			Log::Error("TextureLoadResutls for MatID=%d not found!", assignment.matID);
			continue;
		}

		UINT OcclRoughMtlMap_ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		auto pair_itBeginEnd = mTextureLoadResults.equal_range(assignment.matID);
		for (auto it = pair_itBeginEnd.first; it != pair_itBeginEnd.second; ++it)
		{
			const MaterialID& matID = it->first;
			FTextureLoadResult& result = it->second;

			const TextureID loadedTextureID = result.TexID;
			pRenderer->GetTextureManager().WaitForTexture(loadedTextureID);

			switch (result.type)
			{
			case DIFFUSE           : mat.TexDiffuseMap          = loadedTextureID; break;
			case NORMALS           : mat.TexNormalMap           = loadedTextureID; break;
			case ALPHA_MASK        : mat.TexAlphaMaskMap        = loadedTextureID; break;
			case EMISSIVE          : mat.TexEmissiveMap         = loadedTextureID; break;
			case METALNESS         : mat.TexMetallicMap         = loadedTextureID; mat.metalness = 1.0f; break;
			case ROUGHNESS         : mat.TexRoughnessMap        = loadedTextureID; mat.roughness = 1.0f; break;
			case HEIGHT            : mat.TexHeightMap           = loadedTextureID; break;
			case AMBIENT_OCCLUSION : mat.TexAmbientOcclusionMap = loadedTextureID; break;
			case SPECULAR          : assert(false); /*mat.TexSpecularMap  = result.texLoadResult.get();*/ break;
			case CUSTOM_MAP        :
			{
				const ECustomMapType customMapType = DetermineCustomMapType(result.TexturePath);
				switch (customMapType)
				{
				case OCCLUSION_ROUGHNESS_METALNESS:
					OcclRoughMtlMap_ComponentMapping; // leave as is
					mat.TexOcclusionRoughnessMetalnessMap = result.TexID;
					mat.metalness = 1.0f;
					mat.roughness = 1.0f;
					break;
				case METALNESS_ROUGHNESS:
					// turns out: even the file is named in reverse order, the common thing to do is to store
					//            roughness in G and metalness in B channels. Hence we're not handling this case 
					//            here.
#if 0 
					OcclRoughMtlMap_ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
						  D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1
						, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2
						, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1
						, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0
					);
					mat.TexOcclusionRoughnessMetalnessMap = result.TexID;
					break;
#endif
				case ROUGHNESS_METALNESS:
					OcclRoughMtlMap_ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
						D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1
						, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1
						, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2
						, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0
					);
					mat.TexOcclusionRoughnessMetalnessMap = result.TexID;
					mat.metalness = 1.0f;
					mat.roughness = 1.0f;
					break;
				default:
				case UNKNOWN:
					//Log::Warning("Unknown custom map (%s) type for material MatID=%d!", result.TexturePath.c_str(), assignment.matID);
					DetermineCustomMapType(result.TexturePath);
					break;
				}
			} break;
			default:
				Log::Warning("TODO");
				break;
			}
			
			// store the loaded texture path if we have a successful texture creation
			if (loadedTextureID != INVALID_ID)
			{
				std::lock_guard<std::mutex> lk(mtxTexturePaths);
				TexturePaths[loadedTextureID] = result.TexturePath;
			}
		}

		if (mat.SRVMaterialMaps == INVALID_ID)
		{
			mat.SRVMaterialMaps = pRenderer->AllocateSRV(NUM_MATERIAL_TEXTURE_MAP_BINDINGS - 1);
			mat.SRVHeightMap = pRenderer->AllocateSRV(1);
		}
		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::ALBEDO, mat.TexDiffuseMap);
		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::NORMALS, mat.TexNormalMap);
		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::EMISSIVE, mat.TexEmissiveMap);
		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::ALPHA_MASK, mat.TexAlphaMaskMap);
		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::METALLIC, mat.TexMetallicMap);
		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::ROUGHNESS, mat.TexRoughnessMap);
		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::OCCLUSION_ROUGHNESS_METALNESS, mat.TexOcclusionRoughnessMetalnessMap, OcclRoughMtlMap_ComponentMapping);
		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::AMBIENT_OCCLUSION, mat.TexAmbientOcclusionMap);
		pRenderer->InitializeSRV(mat.SRVHeightMap, 0, mat.TexHeightMap);
	}
}

//----------------------------------------------------------------------------------------------------------------
// ASSIMP HELPER FUNCTIONS
//----------------------------------------------------------------------------------------------------------------
static std::vector<AssetLoader::FTextureLoadParams> GenerateTextureLoadParams(
	  const aiMaterial*  pMaterial
	, MaterialID         matID
	, aiTextureType      type
	, const std::string& modelDirectory
)
{
	SCOPED_CPU_MARKER("GenerateTextureLoadParams()");
	std::vector<AssetLoader::FTextureLoadParams> TexLoadParams;
	for (unsigned int i = 0; i < pMaterial->GetTextureCount(type); ++i)
	{
		aiString str;
		pMaterial->GetTexture(type, i, &str);
		std::string path = str.C_Str();

		AssetLoader::FTextureLoadParams params = {};
		params.TexturePath = modelDirectory + path;
		params.MatID = matID;
		params.TexType = GetTextureType(type);
		TexLoadParams.push_back(params);
	}
	return TexLoadParams;
}

static Mesh ProcessAssimpMesh(
	VQRenderer*          pRenderer
	, const aiMesh*      pMesh
	, const std::string& ModelName
)
{
	SCOPED_CPU_MARKER("ProcessAssimpMesh()");
	GeometryData< FVertexWithNormalAndTangent, unsigned> GeometryData(1);
	std::vector<FVertexWithNormalAndTangent>& Vertices = GeometryData.LODVertices[0];
	std::vector<unsigned>& Indices = GeometryData.LODIndices[0];

	{
		SCOPED_CPU_MARKER("MemAlloc");
		size_t NumIndices = 0;
		for (unsigned int i = 0; i < pMesh->mNumFaces; i++) NumIndices += pMesh->mFaces[i].mNumIndices;
		Indices.resize(NumIndices);
		Vertices.resize(pMesh->mNumVertices);
	}

	{
		SCOPED_CPU_MARKER("Verts");
		// Walk through each of the mesh's vertices
		for (unsigned int i = 0; i < pMesh->mNumVertices; i++)
		{
			FVertexWithNormalAndTangent& Vert = Vertices[i];

			// POSITIONS
			Vert.position[0] = pMesh->mVertices[i].x;
			Vert.position[1] = pMesh->mVertices[i].y;
			Vert.position[2] = pMesh->mVertices[i].z;

			// TEXTURE COORDINATES
			// a vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't 
			// use models where a vertex can have multiple texture coordinates so we always take the first set (0).
			Vert.uv[0] = pMesh->mTextureCoords[0] ? pMesh->mTextureCoords[0][i].x : 0;
			Vert.uv[1] = pMesh->mTextureCoords[0] ? pMesh->mTextureCoords[0][i].y : 0;

			// NORMALS
			if (pMesh->mNormals)
			{
				Vert.normal[0] = pMesh->mNormals[i].x;
				Vert.normal[1] = pMesh->mNormals[i].y;
				Vert.normal[2] = pMesh->mNormals[i].z;
			}

			// TANGENT
			if (pMesh->mTangents)
			{
				Vert.tangent[0] = pMesh->mTangents[i].x;
				Vert.tangent[1] = pMesh->mTangents[i].y;
				Vert.tangent[2] = pMesh->mTangents[i].z;
			}

			// BITANGENT ( NOT USED )
			// Vert.bitangent = XMFLOAT3(
			// 	mesh->mBitangents[i].x,
			// 	mesh->mBitangents[i].y,
			// 	mesh->mBitangents[i].z
			// );
		}
	}

	// now walk through each of the mesh's faces (a face is a mesh its triangle) and retrieve the corresponding vertex indices.
	{
		SCOPED_CPU_MARKER("Indices");
		size_t iIdx = 0;
		for (unsigned int i = 0; i < pMesh->mNumFaces; i++)
		{
			aiFace face = pMesh->mFaces[i];
			memcpy(&Indices[iIdx], face.mIndices, sizeof(unsigned) * face.mNumIndices);
			iIdx += face.mNumIndices;
		}
	}

	return Mesh(nullptr, std::move(GeometryData), ModelName);
}

static std::string CreateUniqueMaterialName(const aiMaterial* material, size_t iMat, const std::string& modelDirectory)
{
	std::string uniqueMatName;
	aiString matName;
	if (aiReturn_SUCCESS != material->Get(AI_MATKEY_NAME, matName))
	{ // material doesn't have a name, use generic name Material#
		matName = std::string("Material#") + std::to_string(iMat);
	}

	// Data/Models/%MODEL_NAME%/... : index 2 will give model name
	auto vFolders = DirectoryUtil::GetFlattenedFolderHierarchy(modelDirectory);
	assert(vFolders.size() > 2);
	const std::string ModelFolderName = vFolders[2];
	uniqueMatName = matName.C_Str();

	// modelDirectory = "Data/Models/%MODEL_NAME%/"
	// Materials use the following unique naming: %MODEL_NAME%/%MATERIAL_NAME%
	uniqueMatName = ModelFolderName + "/" + uniqueMatName;

	return uniqueMatName;
}

static void QueueUpTextureLoadRequests(
	AssetLoader* pAssetLoader,
	const std::string& modelDirectory,
	const aiMaterial* material,
	MaterialID matID,
	TaskID taskID
)
{
	SCOPED_CPU_MARKER("QueueUpTextureLoadRequests");

	// get texture paths to load
	std::vector<AssetLoader::FTextureLoadParams> diffuseMaps;
	std::vector<AssetLoader::FTextureLoadParams> specularMaps;
	std::vector<AssetLoader::FTextureLoadParams> normalMaps;
	std::vector<AssetLoader::FTextureLoadParams> heightMaps;
	std::vector<AssetLoader::FTextureLoadParams> alphaMaps;
	std::vector<AssetLoader::FTextureLoadParams> emissiveMaps;
	std::vector<AssetLoader::FTextureLoadParams> occlRoughMetlMaps;
	std::vector<AssetLoader::FTextureLoadParams> aoMaps;
	{
		diffuseMaps = GenerateTextureLoadParams(material, matID, aiTextureType_DIFFUSE, modelDirectory);
		specularMaps = GenerateTextureLoadParams(material, matID, aiTextureType_SPECULAR, modelDirectory);
		normalMaps = GenerateTextureLoadParams(material, matID, aiTextureType_NORMALS, modelDirectory);
		heightMaps = GenerateTextureLoadParams(material, matID, aiTextureType_HEIGHT, modelDirectory);
		alphaMaps = GenerateTextureLoadParams(material, matID, aiTextureType_OPACITY, modelDirectory);
		emissiveMaps = GenerateTextureLoadParams(material, matID, aiTextureType_EMISSIVE, modelDirectory);
		occlRoughMetlMaps = GenerateTextureLoadParams(material, matID, aiTextureType_UNKNOWN, modelDirectory);
		aoMaps = GenerateTextureLoadParams(material, matID, aiTextureType_AMBIENT_OCCLUSION, modelDirectory);
	}


	// queue texture load
	std::array<decltype(diffuseMaps)*, 8> TexLoadParams = { &diffuseMaps, &specularMaps, &normalMaps, &heightMaps, &alphaMaps, &emissiveMaps, &occlRoughMetlMaps, &aoMaps };
	int iTexType = 0;
	for (const auto* pvLoadParams : TexLoadParams)
	{
		int iTex = 0;
		for (const AssetLoader::FTextureLoadParams& param : *pvLoadParams)
		{
			pAssetLoader->QueueTextureLoad(taskID, param);
			++iTex;
		}
		++iTexType;
	}
}

static MaterialID ProcessAssimpMaterial(
	  const aiMaterial* material
	, size_t aiMatIndex
	, const std::string& modelDirectory
	, Scene* pScene
	, AssetLoader* pAssetLoader
	, AssetLoader::FMaterialTextureAssignments& MaterialTextureAssignments
	, TaskID taskID
)
{
	// Create new Material, every material assumed to have a name 
	const std::string matName = CreateUniqueMaterialName(material, aiMatIndex, modelDirectory);
	MaterialID matID = pScene->CreateMaterial(matName);
	Material& mat = pScene->GetMaterial(matID);

	QueueUpTextureLoadRequests(pAssetLoader, modelDirectory, material, matID, taskID);
	MaterialTextureAssignments.mAssignments.push_back({ matID });

	// MATERIAL - http://assimp.sourceforge.net/lib_html/materials.html
	aiColor3D color(0.f, 0.f, 0.f);
	if (aiReturn_SUCCESS == material->Get(AI_MATKEY_COLOR_DIFFUSE, color))
	{
		mat.diffuse = XMFLOAT3(color.r, color.g, color.b);
	}

	aiColor3D specular(0.f, 0.f, 0.f);
	if (aiReturn_SUCCESS == material->Get(AI_MATKEY_COLOR_SPECULAR, specular))
	{
		mat.specular = XMFLOAT3(specular.r, specular.g, specular.b);
	}

	aiColor3D transparent(0.0f, 0.0f, 0.0f);
	if (aiReturn_SUCCESS == material->Get(AI_MATKEY_COLOR_TRANSPARENT, transparent))
	{	// Defines the transparent color of the material, this is the color to be multiplied 
		// with the color of translucent light to construct the final 'destination color' 
		// for a particular position in the screen buffer. T
		//
		int a = 5;
	}

	float opacity = 0.0f;
	if (aiReturn_SUCCESS == material->Get(AI_MATKEY_OPACITY, opacity))
	{
		mat.alpha = opacity;
	}

	float shininess = 0.0f;
	if (aiReturn_SUCCESS == material->Get(AI_MATKEY_SHININESS, shininess))
	{
		// Phong Shininess -> Beckmann BRDF Roughness conversion
		//
		// https://simonstechblog.blogspot.com/2011/12/microfacet-brdf.html
		// https://computergraphics.stackexchange.com/questions/1515/what-is-the-accepted-method-of-converting-shininess-to-roughness-and-vice-versa
		//
		mat.roughness = sqrtf(2.0f / (2.0f + shininess));
	}

	aiColor3D emissiveIntensity(0.0f, 0.0f, 0.0f);
	if (aiReturn_SUCCESS == material->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveIntensity))
	{
		mat.emissiveIntensity = emissiveIntensity.r;
	}

	// other material keys to consider
	//
	// AI_MATKEY_TWOSIDED
	// AI_MATKEY_ENABLE_WIREFRAME
	// AI_MATKEY_BLEND_FUNC
	// AI_MATKEY_BUMPSCALING

	return matID;
}


#define THREADED_ASSIMP_MESH_LOAD 1
static Model::Data ProcessAssimpNode(
	const std::string& ModelName,
	aiNode* const      pNode,
	const aiScene*     pAiScene,
	const std::string& modelDirectory,
	AssetLoader*       pAssetLoader,
	Scene*             pScene,
	VQRenderer*        pRenderer,
	AssetLoader::FMaterialTextureAssignments& MaterialTextureAssignments,
	TaskID                                    taskID
)
{
	SCOPED_CPU_MARKER("ProcessAssimpNode()");
	Model::Data modelData;
	
	ThreadPool& WorkerThreadPool = pAssetLoader->mWorkers_MeshLoad;

	std::vector<Mesh> NodeMeshData(pNode->mNumMeshes);
	std::vector<MaterialID> NodeMaterialIDs(pNode->mNumMeshes, INVALID_ID);
	const bool bThreaded = THREADED_ASSIMP_MESH_LOAD && NodeMeshData.size() > 1;

	if (bThreaded)
	{
		constexpr size_t NumMinWorkItemsPerThread = 1;
		const size_t NumWorkItems = NodeMeshData.size();
		const size_t NumThreadsToUse = CalculateNumThreadsToUse(NumWorkItems, WorkerThreadPool.GetThreadPoolSize() + 1, NumMinWorkItemsPerThread);
		const size_t NumWorkerThreadsToUse = NumThreadsToUse - 1;
		auto vRanges = PartitionWorkItemsIntoRanges(NumWorkItems, NumThreadsToUse);

		// synchronization
		std::atomic<int> WorkerCounter(static_cast<int>(std::min(vRanges.size()-1, NumWorkerThreadsToUse)));
		EventSignal WorkerSignal;
		{
			SCOPED_CPU_MARKER("DispatchMeshWorkers");
			for (size_t iRange = 1; iRange < vRanges.size(); ++iRange)
			{
				WorkerThreadPool.AddTask([=, &NodeMeshData, &WorkerSignal, &WorkerCounter]()
				{
					SCOPED_CPU_MARKER_C("MeshWorker", 0xFF0000FF);
					for (size_t i = vRanges[iRange].first; i <= vRanges[iRange].second; ++i)
					{
						aiMesh* pAiMesh = pAiScene->mMeshes[pNode->mMeshes[i]];
						NodeMeshData[i] = ProcessAssimpMesh(pRenderer, pAiMesh, ModelName);
					}
					WorkerCounter.fetch_sub(1);
					WorkerSignal.NotifyOne();
				});
			}
		}
		{
			SCOPED_CPU_MARKER("ThisThread_Mesh");
			for (size_t i = vRanges[0].first; i <= vRanges[0].second; ++i)
			{
				aiMesh* pAiMesh = pAiScene->mMeshes[pNode->mMeshes[i]];
				NodeMeshData[i] = ProcessAssimpMesh(pRenderer, pAiMesh, ModelName);
			}
		}
		{
			SCOPED_CPU_MARKER("ProcessMaterials");
			for (unsigned int i = 0; i < pNode->mNumMeshes; i++)
			{
				aiMesh* pAiMesh = pAiScene->mMeshes[pNode->mMeshes[i]];
				aiMaterial* material = pAiScene->mMaterials[pAiMesh->mMaterialIndex];
				NodeMaterialIDs[i] = ProcessAssimpMaterial(material, pAiMesh->mMaterialIndex, modelDirectory, pScene, pAssetLoader, MaterialTextureAssignments, taskID);
			}
		}
		{
			SCOPED_CPU_MARKER_C("WAIT_MESH_WORKERS", 0xFFAA0000);
			WorkerSignal.Wait([&]() { return WorkerCounter.load() == 0; });
		}
		{
			SCOPED_CPU_MARKER("UpdateSceneData");
			for (unsigned int i = 0; i < pNode->mNumMeshes; i++)
			{	
				aiMesh* pAiMesh = pAiScene->mMeshes[pNode->mMeshes[i]];
				aiMaterial* material = pAiScene->mMaterials[pAiMesh->mMaterialIndex];

				MeshID id = pScene->AddMesh(std::move(NodeMeshData[i]));
				MaterialID matID = NodeMaterialIDs[i];
				Material& mat = pScene->GetMaterial(matID);

				modelData.AddMesh(id, matID, Model::Data::EMeshType::OPAQUE_MESH);
			} // for: NumMeshes
		}
	}
	else
	{
		for (unsigned int i = 0; i < pNode->mNumMeshes; i++)
		{	// process all the node's meshes (if any)
			aiMesh* pAiMesh = pAiScene->mMeshes[pNode->mMeshes[i]];
			aiMaterial* material = pAiScene->mMaterials[pAiMesh->mMaterialIndex];
			Mesh& mesh = NodeMeshData[i];

			// generate data
			MaterialID matID = ProcessAssimpMaterial(material, pAiMesh->mMaterialIndex, modelDirectory, pScene, pAssetLoader, MaterialTextureAssignments, taskID);
			mesh = ProcessAssimpMesh(pRenderer, pAiMesh, ModelName);

			MeshID id = pScene->AddMesh(std::move(mesh));
			Material& mat = pScene->GetMaterial(matID);
			modelData.AddMesh(id, matID, Model::Data::EMeshType::OPAQUE_MESH);
		} // for: NumMeshes
	}


	{
		SCOPED_CPU_MARKER("Children");
		for (unsigned int i = 0; i < pNode->mNumChildren; i++)
		{	// then do the same for each of its children
			Model::Data childModelData = ProcessAssimpNode(ModelName, pNode->mChildren[i], pAiScene, modelDirectory, pAssetLoader, pScene, pRenderer, MaterialTextureAssignments, taskID);
			const std::vector<std::pair<MeshID, MaterialID>>& ChildMeshes = childModelData.GetMeshMaterialIDPairs(Model::Data::EMeshType::OPAQUE_MESH);

			std::copy(ChildMeshes.begin(), ChildMeshes.end(), std::back_inserter(modelData.GetMeshMaterialIDPairs(Model::Data::EMeshType::OPAQUE_MESH)));
			std::unordered_set<MaterialID>& childMats = childModelData.GetMaterials();
			modelData.GetMaterials().insert(childMats.begin(), childMats.end());
		} // for: NumChildren
	}

	return modelData;
}


//----------------------------------------------------------------------------------------------------------------
// IMPORT MODEL FUNCTION FOR WORKER THREADS
//----------------------------------------------------------------------------------------------------------------
ModelID AssetLoader::ImportModel(Scene* pScene, AssetLoader* pAssetLoader, VQRenderer* pRenderer, const std::string& objFilePath, std::string ModelName)
{
	SCOPED_CPU_MARKER("AssetLoader::ImportModel()");
	constexpr auto ASSIMP_LOAD_FLAGS
		= aiProcess_Triangulate
		| aiProcess_CalcTangentSpace
		| aiProcess_MakeLeftHanded
		| aiProcess_FlipUVs
		| aiProcess_FlipWindingOrder
		//| aiProcess_TransformUVCoords 
		//| aiProcess_FixInfacingNormals
		| aiProcess_JoinIdenticalVertices
		| aiProcess_GenSmoothNormals;


	TaskID taskID = GenerateModelLoadTaskID();
	//-----------------------------------------------
	const std::string modelDirectory = DirectoryUtil::GetFolderPath(objFilePath);

	Log::Info("ImportModel: %s - %s", ModelName.c_str(), objFilePath.c_str());
	Timer t;
	t.Start();

	// Import Assimp Scene
	Importer* importer = new Importer();
	const aiScene* pAiScene = nullptr;
	{
		SCOPED_CPU_MARKER("ReadFile()");
		pAiScene = importer->ReadFile(objFilePath, ASSIMP_LOAD_FLAGS);
	}

	if (!pAiScene || pAiScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !pAiScene->mRootNode)
	{
		Log::Error("Assimp error: %s", importer->GetErrorString());
		return INVALID_ID;
	}
	t.Tick(); float fTimeReadFile = t.DeltaTime();
	Log::Info("   [%.2fs] ReadFile=%s ", fTimeReadFile, objFilePath.c_str());

	// parse scene and initialize model data
	FMaterialTextureAssignments MaterialTextureAssignments;
	Model::Data data = ProcessAssimpNode(ModelName, pAiScene->mRootNode, pAiScene, modelDirectory, pAssetLoader, pScene, pRenderer, MaterialTextureAssignments, taskID);

	if (!MaterialTextureAssignments.mAssignments.empty()) // dispatch texture workers
		MaterialTextureAssignments.mTextureLoadResults = pAssetLoader->StartLoadingTextures(taskID);

	// cache the imported model in Scene
	ModelID mID = pScene->CreateModel();
	Model& model = pScene->GetModel(mID);
	model = Model(objFilePath, ModelName, std::move(data));
	
	// async clear, we don't need to block this thread for cleaning up.
	// Don't use ModelLoad workers because app state changes from Loading
	// into Simulating uses ModelLoad worker's task count.
	pAssetLoader->mWorkers_MeshLoad.AddTask([=]()
	{
		SCOPED_CPU_MARKER("CleanUpImporter");
		delete importer;
	});

	// assign TextureIDs to the materials;
	MaterialTextureAssignments.DoAssignments(pScene, pScene->mMtxTexturePaths,  pScene->mTexturePaths, pRenderer);

	// Create buffers for loaded meshes
	pRenderer->WaitHeapsInitialized();
	for (const auto& [meshID, matID] : model.mData.GetMeshMaterialIDPairs(Model::Data::EMeshType::OPAQUE_MESH))
	{
		Mesh& mesh = pScene->GetMesh(meshID);
		mesh.CreateBuffers(pRenderer);
	}
	//pObject->mModelID = modelID;
	

	pRenderer->UploadVertexAndIndexBufferHeaps();

	t.Stop();
	Log::Info("   [%.2fs] Loaded Model '%s': %d meshes, %d materials"
		, fTimeReadFile + t.DeltaTime()
		, ModelName.c_str()
		, model.mData.GetNumMeshesOfAllTypes()
		, model.mData.GetMaterials().size()
	);

	// TODO: post process

	return mID;
}

