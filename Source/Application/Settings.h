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

enum EDisplayMode
{
	WINDOWED = 0,
	WINDOWED_FULLSCREEN,
	EXCLUSIVE_FULLSCREEN,

	NUM_DISPLAY_MODES
};

struct FGraphicsSettings
{
	bool bVsync              = false;
	bool bUseTripleBuffering = false;
	EDisplayMode DisplayMode = EDisplayMode::WINDOWED;

	float RenderScale = 1.0f;

	inline bool IsDisplayModeFullscreen() const { return DisplayMode == EDisplayMode::EXCLUSIVE_FULLSCREEN || DisplayMode == EDisplayMode::WINDOWED_FULLSCREEN; }
};

struct FEngineSettings
{
	FGraphicsSettings gfx;

	int MainWindow_Width  = -1;
	int MainWindow_Height = -1;

	int DebugWindow_Width  = -1;
	int DebugWindow_Height = -1;

	char strMainWindowTitle[64]  = "";
	char strDebugWindowTitle[64] = "";

	bool bAutomatedTestRun     = false;
	int NumAutomatedTestFrames = -1;
};