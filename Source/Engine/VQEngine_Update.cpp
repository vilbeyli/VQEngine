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

#define NOMINMAX

#include "VQEngine.h"
#include "Math.h"
#include "Scene/Scene.h"
#include "../Scenes/Scenes.h" // scene instances

#include "GPUMarker.h"

#include "Libs/VQUtils/Source/utils.h"

#include <algorithm>

#include <dwmapi.h>
#pragma comment(lib, "Dwmapi.lib")

using namespace DirectX;


void VQEngine::UpdateThread_Main()
{
	Log::Info("UpdateThread Created.");

	UpdateThread_Inititalize();

	float dt = 0.0f;
	bool bQuit = false;
	while (!mbStopAllThreads && !bQuit)
	{
		dt = mTimer.Tick(); // update timer

		UpdateThread_Tick(dt);

		// UpdateThread_Logging()
		constexpr int LOGGING_PERIOD = 4; // seconds
		static float LAST_LOG_TIME = 0;
		const float TotalTime = mTimer.TotalTime();
		if (TotalTime - LAST_LOG_TIME > 4)
		{
			Log::Info("UpdateTick() : dt=%.2f ms", (dt * 1000.0f) /* - (dt_RenderWaitTime * 1000.0f)*/);
			LAST_LOG_TIME = TotalTime;
		}
	}

	UpdateThread_Exit();
	Log::Info("UpdateThread_Main() : Exit");
}

void VQEngine::UpdateThread_Inititalize()
{
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	mNumUpdateLoopsExecuted.store(0);
#endif
	mbLoadingEnvironmentMap.store(false);

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	// TODO: remove busy lock
	// busy lock until render thread is initialized
	while (!mbRenderThreadInitialized); 
#endif

	InitializeUI(mpWinMain->GetHWND());

	// immediately load loading screen texture
	LoadLoadingScreenData();

	mTimer.Reset();
	mTimer.Start();
}

void VQEngine::UpdateThread_Tick(const float dt)
{
	float dt_RenderWaitTime = 0.0f;

	SCOPED_CPU_MARKER_C("UpdateThread_Tick()", 0xFF000077);

	dt_RenderWaitTime = UpdateThread_WaitForRenderThread();

	UpdateThread_HandleEvents();

	UpdateThread_PreUpdate();

#if DEBUG_LOG_THREAD_SYNC_VERBOSE
	Log::Info(/*"UpdateThread_Tick() : */"u%d (r=%llu)", mNumUpdateLoopsExecuted.load(), mNumRenderLoopsExecuted.load());
#endif

	UpdateThread_UpdateAppState(dt);

	UpdateThread_PostUpdate();

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	++mNumUpdateLoopsExecuted;

	UpdateThread_SignalRenderThread();
#endif
}

void VQEngine::UpdateThread_Exit()
{
	mpScene->Unload();
	ExitUI();
}

void VQEngine::UpdateThread_PreUpdate()
{
	SCOPED_CPU_MARKER("UpdateThread_PreUpdate()");

	const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCount(mpWinMain->GetHWND());

	if (mpScene)
	{
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
		const int FRAME_DATA_INDEX = mNumUpdateLoopsExecuted % NUM_BACK_BUFFERS;
		const int FRAME_DATA_PREV_INDEX = (FRAME_DATA_INDEX == 0) ? (NUM_BACK_BUFFERS - 1) : (FRAME_DATA_INDEX - 1);
		mpScene->PreUpdate(FRAME_DATA_INDEX, FRAME_DATA_PREV_INDEX);
#else
		mpScene->PreUpdate(0, 0);
#endif
	}
}

void VQEngine::UpdateThread_UpdateAppState(const float dt)
{
	SCOPED_CPU_MARKER("UpdateThread_UpdateAppState()");

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	assert(mbRenderThreadInitialized);
#endif

	switch (mAppState)
	{
	case EAppState::INITIALIZING:
		Log::Info("UpdateThread: loading...");
		Load_SceneData_Dispatch(); // start load level
		mAppState = EAppState::LOADING;
		break;
	case EAppState::LOADING:
		if (mbLoadingLevel || mbLoadingEnvironmentMap)
		{
			// animate loading screen


			// check if loading is done
			const int NumActiveTasks = mWorkers_ModelLoading.GetNumActiveTasks() + mWorkers_TextureLoading.GetNumActiveTasks();
			const bool bLoadTasksFinished = NumActiveTasks == 0;
			if (bLoadTasksFinished)
			{
				if (mbLoadingLevel)
				{
					mpScene->OnLoadComplete();
				}
				// OnEnvMapLoaded = noop

				WaitUntilRenderingFinishes();
				mAppState = EAppState::SIMULATING;

				mbLoadingLevel.store(false);
				mbLoadingEnvironmentMap.store(false);

				mLoadingScreenData.RotateLoadingScreenImageIndex();

				float dt_loading = mTimer.StopGetDeltaTimeAndReset();
				Log::Info("Loading completed in %.2fs, starting scene simulation", dt_loading);
				mTimer.Start();
			}
		}
		break;
	case EAppState::SIMULATING:
		// TODO: threaded?
		UpdateThread_UpdateScene_MainWnd(dt);
		UpdateThread_UpdateScene_DebugWnd(dt);
		break;
	}
}

