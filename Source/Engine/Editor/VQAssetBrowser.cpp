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

const CBV_SRV_UAV* AssetBrowser::ExtensionToIcon(std::string& extension) const
{
	if (extension == ".xml")
	{
		extension = "Material"; 
		return mMaterialIcon.GetTextureSRV(mRenderer);
	}

	if (extension == ".gltf" || extension == ".glb") 
	{
		extension = "MESH";
		return mMeshIcon.GetTextureSRV(mRenderer);
	}
	
	return mFileIcon.GetTextureSRV(mRenderer);
}

AssetBrowser::~AssetBrowser()
{
	DeleteTreeRec(mpRootTree);

	for (TextureSRVPair& icon : mTextureIcons)
	{
		icon.Dispose(mRenderer);
	}
}

void AssetBrowser::DeleteTreeRec(FolderTree* tree) const 
{
	for (int i = 0; i < tree->mFolders.size(); ++i)
	{
		DeleteTreeRec(tree->mFolders[i]);
	}
	delete tree;
}


AssetBrowser::AssetBrowser(VQRenderer& renderer)
: mRenderer(renderer) 
, mFileIcon		(TextureSRVPair(renderer, "Data/Icons/file.png"))
, mFolderIcon	(TextureSRVPair(renderer, "Data/Icons/folder.png"))
, mMeshIcon		(TextureSRVPair(renderer, "Data/Icons/mesh.png"))
, mMaterialIcon	(TextureSRVPair(renderer, "Data/Icons/Material_Icon.png"))
, mCurrentPath	(std::filesystem::current_path() / "Data")
{
	mpFolderIconTexture = mFolderIcon.GetTextureSRV(renderer);

	FolderTree* rootNode = CreateTreeRec(nullptr, mCurrentPath);
	mpRootTree	  = rootNode;
	mpCurrentTree = mpRootTree;
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
		const CBV_SRV_UAV* icon = ExtensionToIcon(extension);
	
		// todo: if extension is texture we need to import textures from path
	
		FileRecord record = FileRecord(filePath, fileName, extension);
		record.mpTexture = icon;
		tree->mFiles.push_back(std::move(record));
	}
	
	for (const auto& directory : std::filesystem::directory_iterator(path))
	{
		if (!directory.is_directory()) continue;
		tree->mFolders.push_back( CreateTreeRec(tree, directory.path()) );
	}
	tree->mpParent = parent;
	return tree;
}

void AssetBrowser::TreeDrawRec(FolderTree* tree, int& id) 
{
	static ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;

	ImGui::PushID(id++);
	if (ImGui::TreeNodeEx(tree->mName.c_str(), flags))
	{
		for (FolderTree* folder : tree->mFolders)
		{
			TreeDrawRec(folder, id);
		}
		ImGui::TreePop();
	}

	if (ImGui::IsItemClicked()) 
	{
		mpCurrentTree = tree; 
	}
	ImGui::PopID();
}

void AssetBrowser::DrawFolders(const std::vector<FolderTree*>& folders, int& id) 
{
	FolderTree* folderRec = mpCurrentTree;
	ImGui::TableNextColumn();

	// todo scroll and zoom in and out
	for (FolderTree* folder : mpCurrentTree->mFolders)
	{
		ImGui::PushID(id++);

		if (VQEditor::GUI::ImageButton(mpFolderIconTexture, VQEditor::filesize))
		{
			folderRec = folder;
		}

		if (ImGui::IsItemHovered()) {
			ImGui::BeginTooltip();
			ImGui::Text(folder->mName.c_str());
			ImGui::EndTooltip();
		}

		ImGui::TextWrapped("%s", folder->mName.c_str());

		ImGui::PopID();
		ImGui::TableNextColumn();
	}

	mpCurrentTree = folderRec;
}

