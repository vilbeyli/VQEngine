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
{
    // https://docs.microsoft.com/en-us/windows/win32/winmsg/window-styles
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;

    ::RECT rect;
    ::SetRect(&rect, 0, 0, width_, height_);
    ::AdjustWindowRect(&rect, style, FALSE);

    HWND hwnd_parent = NULL;
    UINT FlagWindowStyle = WS_OVERLAPPEDWINDOW;

    windowClass_.reset(new WindowClass("VQWindowClass", initParams.hInst, initParams.pfnWndProc));


    // https://docs.microsoft.com/en-us/windows/win32/learnwin32/creating-a-window
    // Create the main window.
    hwnd_ = CreateWindowEx(NULL,
        windowClass_->GetName().c_str(),
        title.c_str(),
        //style,
        FlagWindowStyle,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left, 
        rect.bottom - rect.top, 
        hwnd_parent,
        NULL,    // we aren't using menus, NULL
        initParams.hInst,   // application handle
        NULL);   // used with multiple windows, NULL

    ::SetWindowLongPtr(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR> (this));

    // Show the window and paint its contents.
    ::ShowWindow(hwnd_, SW_SHOWDEFAULT);
    //::UpdateWindow(hwnd_);
}
void Window::OnClose()
{
    this->isClosed_ = true;
    ::ShowWindow(hwnd_, FALSE);
    ::DestroyWindow(hwnd_);
}
///////////////////////////////////////////////////////////////////////////////
bool IWindow::IsClosed() const
{
    return IsClosedImpl();
}

///////////////////////////////////////////////////////////////////////////////
HWND Window::GetHWND() const
{
    return hwnd_;
}

/////////////////////////////////////////////////////////////////////////
WindowClass::WindowClass(const std::string& name, HINSTANCE hInst, ::WNDPROC procedure)
    : name_(name)
{
    ::WNDCLASSA wc = {};

    // Register the window class for the main window.
    // https://docs.microsoft.com/en-us/windows/win32/winmsg/window-class-styles
    wc.style = CS_VREDRAW | CS_HREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = procedure;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInst;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = name_.c_str();

    ::RegisterClassA(&wc);
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