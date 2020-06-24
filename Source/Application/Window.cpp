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
#include "../Renderer/SwapChain.h"
#include "Libs/VQUtils/Source/Log.h"
#include "Libs/VQUtils/Source/utils.h"
#include "Data/Resources/resource.h"

#include <dxgi1_6.h>

///////////////////////////////////////////////////////////////////////////////
IWindow::~IWindow()
{
}

///////////////////////////////////////////////////////////////////////////////
int IWindow::GetWidth() const
{
    return GetWidthImpl();
}

///////////////////////////////////////////////////////////////////////////////
int IWindow::GetHeight() const
{
    return GetHeightImpl();
}

///////////////////////////////////////////////////////////////////////////////
Window::Window(const std::string& title, FWindowDesc& initParams)
    : IWindow(initParams.pWndOwner)
    , width_(initParams.width)
    , height_(initParams.height)
    , isFullscreen_(initParams.bFullscreen)
{
    // https://docs.microsoft.com/en-us/windows/win32/winmsg/window-styles
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;

    ::RECT rect;
    ::SetRect(&rect, 0, 0, width_, height_);
    ::AdjustWindowRect(&rect, style, FALSE);

    HWND hwnd_parent = NULL;
    UINT FlagWindowStyle = WS_OVERLAPPEDWINDOW;

    windowClass_.reset(new WindowClass("VQWindowClass", initParams.hInst, initParams.pfnWndProc));

    // handle preferred display
    struct MonitorEnumCallbackParams
    {
        int PreferredMonitorIndex = 0;
        RECT* pRectOriginal = nullptr;
        RECT* pRectNew = nullptr;
    };
    RECT preferredScreenRect = { CW_USEDEFAULT , CW_USEDEFAULT , CW_USEDEFAULT , CW_USEDEFAULT };
    MonitorEnumCallbackParams p = {};
    p.PreferredMonitorIndex = initParams.preferredDisplay;
    p.pRectOriginal = &rect;
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
        monitorName = StrUtil::split(monitorName, {'/', '\\', '.'})[0];         // strMonitorIndex is "1" for "///./DISPLAY1"
        std::string strMonitorIndex = monitorName.substr(monitorName.size()-1); // monitorIndex    is  0  for "///./DISPLAY1"
        const int monitorIndex = std::atoi(strMonitorIndex.c_str()) - 1;        // -1 so it starts from 0
        
        // copy over the desired monitor's rect
        if (monitorIndex == pParam->PreferredMonitorIndex)
        {
            *pParam->pRectNew = *Arg3;
        }
        return b;
    };
    auto fnCenterScreen = [](const RECT& screenRect, const RECT& wndRect) -> RECT
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
    };

    EnumDisplayMonitors(NULL, NULL, fnCallbackMonitorEnum, (LPARAM)&p);
    const bool bPreferredDisplayNotFound = 
           (preferredScreenRect.right == preferredScreenRect.left == preferredScreenRect.top == preferredScreenRect.bottom )
        && (preferredScreenRect.right == CW_USEDEFAULT);
    RECT centeredRect = bPreferredDisplayNotFound
        ? preferredScreenRect
        : fnCenterScreen(preferredScreenRect, rect);

    // https://docs.microsoft.com/en-us/windows/win32/learnwin32/creating-a-window
    // Create the main window.
    hwnd_ = CreateWindowEx(NULL,
        windowClass_->GetName().c_str(),
        title.c_str(),
        FlagWindowStyle,
        centeredRect.left,      // positions //CW_USEDEFAULT,
        centeredRect.top ,      // positions //CW_USEDEFAULT,
        rect.right - rect.left, // size
        rect.bottom - rect.top, // size
        hwnd_parent,
        NULL,    // we aren't using menus, NULL
        initParams.hInst,   // application handle
        NULL);   // used with multiple windows, NULL

    windowStyle_ = FlagWindowStyle;
    ::SetWindowLongPtr(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR> (this));
}

///////////////////////////////////////////////////////////////////////////////
bool IWindow::IsClosed() const
{
    return IsClosedImpl();
}

bool IWindow::IsFullscreen() const
{
    return IsFullscreenImpl();
}

///////////////////////////////////////////////////////////////////////////////
HWND Window::GetHWND() const
{
    return hwnd_;
}

void Window::Show()
{
    ::ShowWindow(hwnd_, SW_SHOWDEFAULT);
    //::UpdateWindow(hwnd_);
}

void Window::Minimize()
{

    ::ShowWindow(hwnd_, SW_MINIMIZE);
}

void Window::Close()
{
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
        SetWindowLong(hwnd_, GWL_STYLE, windowStyle_ & ~(WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU | WS_THICKFRAME));

        RECT fullscreenWindowRect;
        
        if (pSwapChain)
        {
            // Get the settings of the display on which the app's window is currently displayed
            IDXGIOutput* pOutput = nullptr;
            pSwapChain->mpSwapChain->GetContainingOutput(&pOutput);
            DXGI_OUTPUT_DESC Desc;
            pOutput->GetDesc(&Desc);
            fullscreenWindowRect = Desc.DesktopCoordinates;
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
    }

    isFullscreen_ = !isFullscreen_;
}

/////////////////////////////////////////////////////////////////////////
WindowClass::WindowClass(const std::string& name, HINSTANCE hInst, ::WNDPROC procedure)
    : name_(name)
{
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
    wc.lpszClassName = name_.c_str();
    wc.cbSize = sizeof(WNDCLASSEX);

    ::RegisterClassEx(&wc);
}

/////////////////////////////////////////////////////////////////////////
const std::string& WindowClass::GetName() const
{
    return name_;
}

/////////////////////////////////////////////////////////////////////////
WindowClass::~WindowClass()
{
    ::UnregisterClassA(name_.c_str(), (HINSTANCE)::GetModuleHandle(NULL));
}