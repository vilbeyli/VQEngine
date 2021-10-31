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

#include "../VQEngine.h"
#include "Input.h"

#include <Windowsx.h>

constexpr int MIN_WINDOW_SIZE = 128; // make sure window cannot be resized smaller than 128x128

#define LOG_WINDOW_MESSAGE_EVENTS 0
#define LOG_RAW_INPUT             0
#define LOG_CALLBACKS             0
static void LogWndMsg(UINT uMsg, HWND hwnd);

// ===================================================================================================================================
// WINDOW PROCEDURE
// ===================================================================================================================================
LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LogWndMsg(uMsg, hwnd);
	IWindow* pWindow = reinterpret_cast<IWindow*> (::GetWindowLongPtr(hwnd, GWLP_USERDATA));
	if (!pWindow)
	{
		// WM_CREATE will be sent before SetWindowLongPtr() is called, 
		// hence we'll call OnWindowCreate from inside Window class.
		///if(uMsg == WM_CREATE) Log::Warning("WM_CREATE without pWindow");

		#if 0
		Log::Warning("WndProc::pWindow=nullptr");
		#endif
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}


	switch (uMsg) // HANDLE EVENTS
	{
	//
	// WINDOW
	//
	// https://docs.microsoft.com/en-us/windows/win32/learnwin32/managing-application-state-

	// https://docs.microsoft.com/en-us/windows/win32/learnwin32/writing-the-window-procedure
	case WM_SIZE:   if (pWindow->pOwner) pWindow->pOwner->OnWindowResize(hwnd); return 0;
	case WM_GETMINMAXINFO:
	{   
		LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
		lpMMI->ptMinTrackSize.x = MIN_WINDOW_SIZE;
		lpMMI->ptMinTrackSize.y = MIN_WINDOW_SIZE;
		break;
	}
	case WM_PAINT: // https://docs.microsoft.com/en-us/windows/win32/learnwin32/painting-the-window
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));
		EndPaint(hwnd, &ps);
		return 0;
	}

	// https://docs.microsoft.com/en-us/windows/win32/inputdev/wm-setfocus
	case WM_SETFOCUS: if (pWindow->pOwner) pWindow->pOwner->OnWindowFocus(hwnd); return 0;
	// https://docs.microsoft.com/en-us/windows/win32/inputdev/wm-killfocus
	case WM_KILLFOCUS: if (pWindow->pOwner) pWindow->pOwner->OnWindowLoseFocus(hwnd); return 0;

	// https://docs.microsoft.com/en-us/windows/win32/learnwin32/closing-the-window
	case WM_CLOSE:    if (pWindow->pOwner) pWindow->pOwner->OnWindowClose(hwnd); return 0;
	case WM_DESTROY:  return 0;

	// https://docs.microsoft.com/en-us/windows/win32/inputdev/wm-activate
	case WM_ACTIVATE: 
		if (pWindow->pOwner)
		{
			// **** **** **** **** **** **** **** **** = wParam <-- (32-bits)
			// 0000 0000 0000 0000 1111 1111 1111 1111 = 0xFFFF <-- LOWORD(Wparam)
			UINT wparam_hi  = HIWORD(wParam);
			UINT wparam_low = LOWORD(wParam);

			// wParam:
			// - The low-order word specifies whether the window is being activated or deactivated.
			// - The high-order word specifies the minimized state of the window being activated or deactivated
			// - A nonzero value indicates the window is minimized.
			const bool bWindowInactive     =  wparam_low == WA_INACTIVE;
			const bool bWindowActivation   = (wparam_low == WA_ACTIVE) || (wparam_low == WA_CLICKACTIVE);
			
			// lParam:
			// - A handle to the window being activated or deactivated, depending on the value of the wParam parameter.
			// - If the low-order word of wParam is WA_INACTIVE, lParam is the handle to the window being activated
			// - if the low-order word of wParam is WA_ACTIVE or WA_CLICKACTIVE, lParam is the handle to the window being deactivated. 
			//   This handle can be NULL.
			HWND hwnd = reinterpret_cast<HWND>(lParam);
			if (bWindowInactive)
				pWindow->pOwner->OnWindowActivate(hwnd);
			else if (hwnd != NULL)
				pWindow->pOwner->OnWindowDeactivate(hwnd);
		}
		return 0;

	case WM_MOVE:
		if (pWindow->pOwner)
		{
			int xPos = (int)(short)LOWORD(lParam);
			int yPos = (int)(short)HIWORD(lParam);
			pWindow->pOwner->OnWindowMove(hwnd, xPos, yPos);
		}
		return 0;
	
	// https://docs.microsoft.com/en-us/windows/win32/gdi/wm-displaychange
	// This message is only sent to top-level windows. For all other windows it is posted.
	case WM_DISPLAYCHANGE:
		if (pWindow->pOwner)
		{
			int ImageDepthBitsPerPixel = (int)wParam;
			int ScreenResolutionX = LOWORD(lParam);
			int ScreenResolutionY = HIWORD(lParam);
			pWindow->pOwner->OnDisplayChange(hwnd, ImageDepthBitsPerPixel, ScreenResolutionX, ScreenResolutionY);
		}
		return 0;

	//
	// KEYBOARD
	//
	case WM_KEYDOWN: if (pWindow->pOwner) pWindow->pOwner->OnKeyDown(hwnd, wParam); return 0;
	case WM_KEYUP:   if (pWindow->pOwner) pWindow->pOwner->OnKeyUp(hwnd, wParam);   return 0;
	case WM_SYSKEYDOWN:
		if ((wParam == VK_RETURN) && (lParam & (1 << 29))) // Handle ALT+ENTER
		{
			if (pWindow->pOwner)
			{
				pWindow->pOwner->OnToggleFullscreen(hwnd);
				return 0;
			}
		} break; // Send all other WM_SYSKEYDOWN messages to the default WndProc.
	
	// Turn off MessageBeep sound on Alt+Enter: https://stackoverflow.com/questions/3662192/disable-messagebeep-on-invalid-syskeypress
	case WM_MENUCHAR: return MNC_CLOSE << 16;


	//
	// MOUSE
	//
	case WM_MBUTTONDOWN: // wParam encodes which mouse key is pressed, unlike *BUTTONUP events
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
		if (pWindow->pOwner) pWindow->pOwner->OnMouseButtonDown(hwnd, wParam, false);
		return 0;
	case WM_LBUTTONDBLCLK:
	case WM_RBUTTONDBLCLK:
	case WM_MBUTTONDBLCLK:
		if (pWindow->pOwner)
		{
			pWindow->pOwner->OnMouseButtonDown(hwnd, wParam, true);
			pWindow->pOwner->OnMouseButtonDown(hwnd, wParam, true);
		}
		return 0;

	// For mouse button up events, wParam=0 for some reason, as opposed to the documentation
	// https://docs.microsoft.com/en-us/windows/win32/inputdev/wm-lbuttonup
	// hence we handle each case individually here with the proper key code.
	case WM_MBUTTONUP: if (pWindow->pOwner) pWindow->pOwner->OnMouseButtonUp(hwnd, MK_MBUTTON); return 0;
	case WM_RBUTTONUP: if (pWindow->pOwner) pWindow->pOwner->OnMouseButtonUp(hwnd, MK_RBUTTON); return 0;
	case WM_LBUTTONUP: if (pWindow->pOwner) pWindow->pOwner->OnMouseButtonUp(hwnd, MK_LBUTTON); return 0;


