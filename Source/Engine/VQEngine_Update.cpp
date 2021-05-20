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
#include "Scene/Scene.h" // TODO: this name collision is bad - rename to world?
#include "../Scenes/Scenes.h"


#include "Libs/VQUtils/Source/utils.h"

#include <algorithm>

#include <dwmapi.h>
#pragma comment(lib, "Dwmapi.lib")

using namespace DirectX;


void VQEngine::UpdateThread_Main()
{
	Log::Info("UpdateThread Created.");

	UpdateThread_Inititalize();

	bool bQuit = false;
	float dt = 0.0f;
	float dt_RenderWaitTime = 0.0f;
	while (!mbStopAllThreads && !bQuit)
	{
		dt_RenderWaitTime = UpdateThread_WaitForRenderThread();

		UpdateThread_HandleEvents();

		UpdateThread_PreUpdate(dt);

#if DEBUG_LOG_THREAD_SYNC_VERBOSE
		Log::Info(/*"UpdateThread_Tick() : */"u%d (r=%llu)", mNumUpdateLoopsExecuted.load(), mNumRenderLoopsExecuted.load());
#endif

		UpdateThread_UpdateAppState(dt);

		UpdateThread_PostUpdate();

		++mNumUpdateLoopsExecuted;

		UpdateThread_SignalRenderThread();

		// UpdateThread_Logging()
		constexpr int LOGGING_PERIOD = 4; // seconds
		static float LAST_LOG_TIME = 0;
		const float TotalTime = mTimer.TotalTime();
		if (TotalTime - LAST_LOG_TIME > 4)
		{
			Log::Info("UpdateThread_Main() : dt=%.2f ms", (dt * 1000.0f) - (dt_RenderWaitTime * 1000.0f));
			LAST_LOG_TIME = TotalTime;
		}
	}

	UpdateThread_Exit();
	Log::Info("UpdateThread_Main() : Exit");
}

void VQEngine::UpdateThread_Inititalize()
{
	mNumUpdateLoopsExecuted.store(0);
	mbLoadingEnvironmentMap.store(false);

	// busy lock until render thread is initialized
	while (!mbRenderThreadInitialized); 

	// immediately load loading screen texture
	LoadLoadingScreenData();

	mTimer.Reset();
	mTimer.Start();
}

void VQEngine::UpdateThread_Exit()
{
	mpScene->Unload();
}

void VQEngine::UpdateThread_PreUpdate(float& dt)
{
	// update timer
	dt = mTimer.Tick();

	// TODO: perf entry

	// system-wide input (esc/mouse click on wnd)
	HandleEngineInput();
}

void VQEngine::UpdateThread_UpdateAppState(const float dt)
{
	assert(mbRenderThreadInitialized);


	if (mAppState == EAppState::INITIALIZING)
	{
		// start loading
		Log::Info("UpdateThread: loading...");

		// start load level
		Load_SceneData_Dispatch();

		// set state
		mAppState = EAppState::LOADING;// not thread-safe
		
	}


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

			if (mbLoadingLevel)
			{
				mbLoadingLevel.store(false);
			}
			if (mbLoadingEnvironmentMap)
			{
				mbLoadingEnvironmentMap.store(false);
			}

			mLoadingScreenData.RotateLoadingScreenImage();

			float dt_loading = mTimer.StopGetDeltaTimeAndReset();
			Log::Info("Loading completed in %.2fs, starting scene simulation", dt_loading);
			mTimer.Start();
		}
	}


	else
	{
		// TODO: threaded?
		UpdateThread_UpdateScene_MainWnd(dt);
		UpdateThread_UpdateScene_DebugWnd(dt);
	}

}

void VQEngine::UpdateThread_PostUpdate()
{
	const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCount(mpWinMain->GetHWND());
	const int FRAME_DATA_INDEX = mNumUpdateLoopsExecuted % NUM_BACK_BUFFERS;
	const int FRAME_DATA_NEXT_INDEX = ((mNumUpdateLoopsExecuted % NUM_BACK_BUFFERS) + 1) % NUM_BACK_BUFFERS;

	if (mbLoadingLevel)
	{
		return;
	}

	mpScene->PostUpdate(FRAME_DATA_INDEX, FRAME_DATA_NEXT_INDEX, mWorkers_Update);

	// input post update
	for (auto it = mInputStates.begin(); it != mInputStates.end(); ++it)
	{
		const HWND& hwnd = it->first;
		mInputStates.at(hwnd).PostUpdate(); // non-const accessor
	}
}

