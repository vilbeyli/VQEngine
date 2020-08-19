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

#include "Model.h"

#include<set>

class ThreadPool;

class AssetLoader
{
public:
	static Model ImportModel_obj (const std::string& objFilePath, std::string ModelName = "NONE"); // TODO: rename to LoadModel_obj() ?
	static Model ImportModel_gltf(const std::string& objFilePath, std::string ModelName = "NONE"); // TODO: rename to LoadModel_gltf() ?

public:

	AssetLoader(ThreadPool& WorkerThreads)
		: mWorkers(WorkerThreads)
	{}

	void QueueAssetLoad(const std::string& ModelPath);
	void StartLoadingAssets();

private:
	struct FModelLoadParams
	{
		using pfnImportModel_t = Model(*)(const std::string& objFilePath, std::string ModelName);
		std::string      ModelPath;
		pfnImportModel_t pfnImportModel;
	};
	struct FTextureLoadParams
	{
		std::string TexturePath;
	};

private:
	ThreadPool& mWorkers;

	std::queue<FModelLoadParams> mModelLoadQueue;
	std::set<std::string> mUniqueModelPaths;

	std::queue< FTextureLoadParams> mTextureLoadQueue;
	std::set<std::string> mUniqueTexturePaths;

	std::mutex mMtxQueue_ModelLoad;
	std::mutex mMtxQueue_TextureLoad;
};