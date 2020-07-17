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
	if (mpWinDebug) Input::InitRawInputDevices(mpWinDebug->GetHWND());
#endif

	RegisterWindowForInput(mpWinMain);
	RegisterWindowForInput(mpWinDebug);

	// busy lock until render thread is initialized
	while (!mbRenderThreadInitialized); 
	LoadLoadingScreenData();

	// Do not show windows until we have the loading screen data ready.
	mpWinMain->Show();
	if (mpWinDebug) 
		mpWinDebug->Show();

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
		UpdateThread_UpdateScene_MainWnd(dt);
	}

}

void VQEngine::UpdateThread_UpdateScene_MainWnd(const float dt)
{
	const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCount(mpWinMain->GetHWND());
	const int FRAME_DATA_INDEX = mNumUpdateLoopsExecuted % NUM_BACK_BUFFERS;
	FFrameData& FrameData      = mScene_MainWnd.mFrameData[FRAME_DATA_INDEX];
	const Input& input         = mInputStates.at(mpWinMain->GetHWND());
	
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

void VQEngine::UpdateThread_PostUpdate()
{
	if (mbLoadingLevel)
	{
		return;
	}

	// compute visibility 

	// extract scene view

	// copy over state for next frame
	
	const int NUM_BACK_BUFFERS      = mRenderer.GetSwapChainBackBufferCount(mpWinMain->GetHWND());
	const int FRAME_DATA_INDEX      = mNumUpdateLoopsExecuted % NUM_BACK_BUFFERS;
	const int FRAME_DATA_NEXT_INDEX = ((mNumUpdateLoopsExecuted % NUM_BACK_BUFFERS) + 1) % NUM_BACK_BUFFERS;

	mScene_MainWnd.mFrameData[FRAME_DATA_NEXT_INDEX] = mScene_MainWnd.mFrameData[FRAME_DATA_INDEX];

	// input post update;
	Input& i = mInputStates.at(mpWinMain->GetHWND()); // inline copies Input(), hence explicitly get refs
	i.PostUpdate();
	if (mpWinDebug) 
	{
		Input& iD = mInputStates.at(mpWinDebug->GetHWND());
		iD.PostUpdate();
	}
}

void VQEngine::UpdateThread_HandleEvents()
{
	// Swap event recording buffers so we can read & process a limited number of events safely.
	mInputEventQueue.SwapBuffers();
	std::queue<EventPtr_t>& q = mInputEventQueue.GetBackContainer();

	if (q.empty())
		return;

	// process the events
	std::shared_ptr<IEvent> pEvent = nullptr;
	while (!q.empty())
	{
		pEvent = std::move(q.front());
		q.pop();

		switch (pEvent->mType)
		{
		case KEY_DOWN_EVENT: 
		{
			std::shared_ptr<KeyDownEvent> p = std::static_pointer_cast<KeyDownEvent>(pEvent);
			mInputStates.at(p->hwnd).UpdateKeyDown(p->data);
		} break;
		case KEY_UP_EVENT:
		{
			std::shared_ptr<KeyUpEvent> p = std::static_pointer_cast<KeyUpEvent>(pEvent);
			mInputStates.at(p->hwnd).UpdateKeyUp(p->wparam);
		} break;

		case MOUSE_MOVE_EVENT:
		{
			std::shared_ptr<MouseMoveEvent> p = std::static_pointer_cast<MouseMoveEvent>(pEvent);
			mInputStates.at(p->hwnd).UpdateMousePos(p->x, p->y, 0);
		} break;
		case MOUSE_SCROLL_EVENT:
		{
			std::shared_ptr<MouseScrollEvent> p = std::static_pointer_cast<MouseScrollEvent>(pEvent);
			mInputStates.at(p->hwnd).UpdateMousePos(0, 0, p->scroll);
		} break;
		case MOUSE_INPUT_EVENT:
		{
			std::shared_ptr<MouseInputEvent> p = std::static_pointer_cast<MouseInputEvent>(pEvent);
			mInputStates.at(p->hwnd).UpdateMousePos_Raw(
				p->data.relativeX
				, p->data.relativeY
				, static_cast<short>(p->data.scrollDelta)
				, GetWindow(p->hwnd)->IsMouseCaptured() || 1 // TODO
			);
		} break;
		}
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

		FCameraData camData = {};
		camData.nearPlane = 0.01f;
		camData.farPlane  = 1000.0f;
		camData.x = 0.0f; camData.y = 3.0f; camData.z = -5.0f;
		camData.pitch = 10.0f;
		camData.yaw = 0.0f;
		camData.fovH_Degrees = 60.0f;
		data[0].SceneCamera.InitializeCamera(camData, mpWinMain->GetWidth(), mpWinMain->GetHeight());
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


