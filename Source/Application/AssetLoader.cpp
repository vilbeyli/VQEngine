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
#include "Libs/VQUtils/Source/Timer.h"
#include "Libs/VQUtils/Source/Log.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

using namespace Assimp;
using namespace DirectX;


struct FMaterialTextureAssignments
{
	MaterialID matID;
	AssetLoader::TextureLoadResults_t DiffuseIDs;
	AssetLoader::TextureLoadResults_t SpecularIDs;
	AssetLoader::TextureLoadResults_t NormalsIDs;
	AssetLoader::TextureLoadResults_t HeightMapIDs;
	AssetLoader::TextureLoadResults_t AlphaMapIDs;
};

Model::Data ProcessAssimpNode(
	  aiNode* const pNode
	, const aiScene* pAiScene
	, const std::string& modelDirectory
	, AssetLoader* pAssetLoader
	, Scene* pScene
	, VQRenderer* pRenderer
	, std::vector<FMaterialTextureAssignments>& MaterialTextureAssignments
);


//----------------------------------------------------------------------------------------------------------------
// IMPORTERS
//----------------------------------------------------------------------------------------------------------------

ModelID AssetLoader::ImportModel_obj(Scene* pScene, AssetLoader* pAssetLoader, VQRenderer* pRenderer, const std::string& objFilePath, std::string ModelName)
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
	std::vector<FMaterialTextureAssignments> MaterialTextureAssignments;
	Model::Data data = ProcessAssimpNode(pAiScene->mRootNode, pAiScene, modelDirectory, pAssetLoader, pScene, pRenderer, MaterialTextureAssignments);

	std::vector<std::future<TextureID>> v = pAssetLoader->StartLoadingTextures();

	// cache the imported model in Scene
	ModelID mID = pScene->CreateModel();
	Model& model = pScene->GetModel(mID);
	model = Model(objFilePath, ModelName, std::move(data));

	// wait for textures to be loaded and assign TextureIDs;
	for (FMaterialTextureAssignments& assignment : MaterialTextureAssignments)
	{
		Material& mat = pScene->GetMaterial(assignment.matID);

		auto fnAssignTextureIDs = [](MaterialID matID, const std::string& strTexType, std::vector<std::future<TextureID>>& textures, TextureID& textureMapID)
		{
			if (textures.size() > 1)
			{
				Log::Warning("More than 1 %s in MaterialID=%d, will only use the first texture.", strTexType.c_str(), matID);
			}
			for (std::future<TextureID>& texIDs : textures)
			{
				assert(texIDs.valid());
				texIDs.wait();
				textureMapID = texIDs.get();
				break; // TODO: currently only 1 diffuse map supported (no texture blending)
			}
		};

		fnAssignTextureIDs(assignment.matID, "Diffuse Map"  , assignment.DiffuseIDs  , mat.diffuseMap);
		fnAssignTextureIDs(assignment.matID, "Specular Map" , assignment.SpecularIDs , mat.specularMap);
		fnAssignTextureIDs(assignment.matID, "Normal Map"   , assignment.NormalsIDs  , mat.normalMap);
		fnAssignTextureIDs(assignment.matID, "HeightMap Map", assignment.HeightMapIDs, mat.heightMap);
		fnAssignTextureIDs(assignment.matID, "Alpha Map"    , assignment.AlphaMapIDs , mat.mask);	
	}

	t.Stop();
	Log::Info("Loaded Model '%s' in %.2f seconds.", ModelName.c_str(), t.DeltaTime());
	return mID;
}

ModelID AssetLoader::ImportModel_gltf(Scene* pScene, AssetLoader* pAssetLoader, VQRenderer* pRenderer, const std::string& objFilePath, std::string ModelName)
{
	assert(false); // TODO
	return INVALID_ID;
}



//----------------------------------------------------------------------------------------------------------------
// ASSET LOADER
//----------------------------------------------------------------------------------------------------------------

void AssetLoader::QueueModelLoad(const std::string& ModelPath, const std::string& ModelName)
{
	const std::string FileExtension = DirectoryUtil::GetFileExtension(ModelPath);

	static const std::unordered_map<std::string, FModelLoadParams::pfnImportModel_t> MODEL_IMPORT_FUNCTIONS =
	{
		  { "obj" , AssetLoader::ImportModel_obj }
		, { "gltf", AssetLoader::ImportModel_gltf } // TODO
	};

	std::unique_lock<std::mutex> lk(mMtxQueue_ModelLoad);
	mModelLoadQueue.push({ModelPath, ModelName, MODEL_IMPORT_FUNCTIONS.at(FileExtension)});
}

AssetLoader::ModelLoadResults_t AssetLoader::StartLoadingModels(Scene* pScene)
{
	ModelLoadResults_t ModelLoadResults;
	if (mModelLoadQueue.empty())
	{
		Log::Warning("AssetLoader::StartLoadingModels(): no models to load");
		return ModelLoadResults;
	}

	// process model load queue
	VQRenderer* pRenderer = &mRenderer;
	std::unique_lock<std::mutex> lk(mMtxQueue_ModelLoad);
	do
	{
		FModelLoadParams ModelLoadParams = std::move(mModelLoadQueue.front());
		const std::string& ModelPath = ModelLoadParams.ModelPath;
		mModelLoadQueue.pop();

		// eliminate duplicates
		if (mUniqueModelPaths.find(ModelPath) == mUniqueModelPaths.end())
		{
			mUniqueModelPaths.insert(ModelPath);

			// start loading
			std::future<ModelID> modelLoadResult = mWorkers.AddTask([=]()
			{
				return ModelLoadParams.pfnImportModel(pScene, this, pRenderer, ModelLoadParams.ModelPath, ModelLoadParams.ModelName);
			});
			ModelLoadResults.push_back(std::move(modelLoadResult));
		}
	} while (!mModelLoadQueue.empty());

	mUniqueModelPaths.clear();
	return ModelLoadResults;
}

