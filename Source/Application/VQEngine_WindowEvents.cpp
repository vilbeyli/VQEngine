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



#define LOG_WINDOW_MESSAGE_EVENTS 0
static void LogWndMsg(UINT uMsg, HWND hwnd)
{
#if LOG_WINDOW_MESSAGE_EVENTS
	switch (uMsg)
	{
	case WM_CLOSE: Log::Info("WM_CLOSE<hwnd=0x%x>", hwnd); break;
	case WM_CREATE: Log::Info("WM_CREATE<hwnd=0x%x>", hwnd); break;
	case WM_SIZE: Log::Info("WM_SIZE<hwnd=0x%x>", hwnd); break;
	case WM_DESTROY: Log::Info("WM_DESTROY<hwnd=0x%x>", hwnd); break;
	case WM_KEYDOWN: Log::Info("WM_KEYDOWN<hwnd=0x%x>", hwnd); break;
	case WM_PAINT: Log::Info("WM_PAINT<hwnd=0x%x>", hwnd); break;
	case WM_SETFOCUS: Log::Info("WM_SETFOCUS<hwnd=0x%x>", hwnd); break;
	default: Log::Warning("LogWndMsg not defined for msg=%u", uMsg); break;
	}
#endif
}


LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LogWndMsg(uMsg, hwnd);

	IWindow* pWindow = reinterpret_cast<IWindow*> (::GetWindowLongPtr(hwnd, GWLP_USERDATA));
	if (!pWindow)
	{
		//Log::Warning("WndProc::pWindow=nullptr");
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	switch (uMsg)
	{
		// https://docs.microsoft.com/en-us/windows/win32/learnwin32/managing-application-state-
	case WM_CREATE:
		if (pWindow->pOwner) pWindow->pOwner->OnWindowCreate(pWindow);
		return 0;


		// https://docs.microsoft.com/en-us/windows/win32/learnwin32/writing-the-window-procedure
	case WM_SIZE:
	{
		if (pWindow->pOwner) pWindow->pOwner->OnWindowResize(hwnd);
		return 0;
	}

	case WM_KEYDOWN:
		if (pWindow->pOwner) pWindow->pOwner->OnWindowKeyDown(wParam);
		return 0;

	case WM_SYSKEYDOWN:
		if ((wParam == VK_RETURN) && (lParam & (1 << 29))) // Handle ALT+ENTER
		{
			if (pWindow->pOwner)
			{
				pWindow->pOwner->OnToggleFullscreen(hwnd);
				return 0;
			}
		}
		// Send all other WM_SYSKEYDOWN messages to the default WndProc.
		break;

		// https://docs.microsoft.com/en-us/windows/win32/learnwin32/painting-the-window
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));
		EndPaint(hwnd, &ps);
		return 0;
	}

	case WM_SETFOCUS:
	{
		if (pWindow->pOwner) pWindow->pOwner->OnWindowFocus(pWindow);
		return 0;
	}

	// https://docs.microsoft.com/en-us/windows/win32/learnwin32/closing-the-window
	case WM_CLOSE:
		if (pWindow->pOwner) pWindow->pOwner->OnWindowClose(pWindow);
		return 0;

	case WM_DESTROY:
		return 0;

	}


	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}



void VQEngine::OnWindowCreate(IWindow* pWnd)
{
}

void VQEngine::OnWindowResize(HWND hWnd)
{
	// https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/d3d10-graphics-programming-guide-dxgi#handling-window-resizing
	RECT clientRect = {};
	GetClientRect(hWnd, &clientRect);
	int w = clientRect.right - clientRect.left;
	int h = clientRect.bottom - clientRect.top;
	
	// Due to multi-threading, this thread will record the events and 
	// Render Thread will process the queue at the of a render loop
	mWinEventQueue.AddItem(new WindowResizeEvent(w, h, hWnd));
}

void VQEngine::OnToggleFullscreen(HWND hWnd)
{
	// Due to multi-threading, this thread will record the events and 
	// Render Thread will process the queue at the of a render loop
	mWinEventQueue.AddItem(new ToggleFullscreenEvent(hWnd));
}

void VQEngine::OnWindowMinimize(IWindow* pWnd)
{
}

void VQEngine::OnWindowFocus(IWindow* pWindow)
{
	//Log::Info("On Focus!");
}


void VQEngine::OnWindowKeyDown(WPARAM wParam)
{
}

void VQEngine::OnWindowClose(IWindow* pWindow)
{
	mRenderer.GetWindowSwapChain(static_cast<Window*>(pWindow)->GetHWND()).WaitForGPU();

	pWindow->Close();

	if (pWindow == mpWinMain.get())
		PostQuitMessage(0);

}
 