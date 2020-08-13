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

#include "../Scenes/Scenes.h"

#include "Libs/VQUtils/Source/utils.h"
#include "Libs/VQUtils/Libs/tinyxml2/tinyxml2.h"

#include <fstream>
#include <cassert>

#ifdef _DEBUG
constexpr char* BUILD_CONFIG = "-Debug";
#else
constexpr char* BUILD_CONFIG = "";
#endif
constexpr char* VQENGINE_VERSION = "v0.5.0";


static std::pair<std::string, std::string> ParseLineINI(const std::string& iniLine, bool* pbSectionTag)
{
	assert(!iniLine.empty());
	std::pair<std::string, std::string> SettingNameValuePair;

	const bool bSectionTag = iniLine[0] == '[';
	if(pbSectionTag) 
		*pbSectionTag = bSectionTag;

	if (bSectionTag)
	{
		auto vecSettingNameValue = StrUtil::split(iniLine.substr(1), ']');
		SettingNameValuePair.first = vecSettingNameValue[0];
	}
	else
	{
		auto vecSettingNameValue = StrUtil::split(iniLine, '=');
		assert(vecSettingNameValue.size() >= 2);
		SettingNameValuePair.first = vecSettingNameValue[0];
		SettingNameValuePair.second = vecSettingNameValue[1];
	}

	return SettingNameValuePair;
}

static std::unordered_map<std::string, EDisplayMode> S_LOOKUP_STR_TO_DISPLAYMODE =
{
	  { "Fullscreen"           , EDisplayMode::EXCLUSIVE_FULLSCREEN   }
	, { "Borderless"           , EDisplayMode::BORDERLESS_FULLSCREEN  }
	, { "BorderlessFullscreen" , EDisplayMode::BORDERLESS_FULLSCREEN  }
	, { "BorderlessWindowed"   , EDisplayMode::BORDERLESS_FULLSCREEN  }
	, { "Windowed"             , EDisplayMode::WINDOWED               }
};



#define REPORT_SYSTEM_INFO 1
#if REPORT_SYSTEM_INFO 
void ReportSystemInfo(const VQSystemInfo::FSystemInfo& i, bool bDetailed = false)
{	
	const std::string sysInfo = VQSystemInfo::PrintSystemInfo(i, bDetailed);
	Log::Info("\n%s", sysInfo.c_str());
}
#endif


void VQEngine::MainThread_Tick()
{
	if (this->mSettings.bAutomatedTestRun)
	{
		if (this->mSettings.NumAutomatedTestFrames <= mNumRenderLoopsExecuted)
		{
			PostQuitMessage(0);
		}
	}

	// Sleep(10);

	MainThread_HandleEvents();
}

bool VQEngine::Initialize(const FStartupParameters& Params)
{
	Timer  t;  t.Reset();  t.Start();
	Timer t2; t2.Reset(); t2.Start();

	InitializeEngineSettings(Params);
	InitializeEnvironmentMaps();
	InitializeHDRProfiles();
	float f1 = t.Tick();
	InitializeWindows(Params);
	float f3 = t.Tick();
	InitializeInput();
	InitializeScenes();
	float f2 = t.Tick();
	InitializeThreads();
	CalculateEffectiveFrameRate(mpWinMain->GetHWND());
	float f4 = t.Tick();

	// offload system info acquisition to a thread as it takes a few seconds on Debug build
	mWorkers_Update.AddTask([&]() 
	{
		this->mSysInfo = VQSystemInfo::GetSystemInfo();
#if REPORT_SYSTEM_INFO 
		ReportSystemInfo(this->mSysInfo);
#endif
		HWND hwnd = mpWinMain->GetHWND();
		mEventQueue_WinToVQE_Renderer.AddItem(std::make_shared<SetStaticHDRMetaDataEvent>(hwnd, this->GatherHDRMetaDataParameters(hwnd)));
	});
	float f0 = t.Tick();

#if 0
	Log::Info("[PERF] VQEngine::Initialize() : %.3fs", t2.StopGetDeltaTimeAndReset());
	Log::Info("[PERF]    DispatchSysInfo : %.3fs", f0);
	Log::Info("[PERF]    Settings       : %.3fs", f1);
	Log::Info("[PERF]    Scenes         : %.3fs", f2);
	Log::Info("[PERF]    Windows        : %.3fs", f3);
	Log::Info("[PERF]    Threads        : %.3fs", f4);
#endif
	return true; 
}

void VQEngine::Exit()
{
	ExitThreads();

	mRenderer.Unload();
	mRenderer.Exit();
}



void VQEngine::InitializeInput()
{
#if ENABLE_RAW_INPUT
	Input::InitRawInputDevices(mpWinMain->GetHWND());
#endif

	// initialize input states
	RegisterWindowForInput(mpWinMain);
	RegisterWindowForInput(mpWinDebug);
}

