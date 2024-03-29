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

AssetLoader::AssetLoader(ThreadPool& WorkerThreads_Model, ThreadPool& WorkerThreads_Texture, VQRenderer& renderer)
	: mWorkers_ModelLoad(WorkerThreads_Model)
	, mWorkers_TextureLoad(WorkerThreads_Texture)
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
		FModelLoadParams ModelLoadParams = std::move(mModelLoadQueue.front());
		const std::string& ModelPath = ModelLoadParams.ModelPath;
		mModelLoadQueue.pop();

		// queue unique model paths for loading
		std::shared_future<ModelID> modelLoadResult;
		if (mUniqueModelPaths.find(ModelPath) == mUniqueModelPaths.end())
		{
			mUniqueModelPaths.insert(ModelPath);

			// check whether Exit signal is given to the app before dispatching workers
			if (mWorkers_ModelLoad.IsExiting() || mWorkers_TextureLoad.IsExiting())
			{
				break;
			}

			// start loading the model
			modelLoadResult = std::move(mWorkers_ModelLoad.AddTask([=]()
			{
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

	const std::string texTypeToken = vTokens[vTokens.size()-2];

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
	TextureLoadResults_t TextureLoadResults;
	FLoadTaskContext<FTextureLoadParams>& ctx = mLookup_TextureLoadContext.at(taskID);

	if (ctx.LoadQueue.empty())
	{
		Log::Warning("AssetLoader::StartLoadingTextures(taskID=%d): no Textures to load", taskID);
		return TextureLoadResults;
	}
	
	std::unordered_map<std::string, std::shared_future<TextureID>> Lookup_TextureLoadResult;

	// process model load queue
	do
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

			EProceduralTextures ProcTex = bProceduralTexture 
				? VQRenderer::GetProceduralTextureEnumFromName(vPathTokens[1])
				: EProceduralTextures::NUM_PROCEDURAL_TEXTURES;

			// check whether Exit signal is given to the app before dispatching workers
			if (mWorkers_TextureLoad.IsExiting())
			{
				break;
			}

			// dispatch worker thread
			std::shared_future<TextureID> texLoadResult = std::move(mWorkers_TextureLoad.AddTask([this, TexLoadParams, ProcTex]()
			{
				constexpr bool GENERATE_MIPS = true;
				const bool IS_PROCEDURAL = ProcTex != EProceduralTextures::NUM_PROCEDURAL_TEXTURES;
				if (IS_PROCEDURAL)
				{
					return mRenderer.GetProceduralTexture(ProcTex);
				}

				return mRenderer.CreateTextureFromFile(TexLoadParams.TexturePath.c_str(), GENERATE_MIPS);
			}));

			// update results lookup for the shared textures (among different materials)
			Lookup_TextureLoadResult[TexLoadParams.TexturePath] = texLoadResult;

			// record load results
			TextureLoadResults.emplace(std::make_pair(TexLoadParams.MatID, FTextureLoadResult{ TexLoadParams.TexType, TexLoadParams.TexturePath, std::move(texLoadResult) }));
		}
		// shared textures among materials
		else
		{
			TextureLoadResults.emplace(std::make_pair(TexLoadParams.MatID, FTextureLoadResult{ TexLoadParams.TexType, TexLoadParams.TexturePath, Lookup_TextureLoadResult.at(TexLoadParams.TexturePath) }));
		}
	} while (!ctx.LoadQueue.empty());

	ctx.UniquePaths.clear();

	// Currently mRenderer.CreateTextureFromFile() starts the texture uploads
	///mRenderer.StartTextureUploads();

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

void AssetLoader::FMaterialTextureAssignments::DoAssignments(Scene* pScene, VQRenderer* pRenderer)
{
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
			
			if (mWorkersThreads.IsExiting())
				break;

			assert(result.texLoadResult.valid());

			result.texLoadResult.wait();
			switch (result.type)
			{
			case DIFFUSE           : mat.TexDiffuseMap   = result.texLoadResult.get(); break;
			case NORMALS           : mat.TexNormalMap    = result.texLoadResult.get(); break;
			case ALPHA_MASK        : mat.TexAlphaMaskMap = result.texLoadResult.get(); break;
			case EMISSIVE          : mat.TexEmissiveMap  = result.texLoadResult.get(); break;
			case METALNESS         : mat.TexMetallicMap  = result.texLoadResult.get(); break;
			case ROUGHNESS         : mat.TexRoughnessMap = result.texLoadResult.get(); break;
			case SPECULAR          : assert(false); /*mat.TexSpecularMap  = result.texLoadResult.get();*/ break;
			case HEIGHT            : mat.TexHeightMap    = result.texLoadResult.get(); break;
			case AMBIENT_OCCLUSION : mat.TexAmbientOcclusionMap = result.texLoadResult.get(); break;
			case CUSTOM_MAP        :
			{
				const ECustomMapType customMapType = DetermineCustomMapType(result.TexturePath);
				switch (customMapType)
				{
				case OCCLUSION_ROUGHNESS_METALNESS:
					OcclRoughMtlMap_ComponentMapping; // leave as is
					mat.TexOcclusionRoughnessMetalnessMap = result.texLoadResult.get();
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
					mat.TexOcclusionRoughnessMetalnessMap = result.texLoadResult.get();
					break;
#endif
				case ROUGHNESS_METALNESS:
					OcclRoughMtlMap_ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
						D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1
						, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1
						, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2
						, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0
					);
					mat.TexOcclusionRoughnessMetalnessMap = result.texLoadResult.get();
					break;
				default:
				case UNKNOWN:
					Log::Warning("Unknown custom map (%s) type for material MatID=%d!", result.TexturePath.c_str(), assignment.matID);
					DetermineCustomMapType(result.TexturePath);
					break;
				}
			} break;
			default:
				Log::Warning("TODO");
				break;
			}
		}

		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::ALBEDO, mat.TexDiffuseMap);
		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::NORMALS, mat.TexNormalMap);
		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::EMISSIVE, mat.TexEmissiveMap);
		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::ALPHA_MASK, mat.TexAlphaMaskMap);
		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::METALLIC, mat.TexMetallicMap);
		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::ROUGHNESS, mat.TexRoughnessMap);
		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::OCCLUSION_ROUGHNESS_METALNESS, mat.TexOcclusionRoughnessMetalnessMap, OcclRoughMtlMap_ComponentMapping);
		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::AMBIENT_OCCLUSION, mat.TexAmbientOcclusionMap);
	}
}

