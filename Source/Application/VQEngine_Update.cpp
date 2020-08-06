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
		UpdateThread_HandleEvents();

		UpdateThread_PreUpdate(dt);

#if DEBUG_LOG_THREAD_SYNC_VERBOSE
		Log::Info(/*"UpdateThread_Tick() : */"u%d (r=%llu)", mNumUpdateLoopsExecuted.load(), mNumRenderLoopsExecuted.load());
#endif

		UpdateThread_UpdateAppState(dt);

		UpdateThread_PostUpdate();

		++mNumUpdateLoopsExecuted;

		UpdateThread_SignalRenderThread();

		UpdateThread_WaitForRenderThread();
	}

	UpdateThread_Exit();
	Log::Info("UpdateThread_Main() : Exit");
}

void VQEngine::UpdateThread_Inititalize()
{
	mActiveEnvironmentMapPresetIndex = -1;
	mNumUpdateLoopsExecuted.store(0);

#if ENABLE_RAW_INPUT
	// initialize raw input
	Input::InitRawInputDevices(mpWinMain->GetHWND());
#endif

	// initialize input states
	RegisterWindowForInput(mpWinMain);
	RegisterWindowForInput(mpWinDebug);

	// busy lock until render thread is initialized
	while (!mbRenderThreadInitialized); 

	// immediately load loading screen texture
	LoadLoadingScreenData();

	mTimer.Reset();
	mTimer.Start();
}

