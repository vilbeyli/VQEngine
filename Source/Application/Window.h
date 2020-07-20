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

#pragma once

// ----------------------------------------------------------------------------
// Modified version of the Window class defined in HelloD3D12 from AMD
// Source: https://github.com/GPUOpen-LibrariesAndSDKs/HelloD3D12/
// ----------------------------------------------------------------------------

//
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
#include <string>
#include <Windows.h>
#include <memory>


struct IWindow;
class SwapChain;


/**
* Encapsulate a window class.
*
* Calls <c>::RegisterClass()</c> on create, and <c>::UnregisterClass()</c>
* on destruction.
*/
struct WindowClass final
{
public:
	WindowClass(const std::string& name,
		HINSTANCE hInst,
		::WNDPROC procedure = ::DefWindowProc);
	~WindowClass();

	const std::string& GetName() const;

	WindowClass(const WindowClass&) = delete;
	WindowClass& operator= (const WindowClass&) = delete;

private:
	std::string name_;
};

class IWindowOwner
{
public:
	virtual void OnWindowCreate(HWND hwnd_) = 0;
	virtual void OnWindowResize(HWND) = 0;
	virtual void OnToggleFullscreen(HWND) = 0;
	virtual void OnWindowMinimize(HWND hwnd_) = 0;
	virtual void OnWindowFocus(HWND hwnd_) = 0;
	virtual void OnWindowLoseFocus(HWND hwnd_) = 0;
	virtual void OnWindowClose(HWND hwnd_) = 0;
	virtual void OnWindowActivate(HWND hwnd_) = 0;
	virtual void OnWindowDeactivate(HWND hwnd_) = 0;
	
	virtual void OnKeyDown(HWND, WPARAM) = 0;
	virtual void OnKeyUp(HWND, WPARAM) = 0;

	virtual void OnMouseButtonDown(HWND hwnd, WPARAM wParam, bool bIsDoubleClick) = 0;
	virtual void OnMouseButtonUp(HWND, WPARAM) = 0;
	virtual void OnMouseScroll(HWND hwnd, short scrollDirection) = 0;
	virtual void OnMouseMove(HWND hwnd, long x, long y) = 0;
	virtual void OnMouseInput(HWND hwnd, LPARAM lParam) = 0; // Raw Input
};

struct IWindow
{
public:
	IWindow(IWindowOwner* pOwner_) : pOwner(pOwner_) {}
	IWindow() = default;
	IWindow(const IWindow&) = delete;
	IWindow& operator=(const IWindow&) = delete;

	virtual ~IWindow();

	virtual void Show()     = 0;
	virtual void ToggleWindowedFullscreen(SwapChain* pSwapChain = nullptr) = 0;
	virtual void Minimize() = 0;
	virtual void SetMouseCapture(bool bCapture) = 0;
	virtual void Close()    = 0;

	bool IsClosed() const;
	bool IsFullscreen() const;
	bool IsMouseCaptured() const;

	inline int GetWidth() const            { return GetWidthImpl(); }
	inline int GetHeight() const           { return GetHeightImpl(); }
	inline int GetFullscreenWidth() const  { return GetFullscreenWidthImpl(); }
	inline int GetFullscreenHeight() const { return GetFullscreenHeightImpl(); }

	IWindowOwner* pOwner;
private:
	virtual bool IsClosedImpl() const = 0;
	virtual bool IsFullscreenImpl() const = 0;
	virtual bool IsMouseCapturedImpl() const = 0;
	virtual int GetWidthImpl() const = 0;
	virtual int GetHeightImpl() const = 0;
	virtual int GetFullscreenWidthImpl() const = 0;
	virtual int GetFullscreenHeightImpl() const = 0;
};


using pfnWndProc_t = LRESULT(CALLBACK*)(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
struct FWindowDesc;


#define __MUST_BE_CALLED_FROM_WINMAIN_THREAD_ 

class Window : public IWindow
{
public:
	Window(const std::string& title, FWindowDesc& initParams);

	HWND GetHWND() const;

	void __MUST_BE_CALLED_FROM_WINMAIN_THREAD_ Show() override;
	void                                       Minimize() override;
	void                                       ToggleWindowedFullscreen(SwapChain* pSwapChain = nullptr) override;
	void __MUST_BE_CALLED_FROM_WINMAIN_THREAD_ Close() override;
	void __MUST_BE_CALLED_FROM_WINMAIN_THREAD_ SetMouseCapture(bool bCapture) override;

	inline void OnResize(int w, int h) { width_ = w; height_ = h; }
	inline void SetFullscreen(bool b) { isFullscreen_ = b; }

private:
	inline bool IsClosedImpl()  const override { return isClosed_; }
	inline bool IsFullscreenImpl() const override { return isFullscreen_; }
	inline bool IsMouseCapturedImpl() const override { return isMouseCaptured_; }
	inline int  GetWidthImpl()  const override { return width_; }
	inline int  GetHeightImpl() const override { return height_; }
	inline int  GetFullscreenWidthImpl()  const override { return FSwidth_; }
	inline int  GetFullscreenHeightImpl() const override { return FSheight_; }

private:
	std::unique_ptr<WindowClass> windowClass_;
	HWND hwnd_ = 0;
	RECT rect_;
	bool isClosed_ = false;
	int width_ = -1, height_ = -1;
	bool isFullscreen_ = false;
	UINT windowStyle_;
	int FSwidth_ = -1, FSheight_ = -1;
	bool isMouseCaptured_ = false;
};
