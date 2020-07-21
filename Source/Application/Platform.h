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

#include "Types.h"
#include <Windows.h>
#include <d3d12.h>

#include <mutex>
#include <condition_variable>
#include <vector>

#include "../../Libs/VQUtils/Source/Log.h"

#include "Settings.h"

struct IDXGIAdapter1;
struct IDXGIOutput;

// -------------------------------------------------------------------------------

struct FStartupParameters
{
	HINSTANCE                 hExeInstance;
	int                       iCmdShow;
	Log::LogInitializeParams  LogInitParams;

	FEngineSettings EngineSettings;
	uint8 bOverrideGFXSetting_RenderScale         : 1;
	uint8 bOverrideGFXSetting_bVSync              : 1;
	uint8 bOverrideGFXSetting_bUseTripleBuffering : 1;

	uint8 bOverrideENGSetting_MainWindowHeight    : 1;
	uint8 bOverrideENGSetting_MainWindowWidth     : 1;
	uint8 bOverrideENGSetting_bFullscreen         : 1;
	uint8 bOverrideENGSetting_PreferredDisplay    : 1;

	uint8 bOverrideENGSetting_bDebugWindowEnable          : 1;
	uint8 bOverrideENGSetting_DebugWindowHeight           : 1;
	uint8 bOverrideENGSetting_DebugWindowWidth            : 1;
	uint8 bOverrideENGSetting_DebugWindowbFullscreen      : 1;
	uint8 bOverrideENGSetting_DebugWindowPreferredDisplay : 1;

	uint8 bOverrideENGSetting_bAutomatedTest      : 1;
	uint8 bOverrideENGSetting_bTestFrames         : 1;
};

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// -------------------------------------------------------------------------------