float VQEngine::UpdateThread_WaitForRenderThread()
{
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

// -------------------------------------------------------------------

void VQEngine::HandleEngineInput()
{
	const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCount(mpWinMain->GetHWND());
	const int FRAME_DATA_INDEX = mNumUpdateLoopsExecuted % NUM_BACK_BUFFERS;

	for (decltype(mInputStates)::iterator it = mInputStates.begin(); it != mInputStates.end(); ++it)
	{
		HWND   hwnd  = it->first;
		Input& input = it->second;
		auto&  pWin  = this->GetWindow(hwnd);
		const bool bIsShiftDown = input.IsKeyDown("Shift");

		//
		// Process-level input handling
		//
		if (input.IsKeyTriggered("Esc"))
		{
			if (pWin->IsMouseCaptured())
			{
				mEventQueue_VQEToWin_Main.AddItem(std::make_shared<SetMouseCaptureEvent>(hwnd, false, true));
			}
		}
		if (input.IsAnyMouseDown())
		{
			Input& inp = mInputStates.at(hwnd); // non const ref
			if (inp.GetInputBypassing())
			{
				inp.SetInputBypassing(false);

				// capture mouse only when main window is clicked
				if(hwnd == mpWinMain->GetHWND())
					mEventQueue_VQEToWin_Main.AddItem(std::make_shared<SetMouseCaptureEvent>(hwnd, true, false));
			}
		}
		if (pWin == mpWinMain) 
		{
			// Graphics Settings Controls
			if (input.IsKeyTriggered("V"))
			{
				auto& SwapChain = mRenderer.GetWindowSwapChain(hwnd);
				mEventQueue_WinToVQE_Renderer.AddItem(std::make_shared<SetVSyncEvent>(hwnd, !SwapChain.IsVSyncOn()));
			}
			if (input.IsKeyTriggered("M"))
			{
				mSettings.gfx.bAntiAliasing = !mSettings.gfx.bAntiAliasing;
				Log::Info("Toggle MSAA: %d", mSettings.gfx.bAntiAliasing);	
			}
			if (input.IsKeyTriggered("B"))
			{
				WaitUntilRenderingFinishes();
				FPostProcessParameters& PPParams = mpScene->GetPostProcessParameters(FRAME_DATA_INDEX);
				PPParams.bEnableCAS = !PPParams.bEnableCAS;
				Log::Info("Toggle FFX-CAS: %d", PPParams.bEnableCAS);
			}
			if (input.IsKeyTriggered("G"))
			{
				FPostProcessParameters& PPParams = mpScene->GetPostProcessParameters(FRAME_DATA_INDEX);
				PPParams.TonemapperParams.ToggleGammaCorrection = PPParams.TonemapperParams.ToggleGammaCorrection == 1 ? 0 : 1;
				Log::Info("Tonemapper: ApplyGamma=%d (SDR-only)", PPParams.TonemapperParams.ToggleGammaCorrection);
			}

			// Scene switching
			if (bIsShiftDown)
			{
				const int NumScenes = static_cast<int>(mResourceNames.mSceneNames.size());
				if (input.IsKeyTriggered("PageUp") && !mbLoadingLevel)  { mIndex_SelectedScene = CircularIncrement(mIndex_SelectedScene, NumScenes);     this->StartLoadingScene(mIndex_SelectedScene); }
				if (input.IsKeyTriggered("PageDown") && !mbLoadingLevel){ mIndex_SelectedScene = CircularDecrement(mIndex_SelectedScene, NumScenes - 1); this->StartLoadingScene(mIndex_SelectedScene); }
				if (input.IsKeyTriggered("R") && !mbLoadingLevel)       { this->StartLoadingScene(mIndex_SelectedScene); } // reload scene
			}
			if (input.IsKeyTriggered("1") && !mbLoadingLevel) { mIndex_SelectedScene = 0; this->StartLoadingScene(mIndex_SelectedScene); }
			if (input.IsKeyTriggered("2") && !mbLoadingLevel) { mIndex_SelectedScene = 1; this->StartLoadingScene(mIndex_SelectedScene); }
			if (input.IsKeyTriggered("3") && !mbLoadingLevel) { mIndex_SelectedScene = 2; this->StartLoadingScene(mIndex_SelectedScene); }
			if (input.IsKeyTriggered("4") && !mbLoadingLevel) { mIndex_SelectedScene = 3; this->StartLoadingScene(mIndex_SelectedScene); }
		}
	}
}

bool VQEngine::IsWindowRegistered(HWND hwnd) const
{
	auto it = mWinNameLookup.find(hwnd);
	return it != mWinNameLookup.end();
}

bool VQEngine::ShouldRenderHDR(HWND hwnd) const
{
	const auto& pWin = this->GetWindow(hwnd);
	return mSettings.WndMain.bEnableHDR && pWin->GetIsOnHDRCapableDisplay();
}

void VQEngine::CalculateEffectiveFrameRateLimit(HWND hwnd)
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
	if(bUnlimitedFrameRate) Log::Info("FrameRateLimit : Unlimited");
	else                    Log::Info("FrameRateLimit : %.2fms | %d FPS", mEffectiveFrameRateLimit_ms, static_cast<int>(1000.0f / mEffectiveFrameRateLimit_ms));
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

FSetHDRMetaDataParams VQEngine::GatherHDRMetaDataParameters(HWND hwnd)
{
	FSetHDRMetaDataParams params;

	while (!mbRenderThreadInitialized); // wait until renderer is initialized

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

void VQEngine::SetWindowName(HWND hwnd, const std::string& name){	mWinNameLookup[hwnd] = name; }
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
	this->WaitUntilRenderingFinishes();
	mAppState = EAppState::LOADING;
	mbLoadingEnvironmentMap = true;
	mWorkers_TextureLoading.AddTask([&, IndexEnvMap]()
	{
		LoadEnvironmentMap(mResourceNames.mEnvironmentMapPresetNames[IndexEnvMap]);
	});
}

void VQEngine::StartLoadingScene(int IndexScene)
{
	assert(IndexScene >= 0 && IndexScene < mResourceNames.mSceneNames.size());

	// get scene representation 
	const std::string& SceneName = mResourceNames.mSceneNames[IndexScene];



	// queue the selected scene for loading
	mQueue_SceneLoad.push(mResourceNames.mSceneNames[IndexScene]);

	mAppState = INITIALIZING;
	mbLoadingLevel.store(true);    // thread-safe
	Log::Info("StartLoadingScene: %d", IndexScene);
}


void VQEngine::WaitUntilRenderingFinishes()
{
	while (mNumRenderLoopsExecuted != mNumUpdateLoopsExecuted);
}

// ---------------------------------------------------------------------

void VQEngine::UpdateThread_UpdateScene_MainWnd(const float dt)
{
	std::unique_ptr<Window>& pWin = mpWinMain;
	HWND hwnd                     = pWin->GetHWND();

	const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCount(hwnd);
	const int FRAME_DATA_INDEX = mNumUpdateLoopsExecuted % NUM_BACK_BUFFERS;

	mpScene->Update(dt, FRAME_DATA_INDEX);
}

void VQEngine::UpdateThread_UpdateScene_DebugWnd(const float dt)
{
	if (!mpWinDebug) return;

	std::unique_ptr<Window>& pWin = mpWinDebug;
	HWND hwnd                     = pWin->GetHWND();
	const Input& input            = mInputStates.at(hwnd);


}


void VQEngine::Load_SceneData_Dispatch()
{
	if (mQueue_SceneLoad.empty())
		return;

	const std::string SceneFileName = mQueue_SceneLoad.front();
	mQueue_SceneLoad.pop();
	

	const int NUM_SWAPCHAIN_BACKBUFFERS = mSettings.gfx.bUseTripleBuffering ? 3 : 2;
	const Input& input                  = mInputStates.at(mpWinMain->GetHWND());

	auto fnCreateSceneInstance = [&](const std::string& SceneType, std::unique_ptr<Scene>& pScene) -> void
	{
		     if (SceneType == "Default")          pScene = std::make_unique<DefaultScene>(*this, NUM_SWAPCHAIN_BACKBUFFERS, input, mpWinMain, mRenderer);
		else if (SceneType == "Sponza")           pScene = std::make_unique<SponzaScene >(*this, NUM_SWAPCHAIN_BACKBUFFERS, input, mpWinMain, mRenderer);
		else if (SceneType == "StressTest")       pScene = std::make_unique<StressTestScene >(*this, NUM_SWAPCHAIN_BACKBUFFERS, input, mpWinMain, mRenderer);
		else if (SceneType == "GeometryUnitTest") pScene = std::make_unique<GeometryUnitTestScene >(*this, NUM_SWAPCHAIN_BACKBUFFERS, input, mpWinMain, mRenderer);
	};

	if (mpScene)
	{
		this->WaitUntilRenderingFinishes();
		mpScene->Unload();
	}

	// load scene representation from disk
	const std::string SceneFilePath = "Data/Levels/" + SceneFileName + ".xml";
	FSceneRepresentation SceneRep = VQEngine::ParseSceneFile(SceneFilePath);
	fnCreateSceneInstance(SceneRep.SceneName, mpScene);

	// start loading textures, models, materials with worker threads
	mpScene->StartLoading(this->mBuiltinMeshes, SceneRep);

	// start loading environment map textures
	if (!SceneRep.EnvironmentMapPreset.empty()) 
	{ 
		mWorkers_TextureLoading.AddTask([=]() { LoadEnvironmentMap(SceneRep.EnvironmentMapPreset); });
	}
}

SRV_ID FLoadingScreenData::GetSelectedLoadingScreenSRV_ID() const
{
	assert(SelectedLoadingScreenSRVIndex < SRVs.size());
	return SRVs[SelectedLoadingScreenSRVIndex];
}
void FLoadingScreenData::RotateLoadingScreenImage()
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
			const SRV_ID srvID = mRenderer.CreateAndInitializeSRV(texID);
			std::lock_guard<std::mutex> lk(data.Mtx);
			data.SRVs.push_back(srvID);
		});

	}

	// load the selected loading screen image
	{
		const std::string LoadingScreenTextureFilePath = LoadingScreenTextureFileDirectory + (std::to_string(SelectedLoadingScreenIndex) + ".png");
		TextureID texID = mRenderer.CreateTextureFromFile(LoadingScreenTextureFilePath.c_str());
		SRV_ID    srvID = mRenderer.CreateAndInitializeSRV(texID);
		std::lock_guard<std::mutex> lk(data.Mtx);
		data.SRVs.push_back(srvID);
		data.SelectedLoadingScreenSRVIndex = static_cast<int>(data.SRVs.size() - 1);
	}

}

