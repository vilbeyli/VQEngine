//	VQE
//	Copyright(C) 2025  - Volkan Ilbeyli
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

#include "Mesh.h"
#include "Model.h"
#include "Material.h"
#include "Transform.h"
#include "GameObject.h"

// For the time being, this is simply a flat list of bounding boxes -- there is not much of a hierarchy to speak of.
class SceneBoundingBoxHierarchy
{
public:
	SceneBoundingBoxHierarchy(
		  const std::unordered_map<MeshID, Mesh>& Meshes
		, const std::unordered_map<ModelID, Model>& Models
		, const std::unordered_map<MaterialID, Material>& Materials
		, const std::vector<size_t>& TransformHandles
	)
		: mMeshes(Meshes)
		, mModels(Models)
		, mMaterials(Materials)
		, mTransformHandles(TransformHandles)
	{}
	SceneBoundingBoxHierarchy() = delete;

	void Build(const Scene* pScene, const std::vector<size_t>& GameObjectHandles, ThreadPool& UpdateWorkerThreadPool);
	void Clear();
	void ResizeGameObjectBoundingBoxContainer(size_t sz);

	const std::vector<int>& GetNumMeshesLODs() const { return mNumMeshLODs; }
	const std::vector<MeshID>& GetMeshesIDs() const { return mMeshIDs; }
	const std::vector<MaterialID>& GetMeshMaterialIDs() const { return mMeshMaterials; }
	const std::vector<const Transform*>& GetMeshTransforms() const { return mMeshTransforms; }
	const std::vector<size_t>& GetMeshGameObjectHandles() const { return mMeshGameObjectHandles; }

private:
	void ResizeGameMeshBoxContainer(size_t size);

	void BuildGameObjectBoundingSpheres(const std::vector<size_t>& GameObjectHandles);
	void BuildGameObjectBoundingSpheres_Range(const std::vector<size_t>& GameObjectHandles, size_t iBegin, size_t iEnd);

	void BuildGameObjectBoundingBox(const Scene* pScene, size_t ObjectHandle, size_t iBB);
	void BuildGameObjectBoundingBoxes(const Scene* pScene, const std::vector<size_t>& GameObjectHandles);
	void BuildGameObjectBoundingBoxes_Range(const Scene* pScene, const std::vector<size_t>& GameObjectHandles, size_t iBegin, size_t iEnd);

	void BuildMeshBoundingBox(const Scene* pScene, size_t ObjectHandle, size_t iBB_Begin, size_t iBB_End);
	void BuildMeshBoundingBoxes(const Scene* pScene, const std::vector<size_t>& GameObjectHandles);
	void BuildMeshBoundingBoxes_Range(const Scene* pScene, const std::vector<size_t>& GameObjectHandles, size_t iBegin, size_t iEnd, size_t iMeshBB);

private:
	friend class Scene;
	FBoundingBox mSceneBoundingBox;

	// list of game object bounding boxes for coarse culling
	//------------------------------------------------------
	std::vector<FBoundingBox>      mGameObjectBoundingBoxes;
	std::vector<size_t>            mGameObjectHandles;
	std::vector<size_t>            mGameObjectNumMeshes;
	//------------------------------------------------------

	// list of mesh bounding boxes for fine culling
	//------------------------------------------------------
	// these are same size containers, mapping bounding boxes to gameobjects, meshIDs, etc.
	size_t mNumValidMeshBoundingBoxes = 0;
	std::vector<FBoundingBox>      mMeshBoundingBoxes;
	std::vector<MeshID>            mMeshIDs;
	std::vector<int>               mNumMeshLODs;
	std::vector<MaterialID>        mMeshMaterials;
	std::vector<const Transform*>  mMeshTransforms;
	std::vector<size_t>            mMeshGameObjectHandles;
	//------------------------------------------------------

	// scene data container references
	const std::unordered_map<MeshID, Mesh>& mMeshes;
	const std::unordered_map<ModelID, Model>& mModels;
	const std::unordered_map<MaterialID, Material>& mMaterials;
	const std::vector<size_t>& mTransformHandles;
};