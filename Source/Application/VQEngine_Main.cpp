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

#include "VQEngine.h"

#include "Libs/VQUtils/Source/utils.h"

#include <fstream>
#include <sstream>
#include <cassert>

#ifdef _DEBUG
constexpr char* BUILD_CONFIG = "Debug";
#else
constexpr char* BUILD_CONFIG = "Release";
#endif
constexpr char* VQENGINE_VERSION = "v0.0.0";


void VQEngine::MainThread_Tick()
{
	if (this->mSettings.bAutomatedTestRun)
	{
		if (this->mSettings.NumAutomatedTestFrames <= mNumRenderLoopsExecuted)
		{
			PostQuitMessage(0);
		}
	}

	// TODO: populate input queue and signal Update thread 
	//       to drain the buffered input from the queue
}

bool VQEngine::Initialize(const FStartupParameters& Params)
{
	InititalizeEngineSettings(Params);
	InitializeApplicationWindows(Params);
	InitializeThreads();

	return true;
}

void VQEngine::Exit()
{
	ExitThreads();
}



void VQEngine::InititalizeEngineSettings(const FStartupParameters& Params)
{
	// Defaults
	mSettings.gfx.bFullscreen = false;
	mSettings.gfx.bVsync = false;
	mSettings.gfx.bUseTripleBuffering = true;
	mSettings.gfx.RenderResolutionX = 1920;
	mSettings.gfx.RenderResolutionY = 1080;


	mSettings.bAutomatedTestRun = false;
	mSettings.NumAutomatedTestFrames = 100; // default num frames to run if -Test is specified in cmd line params
	mSettings.DebugWindow_Width  = 600;
	mSettings.DebugWindow_Height = 600;
	mSettings.MainWindow_Width = 1920;
	mSettings.MainWindow_Height = 1080;

	sprintf_s(mSettings.strMainWindowTitle , "VQEngine %s-%s", VQENGINE_VERSION, BUILD_CONFIG);
	sprintf_s(mSettings.strDebugWindowTitle, "VQDebugging");

	// Override #0 : from file
	FStartupParameters paramFile = VQEngine::ParseEngineSettingsFile();
	if (paramFile.bOverrideGFXSetting_bFullscreen)         mSettings.gfx.bFullscreen         = paramFile.EngineSettings.gfx.bFullscreen;
	if (paramFile.bOverrideGFXSetting_bVSync     )         mSettings.gfx.bVsync              = paramFile.EngineSettings.gfx.bVsync;
	if (paramFile.bOverrideGFXSetting_bUseTripleBuffering) mSettings.gfx.bUseTripleBuffering = paramFile.EngineSettings.gfx.bUseTripleBuffering;
	if (paramFile.bOverrideGFXSetting_Width)               mSettings.gfx.RenderResolutionX   = paramFile.EngineSettings.gfx.RenderResolutionX;
	if (paramFile.bOverrideGFXSetting_Height)              mSettings.gfx.RenderResolutionY   = paramFile.EngineSettings.gfx.RenderResolutionY;

	if (paramFile.bOverrideENGSetting_MainWindowWidth)    mSettings.MainWindow_Width  = paramFile.EngineSettings.MainWindow_Width;
	if (paramFile.bOverrideENGSetting_MainWindowHeight)   mSettings.MainWindow_Height = paramFile.EngineSettings.MainWindow_Height;
	if (paramFile.bOverrideENGSetting_bAutomatedTest)     mSettings.bAutomatedTestRun = paramFile.EngineSettings.bAutomatedTestRun;
	if (paramFile.bOverrideENGSetting_bTestFrames)
	{ 
		mSettings.bAutomatedTestRun = true; 
		mSettings.NumAutomatedTestFrames = paramFile.EngineSettings.NumAutomatedTestFrames; 
	}


	// Override #1 : if there's command line params
	if (Params.bOverrideGFXSetting_bFullscreen)         mSettings.gfx.bFullscreen         = Params.EngineSettings.gfx.bFullscreen;
	if (Params.bOverrideGFXSetting_bVSync     )         mSettings.gfx.bVsync              = Params.EngineSettings.gfx.bVsync;
	if (Params.bOverrideGFXSetting_bUseTripleBuffering) mSettings.gfx.bUseTripleBuffering = Params.EngineSettings.gfx.bUseTripleBuffering;
	if (Params.bOverrideGFXSetting_Width)               mSettings.gfx.RenderResolutionX   = Params.EngineSettings.gfx.RenderResolutionX;
	if (Params.bOverrideGFXSetting_Height)              mSettings.gfx.RenderResolutionY   = Params.EngineSettings.gfx.RenderResolutionY;

	if (Params.bOverrideENGSetting_MainWindowWidth)    mSettings.MainWindow_Width  = Params.EngineSettings.MainWindow_Width;
	if (Params.bOverrideENGSetting_MainWindowHeight)   mSettings.MainWindow_Height = Params.EngineSettings.MainWindow_Height;
	if (Params.bOverrideENGSetting_bAutomatedTest)     mSettings.bAutomatedTestRun = Params.EngineSettings.bAutomatedTestRun;
	if (Params.bOverrideENGSetting_bTestFrames)
	{
		mSettings.bAutomatedTestRun = true;
		mSettings.NumAutomatedTestFrames = Params.EngineSettings.NumAutomatedTestFrames;
	}
}

