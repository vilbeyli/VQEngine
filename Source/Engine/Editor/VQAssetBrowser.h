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

#include <filesystem>

class VQRenderer;

class AssetBrowser
{
public:
	struct FileRecord
	{
		std::string mPath;
		std::string mName;
		std::string mExtension;
		
		CBV_SRV_UAV mTexture;

		FileRecord() {}
		
		FileRecord(std::string& _path, std::string& _name, std::string& _extension, CBV_SRV_UAV& texture)
		:	mPath		(std::move(_path)) 
		,	mName		(std::move(_name))
		,   mExtension	(std::move(_extension))
		,	mTexture	(std::move(texture))
		{}
	};

	struct FolderTree
	{
		FolderTree() : mpParent(nullptr) {}
		FolderTree(std::filesystem::path _path, std::string& _name) : mPath(_path), mName(std::move(_name)) {}

		std::filesystem::path mPath;
		FolderTree* mpParent = nullptr;
		std::string mName;
		std::vector<FolderTree*> mFolders; // < -- subfolders
		std::vector<FileRecord> mFiles;
	};

	struct TextureSRVPair
	{
		TextureID mTextureId;
		SRV_ID mSrv;
		
		const CBV_SRV_UAV GetTextureSRV(VQRenderer& renderer) const;
		
		TextureSRVPair() {}

		TextureSRVPair(VQRenderer& vqRenderer, const char* filename);
		
		void Dispose(VQRenderer& renderer);
	};

private:

	FolderTree* CreateTreeRec(FolderTree* parent, const std::filesystem::path& path);

	void TreeDrawRec	(FolderTree* tree, int& id);

	void DrawFolders	(const std::vector<FolderTree*>& folders, int& id);

	void DrawFiles      (const std::vector<FileRecord>& files, int& id) const;

	void RecursiveSearch(const char* key, const size_t len, FolderTree* tree);

	void SearchProcess	(const char* SearchText);

	bool FindInString	(const std::string& str, const char* key, const size_t len) const;

	void DeleteTreeRec(FolderTree* tree) const;

public:

	AssetBrowser(VQRenderer& renderer);
	
	~AssetBrowser();

	void DrawWindow(uint WinSizeX, uint WinSizeY);

	std::filesystem::path GetCurrentPath() const { return mCurrentPath; }

private:
	std::filesystem::path mCurrentPath;

	TextureSRVPair mFileIcon    ;	
	TextureSRVPair mFolderIcon  ;
	TextureSRVPair mMeshIcon	;  
	TextureSRVPair mMaterialIcon;
	
	CBV_SRV_UAV mpFolderIconTexture;

	FolderTree* mpRootTree;
	FolderTree* mpCurrentTree;

	std::vector<FolderTree*>    mSearchFolders; // after search store matching folders
	std::vector<FileRecord>     mSearchFiles  ; // after search store matching files 
	std::vector<TextureSRVPair> mTextureIcons ; // for disposing textures after engine shutdown
	VQRenderer& mRenderer;
};