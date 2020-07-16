//	VQEngine | DirectX11 Renderer
//	Copyright(C) 2018  - Volkan Ilbeyli
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

#include "Input.h"

#include <algorithm>

#define VERBOSE_LOGGING 0

// keyboard mapping for windows keycodes.
// use case: this->IsKeyDown("F4")
static const Input::KeyMapping KEY_MAP = []() 
{	
	Input::KeyMapping m;
	m["F1"] = 112;	m["F2"] = 113;	m["F3"] = 114;	m["F4"] = 115;
	m["F5"] = 116;	m["F6"] = 117;	m["F7"] = 118;	m["F8"] = 119;
	m["F9"] = 120;	m["F10"] = 121;	m["F11"] = 122;	m["F12"] = 123;
	
	m["0"] = 48;		m["1"] = 49;	m["2"] = 50;	m["3"] = 51;
	m["4"] = 52;		m["5"] = 53;	m["6"] = 54;	m["7"] = 55;
	m["8"] = 56;		m["9"] = 57;
	
	m["A"] = 65;		m["a"] = 65;	m["B"] = 66;	m["b"] = 66;
	m["C"] = 67;		m["c"] = 67;	m["N"] = 78;	m["n"] = 78;
	m["R"] = 82;		m["r"] = 82;	m["T"] = 'T';	m["t"] = 'T';
	m["F"] = 'F';		m["f"] = 'F';	m["J"] = 'J';	m["j"] = 'J';
	m["K"] = 'K';		m["k"] = 'K';


	m["\\"] = 220;		m[";"] = 186;
	m["'"] = 222;
	m["Shift"] = 16;	m["shift"] = 16;
	m["Enter"] = 13;	m["enter"] = 13;
	m["Backspace"] = 8; m["backspace"] = 8;
	m["Escape"] = 0x1B; m["escape"] = 0x1B; m["ESC"] = 0x1B; m["esc"] = 0x1B;
	m["PageUp"] = 33;	m["PageDown"] = 34;
	m["Space"] = VK_SPACE; m["space"] = VK_SPACE;

	m["ctrl"] = VK_CONTROL;  m["Ctrl"] = VK_CONTROL;
	m["rctrl"] = VK_RCONTROL; m["RCtrl"] = VK_RCONTROL; m["rCtrl"] = VK_RCONTROL;
	m["alt"] = VK_MENU;	m["Alt"] = VK_MENU;


	m["Numpad7"] = 103;		m["Numpad8"] = 104;			m["Numpad9"] = 105;
	m["Numpad4"] = 100;		m["Numpad5"] = 101;			m["Numpad6"] = 102;
	m["Numpad1"] = 97 ;		m["Numpad2"] = 98 ;			m["Numpad3"] = 99;
	m["Numpad+"] = VK_ADD;	m["Numpad-"] = VK_SUBTRACT;	
	m["+"]		 = VK_ADD;	m["-"]		 = VK_SUBTRACT;	
	return std::move(m);
}();


Input::Input()
	:
	mbIgnoreInput(false),
	mMouseDelta{0, 0},
	mMousePosition{0, 0},
	mMouseScroll(0)
{
	mMouseButtons[EMouseButtons::MOUSE_BUTTON_LEFT]   = 0;
	mMouseButtons[EMouseButtons::MOUSE_BUTTON_RIGHT]  = 0;
	mMouseButtons[EMouseButtons::MOUSE_BUTTON_MIDDLE] = 0;
}


void Input::UpdateKeyDown(KeyCode key)
{
	mKeys[key] = true;
}


void Input::UpdateKeyUp(KeyCode key)
{
	mKeys[key] = false;
}

void Input::UpdateButtonDown(EMouseButtons btn)
{
	mMouseButtons[btn] = true;
}

void Input::UpdateButtonUp(EMouseButtons btn)
{

	mMouseButtons[btn] = false;
}

void Input::UpdateMousePos(long x, long y, short scroll)
{
#ifdef ENABLE_RAW_INPUT
	mMouseDelta[0] = static_cast<float>(x);
	mMouseDelta[1] = static_cast<float>(y);

	// unused for now
	mMousePosition[0] = 0;
	mMousePosition[1] = 0;
#else
	mMouseDelta[0] = max(-1, min(x - mMousePosition[0], 1));
	mMouseDelta[1] = max(-1, min(y - mMousePosition[1], 1));

	mMousePosition[0] = x;
	mMousePosition[1] = y;
#endif

#if defined(_DEBUG) && VERBOSE_LOGGING
	Log::Info("Mouse Delta: (%d, %d)\tMouse Position: (%d, %d)\tMouse Scroll: (%d)", 
		mMouseDelta[0], mMouseDelta[1],
		mMousePosition[0], mMousePosition[1],
		(int)scroll);
#endif
	mMouseScroll = scroll;
}

bool Input::IsScrollUp() const
{
	return mMouseScroll > 0 && !mbIgnoreInput;
}

bool Input::IsScrollDown() const
{
	return mMouseScroll < 0 && !mbIgnoreInput;
}

bool Input::IsKeyDown(KeyCode key) const
{
	return mKeys[key] && !mbIgnoreInput;
}

bool Input::IsKeyDown(const char * key) const
{
	const KeyCode code = KEY_MAP.at(key);
	return mKeys[code] && !mbIgnoreInput;
}

bool Input::IsKeyUp(const char * key) const
{
	const KeyCode code = KEY_MAP.at(key);
	return (!mKeys[code] && mKeysPrevious[code]) && !mbIgnoreInput;
}

bool Input::IsKeyDown(const std::string& key) const
{
	const KeyCode code = KEY_MAP.at(key.c_str());
	return mKeys[code] && !mbIgnoreInput;
}

#if 0
bool Input::IsMouseDown(KeyCode btn) const
{
	return mMouseButtons[btn] && !mbIgnoreInput;
}
#endif

bool Input::IsKeyTriggered(KeyCode key) const
{
	return !mKeysPrevious[key] && mKeys[key] && !mbIgnoreInput;
}

bool Input::IsKeyTriggered(const char *key) const
{
	const KeyCode code = KEY_MAP.at(key);
	return !mKeysPrevious[code] && mKeys[code] && !mbIgnoreInput;
}

bool Input::IsKeyTriggered(const std::string & key) const
{
	const KeyCode code = KEY_MAP.at(key.data());
	return !mKeysPrevious[code] && mKeys[code] && !mbIgnoreInput;
}

int Input::MouseDeltaX() const
{
	return !mbIgnoreInput ? mMouseDelta[0] : 0;
}

int Input::MouseDeltaY() const
{
	return !mbIgnoreInput ? mMouseDelta[1] : 0;
}

// called at the end of the frame
#include <algorithm>
void Input::PostUpdate()
{
	mKeysPrevious = mKeys;
	mMouseDelta[0] = mMouseDelta[1] = 0;
	mMouseScroll = 0;
	mMouseButtons[EMouseButtons::MOUSE_BUTTON_LEFT] = 0;
	mMouseButtons[EMouseButtons::MOUSE_BUTTON_RIGHT] = 0;
	mMouseButtons[EMouseButtons::MOUSE_BUTTON_MIDDLE] = 0;
}
