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

#include <ctime>
#include <cstdlib>
#include <Windows.h>
#include <vector>
#include <string>

#include "Libs/VQUtils/Source/Log.h"
#include "Libs/VQUtils/Source/utils.h"
#include "Window.h"

#include "Platform.h"
#include "VQEngine.h"


void ParseCommandLineParameters(FStartupParameters& refStartupParams, PSTR pScmdl)
{
	const std::string StrCmdLineParams = pScmdl;
	const std::vector<std::string> params = StrUtil::split(StrCmdLineParams, ' ');
	for (const std::string& param : params)
	{
		const std::vector<std::string> paramNameValue = StrUtil::split(param, '=');
		const std::string& paramName = paramNameValue.front();
		std::string  paramValue = paramNameValue.size() > 1 ? paramNameValue[1] : "";

		//
		// Log Settings
		//
		if (paramName == "-LogConsole")
		{
			refStartupParams.LogInitParams.bLogConsole = true;
		}
		if (paramName == "-LogFile")
		{
			refStartupParams.LogInitParams.bLogFile = true;
			refStartupParams.LogInitParams.LogFilePath = std::move(paramValue);
		}

		//
		// Engine Settings
		//
		if (paramName == "-Test")
		{
			refStartupParams.bOverrideENGSetting_bAutomatedTest = true;
			refStartupParams.EngineSettings.bAutomatedTestRun = true;
		}
		if (paramName == "-TestFrames")
		{
			refStartupParams.bOverrideENGSetting_bAutomatedTest = true;
			refStartupParams.EngineSettings.bAutomatedTestRun = true;

			refStartupParams.bOverrideENGSetting_bTestFrames  = true;
			if (paramValue.empty())
			{
				constexpr int NUM_DEFAULT_TEST_FRAMES = 100;
				Log::Warning("Empty -TestFrames value specified, using default: %d", NUM_DEFAULT_TEST_FRAMES);
				refStartupParams.EngineSettings.NumAutomatedTestFrames = NUM_DEFAULT_TEST_FRAMES;
			}
			else
			{
				refStartupParams.EngineSettings.NumAutomatedTestFrames = std::atoi(paramValue.c_str());
			}
		}
		if (paramName == "-Width" || paramName == "-W")
		{
			refStartupParams.bOverrideENGSetting_MainWindowWidth = true;
			refStartupParams.EngineSettings.WndMain.Width = std::atoi(paramValue.c_str());

		}
		if (paramName == "-Height" || paramName == "-H")
		{
			refStartupParams.bOverrideENGSetting_MainWindowHeight = true;
			refStartupParams.EngineSettings.WndMain.Height = std::atoi(paramValue.c_str());
		}
		if (paramName == "-DebugWindow" || paramName == "-dwnd")
		{
			//refStartupParams.
		}
		if (paramName == "-Windowed" || paramName == "-windowed")
		{
			refStartupParams.bOverrideENGSetting_DebugWindowDisplayMode = true;
			refStartupParams.bOverrideENGSetting_bDisplayMode = true;
			refStartupParams.EngineSettings.WndMain.DisplayMode = EDisplayMode::WINDOWED;
			refStartupParams.EngineSettings.WndDebug.DisplayMode = EDisplayMode::WINDOWED;
		}

		//
		// Graphics Settings
		//
		if (paramName == "-Fullscreen" || paramName == "-FullScreen" || paramName == "-fullscreen")
		{
			refStartupParams.bOverrideENGSetting_bDisplayMode = true;
			refStartupParams.EngineSettings.WndMain.DisplayMode = EDisplayMode::EXCLUSIVE_FULLSCREEN;
		}
		if (paramName == "-VSync" || paramName == "-vsync" || paramName == "-Vsync")
		{
			refStartupParams.bOverrideGFXSetting_bVSync = true;
			if (paramValue.empty())
			{
				refStartupParams.EngineSettings.gfx.bVsync = true;
			}
			else
			{
				refStartupParams.EngineSettings.gfx.bVsync = StrUtil::ParseBool(paramValue);
			}
		}
		if (paramName == "-AntiAliasing" || paramName == "-AA")
		{
			refStartupParams.bOverrideGFXSetting_bAA = true;
			if (paramValue.empty())
			{
				refStartupParams.EngineSettings.gfx.bAntiAliasing = true;
			}
			else
			{
				refStartupParams.EngineSettings.gfx.bAntiAliasing = StrUtil::ParseBool(paramValue);
			}
		}
		if (paramName == "-TripleBuffering")
		{
			refStartupParams.bOverrideGFXSetting_bUseTripleBuffering = true;
			refStartupParams.EngineSettings.gfx.bUseTripleBuffering = true;
		}
		if (paramName == "-DoubleBuffering")
		{
			refStartupParams.bOverrideGFXSetting_bUseTripleBuffering = true;
			refStartupParams.EngineSettings.gfx.bUseTripleBuffering = false;
		}
		if (paramName == "-HDR")
		{
			refStartupParams.bOverrideGFXSetting_bHDR = true;
			refStartupParams.EngineSettings.WndMain.bEnableHDR = true;
		}
		if (paramName == "-MaxFrameRate" || paramName == "-MaxFPS")
		{
			refStartupParams.bOverrideGFXSetting_bMaxFrameRate = true;
			if (paramValue == "Unlimited" || paramValue == "0")
				refStartupParams.EngineSettings.gfx.MaxFrameRate = 0;
			else if (paramValue == "Auto" || paramValue == "Automatic" || paramValue == "-1")
				refStartupParams.EngineSettings.gfx.MaxFrameRate = -1;
			else
				refStartupParams.EngineSettings.gfx.MaxFrameRate = StrUtil::ParseInt(paramValue);
		}
	}
}


int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, PSTR pScmdl, int iCmdShow)
{
	FStartupParameters StartupParameters = {};
	StartupParameters.hExeInstance = hInst;
	StartupParameters.iCmdShow = iCmdShow;
	
	srand(static_cast<unsigned>(time(NULL)));
	
	ParseCommandLineParameters(StartupParameters, pScmdl);

	Log::Initialize(StartupParameters.LogInitParams);

	{
		VQEngine Engine = {};
		Engine.Initialize(StartupParameters);

		MSG msg;
		bool bQuit = false;
		while (!bQuit)
		{
			// https://docs.microsoft.com/en-us/windows/win32/learnwin32/window-messages
			// http://www.directxtutorial.com/Lesson.aspx?lessonid=9-1-4
			while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);

				if (msg.message == WM_QUIT)
				{
					Log::Info("WM_QUIT!");
					bQuit = true;
					break;
				}
			}

			Engine.MainThread_Tick();
		}

		Engine.Exit();
	}

	Log::Exit();

	//MessageBox(NULL, "EXIT", "Exit", MB_OK); // quick debugging

	return 0;
}
