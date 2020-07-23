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

#include <Windows.h>

#include "Libs/VQUtils/Source/Multithreading.h"

//
// EVENT BASE CLASS
//

//
// As this is a threaded application, we'll need to utilize an enum-based
// messaging system to handle events on different threads (say, render thread)
// than the threads recording the event (main thread). 
//
// Instead of dynamic casting the pointer and check-null, we'll use an enum
// in each event to distinguish events from one another.
//
enum EEventType
{
	// Windows->VQE window events
	WINDOW_RESIZE_EVENT = 0,
	WINDOW_CLOSE_EVENT,
	TOGGLE_FULLSCREEN_EVENT,
	SET_FULLSCREEN_EVENT,
	SET_VSYNC_EVENT,
	
	// Windows->VQE input events
	KEY_DOWN_EVENT,
	KEY_UP_EVENT,
	MOUSE_MOVE_EVENT,
	MOUSE_INPUT_EVENT,
	MOUSE_SCROLL_EVENT,

	// VQE->Windows window events
	MOUSE_CAPTURE_EVENT,
	HANDLE_WINDOW_TRANSITIONS_EVENT,
	SHOW_WINDOW_EVENT,

	NUM_EVENT_TYPES
};

struct IEvent
{
	IEvent(EEventType Type, HWND hwnd_) : mType(Type), hwnd(hwnd_) {}

	EEventType mType = EEventType::NUM_EVENT_TYPES;
	HWND       hwnd = 0;
};


// -------------------------------------------------------------------------------------
struct SetMouseCaptureEvent : public IEvent
{
	SetMouseCaptureEvent(HWND hwnd_, bool bCap, bool bVis) 
		: IEvent(EEventType::MOUSE_CAPTURE_EVENT, hwnd_)
		, bCapture(bCap)
		, bVisible(bVis) 
	{}
	bool bCapture = false;
	bool bVisible = false;
};

struct HandleWindowTransitionsEvent : public IEvent
{
	HandleWindowTransitionsEvent(HWND hwnd_) : IEvent(EEventType::HANDLE_WINDOW_TRANSITIONS_EVENT, hwnd_) {}
};

struct ShowWindowEvent : public IEvent
{
	ShowWindowEvent(HWND hwnd_) : IEvent(EEventType::SHOW_WINDOW_EVENT, hwnd_) {}
};

// -------------------------------------------------------------------------------------

//
// WINDOW EVENTS
//

struct WindowResizeEvent : public IEvent
{
	WindowResizeEvent(int w, int h, HWND hwnd_) : IEvent(EEventType::WINDOW_RESIZE_EVENT, hwnd_), width(w), height(h) {}

	int width  = 0;
	int height = 0; 
};

struct WindowCloseEvent : public IEvent
{
	WindowCloseEvent(HWND hwnd_) : IEvent(EEventType::WINDOW_CLOSE_EVENT, hwnd_){}

	mutable Signal Signal_WindowDependentResourcesDestroyed;
};

struct ToggleFullscreenEvent : public IEvent
{
	ToggleFullscreenEvent(HWND hwnd_) : IEvent(EEventType::TOGGLE_FULLSCREEN_EVENT, hwnd_) {}
};

struct SetFullscreenEvent : public IEvent
{
	SetFullscreenEvent(HWND hwnd_, bool bFullscreen) : IEvent(EEventType::SET_FULLSCREEN_EVENT, hwnd_), bToggleValue(bFullscreen) {}
	bool bToggleValue = false;
};
struct SetVSyncEvent : public IEvent
{
	SetVSyncEvent(HWND hwnd_, bool bVSync) : IEvent(EEventType::SET_VSYNC_EVENT, hwnd_), bToggleValue(bVSync) {}
	bool bToggleValue = false;
};

//
// INPUT EVENTS
//
union KeyDownEventData
{
	struct Keyboard
	{
		Keyboard(WPARAM wp, bool bIsMouse) : wparam(wp), bMouse(bIsMouse) {}
		WPARAM wparam = 0;
		bool bMouse;
	} keyboard;
	struct Mouse
	{
		Mouse(WPARAM wp, bool bIsMouse, bool cl) : wparam(wp), bDoubleClick(cl), bMouse(bIsMouse){}
		WPARAM wparam = 0;
		bool   bDoubleClick = false;
		bool   bMouse;
	} mouse;

	KeyDownEventData(WPARAM wp, bool bIsMouse, bool cl) : mouse(wp, bIsMouse, cl) {}
};
struct KeyDownEvent : public IEvent
{
	KeyDownEventData data;
	KeyDownEvent(HWND hwnd_, WPARAM wparam_, bool bIsMouse,  bool bDoubleClick_ = false)
		: IEvent(EEventType::KEY_DOWN_EVENT, hwnd_)
		, data(wparam_, bIsMouse, bDoubleClick_)
	{}
};

struct KeyUpEvent : public IEvent
{
	KeyUpEvent(HWND hwnd_, WPARAM wparam_, bool bIsMouse) : IEvent(EEventType::KEY_UP_EVENT, hwnd_), wparam(wparam_), bMouseEvent(bIsMouse) {}
	
	bool bMouseEvent = false;
	WPARAM wparam = 0;
};

struct MouseMoveEvent : public IEvent
{
	MouseMoveEvent(HWND hwnd_, long x_, long y_) : IEvent(EEventType::MOUSE_MOVE_EVENT, hwnd_), x(x_), y(y_) {}
	long x = 0;
	long y = 0;
};

struct MouseScrollEvent : public IEvent
{
	MouseScrollEvent(HWND hwnd_, short scr) : IEvent(EEventType::MOUSE_SCROLL_EVENT, hwnd_), scroll(scr) {}
	short scroll = 0;
};


struct MouseInputEventData
{
	int relativeX = 0;
	int relativeY = 0;
	union
	{
		unsigned long scrollChars;
		unsigned long scrollLines;
	};
	float scrollDelta = 0.0f;
};
struct MouseInputEvent : public IEvent
{
	MouseInputEvent(const MouseInputEventData& d, HWND hwnd_) : IEvent(EEventType::MOUSE_INPUT_EVENT, hwnd_), data(d) {}
	MouseInputEventData data;
};