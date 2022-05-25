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

namespace StringHash
{
	// very fast hash function + compile time
	// http://www.cse.yorku.ca/~oz/hash.html
	inline constexpr uint32 Basic(const char* str)
	{
		uint32 hash = 0; int c = 0;
		while (c = *str++) hash += c;
		return hash;
	}
}

class AssetBrowser
{
public:
	class FileRecord
	{
	public:
		std::string path;
		std::string name;
		std::string extension;
		CBV_SRV_UAV* texture = nullptr;
		FileRecord() {}
		FileRecord(std::string _path, std::string _name, std::string _extension)
		:	path(std::move(_path)),
			name(std::move(_name)),
			extension(std::move(_extension)) {}
	};

	class FolderTree
	{
	public:
		FolderTree() : parent(nullptr) {}
		FolderTree(std::filesystem::path _path, std::string _name) : path(_path), name(std::move(_name)) {}
	public:
		std::filesystem::path  path;
		FolderTree* parent = nullptr;
		std::string name;
		std::vector<FolderTree*> folders; // < -- subfolders
		std::vector<FileRecord> files;
	};

	class TextureSRVPair
	{
	public:
		TextureID textureid;
		SRV_ID srv;
		
		CBV_SRV_UAV* GetTextureSRV(VQRenderer& renderer) const
		{ 
			return const_cast<CBV_SRV_UAV*>(&renderer.GetSRV(srv));
		}
		
		TextureSRVPair() {}

		TextureSRVPair(VQRenderer& vqRenderer, const char* filename)
		: textureid(vqRenderer.CreateTextureFromFile(filename, true))
		, srv(vqRenderer.AllocateSRV(1))
		{
			vqRenderer.InitializeSRV(srv, 0, textureid);
		}

		void Dispose(VQRenderer& renderer) {
			renderer.DestroySRV(srv);
			renderer.DestroyTexture(textureid);
		}
	};

private:

	CBV_SRV_UAV* ExtensionToIcon(std::string& extension) const;

	FolderTree* CreateTreeRec(FolderTree* parent, const std::filesystem::path& path);

	void TreeDrawRec	(FolderTree* tree, int& id);

	void DrawFolders	(std::vector<FolderTree*>& folders, int& id);

	void DrawFiles      (const std::vector<FileRecord>& files, int& id);

	void RecursiveSearch(const char* key, const int len, FolderTree* tree);

	void SearchProcess	(const char* SearchText);
	
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

	CBV_SRV_UAV* mFolderIconTexture;

	FolderTree* mRootTree;
	FolderTree* mCurrentTree;

	std::vector<FolderTree*>    mSearchFolders; // after search store matching folders
	std::vector<FileRecord>     mSearchFiles  ; // after search store matching files 
	std::vector<TextureSRVPair> mTextureIcons ; // for disposing textures after engine shutdown
	VQRenderer& mRenderer;
	
private:
	static constexpr uint32 GltfHash = StringHash::Basic(".gltf");
	static constexpr uint32 GlbHash  = StringHash::Basic(".glb");
	static constexpr uint32 MatHash  = StringHash::Basic(".xml");
};