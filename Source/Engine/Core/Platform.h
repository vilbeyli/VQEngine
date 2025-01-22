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
#include "../Settings.h"

struct LogInitializeParams
{
	bool bLogConsole = false;
	bool bLogFile = false;
	char LogFilePath[512];
};
struct FStartupParameters
{
	HINSTANCE                 hExeInstance;
	int                       iCmdShow;
	LogInitializeParams       LogInitParams;

	FEngineSettings EngineSettings;

	uint8 bOverrideGFXSetting_RenderScale : 1;
	uint8 bOverrideGFXSetting_bVSync : 1;
	uint8 bOverrideGFXSetting_bUseTripleBuffering : 1;
	uint8 bOverrideGFXSetting_bAA : 1;
	uint8 bOverrideGFXSetting_bMaxFrameRate : 1;
	uint8 bOverrideGFXSetting_bHDR : 1;
	uint8 bOverrideGFXSetting_EnvironmentMapResolution : 1;
	uint8 bOverrideGFXSettings_Reflections : 1;

	uint8 bOverrideENGSetting_MainWindowHeight : 1;
	uint8 bOverrideENGSetting_MainWindowWidth : 1;
	uint8 bOverrideENGSetting_bDisplayMode : 1;
	uint8 bOverrideENGSetting_PreferredDisplay : 1;

	uint8 bOverrideENGSetting_bDebugWindowEnable : 1;
	uint8 bOverrideENGSetting_DebugWindowHeight : 1;
	uint8 bOverrideENGSetting_DebugWindowWidth : 1;
	uint8 bOverrideENGSetting_DebugWindowDisplayMode : 1;
	uint8 bOverrideENGSetting_DebugWindowPreferredDisplay : 1;

	uint8 bOverrideENGSetting_bAutomatedTest : 1;
	uint8 bOverrideENGSetting_bTestFrames : 1;
	uint8 bOverrideENGSetting_StartupScene : 1;
};

LRESULT __stdcall WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

template<typename T> static inline T CircularIncrement(T currVal, T maxVal) { return (currVal + 1) % maxVal; }
template<typename T> static inline T CircularDecrement(T currVal, T maxVal, T minVal = 0) { return currVal == minVal ? maxVal - 1  : currVal - 1; }