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

#include "Events.h"

#include <array>
#include <unordered_map>
#include <string>
#include <atomic>

#define ENABLE_RAW_INPUT 1

// A char[] is used for key mappings, large enough array will do
#define NUM_MAX_KEYS 256

#include "Window.h"
using KeyCode = WPARAM;

class Input
{
public:
	using KeyMapping = std::unordered_map<std::string_view, KeyCode>;
	enum EMouseButtons
	{	// windows btn codes: https://docs.microsoft.com/en-us/windows/win32/inputdev/wm-mbuttondown
		MOUSE_BUTTON_LEFT   = MK_LBUTTON,
		MOUSE_BUTTON_RIGHT  = MK_RBUTTON,
		MOUSE_BUTTON_MIDDLE = MK_MBUTTON
	};

	static void InitRawInputDevices(HWND hwnd);
	static bool ReadRawInput_Mouse(LPARAM lParam, MouseInputEventData* pData);

	Input();
	Input(Input&& other);
	~Input() = default;

	inline void ToggleInputBypassing() { mbIgnoreInput = !mbIgnoreInput; }
	inline void SetInputBypassing(bool b) { mbIgnoreInput.store(b); };
	inline bool GetInputBypassing() const { return mbIgnoreInput.load(); }

	// update state
	void UpdateKeyDown(KeyDownEventData); // includes mouse button
	void UpdateKeyUp(KeyCode);	 // includes mouse button
	void UpdateMousePos(long x, long y, short scroll);
	void UpdateMousePos_Raw(int relativeX, int relativeY, short scroll, bool bMouseCaptured);

	bool IsKeyDown(KeyCode) const;
	bool IsKeyDown(const char*) const;
	bool IsKeyDown(const std::string&) const;

	bool IsKeyUp(const char*) const;
	bool IsKeyTriggered(KeyCode) const;
	bool IsKeyTriggered(const char*) const;
	bool IsKeyTriggered(const std::string&) const;

	int  MouseDeltaX() const;
	int  MouseDeltaY() const;
	bool IsMouseDown(EMouseButtons) const;
	bool IsMouseUp(EMouseButtons) const;
	bool IsMouseDoubleClick(EMouseButtons) const;
	bool IsMouseTriggered(EMouseButtons) const;
	bool IsMouseScrollUp() const;
	bool IsMouseScrollDown() const;
	bool IsAnyMouseDown() const;

	void PostUpdate();
	inline const std::array<float, 2>& GetMouseDelta() const { return mMouseDelta; }


private: // On/Off state is represented as char (8-bit) instead of bool (32-bit)
	// state
	std::atomic<bool> mbIgnoreInput;

	// keyboard
#if 1
	std::array<char, NUM_MAX_KEYS> mKeys;
	std::array<char, NUM_MAX_KEYS> mKeysPrevious;
#else
	// how do we populate KeyCodes during initialization?
	// is it better to simply use an array?
	std::unordered_map<KeyCode, char> mKeys;
	std::unordered_map<KeyCode, char> mKeysPrevious;
#endif

	// mouse
	std::unordered_map<EMouseButtons, char> mMouseButtons;
	std::unordered_map<EMouseButtons, char> mMouseButtonsPrevious;
	std::unordered_map<EMouseButtons, char> mMouseButtonDoubleClicks;
	std::array<float, 2>                    mMouseDelta;
	std::array<long, 2>                     mMousePosition;
	short                                   mMouseScroll;


// SOME TEMPLATE FUN HERE --------------------------------------------------------------------
public:
	template<class Args> inline bool are_all_true(int argc, Args...)
	{
		bool bAreAllDown = true;
		va_list args;	// use this unpack function variadic parameters
		va_start(args, argc);
		for (int i = 0; i < argc; ++i)
		{
			bAreAllDown &= va_arg(args, bool);
		}
		va_end(args);
		return bAreAllDown;
	}
	template<class... Args> bool AreKeysDown(int keyCount, Args&&... args)
	{ 	//-------------------------------------------------------------------------------------
		// Note:
		//
		// We want to feed each argument to IsKeyDown() which is not a tmeplate function.
		//
		// IsKeyDown(args)...; -> we can't do this to expand and feed the args to the function. 
		//
		// but if we enclose it in a template function like this: 
		//     f(argc, IsKeyDown(args)...)
		//
		// we can expand the arguments one by one into the IsKeyDown() function.
		// now if the function f is a template function, all its arguments will be bools
		// as the return type of IsKeyDown() is bool. We can simply chain them together to
		// get the final result. f() in this case is 'are_all_true()'.
		//-------------------------------------------------------------------------------------
		assert(false);	// there's a bug. doesn't work in release mode. don't use this for now.
		return are_all_true(keyCount, IsKeyDown(args)...);
	}
};

