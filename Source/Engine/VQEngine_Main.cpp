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
#include "GPUMarker.h"
#include "Scene/SceneViews.h"
#include "Scene/Scene.h"

#include "Core/FileParser.h"
#include "Core/Window.h"

#include "Renderer/Renderer.h"

#include "Libs/VQUtils/Include/utils.h"
#include "Libs/VQUtils/Include/Timer.h"
#include "Libs/DirectXCompiler/inc/dxcapi.h"
#include <cassert>

#ifdef _DEBUG
constexpr const char* BUILD_CONFIG = "-Debug";
#else
constexpr const char* BUILD_CONFIG = "";
#endif
constexpr const char* VQENGINE_VERSION = "v0.12.0";


#define REPORT_SYSTEM_INFO 1
#if REPORT_SYSTEM_INFO 
void ReportSystemInfo(const VQSystemInfo::FSystemInfo& i, bool bDetailed = false)
{
	SCOPED_CPU_MARKER("ReportSystemInfo");
	const std::string sysInfo = VQSystemInfo::PrintSystemInfo(i, bDetailed);
	Log::Info("\n%s", sysInfo.c_str());
}
#endif

// TODO: heed to W4 warnings, initialize the variables
VQEngine::VQEngine()
	: mAssetLoader(mWorkers_ModelLoading, mWorkers_MeshLoading, *mpRenderer)
	, mbBuiltinMeshGenFinished(false)
	, mpRenderer(std::make_unique<VQRenderer>())
	, mpTimer(std::make_unique<Timer>())
{}

void VQEngine::MainThread_Tick()
{
#if !VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	const uint64& mNumRenderLoopsExecuted = mNumSimulationTicks;
#endif

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
	SCOPED_CPU_MARKER("VQEngine.Initialize");

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	ThreadPool& WorkerThreads = mWorkers_Update;
#else
	ThreadPool& WorkerThreads = mWorkers_Simulation;
#endif
	InitializeEngineSettings(Params);
	InitializeEngineThreads();
	InitializeEnvironmentMaps();
	InitializeHDRProfiles();

	InitializeWindows(Params);
	InitializeInput();

	WorkerThreads.AddTask([=]() { InitializeScenes(); });

	// --------------------------------------------------------
	// Note: Device should be initialized from WinMain thread, 
	// otherwise device may be lost if launched from RenderDoc
	mpRenderer->Initialize(mSettings.gfx); // Device, Queues, Heaps, WorkerThreads
	// --------------------------------------------------------

	// offload system info acquisition to a thread as it takes a few seconds on Debug build
	WorkerThreads.AddTask([&]()
	{
		// Offload GetMonitorInfo() into thread as it takes the longest
		// Get others on the same thread.

		this->mSysInfo.CPU  = VQSystemInfo::GetCPUInfo();
		this->mSysInfo.GPUs = VQSystemInfo::GetGPUInfo();
		this->mSysInfo.RAM  = VQSystemInfo::GetRAMInfo();
		
		WorkerThreads.AddTask([&]()
		{
			this->mSysInfo = VQSystemInfo::GetSystemInfo();

#if REPORT_SYSTEM_INFO 
			ReportSystemInfo(this->mSysInfo);
#endif
			HWND hwnd = mpWinMain->GetHWND();
			if (!mpWinMain->IsClosed())
			{
				mEventQueue_WinToVQE_Renderer.AddItem(std::make_shared<SetStaticHDRMetaDataEvent>(hwnd, this->GatherHDRMetaDataParameters(hwnd)));
			}
		});
	});
	return true; 
}

void VQEngine::Destroy()
{
	ExitThreads();

	mpRenderer->Unload();
	mpRenderer->Destroy();
}



void VQEngine::InitializeInput()
{
	SCOPED_CPU_MARKER("InitializeInput");
#if ENABLE_RAW_INPUT
	Input::InitRawInputDevices(mpWinMain->GetHWND());
#endif

	// initialize input states
	RegisterWindowForInput(mpWinMain);
	if (mpWinDebug) RegisterWindowForInput(mpWinDebug);
}

