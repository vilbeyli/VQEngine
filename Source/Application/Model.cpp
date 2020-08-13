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

#include "Model.h"

#if 0
#include "Utilities/Log.h"
#include "Utilities/PerfTimer.h"

#include "Renderer/Renderer.h"

#include "Scene.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "Engine.h"

#include <functional>


const char* ModelLoader::sRootFolderModels = "Data/Models/";

using namespace Assimp;
using fnTypeProcessNode = std::function<Model::Data(aiNode* const, const aiScene*, std::vector<Mesh>&)>;
using fnTypeProcessMesh = std::function<Mesh(aiMesh*, const aiScene*)>;



bool Model::Data::AddMaterial(MeshID meshID, MaterialID matID, bool bTransparent)
{
	auto it = mMaterialLookupPerMesh.find(meshID);
	if (it == mMaterialLookupPerMesh.end())
	{
		mMaterialLookupPerMesh[meshID] = matID;
	}
	else
	{
#if _DEBUG
		Log::Warning("Overriding Material");
#endif
		mMaterialLookupPerMesh[meshID] = matID;

		if (bTransparent)
		{
			mTransparentMeshIDs.push_back(meshID);
		}
	}

	return true;
}


void Model::AddMaterialToMesh(MeshID meshID, MaterialID materialID, bool bTransparent)
{
	mData.AddMaterial(meshID, materialID);
}

void Model::OverrideMaterials(MaterialID materialID)
{
	for (MeshID mesh : mData.mMeshIDs)
	{
		mData.AddMaterial(mesh, materialID);
	}
}


//----------------------------------------------------------------------------------------------------------------
// ASSIMP HELPER FUNCTIONS
//----------------------------------------------------------------------------------------------------------------
std::vector<TextureID> LoadMaterialTextures(
	aiMaterial*			pMaterial, 
	aiTextureType		type, 
	const std::string&	textureName, 
	VQRenderer*			mpRenderer, 
	const std::string&	modelDirectory
)
{
	std::vector<TextureID> textures;
	for (unsigned int i = 0; i < pMaterial->GetTextureCount(type); i++)
	{
		aiString str;
		pMaterial->GetTexture(type, i, &str);
		std::string path = str.C_Str();
		{
			std::unique_lock<std::mutex> lock(Engine::mLoadRenderingMutex);
			textures.push_back(mpRenderer->CreateTextureFromFile(path, modelDirectory, true));
		}
	}
	return textures;
}


Mesh ProcessMesh(aiMesh * mesh, const aiScene * scene)
{
	std::vector<DefaultVertexBufferData> Vertices;
	std::vector<unsigned> Indices;

	// Walk through each of the mesh's vertices
	for (unsigned int i = 0; i < mesh->mNumVertices; i++)
	{
		DefaultVertexBufferData Vert;

		// POSITIONS
		Vert.position = vec3(
			mesh->mVertices[i].x,
			mesh->mVertices[i].y,
			mesh->mVertices[i].z
		);

		// NORMALS
		if (mesh->mNormals)
		{
			Vert.normal = vec3(
				mesh->mNormals[i].x,
				mesh->mNormals[i].y,
				mesh->mNormals[i].z
			);
		}

		// TEXTURE COORDINATES
		// a vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't 
		// use models where a vertex can have multiple texture coordinates so we always take the first set (0).
		Vert.uv = mesh->mTextureCoords[0]
			? vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y)
			: vec2(0, 0);

		// TANGENT
		if (mesh->mTangents)
		{
			Vert.tangent = vec3(
				mesh->mTangents[i].x,
				mesh->mTangents[i].y,
				mesh->mTangents[i].z
			);
		}


		// BITANGENT ( NOT USED )
		// Vert.bitangent = vec3(
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

	Mesh newMesh = [&]()
	{
		std::unique_lock<std::mutex> lck(Engine::mLoadRenderingMutex);
		return Mesh(Vertices, Indices, "ImportedModelMesh0");	// return a mesh object created from the extracted mesh data
	}();
	return newMesh;
}

