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

#include <cassert>

#ifdef _DEBUG
constexpr char* BUILD_CONFIG = "-Debug";
#else
constexpr char* BUILD_CONFIG = "";
#endif
constexpr char* VQENGINE_VERSION = "v0.5.0";


#define REPORT_SYSTEM_INFO 1
#if REPORT_SYSTEM_INFO 
void ReportSystemInfo(const VQSystemInfo::FSystemInfo& i, bool bDetailed = false)
{	
	const std::string sysInfo = VQSystemInfo::PrintSystemInfo(i, bDetailed);
	Log::Info("\n%s", sysInfo.c_str());
}
#endif
VQEngine::VQEngine()
	: mAssetLoader(mWorkers_Load, mRenderer)
{}

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
	CalculateEffectiveFrameRateLimit(mpWinMain->GetHWND());
	float f4 = t.Tick();

	// offload system info acquisition to a thread as it takes a few seconds on Debug build
	mWorkers_Update.AddTask([&]() 
	{
		this->mSysInfo = VQSystemInfo::GetSystemInfo();
#if REPORT_SYSTEM_INFO 
		ReportSystemInfo(this->mSysInfo);
#endif
		HWND hwnd = mpWinMain->GetHWND();
		if(!mpWinMain->IsClosed())
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
	if (mpWinDebug) RegisterWindowForInput(mpWinDebug);
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
	if (paramFile.bOverrideGFXSetting_bVSync)                      s.gfx.bVsync              = pf.gfx.bVsync;
	if (paramFile.bOverrideGFXSetting_bAA)                         s.gfx.bAntiAliasing       = pf.gfx.bAntiAliasing;
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
	if (Params.bOverrideGFXSetting_bVSync)                      s.gfx.bVsync               = p.gfx.bVsync;
	if (Params.bOverrideGFXSetting_bAA)                         s.gfx.bAntiAliasing        = p.gfx.bAntiAliasing;
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
		mResourceNames.mEnvironmentMapPresetNames.push_back(desc.Name);
	}
}

void VQEngine::InitializeScenes()
{
	std::vector<std::string>& mSceneNames = mResourceNames.mSceneNames;

	// Read Scene Index Mappings from file and initialize @mSceneNames
	{
		std::vector<std::pair<std::string, int>> SceneIndexMappings = VQEngine::ParseSceneIndexMappingFile();
		for (auto& nameIndex : SceneIndexMappings)
			mSceneNames.push_back(std::move(nameIndex.first));
	}

	// read scene files from disk: Data/Scenes/

	// set the selected scene index
	auto it2 = std::find_if(mSceneNames.begin(), mSceneNames.end(), [&](const std::string& scn) { return scn == mSettings.StartupScene; });
	bool bSceneFound = it2 != mSceneNames.end();
	if (!bSceneFound)
	{
		Log::Error("Couldn't find scene '%s' among scene file names", mSettings.StartupScene.c_str());
		it2 = mSceneNames.begin();
	}
	mIndex_SelectedScene = static_cast<int>(it2 - mSceneNames.begin());

	this->StartLoadingScene(mIndex_SelectedScene);
}

void VQEngine::InitializeThreads()
{
	const int NUM_SWAPCHAIN_BACKBUFFERS = mSettings.gfx.bUseTripleBuffering ? 3 : 2;
	const size_t HWThreads  = ThreadPool::sHardwareThreadCount;
	const size_t HWCores    = HWThreads / 2;
	const size_t NumWorkers = HWCores - 2; // reserve 2 cores for (Update + Render) + Main threads

	mpSemUpdate.reset(new Semaphore(NUM_SWAPCHAIN_BACKBUFFERS, NUM_SWAPCHAIN_BACKBUFFERS));
	mpSemRender.reset(new Semaphore(0                        , NUM_SWAPCHAIN_BACKBUFFERS));
	
	mbRenderThreadInitialized.store(false);
	mbStopAllThreads.store(false);
	mWorkers_Load.Initialize(NumWorkers, "LoadWorkers");
	mRenderThread = std::thread(&VQEngine::RenderThread_Main, this);
	mUpdateThread = std::thread(&VQEngine::UpdateThread_Main, this);
	mWorkers_Update.Initialize(NumWorkers, "UpdateWorkers");
	mWorkers_Render.Initialize(NumWorkers, "RenderWorkers");
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