void VQEngine::InitializeApplicationWindows(const FStartupParameters& Params)
{
	FWindowDesc mainWndDesc = {};
	mainWndDesc.width  = mSettings.MainWindow_Width;
	mainWndDesc.height = mSettings.MainWindow_Height;
	mainWndDesc.hInst = Params.hExeInstance;
	mainWndDesc.pWndOwner = this;
	mainWndDesc.pfnWndProc = WndProc;
	mpWinMain.reset(new Window(mSettings.strMainWindowTitle, mainWndDesc));
	Log::Info("Created main window<0x%x>: %dx%d", mpWinMain->GetHWND(), mainWndDesc.width, mainWndDesc.height);

	mainWndDesc.width  = mSettings.DebugWindow_Width;
	mainWndDesc.height = mSettings.DebugWindow_Height;
	mpWinDebug.reset(new Window(mSettings.strDebugWindowTitle, mainWndDesc));
	Log::Info("Created debug window<0x%x>: %dx%d", mpWinDebug->GetHWND(), mainWndDesc.width, mainWndDesc.height);
}

void VQEngine::InitializeThreads()
{
	const int NUM_SWAPCHAIN_BACKBUFFERS = mSettings.gfx.bUseTripleBuffering ? 3 : 2;
	mpSemUpdate.reset(new Semaphore(NUM_SWAPCHAIN_BACKBUFFERS, NUM_SWAPCHAIN_BACKBUFFERS));
	mpSemRender.reset(new Semaphore(0                        , NUM_SWAPCHAIN_BACKBUFFERS));

	mbStopAllThreads.store(false);
	mRenderThread = std::thread(&VQEngine::RenderThread_Main, this);
	mUpdateThread = std::thread(&VQEngine::UpdateThread_Main, this);

	const size_t HWThreads = ThreadPool::sHardwareThreadCount;
	const size_t HWCores   = HWThreads/2;
	const size_t NumWorkers = HWCores - 2; // reserve 2 cores for (Update + Render) + Main threads
	mUpdateWorkerThreads.Initialize(NumWorkers);
	mRenderWorkerThreads.Initialize(NumWorkers);
}

void VQEngine::ExitThreads()
{
	mbStopAllThreads.store(true);
	mRenderThread.join();
	mUpdateThread.join();

	mUpdateWorkerThreads.Exit();
	mRenderWorkerThreads.Exit();
}

void VQEngine::LoadLoadingScreenData()
{
	// start loading loadingscreen data for each window 
	auto fMain = mUpdateWorkerThreads.AddTask([&]()
	{
		FLoadingScreenData data;
		data.SwapChainClearColor = { 0.0f, 0.2f, 0.4f, 1.0f };
		const int NumBackBuffer_WndMain = mRenderer.GetSwapChainBackBufferCountOfWindow(mpWinMain);
		mScene_MainWnd.mLoadingScreenData.resize(NumBackBuffer_WndMain, data);

		mWindowUpdateContextLookup[mpWinMain->GetHWND()] = &mScene_MainWnd;
		
	});

	Log::Info("Load_LoadingScreenData_Dispatch");

	if (mpWinDebug)
	{
		auto fDbg = mUpdateWorkerThreads.AddTask([&]()
		{
			FLoadingScreenData data;
			data.SwapChainClearColor = { 0.5f, 0.4f, 0.01f, 1.0f };
			const int NumBackBuffer_WndDbg = mRenderer.GetSwapChainBackBufferCountOfWindow(mpWinDebug);
			mScene_DebugWnd.mLoadingScreenData.resize(NumBackBuffer_WndDbg, data);

			mWindowUpdateContextLookup[mpWinDebug->GetHWND()] = &mScene_DebugWnd;
		});
		if(fDbg.valid()) fDbg.get();
	}

	// loading screen data must be loaded right away.
	if(fMain.valid()) fMain.get();

	Log::Info("Load_LoadingScreenData_Dispatch - DONE");

}