void VQEngine::InitializeEngineSettings(const FStartupParameters& Params)
{
	SCOPED_CPU_MARKER("InitializeEngineSettings");
	const FEngineSettings& p = Params.EngineSettings;

	// Defaults
	FEngineSettings& s = mSettings;
	s.gfx.bVsync = false;
	s.gfx.bUseTripleBuffering = true;
	s.gfx.RenderResolutionScale = 1.0f;
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

	strncpy_s(s.StartupScene, "Default", sizeof(s.StartupScene));

	// Override #0 : from file
	FStartupParameters paramFile;
	FileParser::ParseEngineSettingsFile(paramFile);

	const FEngineSettings& pf = paramFile.EngineSettings;
	if (paramFile.bOverrideGFXSetting_bVSync)                      s.gfx.bVsync                   = pf.gfx.bVsync;
	if (paramFile.bOverrideGFXSetting_bAA)                         s.gfx.AntiAliasing             = pf.gfx.AntiAliasing;
	if (paramFile.bOverrideGFXSetting_bUseTripleBuffering)         s.gfx.bUseTripleBuffering      = pf.gfx.bUseTripleBuffering;
	if (paramFile.bOverrideGFXSetting_RenderScale)                 s.gfx.RenderResolutionScale    = pf.gfx.RenderResolutionScale;
	if (paramFile.bOverrideGFXSetting_bMaxFrameRate)               s.gfx.MaxFrameRate             = pf.gfx.MaxFrameRate;
	if (paramFile.bOverrideGFXSetting_EnvironmentMapResolution)    s.gfx.EnvironmentMapResolution = pf.gfx.EnvironmentMapResolution;
	if (paramFile.bOverrideGFXSettings_Reflections)                s.gfx.Reflections              = pf.gfx.Reflections;

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

	if (paramFile.bOverrideENGSetting_StartupScene)              strncpy_s(s.StartupScene, pf.StartupScene, sizeof(s.StartupScene));


	// Override #1 : if there's command line params
	if (Params.bOverrideGFXSetting_bVSync)                      s.gfx.bVsync               = p.gfx.bVsync;
	if (Params.bOverrideGFXSetting_bAA)                         s.gfx.AntiAliasing         = p.gfx.AntiAliasing;
	if (Params.bOverrideGFXSetting_bUseTripleBuffering)         s.gfx.bUseTripleBuffering  = p.gfx.bUseTripleBuffering;
	if (Params.bOverrideGFXSetting_RenderScale)                 s.gfx.RenderResolutionScale          = p.gfx.RenderResolutionScale;
	if (Params.bOverrideGFXSetting_bMaxFrameRate)               s.gfx.MaxFrameRate         = p.gfx.MaxFrameRate;
	if (Params.bOverrideGFXSettings_Reflections)                s.gfx.Reflections          = p.gfx.Reflections;

	if (Params.bOverrideENGSetting_MainWindowWidth)             s.WndMain.Width            = p.WndMain.Width;
	if (Params.bOverrideENGSetting_MainWindowHeight)            s.	WndMain.Height         = p.WndMain.Height;
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

	if (Params.bOverrideENGSetting_StartupScene)               strncpy_s(s.StartupScene, p.StartupScene, sizeof(s.StartupScene));
}

void VQEngine::InitializeWindows(const FStartupParameters& Params)
{
	SCOPED_CPU_MARKER("InitializeWindows");
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
	mSignalMainWindowCreated.NotifyAll();

	if (mSettings.bShowDebugWindow)
	{
		fnInitializeWindow(mSettings.WndDebug, Params.hExeInstance, mpWinDebug, "Debug Window");
		Log::Info("Created debug window<0x%x>: %dx%d", mpWinDebug->GetHWND(), mpWinDebug->GetWidth(), mpWinDebug->GetHeight());
	}

#if 0
	this->SetMouseCaptureForWindow(mpWinMain->GetHWND(), true);
#endif
}

void VQEngine::InitializeHDRProfiles()
{
	SCOPED_CPU_MARKER("ParseHDRProfilesFile");
	mDisplayHDRProfiles = FileParser::ParseHDRProfilesFile();
}

void VQEngine::InitializeEnvironmentMaps()
{
	SCOPED_CPU_MARKER("InitializeEnvironmentMaps");
	std::vector<FEnvironmentMapDescriptor> descs = FileParser::ParseEnvironmentMapsFile();
	for (const FEnvironmentMapDescriptor& desc : descs)
	{
		mLookup_EnvironmentMapDescriptors[desc.Name] = desc;
		mResourceNames.mEnvironmentMapPresetNames.push_back(desc.Name);
	}
}