void VQEngine::UpdateThread_PostUpdate()
{
	SCOPED_CPU_MARKER("UpdateThread_PostUpdate()");

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCount(mpWinMain->GetHWND());
	const int FRAME_DATA_INDEX = mNumUpdateLoopsExecuted % NUM_BACK_BUFFERS;
	ThreadPool& mWorkerThreads = mWorkers_Update;
#else
	const int FRAME_DATA_INDEX = 0;
	ThreadPool& mWorkerThreads = mWorkers_Simulation;
#endif

	if (mbLoadingLevel)
	{
		return;
	}

	mpScene->PostUpdate(mWorkerThreads, FRAME_DATA_INDEX);

	// input post update
	for (auto it = mInputStates.begin(); it != mInputStates.end(); ++it)
	{
		const HWND& hwnd = it->first;
		mInputStates.at(hwnd).PostUpdate(); // non-const accessor
	}
}

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
float VQEngine::UpdateThread_WaitForRenderThread()
{
	SCOPED_CPU_MARKER("UpdateThread_WaitForRenderThread()");

#if DEBUG_LOG_THREAD_SYNC_VERBOSE
	Log::Info("u:wait : u=%llu, r=%llu", mNumUpdateLoopsExecuted.load(), mNumRenderLoopsExecuted.load());
#endif

	if (mbStopAllThreads)
		return 0.0f;

	Timer t;
	t.Start();
	mpSemUpdate->Wait();
	t.Stop();
	return t.DeltaTime();
}
void VQEngine::UpdateThread_SignalRenderThread()
{
	mpSemRender->Signal();
}

void VQEngine::WaitUntilRenderingFinishes()
{
	while (mNumRenderLoopsExecuted != mNumUpdateLoopsExecuted);
}
#else
float VQEngine::UpdateThread_WaitForRenderThread() { return 0.0f; }
void VQEngine::UpdateThread_SignalRenderThread(){}
void VQEngine::WaitUntilRenderingFinishes(){}
#endif

// -------------------------------------------------------------------
#if !VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
constexpr int FRAME_DATA_INDEX = 0;
#endif

bool VQEngine::IsWindowRegistered(HWND hwnd) const
{
	auto it = mWinNameLookup.find(hwnd);
	return it != mWinNameLookup.end();
}

bool VQEngine::ShouldRenderHDR(HWND hwnd) const
{
	const auto& pWin = this->GetWindow(hwnd);
	return IsHDRSettingOn() && pWin->GetIsOnHDRCapableDisplay();
}

bool VQEngine::IsHDRSettingOn() const
{
	return mSettings.WndMain.bEnableHDR;
}

void VQEngine::SetEffectiveFrameRateLimit()
{
	if (mSettings.gfx.MaxFrameRate == -1)
	{
		// Get monitor refresh rate (primary monitor?)
		DWM_TIMING_INFO dti = {};
		dti.cbSize = sizeof(DWM_TIMING_INFO);
		HRESULT hr = DwmGetCompositionTimingInfo(NULL, &dti);
		assert(dti.rateRefresh.uiDenominator != 0 && dti.rateRefresh.uiNumerator != 0);
		const float DisplayRefreshRate = static_cast<float>(dti.rateRefresh.uiNumerator) / dti.rateRefresh.uiDenominator;
		Log::Info("Getting Monitor Refresh Rate: %.1fHz", DisplayRefreshRate);
		mEffectiveFrameRateLimit_ms = 1000.0f / (DisplayRefreshRate * 1.15f);
	}
	else if (mSettings.gfx.MaxFrameRate == 0)
	{
		mEffectiveFrameRateLimit_ms = 0.0f;
	}
	else
	{
		mEffectiveFrameRateLimit_ms = 1000.0f / mSettings.gfx.MaxFrameRate;
	}
	const bool bUnlimitedFrameRate = mEffectiveFrameRateLimit_ms == 0.0f;
	if (bUnlimitedFrameRate) Log::Info("FrameRateLimit : Unlimited");
	else                     Log::Info("FrameRateLimit : %.2fms | %d FPS", mEffectiveFrameRateLimit_ms, static_cast<int>(1000.0f / mEffectiveFrameRateLimit_ms));
}

