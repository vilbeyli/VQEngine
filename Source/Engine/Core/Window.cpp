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


// ----------------------------------------------------------------------------
// Modified version of the Window class defined in HelloD3D12 from AMD
// Source: https://github.com/GPUOpen-LibrariesAndSDKs/HelloD3D12/
// Adding multi-monitor support, letting the user specify which monitor
// the window can be created in.
// ----------------------------------------------------------------------------

// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
#include "Window.h"
#include "../VQEngine.h"
#include "Renderer/Core/SwapChain.h"
#include "Libs/VQUtils/Include/Log.h"
#include "Libs/VQUtils/Include/utils.h"
#include "Data/Resources/resource.h"

#include <dxgi1_6.h>
#include <cstring>

#define VERBOSE_LOGGING 0

static RECT CenterScreen(const RECT& screenRect, const RECT& wndRect)
{
    RECT centered = {};

    const int szWndX = wndRect.right - wndRect.left;
    const int szWndY = wndRect.bottom - wndRect.top;
    const int offsetX = (screenRect.right - screenRect.left - szWndX) / 2;
    const int offsetY = (screenRect.bottom - screenRect.top - szWndY) / 2;

    centered.left = screenRect.left + offsetX;
    centered.right = centered.left + szWndX;
    centered.top = screenRect.top + offsetY;
    centered.bottom = centered.top + szWndY;

    return centered;
}

static RECT GetScreenRectOnPreferredDisplay(const RECT& preferredRect, int PreferredDisplayIndex, bool* pbMonitorFound)
{
    // handle preferred display
    struct MonitorEnumCallbackParams
    {
        int PreferredMonitorIndex = 0;
        const RECT* pRectOriginal = nullptr;
        RECT* pRectNew = nullptr;
        RECT RectDefault;
    };
    RECT preferredScreenRect = { CW_USEDEFAULT , CW_USEDEFAULT , CW_USEDEFAULT , CW_USEDEFAULT };
    MonitorEnumCallbackParams p = {};
    p.PreferredMonitorIndex = PreferredDisplayIndex;
    p.pRectOriginal = &preferredRect;
    p.pRectNew = &preferredScreenRect;

    auto fnCallbackMonitorEnum = [](HMONITOR Arg1, HDC Arg2, LPRECT Arg3, LPARAM Arg4) -> BOOL
    {
        BOOL b = TRUE;
        MonitorEnumCallbackParams* pParam = (MonitorEnumCallbackParams*)Arg4;

        MONITORINFOEX monitorInfo = {};
        monitorInfo.cbSize = sizeof(MONITORINFOEX);
        GetMonitorInfo(Arg1, &monitorInfo);

        // get monitor index from monitor name
        std::string monitorName(monitorInfo.szDevice); // monitorName is usually something like "///./DISPLAY1"
        monitorName = StrUtil::split(monitorName, { '/', '\\', '.' })[0];         
        std::string strMonitorIndex = monitorName.substr(monitorName.size() - 1); // strMonitorIndex is "1" for "///./DISPLAY1"
        const int monitorIndex = std::atoi(strMonitorIndex.c_str()) - 1;          // monitorIndex    is  0  for "///./DISPLAY1"

        // copy over the desired monitor's rect
        if (monitorIndex == pParam->PreferredMonitorIndex)
        {
            *pParam->pRectNew = *Arg3;
        }
        if (monitorIndex == 0)
        {
            pParam->RectDefault = *Arg3;
        }
        return b;
    };

    EnumDisplayMonitors(NULL, NULL, fnCallbackMonitorEnum, (LPARAM)&p);
    const bool bPreferredDisplayNotFound =
        (preferredScreenRect.right == preferredScreenRect.left 
        && preferredScreenRect.left  == preferredScreenRect.top
        && preferredScreenRect.top == preferredScreenRect.bottom)
        && (preferredScreenRect.right == CW_USEDEFAULT);
    
    *pbMonitorFound = !bPreferredDisplayNotFound;

    return bPreferredDisplayNotFound ? p.RectDefault : CenterScreen(preferredScreenRect, preferredRect);
}



///////////////////////////////////////////////////////////////////////////////
IWindow::~IWindow()
{
}

