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
#include "Mesh.h"
#include "Material.h"
#include "Scene.h"

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

//----------------------------------------------------------------------------------------------------------------
// ASSET LOADER
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
			if (mWorkers.IsExiting())
			{
				break;
			}

			// start loading
			modelLoadResult = std::move(mWorkers.AddTask([=]()
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

void AssetLoader::QueueTextureLoad(const FTextureLoadParams& TexLoadParam)
{
	std::unique_lock<std::mutex> lk(mMtxQueue_TextureLoad);
	mTextureLoadQueue.push(TexLoadParam);
}

AssetLoader::TextureLoadResults_t AssetLoader::StartLoadingTextures()
{
	TextureLoadResults_t TextureLoadResults;
	if (mTextureLoadQueue.empty())
	{
		Log::Warning("AssetLoader::StartLoadingTextures(): no Textures to load");
		return TextureLoadResults;
	}
	
	// process model load queue
	std::unique_lock<std::mutex> lk(mMtxQueue_ModelLoad);
	do
	{
		FTextureLoadParams TexLoadParams = std::move(mTextureLoadQueue.front());
		mTextureLoadQueue.pop();

		// eliminate duplicates
		if (mUniqueTexturePaths.find(TexLoadParams.TexturePath) == mUniqueTexturePaths.end())
		{
			mUniqueTexturePaths.insert(TexLoadParams.TexturePath);

			// determine whether we'll load file OR use a procedurally generated texture
			auto vPathTokens = StrUtil::split(TexLoadParams.TexturePath, '/');
			assert(!vPathTokens.empty());
			const bool bProceduralTexture = vPathTokens[0] == "Procedural";

			EProceduralTextures ProcTex = bProceduralTexture 
				? VQRenderer::GetProceduralTextureEnumFromName(vPathTokens[1])
				: EProceduralTextures::NUM_PROCEDURAL_TEXTURES;

			// check whether Exit signal is given to the app before dispatching workers
			if (mWorkers.IsExiting())
			{
				break;
			}

			std::future<TextureID> texLoadResult = std::move(mWorkers.AddTask([this, TexLoadParams, ProcTex]()
			{
				const bool IS_PROCEDURAL = ProcTex != EProceduralTextures::NUM_PROCEDURAL_TEXTURES;
				if (IS_PROCEDURAL)
				{
					return mRenderer.GetProceduralTexture(ProcTex);
				}

				return mRenderer.CreateTextureFromFile(TexLoadParams.TexturePath.c_str());
			}));
			TextureLoadResults.emplace(std::make_pair(TexLoadParams.MatID, TextureLoadResult_t{ TexLoadParams.TexType, std::move(texLoadResult) }));
		}
	} while (!mTextureLoadQueue.empty());

	mUniqueTexturePaths.clear();

	// Currently mRenderer.CreateTextureFromFile() starts the texture uploads
	///mRenderer.StartTextureUploads();

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
	case aiTextureType_AMBIENT:   break;
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
		break;
	case aiTextureType_DIFFUSE_ROUGHNESS:
		break;
	case aiTextureType_AMBIENT_OCCLUSION:
		break;
	case aiTextureType_UNKNOWN:
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

		auto pair_itBeginEnd = mTextureLoadResults.equal_range(assignment.matID);
		for (auto it = pair_itBeginEnd.first; it != pair_itBeginEnd.second; ++it)
		{
			const MaterialID& matID = it->first;
			TextureLoadResult_t& result = it->second;
			
			if (mWorkers.IsExiting())
				break;

			assert(result.texLoadResult.valid());

			result.texLoadResult.wait();
			switch (result.type)
			{
			case DIFFUSE    : mat.TexDiffuseMap   = result.texLoadResult.get();  break;
			case NORMALS    : mat.TexNormalMap    = result.texLoadResult.get();  break;
			case ALPHA_MASK : mat.TexAlphaMaskMap = result.texLoadResult.get();  break;
			case EMISSIVE   : mat.TexEmissiveMap  = result.texLoadResult.get();  break;
			case METALNESS  : mat.TexMetallicMap  = result.texLoadResult.get();  break;
			case ROUGHNESS  : mat.TexRoughnessMap = result.texLoadResult.get();  break;
			case SPECULAR   : mat.TexSpecularMap  = result.texLoadResult.get(); /*pRenderer->InitializeSRV(mat.SRVMaterialMaps, 0, mat.TexSpecularMap);*/ break;
			case HEIGHT     : mat.TexHeightMap    = result.texLoadResult.get(); /*pRenderer->InitializeSRV(mat.SRVMaterialMaps, 0, mat.TexHeightMap)  ;*/ break;
			default:
				Log::Warning("TODO");
				break;
			}
		}

		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::ALBEDO, mat.TexDiffuseMap);
		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::NORMALS, mat.TexNormalMap);
		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::ALPHA_MASK, mat.TexAlphaMaskMap);
		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::EMISSIVE, mat.TexEmissiveMap);
		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::METALLIC, mat.TexMetallicMap);
		pRenderer->InitializeSRV(mat.SRVMaterialMaps, EMaterialTextureMapBindings::ROUGHNESS, mat.TexRoughnessMap);
	}
}