void VQEngine::InitializeScenes()
{
	SCOPED_CPU_MARKER("InitializeScenes");
	std::vector<std::string>& mSceneNames = mResourceNames.mSceneNames;

	// Read Scene Index Mappings from file and initialize @mSceneNames
	{
		std::vector<std::pair<std::string, int>> SceneIndexMappings = FileParser::ParseSceneIndexMappingFile();
		for (auto& nameIndex : SceneIndexMappings)
			mSceneNames.push_back(std::move(nameIndex.first));
	}

	// read scene files from disk: Data/Scenes/

	// set the selected scene index
	auto it2 = std::find_if(mSceneNames.begin(), mSceneNames.end(), [&](const std::string& scn) { return scn == mSettings.StartupScene; });
	bool bSceneNameMatch = it2 != mSceneNames.end();
	if (!bSceneNameMatch)
	{
		// startup scene could be a std::string or an int index
		// if SceneName didn't match, check for index
		if (StrUtil::IsNumber(mSettings.StartupScene))
		{
			int iScene = StrUtil::ParseInt(mSettings.StartupScene);
			bSceneNameMatch = iScene >= 0 && iScene < mSceneNames.size();
			if (bSceneNameMatch)
				it2 = mSceneNames.begin() + iScene;
		}

		if (!bSceneNameMatch)
		{
			it2 = mSceneNames.begin();
			Log::Error("Couldn't find scene '%s' among scene file names, loading level '%s' by default.", mSettings.StartupScene, it2->c_str());
		}
	}
	mIndex_SelectedScene = static_cast<int>(it2 - mSceneNames.begin());

	this->StartLoadingScene(mIndex_SelectedScene);
}

void VQEngine::InitializeEngineThreads()
{
	SCOPED_CPU_MARKER("InitializeEngineThreads");
	const int NUM_SWAPCHAIN_BACKBUFFERS = mSettings.gfx.bUseTripleBuffering ? 3 : 2;
	const size_t HWThreads  = ThreadPool::sHardwareThreadCount;
	const size_t HWCores    = HWThreads / 2;
	const size_t NumRuntimeWorkers = HWCores - 1; // reserve 1 core for Update + Render threads

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	mpSemUpdate.reset(new Semaphore(NUM_SWAPCHAIN_BACKBUFFERS, NUM_SWAPCHAIN_BACKBUFFERS));
	mpSemRender.reset(new Semaphore(0                        , NUM_SWAPCHAIN_BACKBUFFERS));
	
	mbRenderThreadInitialized.store(false);
#endif
	mbStopAllThreads.store(false);

	mWorkers_ModelLoading.Initialize(HWCores    , "LoadWorkers_Model"  , 0xFFDDAA00);
	mWorkers_MeshLoading.Initialize(HWCores     , "LoadWorkers_Mesh"   , 0xFFEE2266);

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	mRenderThread = std::thread(&VQEngine::RenderThread_Main, this);
	mUpdateThread = std::thread(&VQEngine::UpdateThread_Main, this);
	mWorkers_Update.Initialize(NumRuntimeWorkers, "UpdateWorkers");
	SetThreadDescription(mWorkers_Update.native_handle(), StrUtil::ASCIIToUnicode("Update Thread").c_str());
	mWorkers_Render.Initialize(NumRuntimeWorkers, "RenderWorkers");
	SetThreadDescription(mWorkers_Render.native_handle(), StrUtil::ASCIIToUnicode("Render Thread").c_str());
#else
	mSimulationThread = std::thread(&VQEngine::SimulationThread_Main, this);
	SetThreadDescription(mSimulationThread.native_handle(), StrUtil::ASCIIToUnicode("Simulation Thread").c_str());
	mWorkers_Simulation.Initialize(NumRuntimeWorkers, "SimulationWorkers");
#endif
}

void VQEngine::ExitThreads()
{
	SCOPED_CPU_MARKER("ExitThreads");
	mWorkers_ModelLoading.Destroy();
	mWorkers_MeshLoading.Destroy();
	mbStopAllThreads.store(true);

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	mRenderThread.join();
	mUpdateThread.join();

	mWorkers_Update.Exit();
	mWorkers_Render.Exit();
#else
	mSimulationThread.join();
	mWorkers_Simulation.Destroy();
#endif
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