float VQEngine::FramePacing(float dt)
{
	float SleepTime = 0.0f;
	if (mEffectiveFrameRateLimit_ms != 0.0f)
	{
		SCOPED_CPU_MARKER_C("Sleep (FrameLimiter)", 0xFF552200);
		const float TimeBudgetLeft_ms = mEffectiveFrameRateLimit_ms - dt;
		if (TimeBudgetLeft_ms > 0.0f)
		{
#if 0
			// ------------------------------------------------------------------
			//                          WARNING
			// ------------------------------------------------------------------
			// The OS call for Sleep(ms) doesn't yield back in time and the frame
			// time limiter doesn't work as precise, resulting in frame times of
			// 20, 30, 60 and 1000. This is because OS doesn't guarantee exact
			// timing for the Sleep(), hence we should use it with Sleep(0)
			// with an accumulator
			SleepTime = mTimerRender.TotalTime();
			Sleep((DWORD)TimeBudgetLeft_ms);
			SleepTime = mTimerRender.TotalTime() - SleepTime;
			// ------------------------------------------------------------------
#else
			float Acc = TimeBudgetLeft_ms / 1000;
			Timer SleepTimer;
			SleepTimer.Start();
			while (Acc > 0.0f)
			{
				Sleep(0);
				Acc -= SleepTimer.Tick();
			}
#endif
		}
	}
	return SleepTime;
}

const FDisplayHDRProfile* VQEngine::GetHDRProfileIfExists(const wchar_t* pwStrLogicalDisplayName)
{
	const FDisplayHDRProfile* pProfile = nullptr;
	const std::string LogicalDisplayNameToMatch = StrUtil::UnicodeToASCII<256>(pwStrLogicalDisplayName);
	for (const FDisplayHDRProfile& profile : mDisplayHDRProfiles)
	{
		profile.DisplayName;
		for (const VQSystemInfo::FMonitorInfo& dInfo : mSysInfo.Monitors)
		{
			if (dInfo.LogicalDeviceName == LogicalDisplayNameToMatch)
			{
				return &profile;
			}
		}
	}
	return pProfile;
}

// ---------------------------------------------------------------------

void VQEngine::UpdateThread_UpdateScene_MainWnd(const float dt)
{
	std::unique_ptr<Window>& pWin = mpWinMain;
	HWND hwnd = pWin->GetHWND();

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCount(hwnd);
	const int FRAME_DATA_INDEX = mNumUpdateLoopsExecuted % NUM_BACK_BUFFERS;
#else
	const int FRAME_DATA_INDEX = 0;
#endif

	mpScene->Update(dt, FRAME_DATA_INDEX);

	HandleEngineInput(); // system-wide input (esc/mouse click on wnd)
	for (decltype(mInputStates)::iterator it = mInputStates.begin(); it != mInputStates.end(); ++it)
	{
		HWND   hwnd = it->first;
		Input& input = it->second;
		auto& pWin = this->GetWindow(hwnd);
		const bool bIsShiftDown = input.IsKeyDown("Shift");

		if (pWin == mpWinMain)
			HandleMainWindowInput(input, hwnd);
	}
	HandleUIInput();
}

void VQEngine::UpdateThread_UpdateScene_DebugWnd(const float dt)
{
	if (!mpWinDebug) return;

	std::unique_ptr<Window>& pWin = mpWinDebug;
	HWND hwnd = pWin->GetHWND();
	const Input& input = mInputStates.at(hwnd);
}


