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

#include "VQAssetBrowser.h"
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include "../Core/Types.h"
#include "../../Libs/VQUtils/Source/Log.h"

CBV_SRV_UAV* AssetBrowser::ExtensionToIcon(std::string& extension) const
{
	switch (StringHash::Basic(extension.c_str())) // StringToHash Function is located in Helper.hpp
	{
		// is material ?
		case MatHash:  { extension = "Material"; return mMaterialIcon.GetTextureSRV(mRenderer);  }
		// is mesh ?
		case GltfHash:
		case GlbHash : { extension = "MESH";     return mMeshIcon.GetTextureSRV(mRenderer);   }
	}
	return mFileIcon.GetTextureSRV(mRenderer);
}

AssetBrowser::~AssetBrowser()
{
	for (TextureSRVPair& icon : mTextureIcons)
	{
		icon.Dispose(mRenderer);
	}
}

AssetBrowser::AssetBrowser(VQRenderer& renderer)
: mRenderer(renderer) 
, mFileIcon 	(AssetBrowser::TextureSRVPair(renderer, "Data/Icons/file.png"         ))
, mFolderIcon	(AssetBrowser::TextureSRVPair(renderer, "Data/Icons/folder.png"       ))
, mMeshIcon 	(AssetBrowser::TextureSRVPair(renderer, "Data/Icons/mesh.png"         ))
, mMaterialIcon (AssetBrowser::TextureSRVPair(renderer, "Data/Icons/Material_Icon.png"))
, mCurrentPath  (std::filesystem::current_path() / "Data")
{
	mFolderIconTexture = mFolderIcon.GetTextureSRV(renderer);

	FolderTree* rootNode = CreateTreeRec(nullptr, mCurrentPath);
	mRootTree	 = rootNode;
	mCurrentTree = mRootTree;
}

AssetBrowser::FolderTree* AssetBrowser::CreateTreeRec(FolderTree* parent, const std::filesystem::path& path)
{
	std::string folderName = path.filename().u8string();
	FolderTree* tree = new FolderTree(path, folderName);

	for (const auto& directory : std::filesystem::directory_iterator(path))
	{
		if (directory.is_directory()) continue;
		const std::filesystem::path& pathChace = directory.path();
		std::string fileName  = pathChace.filename().u8string();
		std::string filePath  = pathChace.u8string();
		std::string extension = pathChace.extension().u8string();
		CBV_SRV_UAV* icon = ExtensionToIcon(extension);
	
		// if extension is texture we need to import textures from location 
		// if (extension == ".png" || extension == ".jpg") 
		// {
		// 	auto texture = AssetBrowser::TextureSRVPair(mRenderer, filePath.c_str());
		// 	
		// 	// we are pushing to mTextureIcons because when we destruct AssetBrowser we want to dispose all of the textures and srv's
		// 	mTextureIcons.push_back(std::move(texture));
		// 	// get last pushed texture because we use move constructor
		// 	icon = mTextureIcons[mTextureIcons.size()-1].GetTextureSRV(mRenderer);
		// 	extension = "TEXTURE";
		// }
	
		FileRecord record = FileRecord(filePath, fileName, extension);
		record.texture = icon;
		tree->files.push_back(std::move(record));
	}
	
	for (const auto& directory : std::filesystem::directory_iterator(path))
	{
		if (!directory.is_directory()) continue;
		tree->folders.push_back( CreateTreeRec(tree, directory.path()) );
	}
	tree->parent = parent;
	return tree;
}

void AssetBrowser::TreeDrawRec(FolderTree* tree, int& id)
{
	static auto flags = ImGuiTreeNodeFlags_OpenOnArrow;//: ImGuiTreeNodeFlags_OpenOnArrow;

	ImGui::PushID(id++);
	if (ImGui::TreeNodeEx(tree->name.c_str(), flags))
	{
		for (FolderTree* folder : tree->folders)
		{
			TreeDrawRec(folder, id);
		}
		ImGui::TreePop();
	}
	if (ImGui::IsItemClicked()) { mCurrentTree = tree; }
	ImGui::PopID();
}

void AssetBrowser::DrawFolders(std::vector<FolderTree*>& folders, int& id)
{
	FolderTree* folderRec = mCurrentTree;
	ImGui::TableNextColumn();

	// todo scroll and zoom in and out
	for (FolderTree* folder : mCurrentTree->folders)
	{
		ImGui::PushID(id++);

		if (VQEditor::GUI::ImageButton(mFolderIconTexture, VQEditor::filesize))
		{
			folderRec = folder;
		}

		if (ImGui::IsItemHovered()) {
			ImGui::BeginTooltip();
			ImGui::Text(folder->name.c_str());
			ImGui::EndTooltip();
		}

		ImGui::TextWrapped("%s", folder->name.c_str());

		ImGui::PopID();
		ImGui::TableNextColumn();
	}

	mCurrentTree = folderRec;
}