///////////////////////////////////////////////////////////////////////////////
Window::Window(const char* title, FWindowDesc& initParams)
    : IWindow(initParams.pWndOwner)
    , width_(initParams.width)
    , height_(initParams.height)
    , isFullscreen_(initParams.bFullscreen)
{
    // https://docs.microsoft.com/en-us/windows/win32/winmsg/window-styles
    UINT FlagWindowStyle = WS_OVERLAPPEDWINDOW;

    ::RECT rect;
    ::SetRect(&rect, 0, 0, width_, height_);
    ::AdjustWindowRect(&rect, FlagWindowStyle, FALSE);

    HWND hwnd_parent = NULL;

    windowClass_.reset(new WindowClass("VQWindowClass", initParams.hInst, initParams.pfnWndProc));

    bool bPreferredDisplayFound = false;
    RECT preferredScreenRect = GetScreenRectOnPreferredDisplay(rect, initParams.preferredDisplay, &bPreferredDisplayFound);


    // set fullscreen width & height based on the selected monitor
    this->FSwidth_  = preferredScreenRect.right  - preferredScreenRect.left;
    this->FSheight_ = preferredScreenRect.bottom - preferredScreenRect.top;

    // https://docs.microsoft.com/en-us/windows/win32/learnwin32/creating-a-window
    // Create the main window.
    hwnd_ = CreateWindowEx(NULL,
        windowClass_->GetName(),
        title,
        FlagWindowStyle,
        bPreferredDisplayFound ? preferredScreenRect.left : CW_USEDEFAULT, // positions
        bPreferredDisplayFound ? preferredScreenRect.top  : CW_USEDEFAULT, // positions
        rect.right - rect.left, // size
        rect.bottom - rect.top, // size
        hwnd_parent,
        NULL,    // we aren't using menus, NULL
        initParams.hInst,   // application handle
        NULL);   // used with multiple windows, NULL

    if (initParams.pRegistrar && initParams.pfnRegisterWindowName)
    {
        (initParams.pRegistrar->*initParams.pfnRegisterWindowName)(hwnd_, initParams.windowName);
    }

    windowStyle_ = FlagWindowStyle;
    
    // TODO: initial Show() sets the resolution low for the first frame.
    //       Workaround: RenderThread() calls HandleEvents() right before looping to handle the first ShowWindow() before Present().
    ::ShowWindow(hwnd_, initParams.iShowCmd);

    // set the data after the window shows up the first time.
    // otherwise, the Focus event will be sent on ::ShowWindow() before 
    // this function returns, causing issues dereferencing the smart
    // pointer calling this ctor.
    ::SetWindowLongPtr(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR> (this));

    if (!bPreferredDisplayFound)
    {
        Log::Warning("Window(): Couldn't find the preferred display: %d", initParams.preferredDisplay);
    }
}

///////////////////////////////////////////////////////////////////////////////
bool IWindow::IsClosed()     const { return IsClosedImpl(); }
bool IWindow::IsFullscreen() const { return IsFullscreenImpl(); }
bool IWindow::IsMouseCaptured() const { return IsMouseCapturedImpl(); }

///////////////////////////////////////////////////////////////////////////////
HWND Window::GetHWND() const
{
    return hwnd_;
}

void Window::Show()
{
    ::ShowWindow(hwnd_, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd_);
}

void Window::Minimize()
{

    ::ShowWindow(hwnd_, SW_MINIMIZE);
}

void Window::Close()
{
    Log::Info("Window: Closing<%x>", this->hwnd_);
    this->isClosed_ = true;
    ::ShowWindow(hwnd_, FALSE);
    ::DestroyWindow(hwnd_);
}