void VQEngine::InitializeEngineSettings(const FStartupParameters& Params)
{
	const FEngineSettings& p = Params.EngineSettings;

	// Defaults
	FEngineSettings& s = mSettings;
	s.gfx.bVsync = false;
	s.gfx.bUseTripleBuffering = true;
	s.gfx.RenderScale = 1.0f;
	s.gfx.MaxFrameRate = -1; // Auto

	s.WndMain.Width = 1920;
	s.WndMain.Height = 1080;
	s.WndMain.DisplayMode = EDisplayMode::WINDOWED;
	s.WndMain.PreferredDisplay = 0;
	s.WndMain.bEnableHDR = false;
	sprintf_s(s.WndMain.Title , "VQEngine %s%s", VQENGINE_VERSION, BUILD_CONFIG);

	s.WndDebug.Width  = 600;
	s.WndDebug.Height = 600;
	s.WndDebug.DisplayMode = EDisplayMode::WINDOWED;
	s.WndDebug.PreferredDisplay = 1;
	s.WndDebug.bEnableHDR = false;
	sprintf_s(s.WndDebug.Title, "VQDebugging");

	s.bAutomatedTestRun = false;
	s.NumAutomatedTestFrames = 100; // default num frames to run if -Test is specified in cmd line params

	s.StartupScene = "Default";

	// Override #0 : from file
	FStartupParameters paramFile = VQEngine::ParseEngineSettingsFile();
	const FEngineSettings& pf = paramFile.EngineSettings;
	if (paramFile.bOverrideGFXSetting_bVSync     )                 s.gfx.bVsync              = pf.gfx.bVsync;
	if (paramFile.bOverrideGFXSetting_bAA        )                 s.gfx.bAntiAliasing       = pf.gfx.bAntiAliasing;
	if (paramFile.bOverrideGFXSetting_bUseTripleBuffering)         s.gfx.bUseTripleBuffering = pf.gfx.bUseTripleBuffering;
	if (paramFile.bOverrideGFXSetting_RenderScale)                 s.gfx.RenderScale         = pf.gfx.RenderScale;
	if (paramFile.bOverrideGFXSetting_bMaxFrameRate)               s.gfx.MaxFrameRate        = pf.gfx.MaxFrameRate;

	if (paramFile.bOverrideENGSetting_MainWindowWidth)             s.WndMain.Width            = pf.WndMain.Width;
	if (paramFile.bOverrideENGSetting_MainWindowHeight)            s.WndMain.Height           = pf.WndMain.Height;
	if (paramFile.bOverrideENGSetting_bDisplayMode)                s.WndMain.DisplayMode      = pf.WndMain.DisplayMode;
	if (paramFile.bOverrideENGSetting_PreferredDisplay)            s.WndMain.PreferredDisplay = pf.WndMain.PreferredDisplay;
	if (paramFile.bOverrideGFXSetting_bHDR)                        s.WndMain.bEnableHDR       = pf.WndMain.bEnableHDR;

	if (paramFile.bOverrideENGSetting_bDebugWindowEnable)          s.bShowDebugWindow          = pf.bShowDebugWindow;
	if (paramFile.bOverrideENGSetting_DebugWindowWidth)            s.WndDebug.Width            = pf.WndDebug.Width;
	if (paramFile.bOverrideENGSetting_DebugWindowHeight)           s.WndDebug.Height           = pf.WndDebug.Height;
	if (paramFile.bOverrideENGSetting_DebugWindowDisplayMode)      s.WndDebug.DisplayMode      = pf.WndDebug.DisplayMode;
	if (paramFile.bOverrideENGSetting_DebugWindowPreferredDisplay) s.WndDebug.PreferredDisplay = pf.WndDebug.PreferredDisplay;

	if (paramFile.bOverrideENGSetting_bAutomatedTest)              s.bAutomatedTestRun = pf.bAutomatedTestRun;
	if (paramFile.bOverrideENGSetting_bTestFrames)
	{ 
		s.bAutomatedTestRun = true; 
		s.NumAutomatedTestFrames = pf.NumAutomatedTestFrames; 
	}

	if (paramFile.bOverrideENGSetting_StartupScene)              s.StartupScene = pf.StartupScene;

	// Override #1 : if there's command line params
	if (Params.bOverrideGFXSetting_bVSync     )                 s.gfx.bVsync               = p.gfx.bVsync;
	if (Params.bOverrideGFXSetting_bAA        )                 s.gfx.bAntiAliasing        = p.gfx.bAntiAliasing;
	if (Params.bOverrideGFXSetting_bUseTripleBuffering)         s.gfx.bUseTripleBuffering  = p.gfx.bUseTripleBuffering;
	if (Params.bOverrideGFXSetting_RenderScale)                 s.gfx.RenderScale          = p.gfx.RenderScale;
	if (Params.bOverrideGFXSetting_bMaxFrameRate)               s.gfx.MaxFrameRate         = p.gfx.MaxFrameRate;

	if (Params.bOverrideENGSetting_MainWindowWidth)             s.WndMain.Width            = p.WndMain.Width;
	if (Params.bOverrideENGSetting_MainWindowHeight)            s.WndMain.Height           = p.WndMain.Height;
	if (Params.bOverrideENGSetting_bDisplayMode)                s.WndMain.DisplayMode      = p.WndMain.DisplayMode;
	if (Params.bOverrideENGSetting_PreferredDisplay)            s.WndMain.PreferredDisplay = p.WndMain.PreferredDisplay;
	if (Params.bOverrideGFXSetting_bHDR)                        s.WndMain.bEnableHDR       = p.WndMain.bEnableHDR;
	
	if (Params.bOverrideENGSetting_bDebugWindowEnable)          s.bShowDebugWindow         = p.bShowDebugWindow;
	if (Params.bOverrideENGSetting_DebugWindowWidth)            s.WndDebug.Width           = p.WndDebug.Width;
	if (Params.bOverrideENGSetting_DebugWindowHeight)           s.WndDebug.Height          = p.WndDebug.Height;
	if (Params.bOverrideENGSetting_DebugWindowDisplayMode)      s.WndDebug.DisplayMode     = p.WndDebug.DisplayMode;
	if (Params.bOverrideENGSetting_DebugWindowPreferredDisplay) s.WndDebug.PreferredDisplay= p.WndDebug.PreferredDisplay;

	if (Params.bOverrideENGSetting_bAutomatedTest)              s.bAutomatedTestRun        = p.bAutomatedTestRun;
	if (Params.bOverrideENGSetting_bTestFrames)
	{
		s.bAutomatedTestRun = true;
		s.NumAutomatedTestFrames = p.NumAutomatedTestFrames;
	}

	if (Params.bOverrideENGSetting_StartupScene)             s.StartupScene           = p.StartupScene;
}

