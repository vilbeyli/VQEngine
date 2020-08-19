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

#include "Libs/VQUtils/Source/Multithreading.h"
#include "Libs/VQUtils/Source/utils.h"

Model AssetLoader::ImportModel_obj(const std::string& objFilePath, std::string ModelName)
{
	Model::Data modelData;

	Log::Info("ImportModel_obj: %s - %s", ModelName.c_str(), objFilePath.c_str());
	// TODO: assimp model import

	return Model(objFilePath, ModelName, std::move(modelData));
}

Model AssetLoader::ImportModel_gltf(const std::string& objFilePath, std::string ModelName)
{
	assert(false); // TODO
	return Model();
}

void AssetLoader::QueueAssetLoad(const std::string& ModelPath)
{
	const std::string FileExtension = DirectoryUtil::GetFileExtension(ModelPath);

	static std::unordered_map < std::string, FModelLoadParams::pfnImportModel_t> MODEL_IMPORT_FUNCTIONS =
	{
		  { "obj" , AssetLoader::ImportModel_obj }
		, { "gltf", AssetLoader::ImportModel_gltf } // TODO
	};

	std::unique_lock<std::mutex> lk(mMtxQueue_ModelLoad);
	mModelLoadQueue.push({ModelPath, MODEL_IMPORT_FUNCTIONS.at(FileExtension)});
}

void AssetLoader::StartLoadingAssets()
{
	if (mModelLoadQueue.empty())
	{
		Log::Warning("AssetLoader::StartLoadingAssets(): no models to load");
		return;
	}

	// process model load queue
	std::unique_lock<std::mutex> lk(mMtxQueue_ModelLoad);
	do
	{
		FModelLoadParams ModelLoadParams = mModelLoadQueue.front();
		const std::string& ModelPath = ModelLoadParams.ModelPath;
		mModelLoadQueue.pop();

		// eliminate duplicates
		if (mUniqueModelPaths.find(ModelPath) == mUniqueModelPaths.end())
		{
			mUniqueModelPaths.insert(ModelPath);

			// start loading
			mWorkers.AddTask([=]()
			{
				ModelLoadParams.pfnImportModel(ModelLoadParams.ModelPath, "");
			});
		}
	} while (!mModelLoadQueue.empty());

	mUniqueModelPaths.clear();
}