Model::Data ProcessNode(
	aiNode* const		pNode,
	const aiScene*		pAiScene,
	const std::string&	modelDirectory,
	VQRenderer*			mpRenderer,		// creates resources
	Scene*				pScene			// write
)
{
	Model::Data modelData;
	std::vector<MeshID>& ModelMeshIDs = modelData.mMeshIDs;

	for (unsigned int i = 0; i < pNode->mNumMeshes; i++)
	{	// process all the node's meshes (if any)
		aiMesh* pAiMesh = pAiScene->mMeshes[pNode->mMeshes[i]];

		// MATERIAL - http://assimp.sourceforge.net/lib_html/materials.html
		aiMaterial* material = pAiScene->mMaterials[pAiMesh->mMaterialIndex];
		std::vector<TextureID> diffuseMaps  = LoadMaterialTextures(material, aiTextureType_DIFFUSE	, "texture_diffuse" , mpRenderer, modelDirectory);
		std::vector<TextureID> specularMaps = LoadMaterialTextures(material, aiTextureType_SPECULAR , "texture_specular", mpRenderer, modelDirectory);
		std::vector<TextureID> normalMaps   = LoadMaterialTextures(material, aiTextureType_NORMALS	, "texture_normal"  , mpRenderer, modelDirectory);
		std::vector<TextureID> heightMaps   = LoadMaterialTextures(material, aiTextureType_HEIGHT   , "texture_height"  , mpRenderer, modelDirectory);
		std::vector<TextureID> alphaMaps    = LoadMaterialTextures(material, aiTextureType_OPACITY	, "texture_alpha"   , mpRenderer, modelDirectory);

		BRDF_Material* pBRDF = static_cast<BRDF_Material*>(pScene->CreateNewMaterial(GGX_BRDF));
		assert(diffuseMaps.size() <= 1);	assert(normalMaps.size() <= 1);
		assert(specularMaps.size() <= 1);	assert(heightMaps.size() <= 1);
		assert(alphaMaps.size() <= 1);
		if (!diffuseMaps.empty())	pBRDF->diffuseMap = diffuseMaps[0];
		if (!normalMaps.empty())	pBRDF->normalMap = normalMaps[0];
		if (!specularMaps.empty())	pBRDF->specularMap = specularMaps[0];
		if (!heightMaps.empty())	pBRDF->heightMap = heightMaps[0];
		if (!alphaMaps.empty())		pBRDF->mask = alphaMaps[0];

		aiString name;
		if (aiReturn_SUCCESS == material->Get(AI_MATKEY_NAME, name))
		{
			// we don't store names for materials. probably best to store them in a lookup somewhere,
			// away from the material data.
			//
			// pBRDF->
		}

		aiColor3D color(0.f, 0.f, 0.f);
		if (aiReturn_SUCCESS == material->Get(AI_MATKEY_COLOR_DIFFUSE, color))
		{
			pBRDF->diffuse = vec3(color.r, color.g, color.b);
		}

		aiColor3D specular(0.f, 0.f, 0.f);
		if (aiReturn_SUCCESS == material->Get(AI_MATKEY_COLOR_SPECULAR, specular))
		{
			pBRDF->specular = vec3(specular.r, specular.g, specular.b);
		}

		aiColor3D transparent(0.0f, 0.0f, 0.0f);
		if (aiReturn_SUCCESS == material->Get(AI_MATKEY_COLOR_TRANSPARENT, transparent))
		{	// Defines the transparent color of the material, this is the color to be multiplied 
			// with the color of translucent light to construct the final 'destination color' 
			// for a particular position in the screen buffer. T
			//
			//pBRDF->specular = vec3(specular.r, specular.g, specular.b);
		}

		float opacity = 0.0f;
		if (aiReturn_SUCCESS == material->Get(AI_MATKEY_OPACITY, opacity))
		{
			pBRDF->alpha = opacity;
		}

		float shininess = 0.0f;
		if (aiReturn_SUCCESS == material->Get(AI_MATKEY_SHININESS, shininess))
		{
			// Phong Shininess -> Beckmann BRDF Roughness conversion
			//
			// https://simonstechblog.blogspot.com/2011/12/microfacet-brdf.html
			// https://computergraphics.stackexchange.com/questions/1515/what-is-the-accepted-method-of-converting-shininess-to-roughness-and-vice-versa
			//
			pBRDF->roughness = sqrtf(2.0f / (2.0f + shininess));
		}

#if MAKE_IRONMAN_METALLIC || MAKE_ZENBALL_METALLIC

		// ---
		// quick hack to assign metallic value to the loaded mesh
		//
		std::string fileName(pAiScene->mRootNode->mName.C_Str());
		std::transform(RANGE(fileName), fileName.begin(), ::tolower);
		auto tokens = StrUtil::split(fileName, '.');
		if (!tokens.empty() && (tokens[0] == "ironman" || tokens[0] == "zen_orb"))
		{
			pBRDF->metalness = 1.0f;
		}
		//---
#endif

		// other material keys to consider
		//
		// AI_MATKEY_TWOSIDED
		// AI_MATKEY_ENABLE_WIREFRAME
		// AI_MATKEY_BLEND_FUNC
		// AI_MATKEY_BUMPSCALING
		
		Mesh mesh = ProcessMesh(pAiMesh, pAiScene);
		{
			MeshID id = pScene->AddMesh_Async(mesh);
			ModelMeshIDs.push_back(id);
		}

		modelData.mMaterialLookupPerMesh[ModelMeshIDs.back()] = pBRDF->ID;
		if (pBRDF->IsTransparent())
		{
			modelData.mTransparentMeshIDs.push_back(ModelMeshIDs.back());
		}
	}
	for (unsigned int i = 0; i < pNode->mNumChildren; i++)
	{	// then do the same for each of its children
		Model::Data childModelData = ProcessNode(pNode->mChildren[i], pAiScene, modelDirectory, mpRenderer, pScene);
		std::vector<MeshID>& ChildMeshes = childModelData.mMeshIDs;
		std::vector<MeshID>& ChildMeshesTransparent = childModelData.mTransparentMeshIDs;

		std::copy(ChildMeshes.begin(), ChildMeshes.end(), std::back_inserter(ModelMeshIDs));
		std::copy(ChildMeshesTransparent.begin(), ChildMeshesTransparent.end(), std::back_inserter(modelData.mTransparentMeshIDs));
		for (auto& kvp : childModelData.mMaterialLookupPerMesh)
		{
#if _DEBUG
			assert(modelData.mMaterialLookupPerMesh.find(kvp.first) == modelData.mMaterialLookupPerMesh.end());
#endif
			modelData.mMaterialLookupPerMesh[kvp.first] = kvp.second;
		}
	}
	return modelData;
}