void VQEngine::InitializeWindows(const FStartupParameters& Params)
{
	mbMainWindowHDRTransitionInProgress.store(false);

	auto fnInitializeWindow = [&](const FWindowSettings& settings, HINSTANCE hInstance, std::unique_ptr<Window>& pWin, const std::string& WindowName)
	{
		FWindowDesc desc = {};
		desc.width = settings.Width;
		desc.height= settings.Height;
		desc.hInst = hInstance;
		desc.pWndOwner  = this;
		desc.pfnWndProc = WndProc;
		desc.bFullscreen = settings.DisplayMode == EDisplayMode::EXCLUSIVE_FULLSCREEN;
		desc.preferredDisplay = settings.PreferredDisplay;
		desc.iShowCmd = Params.iCmdShow;
		desc.windowName = WindowName;
		desc.pfnRegisterWindowName = &VQEngine::SetWindowName;
		desc.pRegistrar = this;
		pWin.reset(new Window(settings.Title, desc));
		pWin->pOwner->OnWindowCreate(pWin->GetHWND());
	};

	fnInitializeWindow(mSettings.WndMain, Params.hExeInstance, mpWinMain, "Main Window");
	Log::Info("Created main window<0x%x>: %dx%d", mpWinMain->GetHWND(), mpWinMain->GetWidth(), mpWinMain->GetHeight());

	if (mSettings.bShowDebugWindow)
	{
		fnInitializeWindow(mSettings.WndDebug, Params.hExeInstance, mpWinDebug, "Debug Window");
		Log::Info("Created debug window<0x%x>: %dx%d", mpWinDebug->GetHWND(), mpWinDebug->GetWidth(), mpWinDebug->GetHeight());
	}

	this->SetMouseCaptureForWindow(mpWinMain->GetHWND(), true);
}

void VQEngine::InitializeHDRProfiles()
{
	mDisplayHDRProfiles = VQEngine::ParseHDRProfilesFile();
}

void VQEngine::InitializeEnvironmentMaps()
{
	std::vector<FEnvironmentMapDescriptor> descs = VQEngine::ParseEnvironmentMapsFile();
	for (const FEnvironmentMapDescriptor& desc : descs)
	{
		mLookup_EnvironmentMapDescriptors[desc.Name] = desc;
		mEnvironmentMapPresetNames.push_back(desc.Name);
	}
}