void VQEngine::Load_SceneData_Dispatch()
{
	mUpdateWorkerThreads.AddTask([&]() { Sleep(1000); Log::Info("Worker SLEEP done!"); }); // Test-task
	mUpdateWorkerThreads.AddTask([&]()
	{
		// TODO: initialize window scene data here for now, should update this to proper location later on (Scene probably?)
		FFrameData data[2];
		data[0].SwapChainClearColor = { 0.07f, 0.07f, 0.07f, 1.0f };
		data[1].SwapChainClearColor = { 0.80f, 0.45f, 0.01f, 1.0f };
		const int NumBackBuffer_WndMain = mRenderer.GetSwapChainBackBufferCountOfWindow(mpWinMain);
		const int NumBackBuffer_WndDbg = mRenderer.GetSwapChainBackBufferCountOfWindow(mpWinDebug);
		mScene_MainWnd.mFrameData.resize(NumBackBuffer_WndMain, data[0]);
		mScene_DebugWnd.mFrameData.resize(NumBackBuffer_WndDbg, data[1]);

		mWindowUpdateContextLookup[mpWinMain->GetHWND()] = &mScene_MainWnd;
		if(mpWinDebug) mWindowUpdateContextLookup[mpWinDebug->GetHWND()] = &mScene_DebugWnd;
	});
}

void VQEngine::Load_SceneData_Join()
{
}



static std::pair<std::string, std::string> ParseLineINI(const std::string& iniLine)
{
	assert(!iniLine.empty());
	std::pair<std::string, std::string> SettingNameValuePair;

	const bool bSectionTag = iniLine[0] == '[';

	if (bSectionTag)
	{
		auto vecSettingNameValue = StrUtil::split(iniLine.substr(1), ']');
		SettingNameValuePair.first = vecSettingNameValue[0];
	}
	else
	{
		auto vecSettingNameValue = StrUtil::split(iniLine, '=');
		assert(vecSettingNameValue.size() >= 2);
		SettingNameValuePair.first  = vecSettingNameValue[0];
		SettingNameValuePair.second = vecSettingNameValue[1];
	}


	return SettingNameValuePair;
}
static bool ParseBool(const std::string& s) { bool b; std::istringstream(s) >> b; return b; }
static int  ParseInt(const std::string& s) { return std::atoi(s.c_str()); }

FStartupParameters VQEngine::ParseEngineSettingsFile()
{
	constexpr char* ENGINE_SETTINGS_FILE_NAME = "Data/EngineSettings.ini";
	FStartupParameters params = {};

	std::ifstream file(ENGINE_SETTINGS_FILE_NAME);
	if (file.is_open())
	{
		std::string line;
		std::string currSection;
		while (std::getline(file, line))
		{
			if (line[0] == ';') continue; // skip comment lines
			if (line == "") continue; // skip empty lines

			const std::pair<std::string, std::string> SettingNameValuePair = ParseLineINI(line);
			const std::string& SettingName  = SettingNameValuePair.first;
			const std::string& SettingValue = SettingNameValuePair.second;

			// Header sections;
			if (SettingName == "Graphics" || SettingName == "Engine")
			{
				currSection = SettingName;
				continue;
			}


			// 
			// Graphics
			//
			if (SettingName == "VSync")
			{
				params.bOverrideGFXSetting_bVSync = true;
				params.EngineSettings.gfx.bVsync = ParseBool(SettingValue);
			}
			if (SettingName == "ResolutionX")
			{
				params.bOverrideGFXSetting_Width = true;
				params.EngineSettings.gfx.RenderResolutionX = ParseInt(SettingValue);
			}
			if (SettingName == "ResolutionY")
			{
				params.bOverrideGFXSetting_Height = true;
				params.EngineSettings.gfx.RenderResolutionY = ParseInt(SettingValue);
			}


			// 
			// Engine
			//
			if (SettingName == "Width")
			{
				params.bOverrideENGSetting_MainWindowWidth = true;
				params.EngineSettings.MainWindow_Width = ParseInt(SettingValue);
			}
			if (SettingName == "Height")
			{
				params.bOverrideENGSetting_MainWindowHeight = true;
				params.EngineSettings.MainWindow_Height = ParseInt(SettingValue);
			}
		}
	}
	else
	{
		Log::Warning("Cannot find settings file %s in current directory: %s", ENGINE_SETTINGS_FILE_NAME, DirectoryUtil::GetCurrentPath().c_str());
		Log::Warning("Will use default settings for Engine & Graphics.", ENGINE_SETTINGS_FILE_NAME, DirectoryUtil::GetCurrentPath().c_str());
	}

	return params;
}
