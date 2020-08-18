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
#pragma once

//#include "Material.h" // TODO
#include "Mesh.h"
#include "Types.h"

#include <vector>
#include <unordered_map>
#include <queue>

#include <mutex>

struct aiScene;
struct aiNode;
struct aiMesh;
struct aiMaterial;
class VQRenderer;
// class GameObject; // TODO
// class Scene; // TODO

struct MeshRenderSettings
{
	enum EMeshRenderMode
	{
		FILL = 0,
		WIREFRAME,

		NUM_MESH_RENDER_MODES
	};
	static EMeshRenderMode DefaultRenderSettings() { return { EMeshRenderMode::FILL }; }
	EMeshRenderMode renderMode = EMeshRenderMode::FILL;
};

using MeshMaterialLookup_t       = std::unordered_map<MeshID, MaterialID>;
using MeshRenderSettingsLookup_t = std::unordered_map<MeshID, MeshRenderSettings>;


//
// MODEL 
//
struct Model
{
public:
	struct Data
	{
		std::vector<MeshID>         mMeshIDs;
		MeshMaterialLookup_t        mOpaqueMaterials;
		MeshMaterialLookup_t        mTransparentMaterials;
		inline bool HasMaterial() const { return !mOpaqueMaterials.empty() || !mTransparentMaterials.empty(); }
		bool AddMaterial(MeshID meshID, MaterialID matID, bool bTransparent = false);
	};

	void AddMaterialToMesh(MeshID meshID, MaterialID materialID, bool bTransparent);
	void OverrideMaterials(MaterialID materialID);

	Model() = default;
	Model(const std::string& directoryFullPath, const std::string& modelName, Data&& modelDataIn)
		: mData(modelDataIn)
		, mModelName(modelName)
		, mModelDirectory(directoryFullPath)
		, mbLoaded(true)
	{}

	//---------------------------------------

	Data         mData;
	std::string  mModelName;
	std::string  mModelDirectory;
	bool         mbLoaded = false;

private:
	friend class GameObject;
	friend class Scene;

	// queue of materials to be assigned in case the model has not been loaded yet.
	std::queue<MaterialID> mMaterialAssignmentQueue;
};




//
// MODEL LOADER
//
class ModelLoader
{
public:
	inline void Initialize(VQRenderer* pRenderer) { mpRenderer = pRenderer; }

	// Loads the Model in a serial fashion - blocks thread
	//
	Model	LoadModel(const std::string& modelPath, Scene* pScene);

	// Ends async loading.
	//
	Model	LoadModel_Async(const std::string& modelPath, Scene* pScene);


	void UnloadSceneModels(Scene* pScene);


private:
	static const char* sRootFolderModels;


	using ModelLookUpTable             = std::unordered_map<std::string, Model>;
	using PerSceneModelNameLookupTable = std::unordered_map<Scene*, std::vector<std::string>>;


private:
	VQRenderer* mpRenderer;
	ModelLookUpTable mLoadedModels;
	PerSceneModelNameLookupTable mSceneModels;

	std::mutex mLoadedModelMutex;
	std::mutex mSceneModelsMutex;
};