#if ENABLE_RAW_INPUT // https://msdn.microsoft.com/en-us/library/windows/desktop/ee418864.aspx
	case WM_INPUT: if (pWindow->pOwner) pWindow->pOwner->OnMouseInput(hwnd, lParam); return 0;
#else
		
	case WM_MOUSEMOVE  : if (pWindow->pOwner) pWindow->pOwner->OnMouseMove(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0; 
	case WM_MOUSEWHEEL :
	{
		const WORD fwKeys  = GET_KEYSTATE_WPARAM(wParam);
		const short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
		if (pWindow->pOwner) pWindow->pOwner->OnMouseScroll(hwnd, zDelta); return 0;
	}
#endif // ENABLE_RAW_INPUT
	}


	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}




// ===================================================================================================================================
// WINDOW EVENTS
// ===================================================================================================================================
// Due to multi-threading, this thread will record the events and 
// Render Thread will process the queue at the beginning & end of a render loop
void VQEngine::OnWindowResize(HWND hWnd)
{
	// https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/d3d10-graphics-programming-guide-dxgi#handling-window-resizing
	RECT clientRect = {};
	GetClientRect(hWnd, &clientRect);
	int w = clientRect.right - clientRect.left;
	int h = clientRect.bottom - clientRect.top;

#if 0 // MinWindowSize prevents the crash from h==0, no need for the code below.
	if (h == 0) { h = 8; Log::Warning("WND RESIZE TOO SMALL"); }
	if (w == 0) { w = 8; Log::Warning("WND RESIZE TOO SMALL"); }
#endif

	mEventQueue_WinToVQE_Renderer.AddItem(std::make_unique<WindowResizeEvent>(w, h, hWnd));
	mEventQueue_WinToVQE_Update.AddItem(std::make_unique<WindowResizeEvent>(w, h, hWnd));
}

