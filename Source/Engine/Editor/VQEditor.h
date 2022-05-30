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
#include "FontAwesome4.h"

#include "Libs/imgui/imgui.h"
// To use the 'disabled UI state' functionality (ImGuiItemFlags_Disabled), include internal header
// https://github.com/ocornut/imgui/issues/211#issuecomment-339241929
#include "Libs/imgui/imgui_internal.h"
#include <functional>
#include "../../Renderer/ResourceViews.h"

typedef void(*Action)();

namespace VQEditor
{
	constexpr float filesize = 35;
	constexpr float miniSize = 12.5f;

	void DeepDarkTheme();
	void DarkTheme();
	void LightTheme();

	struct TitleAndAction
	{
		const char* title;
		Action action;
		TitleAndAction() : title("empty title"), action(nullptr) {}
		TitleAndAction(const char* _title, Action _action) : title(_title), action(_action) {}
	};

	typedef void(*FileCallback)(const char* ptr);

	// contains helper/wrapper functions for imgui
	namespace GUI
	{
		inline bool ImageButton(ImTextureID texture, const float& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1))
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.30f, 0.30f, 0.65f));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
			bool clicked = ImGui::ImageButton((const void*)texture, { size , size }, uv0, uv1);
			ImGui::PopStyleColor(1);
			ImGui::PopStyleVar(1);
			return clicked;
		}

		inline bool IconButton(const char* name, const ImVec2& size = ImVec2(0, 0))
		{
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.15f, 1));
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.16f, 0.16f, 0.18f));

			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.3f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
			bool clicked = ImGui::Button(name, size);
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(2);

			return clicked;
		}

		void Header(const char* title);
		// void TextureField(const char* name, Texture*& texture);

		bool EnumField(int& value, const char** names, const int& count, const char* label,
			const Action& onSellect = NULL, const ImGuiComboFlags& flags = 0);

		void RightClickPopUp(const char* name, const TitleAndAction* menuItems, const int& count);

		bool DragUIElementString(const char* file, const char* type, const CBV_SRV_UAV* texture);
		void DropUIElementString(const char* type, const FileCallback& callback);

		template<typename T>
		bool DragUIElement(const T* file, const char* type, const CBV_SRV_UAV* texture);
		template<typename T>
		void DropUIElement(const char* type, const std::function<T>& callback);
	}
}