void AssetLoader::QueueTextureLoad(const FTextureLoadParams& TexLoadParam)
{
	std::unique_lock<std::mutex> lk(mMtxQueue_TextureLoad);
	mTextureLoadQueue.push(TexLoadParam);
}

std::vector<std::future<TextureID>> AssetLoader::StartLoadingTextures()
{
	std::vector<std::future<TextureID>> TextureLoadResults;
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

			std::future<TextureID> loadResult = mWorkers.AddTask([this, TexLoadParams]()
			{
				return mRenderer.CreateTextureFromFile(TexLoadParams.TexturePath.c_str());
			});
			TextureLoadResults.push_back(std::move(loadResult));
		}
	} while (!mTextureLoadQueue.empty());

	mUniqueTexturePaths.clear();

	return TextureLoadResults;
}





//----------------------------------------------------------------------------------------------------------------
// ASSIMP HELPER FUNCTIONS
//----------------------------------------------------------------------------------------------------------------
static std::vector<AssetLoader::FTextureLoadParams> GenerateTextureLoadParams(
	  aiMaterial*        pMaterial
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

		// NORMALS
		if (mesh->mNormals)
		{
			Vert.normal[0] = mesh->mNormals[i].x;
			Vert.normal[1] = mesh->mNormals[i].y;
			Vert.normal[2] = mesh->mNormals[i].z;
		}

		// TEXTURE COORDINATES
		// a vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't 
		// use models where a vertex can have multiple texture coordinates so we always take the first set (0).
		Vert.uv[0] = mesh->mTextureCoords[0] ? mesh->mTextureCoords[0][i].x : 0;
		Vert.uv[1] = mesh->mTextureCoords[0] ? mesh->mTextureCoords[0][i].y : 0;
		
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
	return Mesh(pRenderer, Vertices, Indices, "");
}

static Model::Data ProcessAssimpNode(
	aiNode* const      pNode,
	const aiScene*     pAiScene,
	const std::string& modelDirectory,
	AssetLoader*       pAssetLoader,
	Scene*             pScene,
	VQRenderer*        pRenderer,
	std::vector<FMaterialTextureAssignments>& MaterialTextureAssignments
)
{
	Model::Data modelData;

	for (unsigned int i = 0; i < pNode->mNumMeshes; i++)
	{	// process all the node's meshes (if any)
		aiMesh* pAiMesh = pAiScene->mMeshes[pNode->mMeshes[i]];

		// MATERIAL - http://assimp.sourceforge.net/lib_html/materials.html
		aiMaterial* material = pAiScene->mMaterials[pAiMesh->mMaterialIndex];

		// get texture paths to load
		FMaterialTextureAssignments MatAssignment = {};
		std::vector<AssetLoader::FTextureLoadParams> diffuseMaps  = GenerateTextureLoadParams(material, aiTextureType_DIFFUSE, "texture_diffuse", modelDirectory);
		std::vector<AssetLoader::FTextureLoadParams> specularMaps = GenerateTextureLoadParams(material, aiTextureType_SPECULAR, "texture_specular", modelDirectory);
		std::vector<AssetLoader::FTextureLoadParams> normalMaps   = GenerateTextureLoadParams(material, aiTextureType_NORMALS, "texture_normal", modelDirectory);
		std::vector<AssetLoader::FTextureLoadParams> heightMaps   = GenerateTextureLoadParams(material, aiTextureType_HEIGHT, "texture_height", modelDirectory);
		std::vector<AssetLoader::FTextureLoadParams> alphaMaps    = GenerateTextureLoadParams(material, aiTextureType_OPACITY, "texture_alpha", modelDirectory);

		// start loading textures
		std::array<decltype(diffuseMaps)*, 5>             TexLoadParams   = { &diffuseMaps, &specularMaps, &normalMaps, &heightMaps, &alphaMaps };
		std::array<AssetLoader::TextureLoadResults_t*, 5> TexAssignParams = 
		{ 
			  &MatAssignment.DiffuseIDs
			, &MatAssignment.SpecularIDs
			, &MatAssignment.NormalsIDs
			, &MatAssignment.HeightMapIDs
			, &MatAssignment.AlphaMapIDs
		};
		
		int iTexType = 0;
		for (const auto* pvLoadParams : TexLoadParams)
		{
			int iTex = 0;
			for (const AssetLoader::FTextureLoadParams& param : *pvLoadParams)
			{
				pAssetLoader->QueueTextureLoad(param);
				++iTex;
			}

			// kick off texture load workers only if we have at least 1 texture
			if (iTex > 0)
			{
				//*TexAssignParams[iTexType] = std::move(pAssetLoader->StartLoadingTextures());
			}

			++iTexType;
		}

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
		}

		// unflatten the material texture assignments
		MatAssignment.matID = matID;
		MaterialTextureAssignments.push_back(std::move(MatAssignment));

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