void VQEngine::OnToggleFullscreen(HWND hWnd)
{
	mEventQueue_WinToVQE_Renderer.AddItem(std::make_unique<ToggleFullscreenEvent>(hWnd));
	mEventQueue_WinToVQE_Update.AddItem(std::make_unique<ToggleFullscreenEvent>(hWnd));
}

//------------------------------------------------------------------------------------
// windows ACTIVATE msg contains the other HWND that the application switches to.
// this could be an HWND of any program on the user machine, so ensure VQEngine
// owns the hWnd before calling GetWindowName() for OnWindowActivate() and 
// OnWindowDeactivate().
//------------------------------------------------------------------------------------
void VQEngine::OnWindowActivate(HWND hWnd)
{
	if (IsWindowRegistered(hWnd))
	{
#if LOG_CALLBACKS
		Log::Warning("OnWindowActivate<%0x, %s>", hWnd, GetWindowName(hWnd).c_str());
#endif
	}
}
void VQEngine::OnWindowDeactivate(HWND hWnd)
{
	if (IsWindowRegistered(hWnd))
	{
#if LOG_CALLBACKS
		Log::Warning("OnWindowDeactivate<%0x, %s> ", hWnd, GetWindowName(hWnd).c_str());
#endif
	}
}
//-------------------------------------------------------------------------------------


void VQEngine::DispatchHDRSwapchainTransitionEvents(HWND hwnd)
{
	// Note: @mbMainWindowHDRTransitionInProgress is a necessary state here to avoid one of the following:
	// - 1.1: OnMove() and OnDisplayChange() updates the Window.IsOnHDRCapableDisplay
	// - 1.2: DX12 validation error due to Window.IsOnHDRCapableDisplay changes the render path and causes a mismatch in swapchain output format
	// - 2.1: Render thread updates the Window.IsOnHDRCapableDisplay, but,
	// - 2.2: Main thread sends multiple messages of SetSwapchainFormatEvent due to not seeing Window.IsOnHDRCapableDisplay updated just yet
	// Solution: Keep (2) and use @mbMainWindowHDRTransitionInProgress to block additional main thread messages to render thread
	if (mbMainWindowHDRTransitionInProgress)
		return;

	auto& pWin = GetWindow(hwnd);
	const uint32 W = pWin->GetWidth();
	const uint32 H = pWin->GetHeight();
	const bool bCurrentMonitorSupportsHDR = VQSystemInfo::FMonitorInfo::CheckHDRSupport(hwnd);
	const bool bCurrentMonitorWasSupportingHDR = pWin->GetIsOnHDRCapableDisplay();
	// Note: pWin->SetIsOnHDRCapableDisplay() is called from the Render Thread when handling SwapchainFormatEvent.

	if (bCurrentMonitorWasSupportingHDR ^ bCurrentMonitorSupportsHDR)
	{
		DXGI_FORMAT FORMAT = bCurrentMonitorSupportsHDR ? PREFERRED_HDR_FORMAT : PREFERRED_SDR_FORMAT;
		Log::Info("OnWindowMove<%0x, %s>() : Window moved to %s monitor."
			, hwnd
			, GetWindowName(hwnd).c_str()
			, (bCurrentMonitorSupportsHDR ? "HDR-capable" : "SDR")
		);
		mbMainWindowHDRTransitionInProgress.store(true);
		mEventQueue_WinToVQE_Renderer.AddItem(std::make_shared<SetSwapchainFormatEvent>(hwnd, FORMAT));

		// recycle resize events to reload frame-dependent resources in order to
		// update tonemapper PSO so it has the right HDR or SDR output
		mEventQueue_WinToVQE_Renderer.AddItem(std::make_unique<WindowResizeEvent>(W, H, hwnd));
		mEventQueue_WinToVQE_Update.AddItem(std::make_unique<WindowResizeEvent>(W, H, hwnd));
	}
}

