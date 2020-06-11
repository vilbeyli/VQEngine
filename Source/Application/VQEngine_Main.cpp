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

#ifdef _DEBUG
constexpr char* BUILD_CONFIG = "Debug";
#else
constexpr char* BUILD_CONFIG = "Release";
#endif


void VQEngine::MainThread_Tick()
{
	if (mpWinMain->IsClosed())
	{
		mpWinDebug->OnClose();
		PostQuitMessage(0);
	}

	// TODO: populate input queue and signal Update thread 
	//       to drain the buffered input from the queue
}


void VQEngine::InititalizeEngineSettings(const FStartupParameters& Params)
{
	// Defaults
	mSettings.gfx.bFullscreen = false;
	mSettings.gfx.bVsync = false;
	mSettings.gfx.bUseTripleBuffering = true;
	mSettings.gfx.Width = 1920;
	mSettings.gfx.Height = 1080;

	mSettings.DebugWindow_Width = 600;
	mSettings.DebugWindow_Height = 600;

	sprintf_s(mSettings.strMainWindowTitle , "VQEngine v0.1.0-%s", BUILD_CONFIG);
	sprintf_s(mSettings.strDebugWindowTitle, "VQDebug");

	// Override #0 : from file


	// Override #1 : if there's command line params
	if (Params.bOverrideGFXSetting_bFullscreen) mSettings.gfx.bFullscreen         = Params.GraphicsSettings.bFullscreen;
	if (Params.bOverrideGFXSetting_bVSync     ) mSettings.gfx.bVsync              = Params.GraphicsSettings.bVsync;
	if (Params.bOverrideGFXSetting_bUseTripleBuffering) mSettings.gfx.bUseTripleBuffering = Params.GraphicsSettings.bUseTripleBuffering;
	if (Params.bOverrideGFXSetting_Width)       mSettings.gfx.Width               = Params.GraphicsSettings.Width;
	if (Params.bOverrideGFXSetting_Height)      mSettings.gfx.Height              = Params.GraphicsSettings.Height;
}

void VQEngine::InitializeWindow(const FStartupParameters& Params)
{
	FWindowDesc mainWndDesc = {};
	mainWndDesc.width  = mSettings.gfx.Width;
	mainWndDesc.height = mSettings.gfx.Height;
	mainWndDesc.hInst = Params.hExeInstance;
	mainWndDesc.pWndOwner = this;
	mainWndDesc.pfnWndProc = WndProc;
	mpWinMain.reset(new Window(mSettings.strMainWindowTitle, mainWndDesc));

	mainWndDesc.width  = mSettings.DebugWindow_Width;
	mainWndDesc.height = mSettings.DebugWindow_Height;
	mpWinDebug.reset(new Window(mSettings.strDebugWindowTitle, mainWndDesc));
}

void VQEngine::InitializeThreads()
{
	mbStopAllThreads.store(false);
	mRenderThread = std::thread(&VQEngine::RenderThread_Main, this);
	mUpdateThread = std::thread(&VQEngine::UpdateThread_Main, this);
	mLoadThread   = std::thread(&VQEngine::LoadThread_Main, this);
}

void VQEngine::ExitThreads()
{
	mbStopAllThreads.store(true);
	mRenderThread.join();
	mUpdateThread.join();

	// no need to lock here: https://en.cppreference.com/w/cpp/thread/condition_variable/notify_all
	// The notifying thread does not need to hold the lock on the same mutex 
	// as the one held by the waiting thread(s); in fact doing so is a pessimization, 
	// since the notified thread would immediately block again, waiting for the 
	// notifying thread to release the lock.
	mCVLoadTasksReadyForProcess.notify_all();

	mLoadThread.join();
}

bool VQEngine::Initialize(const FStartupParameters& Params)
{
	InititalizeEngineSettings(Params);
	InitializeWindow(Params);
	InitializeThreads();
	
	return true;
}

void VQEngine::Exit()
{
	ExitThreads();
}