void VQEngine::InitializeScenes()
{
	const int NUM_SWAPCHAIN_BACKBUFFERS = mSettings.gfx.bUseTripleBuffering ? 3 : 2;
	const Input&                  input = mInputStates.at(mpWinMain->GetHWND());

	auto fnCreateSceneInstance = [&](const std::string& SceneType, std::unique_ptr<Scene>& pScene) -> void
	{
		     if (SceneType == "Default")    pScene = std::make_unique<DefaultScene>(NUM_SWAPCHAIN_BACKBUFFERS, input, mpWinMain);
		else if (SceneType == "Sponza")     pScene = std::make_unique<SponzaScene >(NUM_SWAPCHAIN_BACKBUFFERS, input, mpWinMain);
	};


	// Read Scene Index Mappings from file and initialize @mSceneNames
	{
		std::vector<std::pair<std::string, int>> SceneIndexMappings = VQEngine::ParseSceneIndexMappingFile();
		for (auto& nameIndex : SceneIndexMappings)
			mSceneNames.push_back(std::move(nameIndex.first));
	}
	// read scene files from disk: Data/Scenes/
	std::vector< FSceneRepresentation> SceneReps = VQEngine::ParseScenesFile();

	// find out which scene to load
	auto it = std::find_if(SceneReps.begin(), SceneReps.end(), [&](const FSceneRepresentation& s) { return s.SceneName == mSettings.StartupScene; });
	bool bSceneFound = it != SceneReps.end();
	if (!bSceneFound)
	{
		Log::Error("Couldn't find scene '%s' among parsed scene files.", mSettings.StartupScene.c_str());
		Log::Warning("DefaultScene will be loaded");
		it = std::find_if(SceneReps.begin(), SceneReps.end(), [&](const FSceneRepresentation& s) { return s.SceneName == "Default"; });
		mSettings.StartupScene = "Default";
	}

	// Create the scene instance
	fnCreateSceneInstance(mSettings.StartupScene, mpScene);

	// queue the selected scene (@mSettings.StartupScene) for loading
	assert(it != SceneReps.end());
	mQueue_SceneLoad.push(*it);

	// set the selected scene index for easily 
	auto it2 = std::find_if(mSceneNames.begin(), mSceneNames.end(), [&](const std::string& scn) { return scn == mSettings.StartupScene; });
	bSceneFound = it2 != mSceneNames.end();
	if (!bSceneFound)
	{
		Log::Error("Couldn't find scene '%s' among scene file names", mSettings.StartupScene.c_str());
		it2 = mSceneNames.begin();
	}
	mIndex_SelectedScene = static_cast<int>(it2 - mSceneNames.begin());
}

void VQEngine::InitializeThreads()
{
	const int NUM_SWAPCHAIN_BACKBUFFERS = mSettings.gfx.bUseTripleBuffering ? 3 : 2;
	const size_t HWThreads  = ThreadPool::sHardwareThreadCount;
	const size_t HWCores    = HWThreads / 2;
	const size_t NumWorkers = HWCores - 2; // reserve 2 cores for (Update + Render) + Main threads

	mpSemUpdate.reset(new Semaphore(NUM_SWAPCHAIN_BACKBUFFERS, NUM_SWAPCHAIN_BACKBUFFERS));
	mpSemRender.reset(new Semaphore(0                        , NUM_SWAPCHAIN_BACKBUFFERS));

	mbStopAllThreads.store(false);
	mWorkers_Load.Initialize(NumWorkers);
	mRenderThread = std::thread(&VQEngine::RenderThread_Main, this);
	mUpdateThread = std::thread(&VQEngine::UpdateThread_Main, this);
	mWorkers_Update.Initialize(NumWorkers);
	mWorkers_Render.Initialize(NumWorkers);
}

void VQEngine::ExitThreads()
{
	mWorkers_Load.Exit();
	mbStopAllThreads.store(true);
	mRenderThread.join();
	mUpdateThread.join();

	mWorkers_Update.Exit();
	mWorkers_Render.Exit();
}




std::unique_ptr<Window>& VQEngine::GetWindow(HWND hwnd)
{
	if (mpWinMain->GetHWND() == hwnd)
		return mpWinMain;
	else if (mpWinDebug->GetHWND() == hwnd)
		return mpWinDebug;

	// TODO: handle other windows here when they're implemented

	Log::Warning("VQEngine::GetWindow() : Invalid hwnd=0x%x, returning Main Window", hwnd);
	return mpWinMain;
}

const std::unique_ptr<Window>& VQEngine::GetWindow(HWND hwnd) const
{
	if (mpWinMain->GetHWND() == hwnd)
		return mpWinMain;
	else if (mpWinDebug->GetHWND() == hwnd)
		return mpWinDebug;

	// TODO: handle other windows here when they're implemented

	Log::Warning("VQEngine::GetWindow() : Invalid hwnd=0x%x, returning Main Window", hwnd);
	return mpWinMain;
}

const FWindowSettings& VQEngine::GetWindowSettings(HWND hwnd) const
{
	if (mpWinMain->GetHWND() == hwnd)
		return mSettings.WndMain;
	else if (mpWinDebug->GetHWND() == hwnd)
		return mSettings.WndDebug;

	// TODO: handle other windows here when they're implemented

	Log::Warning("VQEngine::GetWindowSettings() : Invalid hwnd=0x%x, returning Main Window Settings", hwnd);
	return mSettings.WndMain;
}
FWindowSettings& VQEngine::GetWindowSettings(HWND hwnd)
{
	if (mpWinMain->GetHWND() == hwnd)
		return mSettings.WndMain;
	else if (mpWinDebug->GetHWND() == hwnd)
		return mSettings.WndDebug;

	// TODO: handle other windows here when they're implemented

	Log::Warning("VQEngine::GetWindowSettings() : Invalid hwnd=0x%x, returning Main Window Settings", hwnd);
	return mSettings.WndMain;
}

void VQEngine::RegisterWindowForInput(const std::unique_ptr<Window>& pWnd)
{
	if (!pWnd)
	{
		Log::Warning("RegisterWindowForInput() called with nullptr");
		return;
	}

	mInputStates.emplace(pWnd->GetHWND(), std::move(Input()));
}