void AssetLoader::FMaterialTextureAssignments::WaitForTextureLoads()
{
	for (auto it = mTextureLoadResults.begin(); it != mTextureLoadResults.end(); ++it)
	{
		const MaterialID& matID = it->first;
		const TextureLoadResult_t& result = it->second;
		assert(result.texLoadResult.valid());

		if (mWorkers.IsExiting())
			break;

		result.texLoadResult.wait();
	}
}

std::vector<AssetLoader::FTextureLoadResult>& AssetLoader::FMaterialTextureAssignment::GetTextureMapCollection(ETextureType type)
{
	switch (type)
	{
	case AssetLoader::DIFFUSE    : return DiffuseIDs;
	case AssetLoader::NORMALS    : return NormalsIDs;
	case AssetLoader::SPECULAR   : assert(false); return DiffuseIDs; // currently not supported
	case AssetLoader::ALPHA_MASK : return AlphaMapIDs;
	case AssetLoader::EMISSIVE   : assert(false); return DiffuseIDs; // TODO
	case AssetLoader::HEIGHT     : return HeightMapIDs;
	case AssetLoader::METALNESS  : assert(false); return DiffuseIDs; // TODO
	case AssetLoader::ROUGHNESS  : assert(false); return DiffuseIDs; // TODO
	}
	assert(false); return DiffuseIDs;
}


//----------------------------------------------------------------------------------------------------------------
// IMPORTERS
//----------------------------------------------------------------------------------------------------------------
Model::Data ProcessAssimpNode(
	aiNode* const pNode
	, const aiScene* pAiScene
	, const std::string& modelDirectory
	, AssetLoader* pAssetLoader
	, Scene* pScene
	, VQRenderer* pRenderer
	, AssetLoader::FMaterialTextureAssignments& MaterialTextureAssignments
);

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


	const std::string modelDirectory = DirectoryUtil::GetFolderPath(objFilePath);

	Log::Info("ImportModel_obj: %s - %s", ModelName.c_str(), objFilePath.c_str());
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
	Log::Info("   ReadFile(): %.3fs", fTimeReadFile);

	// parse scene and initialize model data
	FMaterialTextureAssignments MaterialTextureAssignments(pAssetLoader->mWorkers);
	Model::Data data = ProcessAssimpNode(pAiScene->mRootNode, pAiScene, modelDirectory, pAssetLoader, pScene, pRenderer, MaterialTextureAssignments);

	pRenderer->UploadVertexAndIndexBufferHeaps(); // load VB/IBs
	MaterialTextureAssignments.mTextureLoadResults = pAssetLoader->StartLoadingTextures();

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
	Log::Info("Loaded Model '%s' in %.2f seconds.", ModelName.c_str(), t.DeltaTime());
	return mID;
}

