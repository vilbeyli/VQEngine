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

#include <cassert>

bool Model::Data::AddMaterial(MeshID meshID, MaterialID matID, bool bTransparent)
{
	assert(false);
	return false;
}

size_t Model::Data::GetNumMeshesOfAllTypes() const
{
	size_t NumMeshes = 0;
	for (const auto& vPairs : mMeshMaterialIDPairsPerMeshType) NumMeshes += vPairs.size();
	return NumMeshes;
}

Model::Data::Data(MeshID meshID, MaterialID matID, EMeshType eType)
{
	AddMesh(meshID, matID, eType);
}

void Model::Data::AddMesh(MeshID meshID, MaterialID matID, EMeshType eType)
{
	mMeshMaterialIDPairsPerMeshType[eType].push_back({ meshID, matID });
	if (matID != INVALID_ID)
		mMaterials.emplace(matID);
}