void VQEngine::Load_SceneData_Dispatch()
{
	if (mQueue_SceneLoad.empty())
		return;

	const std::string SceneFileName = mQueue_SceneLoad.front();
	mQueue_SceneLoad.pop();


	const int NUM_SWAPCHAIN_BACKBUFFERS = mSettings.gfx.bUseTripleBuffering ? 3 : 2;
	const Input& input = mInputStates.at(mpWinMain->GetHWND());

	auto fnCreateSceneInstance = [&](const std::string& SceneType, std::unique_ptr<Scene>& pScene) -> void
	{
		     if (SceneType == "Default")          pScene = std::make_unique<DefaultScene>(*this, NUM_SWAPCHAIN_BACKBUFFERS, input, mpWinMain, mRenderer);
		else if (SceneType == "Sponza")           pScene = std::make_unique<SponzaScene >(*this, NUM_SWAPCHAIN_BACKBUFFERS, input, mpWinMain, mRenderer);
		else if (SceneType == "StressTest")       pScene = std::make_unique<StressTestScene >(*this, NUM_SWAPCHAIN_BACKBUFFERS, input, mpWinMain, mRenderer);
		else if (SceneType == "EnvironmentMapUnitTest") pScene = std::make_unique<EnvironmentMapUnitTestScene >(*this, NUM_SWAPCHAIN_BACKBUFFERS, input, mpWinMain, mRenderer);
	};

	const bool bUpscalingEnabled = mpScene ? mpScene->GetPostProcessParameters(0).IsFSREnabled() : false;
	if (mpScene)
	{
		this->WaitUntilRenderingFinishes();
		mpScene->Unload(); // is this really necessary when we fnCreateSceneInstance() ?
	}

	// load scene representation from disk
	const std::string SceneFilePath = "Data/Levels/" + SceneFileName + ".xml";
	FSceneRepresentation SceneRep = VQEngine::ParseSceneFile(SceneFilePath);
	fnCreateSceneInstance(SceneRep.SceneName, mpScene);

	//----------------------------------------------------------------------
	// Workaround
	// ----------
	// PostProcess settings won't carry over when the level is changed.
	// When Upscaling is enabled in the previous scene, the next scene won't
	// know about it, hence we just pre-emptively send a WindowResize event
	// to re-initialize the render-resolution-dependent resources with their
	// proper size.
	if (bUpscalingEnabled)
	{
		const uint32 W = mpWinMain->GetWidth();
		const uint32 H = mpWinMain->GetHeight();
		mEventQueue_WinToVQE_Renderer.AddItem(std::make_unique<WindowResizeEvent>(W, H, mpWinMain->GetHWND()));
		mEventQueue_WinToVQE_Update.AddItem(std::make_unique<WindowResizeEvent>(W, H, mpWinMain->GetHWND()));
	}
	//----------------------------------------------------------------------
	

	// start loading textures, models, materials with worker threads
	mpScene->StartLoading(this->mBuiltinMeshes, SceneRep);

	// start loading environment map textures
	if (!SceneRep.EnvironmentMapPreset.empty())
	{
		mWorkers_TextureLoading.AddTask([=]() { LoadEnvironmentMap(SceneRep.EnvironmentMapPreset, mSettings.gfx.EnvironmentMapResolution); });
	}
}

SRV_ID FLoadingScreenData::GetSelectedLoadingScreenSRV_ID() const
{
	assert(SelectedLoadingScreenSRVIndex < SRVs.size());
	return SRVs[SelectedLoadingScreenSRVIndex];
}
void FLoadingScreenData::RotateLoadingScreenImageIndex()
{
	SelectedLoadingScreenSRVIndex = (int)MathUtil::RandU(0, (int)SRVs.size());
}
void VQEngine::LoadLoadingScreenData()
{
	FLoadingScreenData& data = mLoadingScreenData;

	data.SwapChainClearColor = { 0.0f, 0.2f, 0.4f, 1.0f };

	constexpr uint NUM_LOADING_SCREEN_BACKGROUNDS = 4;

	srand(static_cast<unsigned>(time(NULL)));
	const std::string LoadingScreenTextureFileDirectory = "Data/Textures/LoadingScreen/";
	const size_t SelectedLoadingScreenIndex = MathUtil::RandU(0u, NUM_LOADING_SCREEN_BACKGROUNDS);

	// dispatch background workers for other 
	for (size_t i = 0; i < NUM_LOADING_SCREEN_BACKGROUNDS; ++i)
	{
		if (i == SelectedLoadingScreenIndex)
			continue; // will be loaded on this thread

		const std::string LoadingScreenTextureFilePath = LoadingScreenTextureFileDirectory + (std::to_string(i) + ".png");

		mWorkers_TextureLoading.AddTask([this, &data, LoadingScreenTextureFilePath]()
			{
				const TextureID texID = mRenderer.CreateTextureFromFile(LoadingScreenTextureFilePath.c_str());
				const SRV_ID srvID = mRenderer.AllocateAndInitializeSRV(texID);
				std::lock_guard<std::mutex> lk(data.Mtx);
				data.SRVs.push_back(srvID);
			});

	}

	// load the selected loading screen image
	{
		const std::string LoadingScreenTextureFilePath = LoadingScreenTextureFileDirectory + (std::to_string(SelectedLoadingScreenIndex) + ".png");
		TextureID texID = mRenderer.CreateTextureFromFile(LoadingScreenTextureFilePath.c_str());
		SRV_ID    srvID = mRenderer.AllocateAndInitializeSRV(texID);
		std::lock_guard<std::mutex> lk(data.Mtx);
		data.SRVs.push_back(srvID);
		data.SelectedLoadingScreenSRVIndex = static_cast<int>(data.SRVs.size() - 1);
	}

}