void VQEngine::OnWindowMove(HWND hwnd_, int x, int y)
{
#if LOG_CALLBACKS
	Log::Warning("OnWindowMove<%0x, %s>: (%d, %d)", hwnd_, GetWindowName(hwnd_).c_str(), x, y);
#endif

	if (mSettings.WndMain.bEnableHDR)
	{
		DispatchHDRSwapchainTransitionEvents(hwnd_);
	}
}
void VQEngine::OnDisplayChange(HWND hwnd_, int ImageDepthBitsPerPixel, int ScreenWidth, int ScreenHeight)
{
	// If the display's advanced color state has changed (e.g. HDR display plug/unplug, or OS HDR setting on/off)
	mSysInfo.Monitors = VQSystemInfo::GetDisplayInfo(); // re-acquire Monitors info 


	if (mSettings.WndMain.bEnableHDR)
	{
		DispatchHDRSwapchainTransitionEvents(hwnd_);
	}
}
//------------------------------------------------------------------------------------

void VQEngine::OnWindowCreate(HWND hWnd)
{
#if LOG_CALLBACKS
	Log::Info("OnWindowCreate<%0x, %s> ", hWnd, GetWindowName(hWnd).c_str());
#endif
	GetWindow(hWnd)->SetIsOnHDRCapableDisplay(VQSystemInfo::FMonitorInfo::CheckHDRSupport(hWnd));
}

void VQEngine::OnWindowClose(HWND hwnd_)
{
	std::shared_ptr<WindowCloseEvent> ptr = std::make_shared<WindowCloseEvent>(hwnd_);
	mEventQueue_WinToVQE_Renderer.AddItem(ptr);

	ptr->Signal_WindowDependentResourcesDestroyed.Wait();
	if (hwnd_ == mpWinMain->GetHWND())
	{
		PostQuitMessage(0); // must be called from the main thread.
	}
	GetWindow(hwnd_)->Close(); // must be called from the main thread.
}

void VQEngine::OnWindowMinimize(HWND hwnd)
{
}

void VQEngine::OnWindowFocus(HWND hwnd)
{
	const bool bMainWindowFocused = hwnd == mpWinMain->GetHWND();

#if LOG_CALLBACKS
	Log::Warning("OnWindowFocus<%x, %s>", hwnd, this->GetWindowName(hwnd).c_str());
#endif
}

void VQEngine::OnWindowLoseFocus(HWND hwnd)
{
#if LOG_CALLBACKS
	Log::Warning("OnWindowLoseFocus<%x, %s>", hwnd, this->GetWindowName(hwnd).c_str());
#endif

	// make sure the mouse becomes visible when Main Window is not the one that is focused
	if(hwnd == mpWinMain->GetHWND() && mpWinMain->IsMouseCaptured())
		this->SetMouseCaptureForWindow(mpWinMain->GetHWND(), false, true);
}



// ===================================================================================================================================
// KEYBOARD EVENTS
// ===================================================================================================================================
// Due to multi-threading, this thread will record the events and 
// Update Thread will process the queue at the beginning of an update loop
void VQEngine::OnKeyDown(HWND hwnd, WPARAM wParam)
{
	constexpr bool bIsMouseEvent = false;
	mEventQueue_WinToVQE_Update.AddItem(std::make_unique<KeyDownEvent>(hwnd, wParam, bIsMouseEvent));
}

