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

#include <memory>

struct FMagnifierParameters;
struct FMagnifierUIState
{
	bool  bUseMagnifier = false;
	bool  bLockMagnifierPosition = false;
	bool  bLockMagnifierPositionHistory = false;
	int   LockedMagnifiedScreenPositionX = 0;
	int   LockedMagnifiedScreenPositionY = 0;
	std::shared_ptr<FMagnifierParameters> pMagnifierParams = nullptr;

	void ToggleMagnifierLock();
	void AdjustMagnifierSize(float increment = 0.05f);
	void AdjustMagnifierMagnification(float increment = 1.00f);
};

struct FUIState
{
	bool bWindowVisible_KeyMappings;
	bool bWindowVisible_Profiler;
	bool bWindowVisible_SceneControls;
	bool bWindowVisible_DebugPanel;
	bool bWindowVisible_GraphicsSettingsPanel;
	bool bHideAllWindows; // masks all the windows above

	bool bUIOnSeparateWindow;
	bool bProfiler_ShowEngineStats;
	std::unique_ptr<FMagnifierUIState> mpMagnifierState = nullptr;
	
	void GetMouseScreenPosition(int& X, int& Y) const;

	~FUIState();
};