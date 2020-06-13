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

#include <d3d12.h>
#include <dxgi.h>


void VQEngine::RenderThread_Main()
{
	Log::Info("RenderThread_Main()");
	this->RenderThread_Inititalize();

	bool bQuit = false;
	while (!this->mbStopAllThreads && !bQuit)
	{
		RenderThread_WaitForUpdateThread();

#if DEBUG_LOG_THREAD_SYNC_VERBOSE
		Log::Info(/*"RenderThread_Tick() : */"r%d (u=%llu)", mNumRenderLoopsExecuted.load(), mNumUpdateLoopsExecuted.load());
#endif

		RenderThread_PreRender();
		RenderThread_Render();

		++mNumRenderLoopsExecuted;

		RenderThread_SignalUpdateThread();
	}

	this->RenderThread_Exit();
	Log::Info("RenderThread_Main() : Exit");
}


void VQEngine::RenderThread_WaitForUpdateThread()
{
#if DEBUG_LOG_THREAD_SYNC_VERBOSE
	Log::Info("r:wait : u=%llu, r=%llu", mNumUpdateLoopsExecuted.load(), mNumRenderLoopsExecuted.load());
#endif

	mpSemRender->Wait();
}

void VQEngine::RenderThread_SignalUpdateThread()
{
	mpSemUpdate->Signal();
}






void VQEngine::RenderThread_Inititalize()
{
	FRendererInitializeParameters params = {};
	params.Windows.push_back(FWindowRepresentation(mpWinMain , mSettings.gfx.bVsync));
	params.Windows.push_back(FWindowRepresentation(mpWinDebug, false));
	params.Settings = mSettings.gfx;
	mRenderer.Initialize(params);

	mbRenderThreadInitialized.store(true);
	mNumRenderLoopsExecuted.store(0);
}

void VQEngine::RenderThread_Exit()
{
	mRenderer.Exit();
}

void VQEngine::RenderThread_PreRender()
{
}

void VQEngine::RenderThread_Render()
{
	const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCountOfWindow(mpWinMain);
	const int FRAME_DATA_INDEX  = mNumRenderLoopsExecuted % NUM_BACK_BUFFERS;
	

	if (mbLoadingLevel)
	{
		// TODO: proper data encalsulation + remove reinterpret cast.
		//       this was good enough for testing threaded loading and is meant to be temporary.
		mRenderer.RenderWindowContext(mpWinMain->GetHWND() , *reinterpret_cast<FFrameData*>(&mScene_MainWnd.mLoadingScreenData[FRAME_DATA_INDEX]));
		mRenderer.RenderWindowContext(mpWinDebug->GetHWND(), *reinterpret_cast<FFrameData*>(&mScene_DebugWnd.mLoadingScreenData[FRAME_DATA_INDEX]));
	}
	
	else
	{
		// TODO: render in parallel???
		mRenderer.RenderWindowContext(mpWinMain->GetHWND(), mScene_MainWnd.mFrameData[FRAME_DATA_INDEX]);
		mRenderer.RenderWindowContext(mpWinDebug->GetHWND(), mScene_DebugWnd.mFrameData[FRAME_DATA_INDEX]);
	}
}

