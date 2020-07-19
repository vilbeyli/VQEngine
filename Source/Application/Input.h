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
	enum EMouseButtons
	{	// windows btn codes: https://docs.microsoft.com/en-us/windows/win32/inputdev/wm-mbuttondown
		MOUSE_BUTTON_LEFT   = MK_LBUTTON,
		MOUSE_BUTTON_RIGHT  = MK_RBUTTON,
		MOUSE_BUTTON_MIDDLE = MK_MBUTTON
	};
	// ---------------------------------------------------------------------------------------------
	using KeyState         = char;
	using KeyMapping       = std::unordered_map<std::string_view, KeyCode>;
	using ButtonStateMap_t = std::unordered_map<EMouseButtons, KeyState>;
	// ---------------------------------------------------------------------------------------------

	static void InitRawInputDevices(HWND hwnd);
	static bool ReadRawInput_Mouse(LPARAM lParam, MouseInputEventData* pDataOut);
	
	// ---------------------------------------------------------------------------------------------

	Input();
	Input(Input&& other);
	~Input() = default;

	inline void ToggleInputBypassing() { mbIgnoreInput = !mbIgnoreInput; }
	inline void SetInputBypassing(bool b) { mbIgnoreInput.store(b); };
	inline bool GetInputBypassing() const { return mbIgnoreInput.load(); }

	// update state (key states include mouse buttons)
	void UpdateKeyDown(KeyDownEventData);
	void UpdateKeyUp(KeyCode);
	void UpdateMousePos(long x, long y, short scroll);
	void UpdateMousePos_Raw(int relativeX, int relativeY, short scroll, bool bMouseCaptured);
	void PostUpdate();

	// state check
	bool IsKeyDown(KeyCode) const;
	bool IsKeyDown(const char*) const;
	bool IsKeyDown(const std::string&) const;

	bool IsKeyUp(const char*) const;

	bool IsKeyTriggered(KeyCode) const;
	bool IsKeyTriggered(const char*) const;
	bool IsKeyTriggered(const std::string&) const;

	inline const std::array<float, 2>& GetMouseDelta() const { return mMouseDelta; }
	inline float MouseDeltaX() const { return mMouseDelta[0] && !mbIgnoreInput; };
	inline float MouseDeltaY() const { return mMouseDelta[1] && !mbIgnoreInput; };

	bool IsMouseDown(EMouseButtons) const;
	bool IsMouseUp(EMouseButtons) const;
	bool IsMouseDoubleClick(EMouseButtons) const;
	bool IsMouseTriggered(EMouseButtons) const;
	bool IsMouseScrollUp() const;
	bool IsMouseScrollDown() const;
	bool IsAnyMouseDown() const; // @mbIgnoreInput doesn't affect this


private:
	// state
	std::atomic<bool>                    mbIgnoreInput;

	// keyboard
	std::array<KeyState, NUM_MAX_KEYS>   mKeys;
	std::array<KeyState, NUM_MAX_KEYS>   mKeysPrevious;

	// mouse
	ButtonStateMap_t                     mMouseButtons;
	ButtonStateMap_t                     mMouseButtonsPrevious;
	ButtonStateMap_t                     mMouseButtonDoubleClicks;

	std::array<float, 2>                 mMouseDelta;
	std::array<long, 2>                  mMousePosition;
	short                                mMouseScroll;
};

