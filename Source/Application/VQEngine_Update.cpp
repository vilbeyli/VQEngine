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

using namespace DirectX;

void VQEngine::UpdateThread_Main()
{
	Log::Info("UpdateThread_Main()");
	UpdateThread_Inititalize();

	bool bQuit = false;
	float dt = 0.0f;
	while (!mbStopAllThreads && !bQuit)
	{
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

	// update input

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
		mAppState = EAppState::LOADING;

		mbLoadingLevel.store(true);
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
		const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCount(mpWinMain->GetHWND());
		const int FRAME_DATA_INDEX = mNumUpdateLoopsExecuted % NUM_BACK_BUFFERS;

		// update scene data
		mScene_MainWnd.mFrameData[FRAME_DATA_INDEX].TFCube.RotateAroundAxisRadians(YAxis, dt * 0.2f * PI);
	}

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
}


void VQEngine::Load_SceneData_Dispatch()
{
	//mUpdateWorkerThreads.AddTask([&]() { Sleep(2000); Log::Info("Worker SLEEP done!"); }); // simulate 2second loading time
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

		CameraData camData = {};
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