void VQEngine::UnregisterWindowForInput(const std::unique_ptr<Window>& pWnd)
{
	if (pWnd)
	{
		HWND hwnd = pWnd->GetHWND();
		auto it = mInputStates.find(hwnd);
		if (it == mInputStates.end())
		{
			Log::Error("UnregisterWindowForInput() called with an unregistered Window<%x>", hwnd);
			return;
		}

		mInputStates.erase(it);
	}
}


FStartupParameters VQEngine::ParseEngineSettingsFile()
{
	constexpr char* ENGINE_SETTINGS_FILE_NAME = "Data/EngineSettings.ini";
	FStartupParameters params = {};

	std::ifstream file(ENGINE_SETTINGS_FILE_NAME);
	if (file.is_open())
	{
		std::string line;
		std::string currSection;
		bool bReadingSection = false;
		while (std::getline(file, line))
		{
			if (line[0] == ';') continue; // skip comment lines
			if (line == "") continue; // skip empty lines

			const std::pair<std::string, std::string> SettingNameValuePair = ParseLineINI(line, &bReadingSection);
			const std::string& SettingName  = SettingNameValuePair.first;
			const std::string& SettingValue = SettingNameValuePair.second;

			// Header sections;
			if (bReadingSection)
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
				params.EngineSettings.gfx.bVsync = StrUtil::ParseBool(SettingValue);
			}
			if (SettingName == "RenderScale")
			{
				params.bOverrideGFXSetting_RenderScale = true;
				params.EngineSettings.gfx.RenderScale = StrUtil::ParseFloat(SettingValue);
			}
			if (SettingName == "TripleBuffer")
			{
				params.bOverrideGFXSetting_bUseTripleBuffering = true;
				params.EngineSettings.gfx.bUseTripleBuffering = StrUtil::ParseBool(SettingValue);
			}
			if (SettingName == "AntiAliasing" || SettingName == "AA")
			{
				params.bOverrideGFXSetting_bAA = true;
				params.EngineSettings.gfx.bAntiAliasing = StrUtil::ParseBool(SettingValue);
			}
			if (SettingName == "MaxFrameRate" || SettingName == "MaxFPS")
			{
				params.bOverrideGFXSetting_bMaxFrameRate = true;
				if (SettingValue == "Unlimited" || SettingValue == "0")
					params.EngineSettings.gfx.MaxFrameRate = 0;
				else if (SettingValue == "Auto" || SettingValue == "Automatic" || SettingValue == "-1")
					params.EngineSettings.gfx.MaxFrameRate = -1;
				else
					params.EngineSettings.gfx.MaxFrameRate = StrUtil::ParseInt(SettingValue);
			}

			// 
			// Engine
			//
			if (SettingName == "Width")
			{
				params.bOverrideENGSetting_MainWindowWidth = true;
				params.EngineSettings.WndMain.Width = StrUtil::ParseInt(SettingValue);
			}
			if (SettingName == "Height")
			{
				params.bOverrideENGSetting_MainWindowHeight = true;
				params.EngineSettings.WndMain.Height = StrUtil::ParseInt(SettingValue);
			}
			if (SettingName == "DisplayMode")
			{
				if (S_LOOKUP_STR_TO_DISPLAYMODE.find(SettingValue) != S_LOOKUP_STR_TO_DISPLAYMODE.end())
				{
					params.bOverrideENGSetting_bDisplayMode = true;
					params.EngineSettings.WndMain.DisplayMode = S_LOOKUP_STR_TO_DISPLAYMODE.at(SettingValue);
				}
			}
			if (SettingName == "PreferredDisplay")
			{
				params.bOverrideENGSetting_PreferredDisplay = true;
				params.EngineSettings.WndMain.PreferredDisplay = StrUtil::ParseInt(SettingValue);
			}
			if (SettingName == "HDR")
			{
				params.bOverrideGFXSetting_bHDR = true;
				params.EngineSettings.WndMain.bEnableHDR = StrUtil::ParseBool(SettingValue);
			}


			if (SettingName == "DebugWindow")
			{
				params.bOverrideENGSetting_bDebugWindowEnable = true;
				params.EngineSettings.bShowDebugWindow = StrUtil::ParseBool(SettingValue);
			}
			if (SettingName == "DebugWindowWidth")
			{
				params.bOverrideENGSetting_DebugWindowWidth = true;
				params.EngineSettings.WndDebug.Width = StrUtil::ParseInt(SettingValue);
			}
			if (SettingName == "DebugWindowHeight")
			{
				params.bOverrideENGSetting_DebugWindowHeight = true;
				params.EngineSettings.WndDebug.Height = StrUtil::ParseInt(SettingValue);
			}
			if (SettingName == "DebugWindowDisplayMode")
			{
				if (S_LOOKUP_STR_TO_DISPLAYMODE.find(SettingValue) != S_LOOKUP_STR_TO_DISPLAYMODE.end())
				{
					params.bOverrideENGSetting_DebugWindowDisplayMode = true;
					params.EngineSettings.WndDebug.DisplayMode = S_LOOKUP_STR_TO_DISPLAYMODE.at(SettingValue);
				}
			}
			if (SettingName == "DebugWindowPreferredDisplay")
			{
				params.bOverrideENGSetting_DebugWindowPreferredDisplay = true;
				params.EngineSettings.WndDebug.PreferredDisplay = StrUtil::ParseInt(SettingValue);
			}

			if (SettingName == "Scene")
			{
				params.bOverrideENGSetting_StartupScene = true;
				params.EngineSettings.StartupScene = SettingValue;
			}
			
		}
	}
	else
	{
		Log::Warning("Cannot find settings file %s in current directory: %s", ENGINE_SETTINGS_FILE_NAME, DirectoryUtil::GetCurrentPath().c_str());
		Log::Warning("Will use default settings for Engine & Graphics.", ENGINE_SETTINGS_FILE_NAME, DirectoryUtil::GetCurrentPath().c_str());
	}

	file.close();

	return params;
}