void AssetBrowser::DrawFiles(const std::vector<FileRecord>& files, int& id)
{
	for (FileRecord& file : mCurrentTree->files)
	{
		ImGui::PushID(id++);

		CBV_SRV_UAV* icon = file.texture;
	
		VQEditor::GUI::ImageButton(file.texture, VQEditor::filesize, { 0, 0 }, { 1, 1 });
		
		if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) // IsMouseDoubleClicked is not working
		{
			ShellExecute(0, 0, file.path.c_str(), 0, 0, SW_SHOW);
		}
		
		VQEditor::GUI::DragUIElementString(file.path.c_str(), file.extension.c_str(), file.texture);
		
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::Text(file.name.c_str());
			ImGui::EndTooltip();
		}

		// ImGui::TextWrapped("%s", file.name.substr(0, std::min<int>(10, file.name.size())).c_str());
		ImGui::TextWrapped("%s", file.name.c_str());

		ImGui::PopID();
		ImGui::TableNextColumn();
	}
}

void AssetBrowser::RecursiveSearch(const char* key, const int len, FolderTree* tree)
{
	for (FolderTree* folder : tree->folders)
	{
		// search keyword in folder if we find add to list
		// I think this search method better than folder.name.tolower().find(key)
		for (int i = 0; i + len <= folder->name.size(); ++i)
		{
			for (int j = 0; j < len; ++j)
				if (tolower(key[j]) != tolower(folder->name[i + j]))
					goto next_index_folder;
			mSearchFolders.push_back(folder);
			break;
			next_index_folder: {}
		}
		RecursiveSearch(key, len, folder);
	}

	for (FileRecord& file : tree->files)
	{
		// search keyword in file if we find add to list
		// I think this search method better than file.name.tolower().find(key)
		for (int i = 0; i + len <= file.name.size(); ++i)
		{
			for (int j = 0; j < len; ++j)
				if (tolower(key[j]) != tolower(file.name[i + j]))
					goto next_index_file;
			mSearchFiles.push_back(file);
			break;
			next_index_file: {}
		}
	}
}

void AssetBrowser::SearchProcess(const char* SearchText)
{
	const int len = strlen(SearchText);
	mSearchFolders.clear();
	mSearchFiles.clear();
	RecursiveSearch(SearchText, len, mRootTree);
}

void AssetBrowser::DrawWindow()
{
	static bool searching = false;
	
	ImGui::Begin("Resources");
	
	if (ImGui::BeginTable("table-content-browser", 2, ImGuiTableFlags_Resizable))
	{
		// right click to AssetBrowser 
		if (ImGui::BeginPopupContextWindow("Resource Opinions"))
		{
			if (ImGui::MenuItem("Open File Location")) {
				ShellExecute(0, 0, mCurrentPath.u8string().c_str(), 0, 0, SW_SHOW);
			}
			ImGui::EndPopup();
		}

		ImGui::TableNextColumn();

		int id = 0;
		// Draw file tree. Left side of AssetBrowser
		ImGui::BeginChild("resource-file-list");
		{
			TreeDrawRec(mRootTree, id);
		}
		ImGui::EndChild();
	
		ImGui::TableNextColumn();
		
		// draw right side of the AssetBrowser
		ImGui::BeginChild("resource-file-folder-view-container");
		float regionAvail = ImGui::GetContentRegionAvail().x;
		{
			if (VQEditor::GUI::IconButton(ICON_FA_ARROW_LEFT) && mCurrentTree->parent) {
				mCurrentPath = mCurrentPath.parent_path();
				mCurrentTree = mCurrentTree->parent;
			}
			
			ImGui::SameLine();
			ImGui::Text(ICON_FA_SEARCH);
			ImGui::SameLine();
			static char SearchText[128];
			
			if (ImGui::InputText("Search", SearchText, 128))
			{
				SearchProcess(SearchText);
			}
		}
		ImGui::Separator();

		// Draw files/folders
		{
			float columnSize = VQEditor::filesize + 30;
			int columnCount = std::max<int>(1, (int)std::floor<int>(regionAvail / columnSize));

			if (ImGui::BeginTable("files-folders-Table", columnCount))
			{
				DrawFolders(mCurrentTree->folders, id);
				DrawFiles(mCurrentTree->files, id);
				ImGui::EndTable();
			}
		}

		ImGui::EndChild(); // "resource-file-folder-view-container"
		
		ImGui::EndTable(); // "table-content-browser"
	}
	
	ImGui::End();
}