void AssetLoader::FMaterialTextureAssignments::WaitForTextureLoads()
{
	for (auto it = mTextureLoadResults.begin(); it != mTextureLoadResults.end(); ++it)
	{
		const MaterialID& matID = it->first;
		const FTextureLoadResult& result = it->second;
		assert(result.texLoadResult.valid());

		if (mWorkersThreads.IsExiting())
			break;

		result.texLoadResult.wait();
	}
}


//----------------------------------------------------------------------------------------------------------------
// ASSIMP HELPER FUNCTIONS
//----------------------------------------------------------------------------------------------------------------
static std::vector<AssetLoader::FTextureLoadParams> GenerateTextureLoadParams(
	  aiMaterial*        pMaterial
	, MaterialID         matID
	, aiTextureType      type
	, const std::string& modelDirectory
)
{
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
	, aiMesh*            pMesh
	, const aiScene*     pScene
	, const std::string& ModelName
)
{
	std::vector<FVertexWithNormalAndTangent> Vertices;
	std::vector<unsigned> Indices;

	// Walk through each of the mesh's vertices
	for (unsigned int i = 0; i < pMesh->mNumVertices; i++)
	{
		FVertexWithNormalAndTangent Vert;

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
		Vertices.push_back(Vert);
	}

	// now walk through each of the mesh's faces (a face is a mesh its triangle) and retrieve the corresponding vertex indices.
	for (unsigned int i = 0; i < pMesh->mNumFaces; i++)
	{
		aiFace face = pMesh->mFaces[i];
		// retrieve all indices of the face and store them in the indices vector
		for (unsigned int j = 0; j < face.mNumIndices; j++)
			Indices.push_back(face.mIndices[j]);
	}

	return Mesh(pRenderer, Vertices, Indices, ModelName);;
}

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
	Model::Data modelData;

	for (unsigned int i = 0; i < pNode->mNumMeshes; i++)
	{	// process all the node's meshes (if any)
		aiMesh* pAiMesh = pAiScene->mMeshes[pNode->mMeshes[i]];

		// MATERIAL - http://assimp.sourceforge.net/lib_html/materials.html
		aiMaterial* material = pAiScene->mMaterials[pAiMesh->mMaterialIndex];

		// Every material assumed to have a name 
		std::string uniqueMatName;
		{
			aiString matName;
			if (aiReturn_SUCCESS != material->Get(AI_MATKEY_NAME, matName))
			// material doesn't have a name, use generic name Material#
			{
				matName = std::string("Material#") + std::to_string(pAiMesh->mMaterialIndex);
			}

			// Data/Models/%MODEL_NAME%/... : index 2 will give model name
			auto vFolders = DirectoryUtil::GetFlattenedFolderHierarchy(modelDirectory);
			assert(vFolders.size() > 2);
			const std::string ModelFolderName = vFolders[2];
			uniqueMatName = matName.C_Str();
			// modelDirectory = "Data/Models/%MODEL_NAME%/"
			// Materials use the following unique naming: %MODEL_NAME%/%MATERIAL_NAME%
			uniqueMatName = ModelFolderName + "/" + uniqueMatName;
		}

		// Create new Material
		MaterialID matID = pScene->CreateMaterial(uniqueMatName);
		Material& mat = pScene->GetMaterial(matID);

		// get texture paths to load
		AssetLoader::FMaterialTextureAssignment MatTexAssignment = {};
		std::vector<AssetLoader::FTextureLoadParams> diffuseMaps  = GenerateTextureLoadParams(material, matID, aiTextureType_DIFFUSE , modelDirectory);
		std::vector<AssetLoader::FTextureLoadParams> specularMaps = GenerateTextureLoadParams(material, matID, aiTextureType_SPECULAR, modelDirectory);
		std::vector<AssetLoader::FTextureLoadParams> normalMaps   = GenerateTextureLoadParams(material, matID, aiTextureType_NORMALS , modelDirectory);
		std::vector<AssetLoader::FTextureLoadParams> heightMaps   = GenerateTextureLoadParams(material, matID, aiTextureType_HEIGHT  , modelDirectory);
		std::vector<AssetLoader::FTextureLoadParams> alphaMaps    = GenerateTextureLoadParams(material, matID, aiTextureType_OPACITY , modelDirectory);
		std::vector<AssetLoader::FTextureLoadParams> emissiveMaps = GenerateTextureLoadParams(material, matID, aiTextureType_EMISSIVE, modelDirectory);
		std::vector<AssetLoader::FTextureLoadParams> occlRoughMetlMaps = GenerateTextureLoadParams(material, matID, aiTextureType_UNKNOWN, modelDirectory);
		std::vector<AssetLoader::FTextureLoadParams> aoMaps = GenerateTextureLoadParams(material, matID, aiTextureType_AMBIENT_OCCLUSION, modelDirectory);
		
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


		// unflatten the material texture assignments
		MatTexAssignment.matID = matID;
		MaterialTextureAssignments.mAssignments.push_back(std::move(MatTexAssignment));

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
		
		Mesh mesh = ProcessAssimpMesh(pRenderer, pAiMesh, pAiScene, ModelName);
		MeshID id = pScene->AddMesh(std::move(mesh));
		modelData.mOpaueMeshIDs.push_back(id);
		
		modelData.mOpaqueMaterials[modelData.mOpaueMeshIDs.back()] = matID;
		if (mat.IsTransparent())
		{
			modelData.mTransparentMeshIDs.push_back(modelData.mOpaueMeshIDs.back());
		}
	} // for: NumMeshes

	for (unsigned int i = 0; i < pNode->mNumChildren; i++)
	{	// then do the same for each of its children
		Model::Data childModelData = ProcessAssimpNode(ModelName, pNode->mChildren[i], pAiScene, modelDirectory, pAssetLoader, pScene, pRenderer, MaterialTextureAssignments, taskID);
		std::vector<MeshID>& ChildMeshes = childModelData.mOpaueMeshIDs;
		std::vector<MeshID>& ChildMeshesTransparent = childModelData.mTransparentMeshIDs;

		std::copy(ChildMeshes.begin(), ChildMeshes.end(), std::back_inserter(modelData.mOpaueMeshIDs));
		std::copy(ChildMeshesTransparent.begin(), ChildMeshesTransparent.end(), std::back_inserter(modelData.mTransparentMeshIDs));
		for (auto& kvp : childModelData.mOpaqueMaterials)
		{
#if _DEBUG
			assert(modelData.mOpaqueMaterials.find(kvp.first) == modelData.mOpaqueMaterials.end());
#endif
			modelData.mOpaqueMaterials[kvp.first] = kvp.second;
		}
	} // for: NumChildren

	return modelData;
}


