//	VQE
//	Copyright(C) 2020  - Volkan Ilbeyli, Anilcan Gulkaya
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
//	Contact: volkanilbeyli@gmail.com, anilcangulkaya7@gmail.com

#pragma once
#include "VQEditor.h"
#include "../../Renderer/Renderer.h"
#include "../../Libs/VQUtils/Source/Log.h"

class AssetBrowser
{
public:
	class FileRecord
	{
	public:
		std::string mPath;
		std::string mName;
		std::string mExtension;
		const CBV_SRV_UAV* mpTexture = nullptr;
		FileRecord() {}
		FileRecord(std::string _path, std::string _name, std::string _extension)
		:	mPath(std::move(_path)),
			mName(std::move(_name)),
			mExtension(std::move(_extension)) {}
	};

	class FolderTree
	{
	public:
		FolderTree() : mpParent(nullptr) {}
		FolderTree(std::filesystem::path _path, std::string _name) : mPath(_path), mName(std::move(_name)) {}
	public:
		std::filesystem::path mPath;
		FolderTree* mpParent = nullptr;
		std::string mName;
		std::vector<FolderTree*> mFolders; // < -- subfolders
		std::vector<FileRecord> mFiles;
	};

	class TextureSRVPair
	{
	public:
		TextureID mTextureId;
		SRV_ID mSrv;
		
		const CBV_SRV_UAV* GetTextureSRV(VQRenderer& renderer) const
		{ 
			return &renderer.GetSRV(mSrv);
		}
		
		TextureSRVPair() {}

		TextureSRVPair(VQRenderer& vqRenderer, const char* filename)
		: mTextureId(vqRenderer.CreateTextureFromFile(filename, true))
		, mSrv(vqRenderer.AllocateSRV(1))
		{
			vqRenderer.InitializeSRV(mSrv, 0, mTextureId);
		}

		void Dispose(VQRenderer& renderer) {
			renderer.DestroySRV(mSrv);
			renderer.DestroyTexture(mTextureId);
		}
	};

private:

	const CBV_SRV_UAV* ExtensionToIcon(std::string& extension) const;
	
	FolderTree* CreateTreeRec(FolderTree* parent, const std::filesystem::path& path);

	void TreeDrawRec	(FolderTree* tree, int& id);

	void DrawFolders	(const std::vector<FolderTree*>& folders, int& id);

	void DrawFiles      (const std::vector<FileRecord>& files, int& id) const;

	void RecursiveSearch(const char* key, const size_t len, FolderTree* tree);

	void SearchProcess	(const char* SearchText);

	bool FindInString	(const std::string& str, const char* key, int len) const;

	void DeleteTreeRec(FolderTree* tree) const;

public:

	AssetBrowser(VQRenderer& renderer);
	
	~AssetBrowser();

	void DrawWindow();

	std::filesystem::path GetCurrentPath() const { return mCurrentPath; }

private:
	std::filesystem::path mCurrentPath;

	TextureSRVPair mFileIcon    ;	
	TextureSRVPair mFolderIcon  ;
	TextureSRVPair mMeshIcon	;  
	TextureSRVPair mMaterialIcon;

	const CBV_SRV_UAV* mpFolderIconTexture;

	FolderTree* mpRootTree;
	FolderTree* mpCurrentTree;

	std::vector<FolderTree*>    mSearchFolders; // after search store matching folders
	std::vector<FileRecord>     mSearchFiles  ; // after search store matching files 
	std::vector<TextureSRVPair> mTextureIcons ; // for disposing textures after engine shutdown
	VQRenderer& mRenderer;
};