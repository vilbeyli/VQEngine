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

#include <string>

enum EDisplayMode
{
	WINDOWED = 0,
	BORDERLESS_FULLSCREEN,
	EXCLUSIVE_FULLSCREEN,

	NUM_DISPLAY_MODES
};

enum EReflections
{
	REFLECTIONS_OFF,
	SCREEN_SPACE_REFLECTIONS__FFX,
	RAY_TRACED_REFLECTIONS,

	NUM_REFLECTION_SETTINGS
};

struct FGraphicsSettings
{
	bool bVsync              = false;
	bool bUseTripleBuffering = false;
	bool bAntiAliasing       = false;
	EReflections Reflections = EReflections::REFLECTIONS_OFF;

	float RenderScale = 1.0f;
	int   MaxFrameRate = -1; // -1: Auto (RefreshRate x 1.15) | 0: Unlimited | <int>: specified value
	int   EnvironmentMapResolution = 256;
};

struct FWindowSettings
{
	int Width                 = -1;
	int Height                = -1;
	EDisplayMode DisplayMode  = EDisplayMode::WINDOWED;
	unsigned PreferredDisplay = 0;
	char Title[64]            = "";
	bool bEnableHDR           = false;

	inline bool IsDisplayModeFullscreen() const { return DisplayMode == EDisplayMode::EXCLUSIVE_FULLSCREEN || DisplayMode == EDisplayMode::BORDERLESS_FULLSCREEN; }
};

struct FEngineSettings
{
	FGraphicsSettings gfx;

	FWindowSettings WndMain;
	FWindowSettings WndDebug;

	bool bShowDebugWindow = false;

	bool bAutomatedTestRun     = false;
	int NumAutomatedTestFrames = -1;
	
	std::string StartupScene;
};