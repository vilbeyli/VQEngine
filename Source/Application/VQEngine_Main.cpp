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

void VQEngine::InitializeWindow(const FStartupParameters& Params)
{
	FWindowDesc mainWndDesc = {};
	mainWndDesc.width = 640;
	mainWndDesc.height = 480;
	mainWndDesc.hInst = Params.hExeInstance;
	mainWndDesc.pWndOwner = this;
	mainWndDesc.pfnWndProc = WndProc;
	mWinMain.reset(new Window("Main Window", mainWndDesc));

	mainWndDesc.width = 320;
	mainWndDesc.height = 240;
	mWinDebug.reset(new Window("Debug Window", mainWndDesc));
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
	mLoadThread.join();
}

bool VQEngine::Initialize(const FStartupParameters& Params)
{
	InitializeWindow(Params);
	InitializeThreads();
	
	return false;
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





void VQEngine::MainThread_Tick()
{
	if (mWinMain->IsClosed())
	{
		mWinDebug->OnClose();
		PostQuitMessage(0);
	}

	static unsigned long long c = 0;
	static unsigned long long cp = 0;
	constexpr unsigned long long p = 10000;
	if (++c % p)
		;// Log::Info("Tick<%llu> : %llu", p, cp++);

	//const UINT64 newTimeValue = GetTickCount64() - g_TimeOffset;
	//g_TimeDelta = (float)(newTimeValue - g_TimeValue) * 0.001f;
	//g_TimeValue = newTimeValue;
	//g_Time = (float)newTimeValue * 0.001f;
	//
	//Update();
	//Render();
	//Present();
}