std::vector<std::pair<std::string, int>> VQEngine::ParseSceneIndexMappingFile()
{
	constexpr char* SCENE_MAPPING_FILE_NAME = "Data/Scenes.ini";

	std::vector<std::pair<std::string, int>> SceneIndexMappings;

	std::ifstream file(SCENE_MAPPING_FILE_NAME);
	if (file.is_open())
	{
		std::string line;
		std::string currSection;
		bool bReadingSection = false;
		while (std::getline(file, line))
		{
			if (line[0] == ';') continue; // skip comment lines
			if (line == "") continue; // skip empty lines

			const std::pair<std::string, std::string> SettingNameValuePair = ParseLineINI(line, &bReadingSection);
			const std::string& SettingName = SettingNameValuePair.first;
			const std::string& SettingValue = SettingNameValuePair.second;

			// Header sections;
			if (bReadingSection)
			{
				currSection = SettingName;
				continue;
			}

			const int          SceneIndex = StrUtil::ParseInt(SettingValue);
			const std::string& SceneName = SettingName;
			SceneIndexMappings.push_back(std::make_pair(SceneName, SceneIndex));
		}
	}
	else
	{
		Log::Warning("Cannot find settings file %s in current directory: %s", SCENE_MAPPING_FILE_NAME, DirectoryUtil::GetCurrentPath().c_str());
		Log::Warning("Default scene mapping will be used.");
	}

	std::sort(SceneIndexMappings.begin(), SceneIndexMappings.end(), [](const auto& l, const auto& r) { return l.second < r.second; });

	return SceneIndexMappings;
}

std::vector<FEnvironmentMapDescriptor> VQEngine::ParseEnvironmentMapsFile()
{
	constexpr char* SETTINGS_FILE_NAME = "Data/EnvironmentMaps.ini";

	std::vector<FEnvironmentMapDescriptor> EnvironmentMapDescriptors;

	std::ifstream file(SETTINGS_FILE_NAME);
	if (file.is_open())
	{
		std::string line;
		bool bReadingSection = false;
		bool bRecentlyReadEmptyLine = false;
		bool bCurrentlyReadingEnvMap = false;
		FEnvironmentMapDescriptor desc = {};
		while (std::getline(file, line))
		{
			if (line[0] == ';') continue; // skip comment lines
			if (line == "") { bRecentlyReadEmptyLine = true; continue; } // skip empty lines

			const std::pair<std::string, std::string> SettingNameValuePair = ParseLineINI(line, &bReadingSection);
			const std::string& SettingName = SettingNameValuePair.first;
			const std::string& SettingValue = SettingNameValuePair.second;

			// Header sections;
			if (bReadingSection)
			{
				bCurrentlyReadingEnvMap = true;
				if (bRecentlyReadEmptyLine)
				{
					EnvironmentMapDescriptors.push_back(desc);
					desc = {};
					bRecentlyReadEmptyLine = false;
				}
				desc.Name = SettingName;
				continue;
			}

			if (SettingName == "Path")
			{
				desc.FilePath = SettingValue;
			}
			if (SettingName == "MaxCLL")
			{
				desc.MaxContentLightLevel = StrUtil::ParseFloat(SettingValue);
			}
			bRecentlyReadEmptyLine = false;
		}
		if (bCurrentlyReadingEnvMap)
		{
			EnvironmentMapDescriptors.push_back(desc);
			desc = {};
			bCurrentlyReadingEnvMap = false;
		}
	}
	else
	{ 
		Log::Error("Cannot find settings file %s in current directory: %s", SETTINGS_FILE_NAME, DirectoryUtil::GetCurrentPath().c_str());
	}

	return EnvironmentMapDescriptors;
}