FSetHDRMetaDataParams VQEngine::GatherHDRMetaDataParameters(HWND hwnd)
{
	FSetHDRMetaDataParams params;

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	while (!mbRenderThreadInitialized); // wait until renderer is initialized
#endif

	const SwapChain& Swapchain = mRenderer.GetWindowSwapChain(hwnd);
	const DXGI_OUTPUT_DESC1 desc = Swapchain.GetContainingMonitorDesc();
	const FDisplayHDRProfile* pProfile = GetHDRProfileIfExists(desc.DeviceName);

	params.MaxOutputNits = pProfile ? pProfile->MaxBrightness : desc.MaxLuminance;
	params.MinOutputNits = pProfile ? pProfile->MinBrightness : desc.MinLuminance;

	const bool bHDREnvironmentMap = mResources_MainWnd.EnvironmentMap.Tex_HDREnvironment != INVALID_ID;
	if (bHDREnvironmentMap)
	{
		params.MaxContentLightLevel = static_cast<float>(mResources_MainWnd.EnvironmentMap.MaxContentLightLevel);
	}

	params.MaxFrameAverageLightLevel = 80.0f; // TODO: dynamic calculation using histograms?

	return params;
}

void VQEngine::SetWindowName(HWND hwnd, const std::string& name) { mWinNameLookup[hwnd] = name; }
void VQEngine::SetWindowName(const std::unique_ptr<Window>& pWin, const std::string& name) { SetWindowName(pWin->GetHWND(), name); }

const std::string& VQEngine::GetWindowName(HWND hwnd) const
{
#if _DEBUG
	auto it = mWinNameLookup.find(hwnd);
	if (it == mWinNameLookup.end())
	{
		Log::Error("Couldn't find window<%x> name: HWND not called with SetWindowName()!", hwnd);
		assert(false); // gonna crash at .at() call anyways.
	}
#endif
	return mWinNameLookup.at(hwnd);
}


MeshID VQEngine::GetBuiltInMeshID(const std::string& MeshName) const
{
	const auto it = std::find(mResourceNames.mBuiltinMeshNames.begin(), mResourceNames.mBuiltinMeshNames.end(), MeshName);
	if (it == mResourceNames.mBuiltinMeshNames.end())
	{
		Log::Error("Builtin Mesh Not Found: %s", MeshName.c_str());
		return INVALID_ID;
	}
	return static_cast<MeshID>(it - mResourceNames.mBuiltinMeshNames.begin());
}

void VQEngine::StartLoadingEnvironmentMap(int IndexEnvMap)
{
	assert(IndexEnvMap >= 0 && IndexEnvMap < mResourceNames.mEnvironmentMapPresetNames.size());
	this->WaitUntilRenderingFinishes();
	mAppState = EAppState::LOADING;
	mbLoadingEnvironmentMap = true;
	mWorkers_TextureLoading.AddTask([&, IndexEnvMap]()
	{
		LoadEnvironmentMap(mResourceNames.mEnvironmentMapPresetNames[IndexEnvMap], mSettings.gfx.EnvironmentMapResolution);
	});
}

void VQEngine::StartLoadingScene(int IndexScene)
{
	assert(IndexScene >= 0 && IndexScene < mResourceNames.mSceneNames.size());

	// get scene representation 
	const std::string& SceneName = mResourceNames.mSceneNames[IndexScene];

	// queue the selected scene for loading
	mQueue_SceneLoad.push(mResourceNames.mSceneNames[IndexScene]);

	// signal clearing history buffers to the render passes
	mRenderPass_SSR.ClearHistoryBuffers();

	mAppState = INITIALIZING;
	mbLoadingLevel.store(true); // thread-safe
	Log::Info("StartLoadingScene: %d", IndexScene);
}