void AssetBrowser::DrawFiles(const std::vector<FileRecord>& files, int& id) const
{ 
	for (const FileRecord& file : mpCurrentTree->mFiles)
	{
		ImGui::PushID(id++);

		const CBV_SRV_UAV* icon = file.mpTexture;
	
		VQEditor::GUI::ImageButton(file.mpTexture, VQEditor::filesize, { 0, 0 }, { 1, 1 });
		
		if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) // IsMouseDoubleClicked is not working
		{
			ShellExecute(0, 0, file.mPath.c_str(), 0, 0, SW_SHOW);
		}
		
		VQEditor::GUI::DragUIElementString(file.mPath.c_str(), file.mExtension.c_str(), file.mpTexture);
		
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::Text(file.mName.c_str());
			ImGui::EndTooltip();
		}

		// ImGui::TextWrapped("%s", file.name.substr(0, std::min<int>(10, file.name.size())).c_str());
		ImGui::TextWrapped("%s", file.mName.c_str());

		ImGui::PopID();
		ImGui::TableNextColumn();
	}
}

bool AssetBrowser::FindInString(const std::string& str, const char* key, int len) const
{
	for (int i = 0; i + len <= str.size(); ++i)
	{
		bool containsKey = true;

		for (int j = 0; j < len; ++j)
		{
			if (tolower(key[j]) != tolower(str[i + j]))
			{
				containsKey = false;
				break;
			}
		}
				
		if (containsKey) return true;
	}
	return false;
}

void AssetBrowser::RecursiveSearch(const char* key, const size_t len, FolderTree* tree)
{
	for (FolderTree* folder : tree->mFolders)
	{
		if (FindInString(folder->mName, key, len))
		{
			mSearchFolders.push_back(folder);
		} 
		RecursiveSearch(key, len, folder);
	}

	for (FileRecord& file : tree->mFiles)
	{
		if (FindInString(file.mName, key, len))
		{
			mSearchFiles.push_back(file);
		}
	}
}

void AssetBrowser::SearchProcess(const char* SearchText)
{
	const int len = strlen(SearchText);
	mSearchFolders.clear();
	mSearchFiles.clear();
	RecursiveSearch(SearchText, len, mpRootTree);
}

void AssetBrowser::DrawWindow()
{
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
		bool searching = false;
		// Draw file tree. Left side of AssetBrowser
		ImGui::BeginChild("resource-file-list");
		{
			TreeDrawRec(mpRootTree, id);
		}
		ImGui::EndChild();
	
		ImGui::TableNextColumn();
		
		// draw right side of the AssetBrowser
		ImGui::BeginChild("resource-file-folder-view-container");
		float regionAvail = ImGui::GetContentRegionAvail().x;
		{
			if (VQEditor::GUI::IconButton(ICON_FA_ARROW_LEFT) && mpCurrentTree->mpParent) {
				mCurrentPath = mCurrentPath.parent_path();
				mpCurrentTree = mpCurrentTree->mpParent;
			}
			
			ImGui::SameLine();
			ImGui::Text(ICON_FA_SEARCH);
			ImGui::SameLine();
			static char SearchText[128];
			
			if (ImGui::InputText("Search", SearchText, 128))
			{
				SearchProcess(SearchText);
			}
			searching = std::strlen(SearchText);
		}
		ImGui::Separator();

		float columnSize = VQEditor::filesize + 30;
		int columnCount = std::max<int>(1, (int)std::floor<int>(regionAvail / columnSize));
		
		// Draw files/folders
		if (ImGui::BeginTable("files-folders-Table", columnCount))
		{
			if (!searching) // !searching is optimization because most of the time we are not searching
			{
				DrawFolders(mpCurrentTree->mFolders, id);
				DrawFiles(mpCurrentTree->mFiles, id);
			}
			else
			{
				DrawFolders(mSearchFolders, id);
				DrawFiles(mSearchFiles, id);
			}
			ImGui::EndTable();
		}

		ImGui::EndChild(); // "resource-file-folder-view-container"
		
		ImGui::EndTable(); // "table-content-browser"
	}
	
	ImGui::End();
}