std::vector<FDisplayHDRProfile> VQEngine::ParseHDRProfilesFile()
{
	constexpr char* SETTINGS_FILE_NAME = "Data/HDRDisplayProfiles.ini";

	std::vector<FDisplayHDRProfile> HDRProfiles;

	std::ifstream file(SETTINGS_FILE_NAME);
	if (file.is_open())
	{
		std::string line;
		bool bReadingSection = false; // Section is an .ini term
		bool bRecentlyReadEmptyLine = false;
		bool bCurrentlyReadingProfile = false;
		FDisplayHDRProfile profile = {};
		while (std::getline(file, line))
		{
			if (line[0] == ';') continue; // skip comment lines
			if (line == "") { bRecentlyReadEmptyLine = true; continue; } // skip empty lines

			const std::pair<std::string, std::string> SettingNameValuePair = ParseLineINI(line, &bReadingSection);
			const std::string& SettingName = SettingNameValuePair.first;
			const std::string& SettingValue = SettingNameValuePair.second;

			// Header sections;
			if (bReadingSection)
			{
				bCurrentlyReadingProfile = true;
				if (bRecentlyReadEmptyLine)
				{
					HDRProfiles.push_back(profile);
					profile = {};
					bRecentlyReadEmptyLine = false;
				}
				profile.DisplayName = SettingName;
				continue;
			}

			if (SettingName == "MaxBrightness")
			{
				profile.MaxBrightness = StrUtil::ParseFloat(SettingValue);
			}
			if (SettingName == "MinBrightness")
			{
				profile.MinBrightness = StrUtil::ParseFloat(SettingValue);
			}
			bRecentlyReadEmptyLine = false;
		}
		if (bCurrentlyReadingProfile) // Take into account the last item we're reading (.push_back() isn't called above for the last item)
		{
			HDRProfiles.push_back(profile);
			profile = {};
			bCurrentlyReadingProfile = false;
		}
	}
	else
	{
		Log::Error("Cannot find settings file %s in current directory: %s", SETTINGS_FILE_NAME, DirectoryUtil::GetCurrentPath().c_str());
	}
	return HDRProfiles;
}

