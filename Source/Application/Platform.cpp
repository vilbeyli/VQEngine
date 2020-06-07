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

#include "Platform.h"
#include "Window.h"

#define LOG_WINDOW_MESSAGE_EVENTS 1


static void LogWndMsg(UINT uMsg, HWND hwnd)
{
#if LOG_WINDOW_MESSAGE_EVENTS
	switch (uMsg)
	{
	case WM_CLOSE	: Log::Info("WM_CLOSE<hwnd=0x%x>", hwnd); break;
	case WM_CREATE	: Log::Info("WM_CREATE<hwnd=0x%x>", hwnd); break;
	case WM_SIZE	: Log::Info("WM_SIZE<hwnd=0x%x>", hwnd); break;
	case WM_DESTROY	: Log::Info("WM_DESTROY<hwnd=0x%x>", hwnd); break;
	case WM_KEYDOWN	: Log::Info("WM_KEYDOWN<hwnd=0x%x>", hwnd); break;
	case WM_PAINT	: Log::Info("WM_PAINT<hwnd=0x%x>", hwnd); break;
	default: Log::Warning("LogWndMsg not defined for msg=%u", uMsg); break;
	}
#endif
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	IWindow* pWindow = reinterpret_cast<IWindow*> (::GetWindowLongPtr(hwnd, GWLP_USERDATA));
	if (!pWindow)
	{
		Log::Warning("WndProc::pWindow=nullptr");
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	switch (uMsg)
	{
	// https://docs.microsoft.com/en-us/windows/win32/learnwin32/managing-application-state-
	case WM_CREATE:
		LogWndMsg(uMsg, hwnd);
		if(pWindow->pOwner) pWindow->pOwner->OnWindowCreate();
		return 0;


	// https://docs.microsoft.com/en-us/windows/win32/learnwin32/writing-the-window-procedure
	case WM_SIZE:
	{
		LogWndMsg(uMsg, hwnd);
		if (pWindow->pOwner) pWindow->pOwner->OnWindowResize(hwnd);
		return 0;
	}

	case WM_KEYDOWN:
		LogWndMsg(uMsg, hwnd);
		if (pWindow->pOwner) pWindow->pOwner->OnWindowKeyDown(wParam);
		return 0;

	// https://docs.microsoft.com/en-us/windows/win32/learnwin32/painting-the-window
	case WM_PAINT:
	{
		LogWndMsg(uMsg, hwnd);
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));
		EndPaint(hwnd, &ps);
		return 0;
	}


	// https://docs.microsoft.com/en-us/windows/win32/learnwin32/closing-the-window
	case WM_CLOSE:
		LogWndMsg(uMsg, hwnd);
		pWindow->OnClose();
		if (pWindow->pOwner) pWindow->pOwner->OnWindowClose();
		return 0;

	case WM_DESTROY:
		LogWndMsg(uMsg, hwnd);
		return 0;

	}


	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
