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
#include "Math.h"

#include "Libs/VQUtils/Source/utils.h"

using namespace DirectX;

// temporary hardcoded initialization until scene is data driven
static FCameraData GenerateCameraInitializationParameters(const std::unique_ptr<Window>& pWin)
{
	assert(pWin);
	FCameraData camData = {};
	camData.nearPlane = 0.01f;
	camData.farPlane = 1000.0f;
	camData.x = 0.0f; camData.y = 3.0f; camData.z = -5.0f;
	camData.pitch = 10.0f;
	camData.yaw = 0.0f;
	camData.fovH_Degrees = 60.0f;
	camData.aspect = static_cast<float>(pWin->GetWidth()) / pWin->GetHeight();
	return camData;
}

void VQEngine::UpdateThread_Main()
{
	Log::Info("UpdateThread_Main()");

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
	mNumUpdateLoopsExecuted.store(0);

#if ENABLE_RAW_INPUT
	// initialize raw input
	Input::InitRawInputDevices(mpWinMain->GetHWND());

	RegisterWindowForInput(mpWinMain);
	RegisterWindowForInput(mpWinDebug);
#endif

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

void VQEngine::UpdateThread_PreUpdate(float& dt)
{
	// update timer
	dt = mTimer.Tick();

	// system-wide input (esc/mouse click on wnd)
	HandleEngineInput();
}

void VQEngine::HandleEngineInput()
{
	for (decltype(mInputStates)::iterator it = mInputStates.begin(); it != mInputStates.end(); ++it)
	{
		HWND   hwnd  = it->first;
		Input& input = it->second;
		auto&  pWin  = this->GetWindow(hwnd);

		if (input.IsKeyDown("Esc"))
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
	}
}

bool VQEngine::IsWindowRegistered(HWND hwnd) const
{
	auto it = mWinNameLookup.find(hwnd);
	return it != mWinNameLookup.end();
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

void VQEngine::UpdateThread_UpdateAppState(const float dt)
{
	assert(mbRenderThreadInitialized);


	if (mAppState == EAppState::INITIALIZING)
	{
		// start loading
		Log::Info("Main Thread starts loading...");

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
			Log::Info("Main Thread loaded");
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

void VQEngine::UpdateThread_UpdateScene_MainWnd(const float dt)
{
	std::unique_ptr<Window>& pWin = mpWinMain;
	HWND hwnd                     = pWin->GetHWND();
	const int NUM_BACK_BUFFERS    = mRenderer.GetSwapChainBackBufferCount(hwnd);
	const int FRAME_DATA_INDEX    = mNumUpdateLoopsExecuted % NUM_BACK_BUFFERS;
	FFrameData& FrameData         = mScene_MainWnd.mFrameData[FRAME_DATA_INDEX];
	const Input& input            = mInputStates.at(hwnd);
	
	// handle input
	constexpr float CAMERA_MOVEMENT_SPEED_MULTIPLER = 0.75f;
	constexpr float CAMERA_MOVEMENT_SPEED_SHIFT_MULTIPLER = 2.0f;
	XMVECTOR LocalSpaceTranslation = XMVectorSet(0,0,0,0);
	if (input.IsKeyDown('A'))		LocalSpaceTranslation += XMLoadFloat3(&LeftVector);
	if (input.IsKeyDown('D'))		LocalSpaceTranslation += XMLoadFloat3(&RightVector);
	if (input.IsKeyDown('W'))		LocalSpaceTranslation += XMLoadFloat3(&ForwardVector);
	if (input.IsKeyDown('S'))		LocalSpaceTranslation += XMLoadFloat3(&BackVector);
	if (input.IsKeyDown('E'))		LocalSpaceTranslation += XMLoadFloat3(&UpVector);
	if (input.IsKeyDown('Q'))		LocalSpaceTranslation += XMLoadFloat3(&DownVector);
	if (input.IsKeyDown(VK_SHIFT))	LocalSpaceTranslation *= CAMERA_MOVEMENT_SPEED_SHIFT_MULTIPLER;
	LocalSpaceTranslation *= CAMERA_MOVEMENT_SPEED_MULTIPLER;

	if (input.IsKeyTriggered('R')) FrameData.SceneCamera.InitializeCamera(GenerateCameraInitializationParameters(mpWinMain));
	

	constexpr float MOUSE_BUTTON_ROTATION_SPEED_MULTIPLIER = 1.0f;
	if (input.IsMouseDown(Input::EMouseButtons::MOUSE_BUTTON_LEFT))   FrameData.TFCube.RotateAroundAxisRadians(ZAxis, dt * PI * MOUSE_BUTTON_ROTATION_SPEED_MULTIPLIER);
	if (input.IsMouseDown(Input::EMouseButtons::MOUSE_BUTTON_RIGHT))  FrameData.TFCube.RotateAroundAxisRadians(YAxis, dt * PI * MOUSE_BUTTON_ROTATION_SPEED_MULTIPLIER);
	if (input.IsMouseDown(Input::EMouseButtons::MOUSE_BUTTON_MIDDLE)) FrameData.TFCube.RotateAroundAxisRadians(XAxis, dt * PI * MOUSE_BUTTON_ROTATION_SPEED_MULTIPLIER);

	constexpr float DOUBLE_CLICK_MULTIPLIER = 4.0f;
	if (input.IsMouseDoubleClick(Input::EMouseButtons::MOUSE_BUTTON_LEFT))   FrameData.TFCube.RotateAroundAxisRadians(ZAxis, dt * PI * DOUBLE_CLICK_MULTIPLIER);
	if (input.IsMouseDoubleClick(Input::EMouseButtons::MOUSE_BUTTON_RIGHT))  FrameData.TFCube.RotateAroundAxisRadians(YAxis, dt * PI * DOUBLE_CLICK_MULTIPLIER);
	if (input.IsMouseDoubleClick(Input::EMouseButtons::MOUSE_BUTTON_MIDDLE)) FrameData.TFCube.RotateAroundAxisRadians(XAxis, dt * PI * DOUBLE_CLICK_MULTIPLIER);
	
	constexpr float SCROLL_ROTATION_MULTIPLIER = 0.5f; // 90 degs | 0.5 rads
	if (input.IsMouseScrollUp()  ) FrameData.TFCube.RotateAroundAxisRadians(XAxis,  PI * SCROLL_ROTATION_MULTIPLIER);
	if (input.IsMouseScrollDown()) FrameData.TFCube.RotateAroundAxisRadians(XAxis, -PI * SCROLL_ROTATION_MULTIPLIER);

	// update camera
	FCameraInput camInput(LocalSpaceTranslation);
	camInput.DeltaMouseXY = input.GetMouseDelta();
	FrameData.SceneCamera.Update(dt, camInput);
	
	// update scene data
	FrameData.TFCube.RotateAroundAxisRadians(YAxis, dt * 0.2f * PI);
	
}

void VQEngine::UpdateThread_UpdateScene_DebugWnd(const float dt)
{
	if (!mpWinDebug) return;

	std::unique_ptr<Window>& pWin = mpWinDebug;
	HWND hwnd                     = pWin->GetHWND();
	const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCount(hwnd);
	const int FRAME_DATA_INDEX = mNumUpdateLoopsExecuted % NUM_BACK_BUFFERS;
	FFrameData& FrameData      = mScene_DebugWnd.mFrameData[FRAME_DATA_INDEX];
	const Input& input         = mInputStates.at(hwnd);


}

void VQEngine::UpdateThread_PostUpdate()
{
	const int NUM_BACK_BUFFERS      = mRenderer.GetSwapChainBackBufferCount(mpWinMain->GetHWND());
	const int FRAME_DATA_INDEX      = mNumUpdateLoopsExecuted % NUM_BACK_BUFFERS;
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

void VQEngine::Load_SceneData_Dispatch()
{
	mUpdateWorkerThreads.AddTask([&]() { Sleep(1000); Log::Info("Worker SLEEP done!"); }); // simulate 1second loading time
	mUpdateWorkerThreads.AddTask([&]()
	{
		const int NumBackBuffer_WndMain = mRenderer.GetSwapChainBackBufferCount(mpWinMain);
		const int NumBackBuffer_WndDbg  = mRenderer.GetSwapChainBackBufferCount(mpWinDebug);

		// TODO: initialize window scene data here for now, should update this to proper location later on (Scene probably?)
		FFrameData data[2];
		data[0].SwapChainClearColor = { 0.07f, 0.07f, 0.07f, 1.0f };

		// Cube Data
		constexpr XMFLOAT3 CUBE_POSITION         = XMFLOAT3(0, 0, 4);
		constexpr float    CUBE_SCALE            = 1.0f;
		constexpr XMFLOAT3 CUBE_ROTATION_VECTOR  = XMFLOAT3(1, 1, 1);
		constexpr float    CUBE_ROTATION_DEGREES = 60.0f;
		const XMVECTOR     CUBE_ROTATION_AXIS    = XMVector3Normalize(XMLoadFloat3(&CUBE_ROTATION_VECTOR));
		data[0].TFCube = Transform(
			  CUBE_POSITION
			, Quaternion::FromAxisAngle(CUBE_ROTATION_AXIS, CUBE_ROTATION_DEGREES * DEG2RAD)
			, XMFLOAT3(CUBE_SCALE, CUBE_SCALE, CUBE_SCALE)
		);

		FCameraData camData = GenerateCameraInitializationParameters(mpWinMain);
		data[0].SceneCamera.InitializeCamera(camData);
		mScene_MainWnd.mFrameData.resize(NumBackBuffer_WndMain, data[0]);

		data[1].SwapChainClearColor = { 0.20f, 0.21f, 0.21f, 1.0f };
		mScene_DebugWnd.mFrameData.resize(NumBackBuffer_WndDbg, data[1]);

		mWindowUpdateContextLookup[mpWinMain->GetHWND()] = &mScene_MainWnd;
		if (mpWinDebug) mWindowUpdateContextLookup[mpWinDebug->GetHWND()] = &mScene_DebugWnd;
	});
}

void VQEngine::Load_SceneData_Join()
{
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


// ===============================================================================================================================


void MainWindowScene::Update()
{

}

void DebugWindowScene::Update()
{
}