#endif

#if 0
//----------------------------------------------------------------------------------------------------------------
// MODEL LOADER
//----------------------------------------------------------------------------------------------------------------
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

Model ModelLoader::LoadModel(const std::string & modelPath, Scene* pScene)
{
	assert(mpRenderer);
	const std::string fullPath = sRootFolderModels + modelPath;
	const std::string modelDirectory = DirectoryUtil::GetFolderPath(fullPath);
	const std::string modelName = DirectoryUtil::GetFileNameWithoutExtension(fullPath);

	// CHECK CACHE FIRST - don't load the same model more than once
	//
	if (mLoadedModels.find(fullPath) != mLoadedModels.end())
	{
		return mLoadedModels.at(fullPath);
	}


	PerfTimer t;
	t.Start();

	Log::Info("Loading Model: %s ...", modelName.c_str());

	// IMPORT SCENE
	//
	Importer importer;
	const aiScene* scene = importer.ReadFile(fullPath, ASSIMP_LOAD_FLAGS);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		Log::Error("Assimp error: %s", importer.GetErrorString());
		return Model();
	}
	Model::Data data = ProcessNode(scene->mRootNode, scene, modelDirectory, mpRenderer, pScene);

	// cache the model
	const Model model = Model(modelDirectory, modelName, std::move(data));
	mLoadedModels[fullPath] = model;

	// register scene and the model loaded
	if (mSceneModels.find(pScene) == mSceneModels.end())	// THIS BEGS FOR TEMPLATE PROGRAMMING
	{	// first
		mSceneModels[pScene] = std::vector<std::string> { fullPath };
	}
	else
	{
		mSceneModels.at(pScene).push_back(fullPath);
	}
	t.Stop();
	Log::Info("Loaded Model '%s' in %.2f seconds.", modelName.c_str(), t.DeltaTime());
	return model;
}


Model ModelLoader::LoadModel_Async(const std::string& modelPath, Scene* pScene)
{
	assert(mpRenderer);
	const std::string fullPath = sRootFolderModels + modelPath;
	const std::string modelDirectory = DirectoryUtil::GetFolderPath(fullPath);
	const std::string modelName = DirectoryUtil::GetFileNameWithoutExtension(fullPath);

	// CHECK CACHE FIRST - don't load the same model more than once
	//
	{
		std::unique_lock<std::mutex> l(mLoadedModelMutex);
		if (mLoadedModels.find(fullPath) != mLoadedModels.end())
		{
			return mLoadedModels.at(fullPath);
		}
	}

	PerfTimer t;
	t.Start();

	//Log::Info("Loading Model: %s ...", modelName.c_str());

	// IMPORT SCENE
	//
	Importer importer;
	const aiScene* scene = importer.ReadFile(fullPath, ASSIMP_LOAD_FLAGS);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		Log::Error("Assimp error: %s", importer.GetErrorString());
		return Model();
	}
	Model::Data data = ProcessNode(scene->mRootNode, scene, modelDirectory, mpRenderer, pScene);

	// cache the model
	const Model model = Model(modelDirectory, modelName, std::move(data));
	{
		std::unique_lock<std::mutex> l(mLoadedModelMutex);
		mLoadedModels[fullPath] = model;
	}

	// register scene and the model loaded
	{
		std::unique_lock<std::mutex> l(mSceneModelsMutex);
		if (mSceneModels.find(pScene) == mSceneModels.end())	// THIS BEGS FOR TEMPLATE PROGRAMMING
		{	// first
			mSceneModels[pScene] = std::vector<std::string>{ fullPath };
		}
		else
		{
			mSceneModels.at(pScene).push_back(fullPath);
		}
	}

	t.Stop();
	//Log::Info("Loaded Model '%s' in %.2f seconds.", modelName.c_str(), t.DeltaTime());
	return model;
}

void ModelLoader::UnloadSceneModels(Scene * pScene)
{
	if (mSceneModels.find(pScene) == mSceneModels.end()) return;
	for (std::string& modelDirectory : mSceneModels.at(pScene))
	{
		mLoadedModels.erase(modelDirectory);
	}
}
#endif