void VQEngine::OnKeyUp(HWND hwnd, WPARAM wParam)
{
	constexpr bool bIsMouseEvent = false;
	mEventQueue_WinToVQE_Update.AddItem(std::make_unique<KeyUpEvent>(hwnd, wParam, bIsMouseEvent));
}


// ===================================================================================================================================
// MOUSE EVENTS
// ===================================================================================================================================
// Due to multi-threading, this thread will record the events and 
// Update Thread will process the queue at the beginning of an update loop
void VQEngine::OnMouseButtonDown(HWND hwnd, WPARAM wParam, bool bIsDoubleClick)
{
	constexpr bool bIsMouseEvent = true;
	mEventQueue_WinToVQE_Update.AddItem(std::make_unique<KeyDownEvent>(hwnd, wParam, bIsMouseEvent, bIsDoubleClick));
}

void VQEngine::OnMouseButtonUp(HWND hwnd, WPARAM wParam)
{
	constexpr bool bIsMouseEvent = true;
	mEventQueue_WinToVQE_Update.AddItem(std::make_unique<KeyUpEvent>(hwnd, wParam, bIsMouseEvent));
}

void VQEngine::OnMouseScroll(HWND hwnd, short scroll)
{
	mEventQueue_WinToVQE_Update.AddItem(std::make_unique<MouseScrollEvent>(hwnd, scroll));
}


void VQEngine::OnMouseMove(HWND hwnd, long x, long y)
{
	//Log::Info("MouseMove : (%ld, %ld)", x, y);
	mEventQueue_WinToVQE_Update.AddItem(std::make_unique<MouseMoveEvent>(hwnd, x, y));
}


void VQEngine::OnMouseInput(HWND hwnd, LPARAM lParam)
{
	MouseInputEventData data = {};
	const bool bMouseInputEvent = Input::ReadRawInput_Mouse(lParam, &data);

	if (bMouseInputEvent)
	{
		mEventQueue_WinToVQE_Update.AddItem(std::make_shared<MouseInputEvent>(data, hwnd));
	}
}






// ===================================================================================================================================
// MISC
// ===================================================================================================================================

