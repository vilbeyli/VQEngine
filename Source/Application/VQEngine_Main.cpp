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


void VQEngine::InitializeWindow(const FStartupParameters& Params)
{
	FWindowDesc mainWndDesc = {};
	mainWndDesc.width = 640;
	mainWndDesc.height = 480;
	mainWndDesc.hInst = Params.hExeInstance;
	mainWndDesc.pWndOwner = this;
	mainWndDesc.pfnWndProc = WndProc;
	mpWinMain.reset(new Window("Main Window", mainWndDesc));

	mainWndDesc.width = 320;
	mainWndDesc.height = 240;
	mpWinDebug.reset(new Window("Debug Window", mainWndDesc));
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
	InitializeWindow(Params);
	InitializeThreads();
	
	return true;
}

void VQEngine::Exit()
{
	ExitThreads();
}

void VQEngine::OnWindowCreate()
{
}

void VQEngine::OnWindowResize(HWND hWnd)
{
	// https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/d3d10-graphics-programming-guide-dxgi#handling-window-resizing
#if 0 // TODO
	RECT clientRect = {};
	GetClientRect(hWnd, &clientRect);
	dxSample->OnResize(clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);

	bIsMinimized = (IsIconic(hWnd) == TRUE);
#endif
}

void VQEngine::OnWindowMinimize()
{
}

void VQEngine::OnWindowFocus()
{
}


void VQEngine::OnWindowKeyDown(WPARAM wParam)
{
}

void VQEngine::OnWindowClose()
{
}