// from MS D3D12Fullscreen sample
void Window::ToggleWindowedFullscreen(SwapChain* pSwapChain /*= nullptr*/)
{
    if (isFullscreen_)
    {
        // Restore the window's attributes and size.
        SetWindowLong(hwnd_, GWL_STYLE, windowStyle_);

        SetWindowPos(
            hwnd_,
            HWND_NOTOPMOST,
            rect_.left,
            rect_.top,
            rect_.right - rect_.left,
            rect_.bottom - rect_.top,
            SWP_FRAMECHANGED | SWP_NOACTIVATE);

        ShowWindow(hwnd_, SW_NORMAL);
    }
    else
    {
        // Save the old window rect so we can restore it when exiting fullscreen mode.
        GetWindowRect(hwnd_, &rect_);

        // Make the window borderless so that the client area can fill the screen.
        SetWindowLong(hwnd_, GWL_STYLE, windowStyle_ & ~(WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME));

        RECT fullscreenWindowRect;
        
        if (pSwapChain)
        {
            // Get the settings of the display on which the app's window is currently displayed
            IDXGIOutput* pOutput = nullptr;
            pSwapChain->mpSwapChain->GetContainingOutput(&pOutput);
            DXGI_OUTPUT_DESC Desc;
            pOutput->GetDesc(&Desc);
            fullscreenWindowRect = Desc.DesktopCoordinates;
            pOutput->Release();
        }
        else
        {
            // Fallback to EnumDisplaySettings implementation
            // Get the settings of the primary display
            DEVMODE devMode = {};
            devMode.dmSize = sizeof(DEVMODE);
            EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &devMode);

            fullscreenWindowRect = {
                devMode.dmPosition.x,
                devMode.dmPosition.y,
                devMode.dmPosition.x + static_cast<LONG>(devMode.dmPelsWidth),
                devMode.dmPosition.y + static_cast<LONG>(devMode.dmPelsHeight)
            };
        }

        SetWindowPos(
            hwnd_,
            HWND_TOPMOST,
            fullscreenWindowRect.left,
            fullscreenWindowRect.top,
            fullscreenWindowRect.right,
            fullscreenWindowRect.bottom,
            SWP_FRAMECHANGED | SWP_NOACTIVATE);

        ShowWindow(hwnd_, SW_MAXIMIZE);

        // save fullscreen width & height 
        this->FSwidth_  = fullscreenWindowRect.right  - fullscreenWindowRect.left;
        this->FSheight_ = fullscreenWindowRect.bottom - fullscreenWindowRect.top;
    }

    isFullscreen_ = !isFullscreen_;
}

void Window::SetMouseCapture(bool bCapture)
{
#if VERBOSE_LOGGING
    Log::Warning("Capture Mouse: %d", bCapture);
#endif

    isMouseCaptured_ = bCapture;
    if (bCapture)
    {
        RECT rcClip;
        GetWindowRect(hwnd_, &rcClip);

        // keep clip cursor rect inside pixel area
        // TODO: properly do it with rects
        constexpr int PX_OFFSET = 15;
        constexpr int PX_WND_TITLE_OFFSET = 30;
        rcClip.left   += PX_OFFSET;
        rcClip.right  -= PX_OFFSET;
        rcClip.top    += PX_OFFSET + PX_WND_TITLE_OFFSET;
        rcClip.bottom -= PX_OFFSET;

        // https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showcursor
        int hr = ShowCursor(FALSE);
        while (hr >= 0) hr = ShowCursor(FALSE);
        switch (hr)
        {
#if VERBOSE_LOGGING
        case -1: Log::Warning("ShowCursor(FALSE): No mouse is installed!"); break;
#endif
        case 0: break;
        //default: Log::Info("ShowCursor(FALSE): %d", hr); break;
        }

        ClipCursor(&rcClip);
        SetForegroundWindow(hwnd_);
        SetFocus(hwnd_);
    }
    else
    {
        ClipCursor(nullptr);
        while (ShowCursor(TRUE) <= 0);
        SetForegroundWindow(NULL);
        // SetFocus(NULL);	// we still want to register events
    }

}

/////////////////////////////////////////////////////////////////////////
WindowClass::WindowClass(const char* name, HINSTANCE hInst, ::WNDPROC procedure)
{
    strncpy_s(name_, name, sizeof(name_));

    ::WNDCLASSEX wc = {};

    // Register the window class for the main window.
    // https://docs.microsoft.com/en-us/windows/win32/winmsg/window-class-styles
    wc.style = CS_VREDRAW | CS_HREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = procedure;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInst;
    wc.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_ICON1));
    if (wc.hIcon == NULL)
    {
        DWORD dw = GetLastError();
        Log::Warning("Couldn't load icon for window: 0x%x", dw);
    }
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = name_;
    wc.cbSize = sizeof(WNDCLASSEX);

    ::RegisterClassEx(&wc);
}

/////////////////////////////////////////////////////////////////////////
WindowClass::~WindowClass()
{
    ::UnregisterClassA(name_, (HINSTANCE)::GetModuleHandle(NULL));
}