//----------------------------------------------------------------------------------------------------------------
// IMPORT MODEL FUNCTION FOR WORKER THREADS
//----------------------------------------------------------------------------------------------------------------
ModelID AssetLoader::ImportModel(Scene* pScene, AssetLoader* pAssetLoader, VQRenderer* pRenderer, const std::string& objFilePath, std::string ModelName)
{
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
	Importer importer;
	const aiScene* pAiScene = importer.ReadFile(objFilePath, ASSIMP_LOAD_FLAGS);
	if (!pAiScene || pAiScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !pAiScene->mRootNode)
	{
		Log::Error("Assimp error: %s", importer.GetErrorString());
		return INVALID_ID;
	}
	t.Tick(); float fTimeReadFile = t.DeltaTime();
	Log::Info("   [%.2fs] ReadFile=%s ", fTimeReadFile, objFilePath.c_str());

	// parse scene and initialize model data
	FMaterialTextureAssignments MaterialTextureAssignments(pAssetLoader->mWorkers_TextureLoad);
	Model::Data data = ProcessAssimpNode(ModelName, pAiScene->mRootNode, pAiScene, modelDirectory, pAssetLoader, pScene, pRenderer, MaterialTextureAssignments, taskID);

	pRenderer->UploadVertexAndIndexBufferHeaps(); // load VB/IBs

	if (!MaterialTextureAssignments.mAssignments.empty())
		MaterialTextureAssignments.mTextureLoadResults = pAssetLoader->StartLoadingTextures(taskID);

	// cache the imported model in Scene
	ModelID mID = pScene->CreateModel();
	Model& model = pScene->GetModel(mID);
	model = Model(objFilePath, ModelName, std::move(data));

	// SYNC POINT : wait for textures to load
	{
		MaterialTextureAssignments.WaitForTextureLoads();
	}

	// assign TextureIDs to the materials;
	MaterialTextureAssignments.DoAssignments(pScene, pRenderer);

	t.Stop();
	Log::Info("   [%.2fs] Loaded Model '%s'.", fTimeReadFile + t.DeltaTime(), ModelName.c_str());
	return mID;
}