std::vector< FSceneRepresentation> VQEngine::ParseScenesFile()
{
	using namespace DirectX;
	using namespace tinyxml2;
	//-----------------------------------------------------------------
	constexpr char* XML_TAG__SCENE                  = "Scene";
	constexpr char* XML_TAG__ENVIRONMENT_MAP        = "EnvironmentMap";
	constexpr char* XML_TAG__ENVIRONMENT_MAP_PRESET = "Preset";
	constexpr char* XML_TAG__CAMERA                 = "Camera";
	constexpr char* XML_TAG__GAMEOBJECT             = "GameObject";
	//-----------------------------------------------------------------
	constexpr char* SCENE_FILES_DIRECTORY          = "Data/Levels/";
	//-----------------------------------------------------------------
	      std::vector< FSceneRepresentation> SceneRepresentations;
	const std::vector< std::string>          SceneFiles = DirectoryUtil::ListFilesInDirectory(SCENE_FILES_DIRECTORY, ".xml");
	//-----------------------------------------------------------------

	// parse vectors --------------------------------------------------
	// e.g." 0.0 9 -1.0f" -> [0.0f, 9.0f, -1.0f]
	auto fnParseF3 = [](const std::string& xyz) -> XMFLOAT3
	{
		XMFLOAT3 f3;
		std::vector<std::string> tokens = StrUtil::split(xyz, ' ');
		assert(tokens.size() == 3);
		f3.x = StrUtil::ParseFloat(tokens[0]);
		f3.y = StrUtil::ParseFloat(tokens[1]);
		f3.z = StrUtil::ParseFloat(tokens[2]);
		return f3;
	};
	auto fnParseF4 = [](const std::string& xyzw) -> XMFLOAT4
	{
		XMFLOAT4 f4;
		std::vector<std::string> tokens = StrUtil::split(xyzw, ' ');
		assert(tokens.size() == 4);
		f4.x = StrUtil::ParseFloat(tokens[0]);
		f4.y = StrUtil::ParseFloat(tokens[1]);
		f4.z = StrUtil::ParseFloat(tokens[2]);
		f4.w = StrUtil::ParseFloat(tokens[3]);
		return f4;
	};
	// parse xml elements ---------------------------------------------
	auto fnParseXMLStringVal = [](XMLElement* pEle, std::string& dest)
	{
		XMLNode* pNode = pEle->FirstChild();
		if (pNode)
		{
			dest = pNode->Value();
		}
	};
	auto fnParseXMLFloatVal = [](XMLElement* pEle, float& dest)
	{
		XMLNode* pNode = pEle->FirstChild();
		if (pNode)
		{
			dest = StrUtil::ParseFloat(pNode->Value());
		}
	};
	auto fnParseXMLFloat3Val = [&](XMLElement* pEle, XMFLOAT3& f3)
	{
		XMLNode* pNode = pEle->FirstChild();
		if (pNode)
		{
			f3 = fnParseF3(pNode->Value());
		}
	};
	auto fnParseXMLFloat4Val = [&](XMLElement* pEle, XMFLOAT4& f4)
	{
		XMLNode* pNode = pEle->FirstChild();
		if (pNode)
		{
			f4 = fnParseF4(pNode->Value());
		}
	};
	// parse engine stuff -------------------------------------------
	auto fnParseTransform = [&](XMLElement* pTransform) -> Transform
	{
		Transform tf;

		XMLElement* pPos = pTransform->FirstChildElement("Position");
		XMLElement* pRot = pTransform->FirstChildElement("Quaternion");
		XMLElement* pScl = pTransform->FirstChildElement("Scale");
		if (pPos) fnParseXMLFloat3Val(pPos, tf._position);
		if (pScl) fnParseXMLFloat3Val(pScl, tf._scale);
		if (pRot)
		{
			XMFLOAT4 qf4; fnParseXMLFloat4Val(pRot, qf4);
			tf._rotation = Quaternion(qf4.w, XMFLOAT3(qf4.x, qf4.y, qf4.z));
		}
		return tf;
	};

	//-----------------------------------------------------------------

	// Start reading scene XML files
	for (const std::string SceneFile : SceneFiles)
	{
		FSceneRepresentation SceneRep = {};

		// parse XML
		tinyxml2::XMLDocument doc;
		doc.LoadFile(SceneFile.c_str());

		// scene name
		SceneRep.SceneName = DirectoryUtil::GetFileNameWithoutExtension(SceneFile);


		XMLElement* pScene = doc.FirstChildElement(XML_TAG__SCENE);
		if (pScene)
		{
			XMLElement* pCurrentSceneElement = pScene->FirstChildElement();
			if (!pCurrentSceneElement)
			{
				SceneRepresentations.push_back(SceneRep);
				continue;
			}

			do
			{
				// Environment Map
				const std::string CurrEle = pCurrentSceneElement->Value();
				if (XML_TAG__ENVIRONMENT_MAP == CurrEle)
				{
					XMLElement* pPreset = pCurrentSceneElement->FirstChildElement(XML_TAG__ENVIRONMENT_MAP_PRESET);
					if (pPreset)
					{
						fnParseXMLStringVal(pPreset, SceneRep.EnvironmentMapPreset);
					}
				}

				// Cameras
				else if (XML_TAG__CAMERA == CurrEle)
				{
					FCameraParameters cam;
					XMLElement*& pCam = pCurrentSceneElement;

					// transform
					XMLElement* pPos   = pCam->FirstChildElement("Position");
					XMLElement* pPitch = pCam->FirstChildElement("Pitch");
					XMLElement* pYaw   = pCam->FirstChildElement("Yaw");

					// projection
					XMLElement* pProj = pCam->FirstChildElement("Projection");
					XMLElement* pFoV  = pCam->FirstChildElement("FoV");
					XMLElement* pNear = pCam->FirstChildElement("Near");
					XMLElement* pFar  = pCam->FirstChildElement("Far");

					// attributes
					XMLElement* pFP     = pCam->FirstChildElement("FirstPerson");
					XMLElement* pTSpeed = pFP ? pFP->FirstChildElement("TranslationSpeed") : nullptr;
					XMLElement* pASpeed = pFP ? pFP->FirstChildElement("AngularSpeed")     : nullptr;
					XMLElement* pDrag   = pFP ? pFP->FirstChildElement("Drag")             : nullptr;

					// transform ----------------------------------------
					if (pPos)
					{
						XMFLOAT3 xyz;
						fnParseXMLFloat3Val(pPos, xyz);
						cam.x = xyz.x;
						cam.y = xyz.y;
						cam.z = xyz.z;
					}
					if (pPitch) fnParseXMLFloatVal(pPitch, cam.Pitch); 
					if (pYaw)   fnParseXMLFloatVal(pYaw, cam.Yaw);

					// projection----------------------------------------
					if(pProj)
					{
						std::string projVal;
						fnParseXMLStringVal(pProj, projVal);
						cam.bPerspectiveProjection = projVal == "Perspective";
					}
					if(pFoV ) fnParseXMLFloatVal(pFoV, cam.FovV_Degrees);
					if(pNear) fnParseXMLFloatVal(pNear, cam.NearPlane);
					if(pFar ) fnParseXMLFloatVal(pFar, cam.FarPlane);
						

					// attributes----------------------------------------
					if (pFP)
					{
						if(pTSpeed)  fnParseXMLFloatVal(pTSpeed, cam.TranslationSpeed);
						if(pASpeed)  fnParseXMLFloatVal(pASpeed, cam.AngularSpeed);
						if(pDrag  )  fnParseXMLFloatVal(pDrag  , cam.Drag);
					}

					SceneRep.Cameras.push_back(cam);
				}


				// Game Objects
				else if (XML_TAG__GAMEOBJECT == CurrEle)
				{
					GameObjectRepresentation obj;

					XMLElement*& pObj = pCurrentSceneElement;
					XMLElement* pTransform = pObj->FirstChildElement("Transform");
					XMLElement* pModel     = pObj->FirstChildElement("Model");

					if (pTransform)
					{
						obj.tf = fnParseTransform(pTransform);
					}

					// TODO: this is currently WIP
					if (pModel)
					{
						//obj.ModelName
					}

					SceneRep.Objects.push_back(obj);
				}

				pCurrentSceneElement = pCurrentSceneElement->NextSiblingElement();
			} while (pCurrentSceneElement);
		}

		SceneRepresentations.push_back(SceneRep);
	}


	return SceneRepresentations;
}