static void LogWndMsg(UINT uMsg, HWND hwnd)
{
#if LOG_WINDOW_MESSAGE_EVENTS
#define HANDLE_CASE(EVENT)    case EVENT: Log::Info(#EVENT"\t(0x%04x)\t\t<hwnd=0x%x>", EVENT, hwnd); break
	switch (uMsg)
	{
		// https://www.autoitscript.com/autoit3/docs/appendix/WinMsgCodes.htm
		HANDLE_CASE(WM_MOUSELAST);
		HANDLE_CASE(WM_ACTIVATE);
		HANDLE_CASE(WM_ACTIVATEAPP);
		HANDLE_CASE(WM_AFXFIRST);
		HANDLE_CASE(WM_AFXLAST);
		HANDLE_CASE(WM_APP);
		HANDLE_CASE(WM_APPCOMMAND);
		HANDLE_CASE(WM_ASKCBFORMATNAME);
		HANDLE_CASE(WM_CANCELJOURNAL);
		HANDLE_CASE(WM_CANCELMODE);
		HANDLE_CASE(WM_CAPTURECHANGED);
		HANDLE_CASE(WM_CHANGECBCHAIN);
		HANDLE_CASE(WM_CHANGEUISTATE);
		HANDLE_CASE(WM_CHAR);
		HANDLE_CASE(WM_CHARTOITEM);
		HANDLE_CASE(WM_CHILDACTIVATE);
		HANDLE_CASE(WM_CLEAR);
		HANDLE_CASE(WM_CLOSE);
		HANDLE_CASE(WM_COMMAND);
		HANDLE_CASE(WM_COMMNOTIFY);
		HANDLE_CASE(WM_COMPACTING);
		HANDLE_CASE(WM_COMPAREITEM);
		HANDLE_CASE(WM_CONTEXTMENU);
		HANDLE_CASE(WM_COPY);
		HANDLE_CASE(WM_COPYDATA);
		HANDLE_CASE(WM_CREATE);
		HANDLE_CASE(WM_CTLCOLORBTN);
		HANDLE_CASE(WM_CTLCOLORDLG);
		HANDLE_CASE(WM_CTLCOLOREDIT);
		HANDLE_CASE(WM_CTLCOLORLISTBOX);
		HANDLE_CASE(WM_CTLCOLORMSGBOX);
		HANDLE_CASE(WM_CTLCOLORSCROLLBAR);
		HANDLE_CASE(WM_CTLCOLORSTATIC);
		HANDLE_CASE(WM_CUT);
		HANDLE_CASE(WM_DEADCHAR);
		HANDLE_CASE(WM_DELETEITEM);
		HANDLE_CASE(WM_DESTROY);
		HANDLE_CASE(WM_DESTROYCLIPBOARD);
		HANDLE_CASE(WM_DEVICECHANGE);
		HANDLE_CASE(WM_DEVMODECHANGE);
		HANDLE_CASE(WM_DISPLAYCHANGE);
		HANDLE_CASE(WM_DRAWCLIPBOARD);
		HANDLE_CASE(WM_DRAWITEM);
		HANDLE_CASE(WM_DROPFILES);
		HANDLE_CASE(WM_ENABLE);
		HANDLE_CASE(WM_ENDSESSION);
		HANDLE_CASE(WM_ENTERIDLE);
		HANDLE_CASE(WM_ENTERMENULOOP);
		HANDLE_CASE(WM_ENTERSIZEMOVE);
		HANDLE_CASE(WM_ERASEBKGND);
		HANDLE_CASE(WM_EXITMENULOOP);
		HANDLE_CASE(WM_EXITSIZEMOVE);
		HANDLE_CASE(WM_FONTCHANGE);
		HANDLE_CASE(WM_GETDLGCODE);
		HANDLE_CASE(WM_GETFONT);
		HANDLE_CASE(WM_GETHOTKEY);
		HANDLE_CASE(WM_GETICON);
		HANDLE_CASE(WM_GETMINMAXINFO);
		HANDLE_CASE(WM_GETOBJECT);
		HANDLE_CASE(WM_GETTEXT);
		HANDLE_CASE(WM_GETTEXTLENGTH);
		HANDLE_CASE(WM_HANDHELDFIRST);
		HANDLE_CASE(WM_HANDHELDLAST);
		HANDLE_CASE(WM_HELP);
		HANDLE_CASE(WM_HOTKEY);
		HANDLE_CASE(WM_HSCROLL);
		HANDLE_CASE(WM_HSCROLLCLIPBOARD);
		HANDLE_CASE(WM_ICONERASEBKGND);
		HANDLE_CASE(WM_IME_CHAR);
		HANDLE_CASE(WM_IME_COMPOSITION);
		HANDLE_CASE(WM_IME_COMPOSITIONFULL);
		HANDLE_CASE(WM_IME_CONTROL);
		HANDLE_CASE(WM_IME_ENDCOMPOSITION);
		HANDLE_CASE(WM_IME_KEYDOWN);
		HANDLE_CASE(WM_IME_KEYUP);
		HANDLE_CASE(WM_IME_NOTIFY);
		HANDLE_CASE(WM_IME_REQUEST);
		HANDLE_CASE(WM_IME_SELECT);
		HANDLE_CASE(WM_IME_SETCONTEXT);
		HANDLE_CASE(WM_IME_STARTCOMPOSITION);
		HANDLE_CASE(WM_INITDIALOG);
		HANDLE_CASE(WM_INITMENU);
		HANDLE_CASE(WM_INITMENUPOPUP);
		HANDLE_CASE(WM_INPUT);
		HANDLE_CASE(WM_INPUTLANGCHANGE);
		HANDLE_CASE(WM_INPUTLANGCHANGEREQUEST);
		HANDLE_CASE(WM_KEYDOWN);
		HANDLE_CASE(WM_KEYLAST);
		HANDLE_CASE(WM_KEYUP);
		HANDLE_CASE(WM_KILLFOCUS);
		HANDLE_CASE(WM_LBUTTONDBLCLK);
		HANDLE_CASE(WM_LBUTTONDOWN);
		HANDLE_CASE(WM_LBUTTONUP);
		HANDLE_CASE(WM_MBUTTONDBLCLK);
		HANDLE_CASE(WM_MBUTTONDOWN);
		HANDLE_CASE(WM_MBUTTONUP);
		HANDLE_CASE(WM_MDIACTIVATE);
		HANDLE_CASE(WM_MDICASCADE);
		HANDLE_CASE(WM_MDICREATE);
		HANDLE_CASE(WM_MDIDESTROY);
		HANDLE_CASE(WM_MDIGETACTIVE);
		HANDLE_CASE(WM_MDIICONARRANGE);
		HANDLE_CASE(WM_MDIMAXIMIZE);
		HANDLE_CASE(WM_MDINEXT);
		HANDLE_CASE(WM_MDIREFRESHMENU);
		HANDLE_CASE(WM_MDIRESTORE);
		HANDLE_CASE(WM_MDISETMENU);
		HANDLE_CASE(WM_MDITILE);
		HANDLE_CASE(WM_MEASUREITEM);
		HANDLE_CASE(WM_MENUCHAR);
		HANDLE_CASE(WM_MENUCOMMAND);
		HANDLE_CASE(WM_MENUDRAG);
		HANDLE_CASE(WM_MENUGETOBJECT);
		HANDLE_CASE(WM_MENURBUTTONUP);
		HANDLE_CASE(WM_MENUSELECT);
		HANDLE_CASE(WM_MOUSEACTIVATE);
		HANDLE_CASE(WM_MOUSEFIRST);
		HANDLE_CASE(WM_MOUSEHOVER);
		HANDLE_CASE(WM_MOUSELEAVE);
		HANDLE_CASE(WM_MOUSEWHEEL);
		HANDLE_CASE(WM_MOVE);
		HANDLE_CASE(WM_MOVING);
		HANDLE_CASE(WM_NCACTIVATE);
		HANDLE_CASE(WM_NCCALCSIZE);
		HANDLE_CASE(WM_NCCREATE);
		HANDLE_CASE(WM_NCDESTROY);
		HANDLE_CASE(WM_NCHITTEST);
		HANDLE_CASE(WM_NCLBUTTONDBLCLK);
		HANDLE_CASE(WM_NCLBUTTONDOWN);
		HANDLE_CASE(WM_NCLBUTTONUP);
		HANDLE_CASE(WM_NCMBUTTONDBLCLK);
		HANDLE_CASE(WM_NCMBUTTONDOWN);
		HANDLE_CASE(WM_NCMBUTTONUP);
		HANDLE_CASE(WM_NCMOUSEHOVER);
		HANDLE_CASE(WM_NCMOUSELEAVE);
		HANDLE_CASE(WM_NCMOUSEMOVE);
		HANDLE_CASE(WM_NCPAINT);
		HANDLE_CASE(WM_NCRBUTTONDBLCLK);
		HANDLE_CASE(WM_NCRBUTTONDOWN);
		HANDLE_CASE(WM_NCRBUTTONUP);
		HANDLE_CASE(WM_NCXBUTTONDBLCLK);
		HANDLE_CASE(WM_NCXBUTTONDOWN);
		HANDLE_CASE(WM_NCXBUTTONUP);
		HANDLE_CASE(WM_NEXTDLGCTL);
		HANDLE_CASE(WM_NEXTMENU);
		HANDLE_CASE(WM_NOTIFY);
		HANDLE_CASE(WM_NOTIFYFORMAT);
		HANDLE_CASE(WM_NULL);
		HANDLE_CASE(WM_PAINT);
		HANDLE_CASE(WM_PAINTCLIPBOARD);
		HANDLE_CASE(WM_PAINTICON);
		HANDLE_CASE(WM_PALETTECHANGED);
		HANDLE_CASE(WM_PALETTEISCHANGING);
		HANDLE_CASE(WM_PARENTNOTIFY);
		HANDLE_CASE(WM_PASTE);
		HANDLE_CASE(WM_PENWINFIRST);
		HANDLE_CASE(WM_PENWINLAST);
		HANDLE_CASE(WM_POWER);
		HANDLE_CASE(WM_POWERBROADCAST);
		HANDLE_CASE(WM_PRINT);
		HANDLE_CASE(WM_PRINTCLIENT);
		HANDLE_CASE(WM_QUERYDRAGICON);
		HANDLE_CASE(WM_QUERYENDSESSION);
		HANDLE_CASE(WM_QUERYNEWPALETTE);
		HANDLE_CASE(WM_QUERYOPEN);
		HANDLE_CASE(WM_QUERYUISTATE);
		HANDLE_CASE(WM_QUEUESYNC);
		HANDLE_CASE(WM_QUIT);
		HANDLE_CASE(WM_RBUTTONDBLCLK);
		HANDLE_CASE(WM_RBUTTONDOWN);
		HANDLE_CASE(WM_RBUTTONUP);
		HANDLE_CASE(WM_RENDERALLFORMATS);
		HANDLE_CASE(WM_RENDERFORMAT);
		HANDLE_CASE(WM_SETCURSOR);
		HANDLE_CASE(WM_SETFOCUS);
		HANDLE_CASE(WM_SETFONT);
		HANDLE_CASE(WM_SETHOTKEY);
		HANDLE_CASE(WM_SETICON);
		HANDLE_CASE(WM_SETREDRAW);
		HANDLE_CASE(WM_SETTEXT);
		HANDLE_CASE(WM_SETTINGCHANGE);
		HANDLE_CASE(WM_SHOWWINDOW);
		HANDLE_CASE(WM_SIZE);
		HANDLE_CASE(WM_SIZECLIPBOARD);
		HANDLE_CASE(WM_SIZING);
		HANDLE_CASE(WM_SPOOLERSTATUS);
		HANDLE_CASE(WM_STYLECHANGED);
		HANDLE_CASE(WM_STYLECHANGING);
		HANDLE_CASE(WM_SYNCPAINT);
		HANDLE_CASE(WM_SYSCHAR);
		HANDLE_CASE(WM_SYSCOLORCHANGE);
		HANDLE_CASE(WM_SYSCOMMAND);
		HANDLE_CASE(WM_SYSDEADCHAR);
		HANDLE_CASE(WM_SYSKEYDOWN);
		HANDLE_CASE(WM_SYSKEYUP);
		HANDLE_CASE(WM_TABLET_FIRST);
		HANDLE_CASE(WM_TABLET_LAST);
		HANDLE_CASE(WM_TCARD);
		HANDLE_CASE(WM_THEMECHANGED);
		HANDLE_CASE(WM_TIMECHANGE);
		HANDLE_CASE(WM_TIMER);
		HANDLE_CASE(WM_UNDO);
		HANDLE_CASE(WM_UNINITMENUPOPUP);
		HANDLE_CASE(WM_UPDATEUISTATE);
		HANDLE_CASE(WM_USER);
		HANDLE_CASE(WM_USERCHANGED);
		HANDLE_CASE(WM_VKEYTOITEM);
		HANDLE_CASE(WM_VSCROLL);
		HANDLE_CASE(WM_VSCROLLCLIPBOARD);
		HANDLE_CASE(WM_WINDOWPOSCHANGED);
		HANDLE_CASE(WM_WINDOWPOSCHANGING);
		HANDLE_CASE(WM_WTSSESSION_CHANGE);
		HANDLE_CASE(WM_XBUTTONDBLCLK);
		HANDLE_CASE(WM_XBUTTONDOWN);
		HANDLE_CASE(WM_XBUTTONUP);

		// duplicate value events
		///HANDLE_CASE(WM_KEYFIRST);
		///HANDLE_CASE(WM_IME_KEYLAST);
		///HANDLE_CASE(WM_MOUSEMOVE);
		///HANDLE_CASE(WM_UNICHAR);
		///HANDLE_CASE(WM_WININICHANGE);
	default: Log::Warning("LogWndMsg not defined for msg=0x%x", uMsg); break;
	}
#undef HANDLE_CASE
#endif
}


