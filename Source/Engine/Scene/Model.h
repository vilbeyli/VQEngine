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

#include "../Core/Types.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <array>
#include <string>

class VQRenderer;

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
// A collection of opaque and transparent meshIDs with materials associated with them
struct Model
{
public:
	struct Data
	{
		enum EMeshType
		{
			OPAQUE_MESH = 0,
			ALPHA_MASKED_MESH = 0,
			TRANSPARENT_MESH,
			TRANSLUCENT_MESH,

			NUM_MESH_TYPES
		};

		inline bool HasMaterial() const { return mMaterials.empty(); }
		bool AddMaterial(MeshID meshID, MaterialID matID, bool bTransparent = false);

		inline size_t GetNumOpaqueMeshes() const { return mMeshMaterialIDPairsPerMeshType[OPAQUE_MESH].size(); }
		inline size_t GetNumTransparentMeshes() const { return mMeshMaterialIDPairsPerMeshType[TRANSPARENT_MESH].size(); }
		inline size_t GetNumTranslucentMeshes() const { return mMeshMaterialIDPairsPerMeshType[TRANSLUCENT_MESH].size(); }
		inline size_t GetNumMeshesOfType(EMeshType eMeshType) const { return mMeshMaterialIDPairsPerMeshType[eMeshType].size(); }
		size_t GetNumMeshesOfAllTypes() const;
		
		inline const std::vector<std::pair<MeshID, MaterialID>>& GetMeshMaterialIDPairs(EMeshType eMeshType) const { return mMeshMaterialIDPairsPerMeshType[eMeshType]; }
		inline       std::vector<std::pair<MeshID, MaterialID>>& GetMeshMaterialIDPairs(EMeshType eMeshType)       { return mMeshMaterialIDPairsPerMeshType[eMeshType]; } // TODO: remove this if possible
		inline       std::unordered_set<MaterialID>& GetMaterials() { return mMaterials; };
		inline const std::unordered_set<MaterialID>& GetMaterials() const { return mMaterials; };

		Data() {};
		Data(MeshID meshID, MaterialID matID, EMeshType eType);
		void AddMesh(MeshID meshID, MaterialID matID, EMeshType eType);

	private:
		std::array<std::vector<std::pair<MeshID, MaterialID>>, NUM_MESH_TYPES> mMeshMaterialIDPairsPerMeshType;

		std::unordered_set<MaterialID> mMaterials;
	};

	//---------------------------------------

	Model() = default;
	Model(const std::string& directoryFullPath, const std::string& modelName, Data&& modelDataIn)
		: mData(modelDataIn)
		, mModelName(modelName)
		, mModelPath(directoryFullPath)
		, mbLoaded(true)
	{}

	//---------------------------------------

	Data         mData;
	std::string  mModelName;
	std::string  mModelPath;
	bool         mbLoaded = false;
};



