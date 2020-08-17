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
#include "Scene.h"

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
	while (!mbStopAllThreads && !bQuit)
	{
		UpdateThread_WaitForRenderThread();

		UpdateThread_HandleEvents();

		UpdateThread_PreUpdate(dt);

#if DEBUG_LOG_THREAD_SYNC_VERBOSE
		Log::Info(/*"UpdateThread_Tick() : */"u%d (r=%llu)", mNumUpdateLoopsExecuted.load(), mNumRenderLoopsExecuted.load());
#endif

		UpdateThread_UpdateAppState(dt);

		UpdateThread_PostUpdate();

		++mNumUpdateLoopsExecuted;

		UpdateThread_SignalRenderThread();
	}

	UpdateThread_Exit();
	Log::Info("UpdateThread_Main() : Exit");
}

void VQEngine::UpdateThread_Inititalize()
{
	mNumUpdateLoopsExecuted.store(0);

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
		Log::Info("Update Thread starts loading...");

		// start load level
		Load_SceneData_Dispatch();
		mAppState = EAppState::LOADING;// not thread-safe

		mbLoadingLevel.store(true);    // thread-safe
	}


	if (mbLoadingLevel)
	{
		// animate loading screen


		// check if loading is done
		const int NumActiveTasks = mWorkers_Load.GetNumActiveTasks();
		const bool bLoadDone = NumActiveTasks == 0;
		if (bLoadDone)
		{
			mpScene->OnLoadComplete();
			Log::Info("Update Thread loaded, starting simulation...");
			mAppState = EAppState::SIMULATING;
			mbLoadingLevel.store(false);
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

	mpScene->PostUpdate(FRAME_DATA_INDEX, FRAME_DATA_NEXT_INDEX);

	// input post update
	for (auto it = mInputStates.begin(); it != mInputStates.end(); ++it)
	{
		const HWND& hwnd = it->first;
		mInputStates.at(hwnd).PostUpdate(); // non-const accessor
	}
}

void VQEngine::UpdateThread_WaitForRenderThread()
{
#if DEBUG_LOG_THREAD_SYNC_VERBOSE
	Log::Info("u:wait : u=%llu, r=%llu", mNumUpdateLoopsExecuted.load(), mNumRenderLoopsExecuted.load());
#endif

	if (mbStopAllThreads)
		return;

	mpSemUpdate->Wait();
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

		//
		// Graphics Settings Controls
		//
		if (pWin == mpWinMain)
		{
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
			if (input.IsKeyTriggered("G"))
			{
				FPostProcessParameters& PPParams = mpScene->GetPostProcessParameters(FRAME_DATA_INDEX);
				PPParams.ToggleGammaCorrection = PPParams.ToggleGammaCorrection == 1 ? 0 : 1;
				Log::Info("Tonemapper: ApplyGamma=%d (SDR-only)", PPParams.ToggleGammaCorrection);
			}
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

void VQEngine::CalculateEffectiveFrameRate(HWND hwnd)
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
	const auto it = std::find(mBuiltinMeshNames.begin(), mBuiltinMeshNames.end(), MeshName);
	if (it == mBuiltinMeshNames.end())
	{
		Log::Error("Builtin Mesh Not Found: %s", MeshName.c_str());
		return INVALID_ID;
	}
	return static_cast<MeshID>(it - mBuiltinMeshNames.begin());
}

Model& VQEngine::GetModel(ModelID id)
{
	// TODO: err msg
	return mModels.at(id);
}

const Model& VQEngine::GetModel(ModelID id) const
{
	// TODO: err msg
	return mModels.at(id);
}

ModelID VQEngine::CreateModel()
{
	static ModelID LAST_USED_MODEL_ID = 0;
	ModelID id = LAST_USED_MODEL_ID++;
	mModels[id] = Model();
	return id;
}

const FEnvironmentMapDescriptor& VQEngine::GetEnvironmentMapDesc(const std::string& EnvMapName) const
{
	static const FEnvironmentMapDescriptor DEFAULT_ENV_MAP_DESC = { "ENV_MAP_NOT_FOUND", "", 0.0f };
	const bool bEnvMapNotFound = mLookup_EnvironmentMapDescriptors.find(EnvMapName) == mLookup_EnvironmentMapDescriptors.end();
	if (bEnvMapNotFound)
	{
		Log::Error("Environment Map %s not found.", EnvMapName.c_str());
	}
	return  bEnvMapNotFound
		? DEFAULT_ENV_MAP_DESC
		: mLookup_EnvironmentMapDescriptors.at(EnvMapName);
}

// ---------------------------------------------------------------------

void VQEngine::UpdateThread_UpdateScene_MainWnd(const float dt)
{
	std::unique_ptr<Window>& pWin = mpWinMain;
	HWND hwnd                     = pWin->GetHWND();
	const Input& input            = mInputStates.at(hwnd);

	const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCount(hwnd);
	const int FRAME_DATA_INDEX = mNumUpdateLoopsExecuted % NUM_BACK_BUFFERS;

	mpScene->Update(dt, FRAME_DATA_INDEX);


	auto fnBusyWaitUntilRenderThreadCatchesUp = [&]()
	{
		while ((mNumRenderLoopsExecuted + 1) != mNumUpdateLoopsExecuted);
	};

	const FEnvironmentMap& env = mResources_MainWnd.EnvironmentMap;
	const int NumEnvMaps = static_cast<int>(mEnvironmentMapPresetNames.size());

	if (input.IsKeyTriggered("PageUp"))
	{
		fnBusyWaitUntilRenderThreadCatchesUp();

		int& ACTIVE_ENV_MAP_INDEX = mpScene->mIndex_ActiveEnvironmentMapPreset;
		ACTIVE_ENV_MAP_INDEX = (ACTIVE_ENV_MAP_INDEX + 1) % NumEnvMaps;

		mAppState = EAppState::LOADING;
		mbLoadingLevel = true;
		mWorkers_Load.AddTask([&]()
		{
			LoadEnvironmentMap(mEnvironmentMapPresetNames[ACTIVE_ENV_MAP_INDEX]);
		});


	}
	if (input.IsKeyTriggered("PageDown"))
	{
		fnBusyWaitUntilRenderThreadCatchesUp();
		
		int& ACTIVE_ENV_MAP_INDEX = mpScene->mIndex_ActiveEnvironmentMapPreset;
		ACTIVE_ENV_MAP_INDEX = ACTIVE_ENV_MAP_INDEX == 0
			? NumEnvMaps - 1
			: ACTIVE_ENV_MAP_INDEX - 1;

		mAppState = EAppState::LOADING;
		mbLoadingLevel = true;
		mWorkers_Load.AddTask([&]()
		{
			LoadEnvironmentMap(mEnvironmentMapPresetNames[ACTIVE_ENV_MAP_INDEX]);
		});
	}
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

	FSceneRepresentation SceneRep = mQueue_SceneLoad.front();
	mQueue_SceneLoad.pop();

	// let the custom scene logic edit the scene representation
	mpScene->StartLoading(SceneRep);

	// start loading;
	Log::Info("[Scene] Loading: %s", SceneRep.SceneName.c_str());

	if (!SceneRep.EnvironmentMapPreset.empty()) 
	{ 
		mWorkers_Load.AddTask([=]() { LoadEnvironmentMap(SceneRep.EnvironmentMapPreset); });
	}
}

void VQEngine::LoadEnvironmentMap(const std::string& EnvMapName)
{
	assert(EnvMapName.size() != 0);
	FEnvironmentMap& env = mResources_MainWnd.EnvironmentMap;

	// if already loaded, unload it
	if (env.Tex_HDREnvironment != INVALID_ID)
	{
		assert(env.SRV_HDREnvironment != INVALID_ID);
		// GPU-sync assumed
		mRenderer.GetWindowSwapChain(mpWinMain->GetHWND()).WaitForGPU();
		mRenderer.DestroySRV(env.SRV_HDREnvironment);
		mRenderer.DestroyTexture(env.Tex_HDREnvironment);
		env.MaxContentLightLevel = 0;
	}

	const FEnvironmentMapDescriptor& desc = this->GetEnvironmentMapDesc(EnvMapName);
	std::vector<std::string>::iterator it = std::find(mEnvironmentMapPresetNames.begin(), mEnvironmentMapPresetNames.end(), EnvMapName);
	const size_t ActiveEnvMapIndex = it - mEnvironmentMapPresetNames.begin();
	if (!desc.FilePath.empty()) // check whether the env map was found or not
	{
		Log::Info("Loading Environment Map: %s", EnvMapName.c_str());
		env.Tex_HDREnvironment = mRenderer.CreateTextureFromFile(desc.FilePath.c_str());
		env.SRV_HDREnvironment = mRenderer.CreateAndInitializeSRV(env.Tex_HDREnvironment);
		env.MaxContentLightLevel = static_cast<int>(desc.MaxContentLightLevel);

		//assert(mpScene->mIndex_ActiveEnvironmentMapPreset == static_cast<int>(ActiveEnvMapIndex)); // Only false durin initialization
		mpScene->mIndex_ActiveEnvironmentMapPreset = static_cast<int>(ActiveEnvMapIndex);

		// Update HDRMetaData when the nvironment map is loaded
		HWND hwnd = mpWinMain->GetHWND();
		mEventQueue_WinToVQE_Renderer.AddItem(std::make_shared<SetStaticHDRMetaDataEvent>(hwnd, this->GatherHDRMetaDataParameters(hwnd)));
	}
	else
	{
		Log::Warning("Have you run Scripts/DownloadAssets.bat?");
		Log::Error("Couldn't find Environment Map: %s", EnvMapName.c_str());
	}
}


void VQEngine::LoadLoadingScreenData()
{
	FLoadingScreenData& data = mLoadingScreenData;

	data.SwapChainClearColor = { 0.0f, 0.2f, 0.4f, 1.0f };

	srand(static_cast<unsigned>(time(NULL)));
	const std::string LoadingScreenTextureFileDirectory = "Data/Textures/LoadingScreen/";
	const std::string LoadingScreenTextureFilePath = LoadingScreenTextureFileDirectory + (std::to_string(MathUtil::RandU(0, 4)) + ".png");
	TextureID texID = mRenderer.CreateTextureFromFile(LoadingScreenTextureFilePath.c_str());
	SRV_ID    srvID = mRenderer.CreateAndInitializeSRV(texID);
	data.SRVLoadingScreen = srvID;
}

