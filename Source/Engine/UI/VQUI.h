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

struct FUIState
{
	bool bWindowVisible_KeyMappings = false;
	bool bWindowVisible_Profiler = false;
	bool bWindowVisible_SceneControls = false;
	bool bWindowVisible_GraphicsSettingsPanel = false;
	bool bHideAllWindows = false; // masks all the windows above

	bool bWindowVisible_Editor = false;
	enum EEditorMode
	{
		MATERIALS = 0,
		LIGHTS,
		OBJECTS,

		NUM_EDITOR_MODES
	};
	EEditorMode EditorMode = EEditorMode::MATERIALS;
	int SelectedEditeeIndex[NUM_EDITOR_MODES];

	bool bTerrainScaleSliderFloat3 = false;
	bool bTessellationSliderFloatVec = false;
	bool bLockTessellationSliders = false;
	
	bool bDrawLightVolume = false;

	bool bUIOnSeparateWindow = false;
	bool bProfiler_ShowEngineStats = true;
	
	float ResolutionScaleSliderValue = 1.0f;

	void GetMouseScreenPosition(int& X, int& Y) const;
};