void VQEngine::UpdateThread_Exit()
{
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
		const int NumActiveTasks = mUpdateWorkerThreads.GetNumActiveTasks();
		const bool bLoadDone = NumActiveTasks == 0;
		if (bLoadDone)
		{
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

	// compute visibility 

	// extract scene view

	// copy over state for next frame
	mScene_MainWnd.mFrameData[FRAME_DATA_NEXT_INDEX] = mScene_MainWnd.mFrameData[FRAME_DATA_INDEX];

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

// temporary hardcoded initialization until scene is data driven
static FCameraData GenerateCameraInitializationParameters(const std::unique_ptr<Window>& pWin)
{
	assert(pWin);
	FCameraData camData = {};
	camData.x = 0.0f; camData.y = 3.0f; camData.z = -5.0f;
	camData.pitch = 15.0f;
	camData.yaw = 0.0f;
	camData.bPerspectiveProjection = true;
	camData.fovV_Degrees = 60.0f;
	camData.nearPlane = 0.01f;
	camData.farPlane = 1000.0f;
	camData.width = static_cast<float>(pWin->GetWidth());
	camData.height = static_cast<float>(pWin->GetHeight());
	return camData;
}
static void Toggle(bool& b) { b = !b; }

void VQEngine::HandleEngineInput()
{
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
		if (input.IsKeyTriggered("V"))
		{
			if (pWin == mpWinMain)
			{
				auto& SwapChain = mRenderer.GetWindowSwapChain(hwnd);
				mEventQueue_WinToVQE_Renderer.AddItem(std::make_shared<SetVSyncEvent>(hwnd, !SwapChain.IsVSyncOn()));
			}
		}
		if (input.IsKeyTriggered("M"))
		{
			if (pWin == mpWinMain)
			{
				mSettings.gfx.bAntiAliasing = !mSettings.gfx.bAntiAliasing;
				Log::Info("Toggle MSAA: %d", mSettings.gfx.bAntiAliasing);
			}
		}
		if (input.IsKeyTriggered("G"))
		{
			if (pWin == mpWinMain)
			{
				FFrameData& data = GetCurrentFrameData(mpWinMain->GetHWND());
				data.PPParams.ToggleGammaCorrection = data.PPParams.ToggleGammaCorrection == 1 ? 0 : 1;
				Log::Info("Tonemapper: ApplyGamma=%d (SDR-only)", data.PPParams.ToggleGammaCorrection);
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

FFrameData& VQEngine::GetCurrentFrameData(HWND hwnd)
{
	const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCount(hwnd);
	const int FRAME_DATA_INDEX = mNumUpdateLoopsExecuted % NUM_BACK_BUFFERS;
	return mWindowUpdateContextLookup.at(hwnd)->mFrameData[FRAME_DATA_INDEX];

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
	FFrameData& FrameData         = GetCurrentFrameData(hwnd);
	const Input& input            = mInputStates.at(hwnd);
	
	// handle input
	if (input.IsKeyTriggered('R')) FrameData.SceneCamera.InitializeCamera(GenerateCameraInitializationParameters(mpWinMain));

	constexpr float CAMERA_MOVEMENT_SPEED_MULTIPLER = 0.75f;
	constexpr float CAMERA_MOVEMENT_SPEED_SHIFT_MULTIPLER = 2.0f;
	XMVECTOR LocalSpaceTranslation = XMVectorSet(0, 0, 0, 0);
	if (input.IsKeyDown('A'))		LocalSpaceTranslation += XMLoadFloat3(&LeftVector);
	if (input.IsKeyDown('D'))		LocalSpaceTranslation += XMLoadFloat3(&RightVector);
	if (input.IsKeyDown('W'))		LocalSpaceTranslation += XMLoadFloat3(&ForwardVector);
	if (input.IsKeyDown('S'))		LocalSpaceTranslation += XMLoadFloat3(&BackVector);
	if (input.IsKeyDown('E'))		LocalSpaceTranslation += XMLoadFloat3(&UpVector);
	if (input.IsKeyDown('Q'))		LocalSpaceTranslation += XMLoadFloat3(&DownVector);
	if (input.IsKeyDown(VK_SHIFT))	LocalSpaceTranslation *= CAMERA_MOVEMENT_SPEED_SHIFT_MULTIPLER;
	LocalSpaceTranslation *= CAMERA_MOVEMENT_SPEED_MULTIPLER;

	if (input.IsKeyTriggered("Space")) Toggle(FrameData.bCubeAnimating);

	constexpr float MOUSE_BUTTON_ROTATION_SPEED_MULTIPLIER = 1.0f;
	if (input.IsMouseDown(Input::EMouseButtons::MOUSE_BUTTON_LEFT))   FrameData.TFCube.RotateAroundAxisRadians(ZAxis, dt * PI * MOUSE_BUTTON_ROTATION_SPEED_MULTIPLIER);
	if (input.IsMouseDown(Input::EMouseButtons::MOUSE_BUTTON_RIGHT))  FrameData.TFCube.RotateAroundAxisRadians(YAxis, dt * PI * MOUSE_BUTTON_ROTATION_SPEED_MULTIPLIER);
	if (input.IsMouseDown(Input::EMouseButtons::MOUSE_BUTTON_MIDDLE)) FrameData.TFCube.RotateAroundAxisRadians(XAxis, dt * PI * MOUSE_BUTTON_ROTATION_SPEED_MULTIPLIER);

	constexpr float DOUBLE_CLICK_MULTIPLIER = 4.0f;
	if (input.IsMouseDoubleClick(Input::EMouseButtons::MOUSE_BUTTON_LEFT))   FrameData.TFCube.RotateAroundAxisRadians(ZAxis, dt * PI * DOUBLE_CLICK_MULTIPLIER);
	if (input.IsMouseDoubleClick(Input::EMouseButtons::MOUSE_BUTTON_RIGHT))  FrameData.TFCube.RotateAroundAxisRadians(YAxis, dt * PI * DOUBLE_CLICK_MULTIPLIER);
	if (input.IsMouseDoubleClick(Input::EMouseButtons::MOUSE_BUTTON_MIDDLE)) FrameData.TFCube.RotateAroundAxisRadians(XAxis, dt * PI * DOUBLE_CLICK_MULTIPLIER);
	
	constexpr float SCROLL_SCALE_DELTA = 1.1f;
	const float CubeScale = FrameData.TFCube._scale.x;
	if (input.IsMouseScrollUp()  ) FrameData.TFCube.SetUniformScale(CubeScale * SCROLL_SCALE_DELTA);
	if (input.IsMouseScrollDown()) FrameData.TFCube.SetUniformScale(std::max(0.5f, CubeScale / SCROLL_SCALE_DELTA));

	
	auto fnBusyWaitUntilRenderThreadCatchesUp = [&]()
	{
		while ((mNumRenderLoopsExecuted+1) != mNumUpdateLoopsExecuted);
	};

	//mUpdateWorkerThreads.AddTask([&,

	const FEnvironmentMap& env = mResources_MainWnd.EnvironmentMap;
	const int NumEnvMaps = static_cast<int>(mEnvironmentMapPresetNames.size());
	if (input.IsKeyTriggered("PageUp"))
	{
		fnBusyWaitUntilRenderThreadCatchesUp();
		mActiveEnvironmentMapPresetIndex = (mActiveEnvironmentMapPresetIndex + 1) % NumEnvMaps;
		mAppState = EAppState::LOADING;
		mbLoadingLevel = true;
		mUpdateWorkerThreads.AddTask([&]() 
		{
			LoadEnvironmentMap(mEnvironmentMapPresetNames[mActiveEnvironmentMapPresetIndex]);
		});
		

	}
	if (input.IsKeyTriggered("PageDown"))
	{
		fnBusyWaitUntilRenderThreadCatchesUp();
		mActiveEnvironmentMapPresetIndex = mActiveEnvironmentMapPresetIndex == 0
			? NumEnvMaps - 1
			: mActiveEnvironmentMapPresetIndex - 1;
		mAppState = EAppState::LOADING;
		mbLoadingLevel = true;
		mUpdateWorkerThreads.AddTask([&]()
		{
			LoadEnvironmentMap(mEnvironmentMapPresetNames[mActiveEnvironmentMapPresetIndex]);
		});
	}


	// update camera
	FCameraInput camInput(LocalSpaceTranslation);
	camInput.DeltaMouseXY = input.GetMouseDelta();
	FrameData.SceneCamera.Update(dt, camInput);
	
	// update scene data
	if(FrameData.bCubeAnimating)
		FrameData.TFCube.RotateAroundAxisRadians(YAxis, dt * 0.2f * PI);
	
}

void VQEngine::UpdateThread_UpdateScene_DebugWnd(const float dt)
{
	if (!mpWinDebug) return;

	std::unique_ptr<Window>& pWin = mpWinDebug;
	HWND hwnd                     = pWin->GetHWND();
	FFrameData& FrameData         = GetCurrentFrameData(hwnd);
	const Input& input            = mInputStates.at(hwnd);


}


void VQEngine::Load_SceneData_Dispatch()
{
	if (mQueue_SceneLoad.empty())
		return;

	const FSceneRepresentation SceneRep = mQueue_SceneLoad.front();
	mQueue_SceneLoad.pop();

	mUpdateWorkerThreads.AddTask([&]() // Load scene data
	{
		const int NumBackBuffer_WndMain = mRenderer.GetSwapChainBackBufferCount(mpWinMain);
		const int NumBackBuffer_WndDbg  = mRenderer.GetSwapChainBackBufferCount(mpWinDebug);

		// TODO: initialize window scene data here for now, should update this to proper location later on (Scene probably?)
		FFrameData data[2];

		//
		// MAIN WINDOW DATA
		//
		data[0].SwapChainClearColor = { 0.07f, 0.07f, 0.07f, 1.0f };

		// Cube Data
		constexpr XMFLOAT3 CUBE_POSITION         = XMFLOAT3(0, 0, 4);
		constexpr float    CUBE_SCALE            = 3.0f;
		constexpr XMFLOAT3 CUBE_ROTATION_VECTOR  = XMFLOAT3(1, 1, 1);
		constexpr float    CUBE_ROTATION_DEGREES = 60.0f;
		const XMVECTOR     CUBE_ROTATION_AXIS    = XMVector3Normalize(XMLoadFloat3(&CUBE_ROTATION_VECTOR));
		data[0].TFCube = Transform(
			  CUBE_POSITION
			, Quaternion::FromAxisAngle(CUBE_ROTATION_AXIS, CUBE_ROTATION_DEGREES * DEG2RAD)
			, XMFLOAT3(CUBE_SCALE, CUBE_SCALE, CUBE_SCALE)
		);
		data[0].bCubeAnimating = true;
		// Camera Data
		FCameraData camData = GenerateCameraInitializationParameters(mpWinMain);
		data[0].SceneCamera.InitializeCamera(camData);
		// Post Process Data
		data[0].PPParams.ContentColorSpace  = EColorSpace::REC_709;
		data[0].PPParams.OutputDisplayCurve = ShouldRenderHDR(mpWinMain->GetHWND()) ? EDisplayCurve::Linear : EDisplayCurve::sRGB;
		data[0].PPParams.DisplayReferenceBrightnessLevel = 200.0f;

		mScene_MainWnd.mFrameData.resize(NumBackBuffer_WndMain, data[0]);

		//
		// DEBUG WINDOW DATA
		//
		data[1].SwapChainClearColor = { 0.20f, 0.21f, 0.21f, 1.0f };
		mScene_DebugWnd.mFrameData.resize(NumBackBuffer_WndDbg, data[1]);

		mWindowUpdateContextLookup[mpWinMain->GetHWND()] = &mScene_MainWnd;
		if (mpWinDebug) mWindowUpdateContextLookup[mpWinDebug->GetHWND()] = &mScene_DebugWnd;
	});
	mUpdateWorkerThreads.AddTask([&, SceneRep]() // Load Environment map textures
	{
		Log::Info("[Scene] Loading: %s", SceneRep.SceneName.c_str());
		LoadEnvironmentMap(SceneRep.EnvironmentMapPreset);
		Log::Info("[Scene] %s loaded.", SceneRep.SceneName.c_str());
	});
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

		this->mActiveEnvironmentMapPresetIndex = static_cast<int>(ActiveEnvMapIndex);

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
	FLoadingScreenData data;

	data.SwapChainClearColor = { 0.0f, 0.2f, 0.4f, 1.0f };

	srand(static_cast<unsigned>(time(NULL)));
	const std::string LoadingScreenTextureFileDirectory = "Data/Textures/LoadingScreen/";
	const std::string LoadingScreenTextureFilePath = LoadingScreenTextureFileDirectory + (std::to_string(MathUtil::RandU(0, 4)) + ".png");
	TextureID texID = mRenderer.CreateTextureFromFile(LoadingScreenTextureFilePath.c_str());
	SRV_ID    srvID = mRenderer.CreateAndInitializeSRV(texID);
	data.SRVLoadingScreen = srvID;

	const int NumBackBuffer_WndMain = mRenderer.GetSwapChainBackBufferCount(mpWinMain);
	mScene_MainWnd.mLoadingScreenData.resize(NumBackBuffer_WndMain, data);

	if (mpWinDebug)
	{
		FLoadingScreenData data;
		data.SwapChainClearColor = { 0.5f, 0.4f, 0.01f, 1.0f };
		const int NumBackBuffer_WndDbg = mRenderer.GetSwapChainBackBufferCount(mpWinDebug);
		mScene_DebugWnd.mLoadingScreenData.resize(NumBackBuffer_WndDbg, data);

		mWindowUpdateContextLookup[mpWinDebug->GetHWND()] = &mScene_DebugWnd;
	}
}