//----------------------------------------------------------------------------------------------------------------
// ASSIMP HELPER FUNCTIONS
//----------------------------------------------------------------------------------------------------------------
static std::vector<AssetLoader::FTextureLoadParams> GenerateTextureLoadParams(
	  aiMaterial*        pMaterial
	, MaterialID         matID
	, aiTextureType      type
	, const std::string& textureName
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

static Mesh ProcessAssimpMesh(VQRenderer* pRenderer, aiMesh* mesh, const aiScene* scene)
{
	std::vector<FVertexWithNormalAndTangent> Vertices;
	std::vector<unsigned> Indices;

	// Walk through each of the mesh's vertices
	for (unsigned int i = 0; i < mesh->mNumVertices; i++)
	{
		FVertexWithNormalAndTangent Vert;

		// POSITIONS
		Vert.position[0] = mesh->mVertices[i].x;
		Vert.position[1] = mesh->mVertices[i].y;
		Vert.position[2] = mesh->mVertices[i].z;

		// TEXTURE COORDINATES
		// a vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't 
		// use models where a vertex can have multiple texture coordinates so we always take the first set (0).
		Vert.uv[0] = mesh->mTextureCoords[0] ? mesh->mTextureCoords[0][i].x : 0;
		Vert.uv[1] = mesh->mTextureCoords[0] ? mesh->mTextureCoords[0][i].y : 0;

		// NORMALS
		if (mesh->mNormals)
		{
			Vert.normal[0] = mesh->mNormals[i].x;
			Vert.normal[1] = mesh->mNormals[i].y;
			Vert.normal[2] = mesh->mNormals[i].z;
		}
	
		// TANGENT
		if (mesh->mTangents)
		{
			Vert.tangent[0] = mesh->mTangents[i].x;
			Vert.tangent[1] = mesh->mTangents[i].y;
			Vert.tangent[2] = mesh->mTangents[i].z;
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
	for (unsigned int i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		// retrieve all indices of the face and store them in the indices vector
		for (unsigned int j = 0; j < face.mNumIndices; j++)
			Indices.push_back(face.mIndices[j]);
	}

	// TODO: mesh name
	return Mesh(pRenderer, Vertices, Indices, "TODO");;
}

static Model::Data ProcessAssimpNode(
	aiNode* const      pNode,
	const aiScene*     pAiScene,
	const std::string& modelDirectory,
	AssetLoader*       pAssetLoader,
	Scene*             pScene,
	VQRenderer*        pRenderer,
	AssetLoader::FMaterialTextureAssignments& MaterialTextureAssignments
)
{
	Model::Data modelData;

	for (unsigned int i = 0; i < pNode->mNumMeshes; i++)
	{	// process all the node's meshes (if any)
		aiMesh* pAiMesh = pAiScene->mMeshes[pNode->mMeshes[i]];

		// MATERIAL - http://assimp.sourceforge.net/lib_html/materials.html
		aiMaterial* material = pAiScene->mMaterials[pAiMesh->mMaterialIndex];

		// Every material assumed to have a name : Create material here
		MaterialID matID = INVALID_ID;
		aiString name;
		if (aiReturn_SUCCESS == material->Get(AI_MATKEY_NAME, name))
		{
			const std::string ModelFolderName = DirectoryUtil::GetFlattenedFolderHierarchy(modelDirectory).back();
			std::string uniqueMatName = name.C_Str();
			// modelDirectory = "Data/Models/%MODEL_NAME%/"
			// Materials use the following unique naming: %MODEL_NAME%/%MATERIAL_NAME%
			uniqueMatName = ModelFolderName + "/" + uniqueMatName;
			matID = pScene->CreateMaterial(uniqueMatName);
			Material& mat = pScene->GetMaterial(matID);
		}

		// get texture paths to load
		AssetLoader::FMaterialTextureAssignment MatTexAssignment = {};
		std::vector<AssetLoader::FTextureLoadParams> diffuseMaps  = GenerateTextureLoadParams(material, matID, aiTextureType_DIFFUSE, "texture_diffuse", modelDirectory);
		std::vector<AssetLoader::FTextureLoadParams> specularMaps = GenerateTextureLoadParams(material, matID, aiTextureType_SPECULAR, "texture_specular", modelDirectory);
		std::vector<AssetLoader::FTextureLoadParams> normalMaps   = GenerateTextureLoadParams(material, matID, aiTextureType_NORMALS, "texture_normal", modelDirectory);
		std::vector<AssetLoader::FTextureLoadParams> heightMaps   = GenerateTextureLoadParams(material, matID, aiTextureType_HEIGHT, "texture_height", modelDirectory);
		std::vector<AssetLoader::FTextureLoadParams> alphaMaps    = GenerateTextureLoadParams(material, matID, aiTextureType_OPACITY, "texture_alpha", modelDirectory);

		// queue texture load
		std::array<decltype(diffuseMaps)*, 5> TexLoadParams = { &diffuseMaps, &specularMaps, &normalMaps, &heightMaps, &alphaMaps };
		int iTexType = 0;
		for (const auto* pvLoadParams : TexLoadParams)
		{
			int iTex = 0;
			for (const AssetLoader::FTextureLoadParams& param : *pvLoadParams)
			{
				pAssetLoader->QueueTextureLoad(param);
				++iTex;
			}
			++iTexType;
		}


		// unflatten the material texture assignments
		MatTexAssignment.matID = matID;
		MaterialTextureAssignments.mAssignments.push_back(std::move(MatTexAssignment));

		Material& mat = pScene->GetMaterial(matID);

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

		// other material keys to consider
		//
		// AI_MATKEY_TWOSIDED
		// AI_MATKEY_ENABLE_WIREFRAME
		// AI_MATKEY_BLEND_FUNC
		// AI_MATKEY_BUMPSCALING
		
		Mesh mesh = ProcessAssimpMesh(pRenderer, pAiMesh, pAiScene);
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
		Model::Data childModelData = ProcessAssimpNode(pNode->mChildren[i], pAiScene, modelDirectory, pAssetLoader, pScene, pRenderer, MaterialTextureAssignments);
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
