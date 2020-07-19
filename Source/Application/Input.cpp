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
#define NOMINMAX

#include "Input.h"

#include "Libs/VQUtils/Source/Log.h"
#include "Libs/VQUtils/Source/Utils.h"

#include <algorithm>
#include <cassert>

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
	m["Escape"] = 0x1B; m["escape"] = 0x1B; m["ESC"] = 0x1B; m["esc"] = 0x1B; m["Esc"] = 0x1B;
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

static constexpr bool IsMouseKey(WPARAM wparam)
{
	return wparam == Input::EMouseButtons::MOUSE_BUTTON_LEFT
		|| wparam == Input::EMouseButtons::MOUSE_BUTTON_RIGHT
		|| wparam == Input::EMouseButtons::MOUSE_BUTTON_MIDDLE
		|| wparam == (Input::EMouseButtons::MOUSE_BUTTON_LEFT | Input::EMouseButtons::MOUSE_BUTTON_RIGHT)
		|| wparam == (Input::EMouseButtons::MOUSE_BUTTON_MIDDLE | Input::EMouseButtons::MOUSE_BUTTON_RIGHT)
		|| wparam == (Input::EMouseButtons::MOUSE_BUTTON_MIDDLE | Input::EMouseButtons::MOUSE_BUTTON_LEFT)
		|| wparam == (Input::EMouseButtons::MOUSE_BUTTON_MIDDLE | Input::EMouseButtons::MOUSE_BUTTON_LEFT | Input::EMouseButtons::MOUSE_BUTTON_RIGHT);
}


bool Input::ReadRawInput_Mouse(LPARAM lParam, MouseInputEventData* pData)
{
	constexpr UINT RAW_INPUT_SIZE_IN_BYTES = 48;

	UINT rawInputSize = RAW_INPUT_SIZE_IN_BYTES;
	LPBYTE inputBuffer[RAW_INPUT_SIZE_IN_BYTES];
	ZeroMemory(inputBuffer, RAW_INPUT_SIZE_IN_BYTES);

	// https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getrawinputdata
	GetRawInputData(
		(HRAWINPUT)lParam,
		RID_INPUT,
		inputBuffer,
		&rawInputSize,
		sizeof(RAWINPUTHEADER));

	// https://docs.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawmouse
	RAWINPUT* raw = (RAWINPUT*)inputBuffer;    assert(raw);
	RAWMOUSE rawMouse = raw->data.mouse;
	bool bIsMouseInput = false;

	// Handle Wheel
	if ((rawMouse.usButtonFlags & RI_MOUSE_WHEEL) == RI_MOUSE_WHEEL ||
		(rawMouse.usButtonFlags & RI_MOUSE_HWHEEL) == RI_MOUSE_HWHEEL)
	{
		static const unsigned long defaultScrollLinesPerWheelDelta = 3;
		static const unsigned long defaultScrollCharsPerWheelDelta = 1;

		float wheelDelta = (float)(short)rawMouse.usButtonData;
		float numTicks = wheelDelta / WHEEL_DELTA;

		bool isHorizontalScroll = (rawMouse.usButtonFlags & RI_MOUSE_HWHEEL) == RI_MOUSE_HWHEEL;
		bool isScrollByPage = false;
		float scrollDelta = numTicks;

		if (isHorizontalScroll)
		{
			pData->scrollChars = defaultScrollCharsPerWheelDelta;
			SystemParametersInfo(SPI_GETWHEELSCROLLCHARS, 0, &pData->scrollChars, 0);
			scrollDelta *= pData->scrollChars;
		}
		else
		{
			pData->scrollLines = defaultScrollLinesPerWheelDelta;
			SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &pData->scrollLines, 0);
			if (pData->scrollLines == WHEEL_PAGESCROLL)
				isScrollByPage = true;
			else
				scrollDelta *= pData->scrollLines;
		}

		pData->scrollDelta = scrollDelta;
		bIsMouseInput = true;
	}

	// Handle Move
	if ((rawMouse.usFlags & MOUSE_MOVE_ABSOLUTE) == MOUSE_MOVE_ABSOLUTE)
	{
		bool isVirtualDesktop = (rawMouse.usFlags & MOUSE_VIRTUAL_DESKTOP) == MOUSE_VIRTUAL_DESKTOP;

		int width = GetSystemMetrics(isVirtualDesktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
		int height = GetSystemMetrics(isVirtualDesktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);

		int absoluteX = int((rawMouse.lLastX / 65535.0f) * width);
		int absoluteY = int((rawMouse.lLastY / 65535.0f) * height);
	}
	else if (rawMouse.lLastX != 0 || rawMouse.lLastY != 0)
	{
		pData->relativeX = rawMouse.lLastX;
		pData->relativeY = rawMouse.lLastY;

		bIsMouseInput = true;
	}


#if LOG_RAW_INPUT
	char szTempOutput[1024];
	StringCchPrintf(szTempOutput, STRSAFE_MAX_CCH, TEXT("%u  Mouse: usFlags=%04x ulButtons=%04x usButtonFlags=%04x usButtonData=%04x ulRawButtons=%04x lLastX=%04x lLastY=%04x ulExtraInformation=%04x\r\n"),
		rawInputSize,
		raw->data.mouse.usFlags,
		raw->data.mouse.ulButtons,
		raw->data.mouse.usButtonFlags,
		raw->data.mouse.usButtonData,
		raw->data.mouse.ulRawButtons,
		raw->data.mouse.lLastX,
		raw->data.mouse.lLastY,
		raw->data.mouse.ulExtraInformation);
	OutputDebugString(szTempOutput);
#endif

	return bIsMouseInput;
}

void Input::InitRawInputDevices(HWND hwnd)
{
	// register mouse for raw input
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms645565.aspx
	RAWINPUTDEVICE Rid[1];
	Rid[0].usUsagePage = (USHORT)0x01;	// HID_USAGE_PAGE_GENERIC;
	Rid[0].usUsage = (USHORT)0x02;	// HID_USAGE_GENERIC_MOUSE;
	Rid[0].dwFlags = 0;
	Rid[0].hwndTarget = hwnd;
	if (FALSE == (RegisterRawInputDevices(Rid, 1, sizeof(Rid[0]))))	// Cast between semantically different integer types : a Boolean type to HRESULT.
	{
		//OutputDebugString("Failed to register raw input device!");
	}

	// get devices and print info
	//-----------------------------------------------------
	UINT numDevices = 0;
	GetRawInputDeviceList(NULL, &numDevices, sizeof(RAWINPUTDEVICELIST));
	if (numDevices == 0) return;

	std::vector<RAWINPUTDEVICELIST> deviceList(numDevices);
	GetRawInputDeviceList(
		&deviceList[0], &numDevices, sizeof(RAWINPUTDEVICELIST));

	std::vector<wchar_t> deviceNameData;
	std::wstring deviceName;
	for (UINT i = 0; i < numDevices; ++i)
	{
		const RAWINPUTDEVICELIST& device = deviceList[i];
		if (device.dwType == RIM_TYPEMOUSE)
		{
			char info[1024];
			sprintf_s(info, "Mouse: Handle=0x%08p\n", device.hDevice);
			//OutputDebugString(info);

			UINT dataSize = 0;
			GetRawInputDeviceInfo(device.hDevice, RIDI_DEVICENAME, nullptr, &dataSize);
			if (dataSize)
			{
				deviceNameData.resize(dataSize);
				UINT result = GetRawInputDeviceInfo(device.hDevice, RIDI_DEVICENAME, &deviceNameData[0], &dataSize);
				if (result != UINT_MAX)
				{
					deviceName.assign(deviceNameData.begin(), deviceNameData.end());
					
					char info[1024];
					const std::string ndeviceName = StrUtil::UnicodeToASCII(deviceNameData.data());
					sprintf_s(info, "  Name=%s\n", ndeviceName.c_str());
					//OutputDebugString(info);
				}
			}

			RID_DEVICE_INFO deviceInfo;
			deviceInfo.cbSize = sizeof deviceInfo;
			dataSize = sizeof deviceInfo;
			UINT result = GetRawInputDeviceInfo(device.hDevice, RIDI_DEVICEINFO, &deviceInfo, &dataSize);
			if (result != UINT_MAX)
			{
#ifdef _DEBUG
				assert(deviceInfo.dwType == RIM_TYPEMOUSE);
#endif
				char info[1024];
				sprintf_s(info,
					"  Id=%u, Buttons=%u, SampleRate=%u, HorizontalWheel=%s\n",
					deviceInfo.mouse.dwId,
					deviceInfo.mouse.dwNumberOfButtons,
					deviceInfo.mouse.dwSampleRate,
					deviceInfo.mouse.fHasHorizontalWheel ? "1" : "0");
				//OutputDebugString(info);
			}
		}
	}
}




Input::Input()
	: mbIgnoreInput(false)
	, mMouseDelta{0, 0}
	, mMousePosition{0, 0}
	, mMouseScroll(0)
{
	mMouseButtons[EMouseButtons::MOUSE_BUTTON_LEFT]   = 0;
	mMouseButtons[EMouseButtons::MOUSE_BUTTON_RIGHT]  = 0;
	mMouseButtons[EMouseButtons::MOUSE_BUTTON_MIDDLE] = 0;
	
	mMouseButtonDoubleClicks = mMouseButtonsPrevious = mMouseButtons;

	mKeys.fill(0);
	mKeysPrevious.fill(0);
}

Input::Input(Input&& other)

	: mbIgnoreInput(other.mbIgnoreInput.load())

	, mMouseDelta   (other.mMouseDelta)
	, mMousePosition(other.mMousePosition)
	, mMouseScroll  (other.mMouseScroll)
	, mMouseButtons (other.mMouseButtons)
	, mMouseButtonsPrevious(other.mMouseButtonsPrevious)
	, mMouseButtonDoubleClicks(other.mMouseButtonsPrevious)

	, mKeys(other.mKeys)
	, mKeysPrevious(other.mKeysPrevious)
{}

// called at the end of the frame
void Input::PostUpdate()
{
	mKeysPrevious = mKeys;
	mMouseButtonsPrevious = mMouseButtons;

	// Reset Mouse Data
	mMouseDelta[0] = mMouseDelta[1] = 0;
	mMouseScroll = 0;

#if VERBOSE_LOGGING
	Log::Info("Input::PostUpdate() : scroll=%d", mMouseScroll);
#endif
}




void Input::UpdateKeyDown(KeyDownEventData data)
{
	const auto& key = data.mouse.wparam;

	// MOUSE KEY
	if (IsMouseKey(key))
	{
		const EMouseButtons mouseBtn = static_cast<EMouseButtons>(key);

		// if left & right mouse is clicked the same time, @key will be
		// Input::EMouseButtons::MOUSE_BUTTON_LEFT | Input::EMouseButtons::MOUSE_BUTTON_RIGHT
		if (mouseBtn & EMouseButtons::MOUSE_BUTTON_LEFT)   mMouseButtons[EMouseButtons::MOUSE_BUTTON_LEFT] = true;
		if (mouseBtn & EMouseButtons::MOUSE_BUTTON_RIGHT)  mMouseButtons[EMouseButtons::MOUSE_BUTTON_RIGHT] = true;
		if (mouseBtn & EMouseButtons::MOUSE_BUTTON_MIDDLE) mMouseButtons[EMouseButtons::MOUSE_BUTTON_MIDDLE] = true;
		if (data.mouse.bDoubleClick)
		{
			if (mouseBtn & EMouseButtons::MOUSE_BUTTON_LEFT)   mMouseButtonDoubleClicks[EMouseButtons::MOUSE_BUTTON_LEFT] = true;
			if (mouseBtn & EMouseButtons::MOUSE_BUTTON_RIGHT)  mMouseButtonDoubleClicks[EMouseButtons::MOUSE_BUTTON_RIGHT] = true;
			if (mouseBtn & EMouseButtons::MOUSE_BUTTON_MIDDLE) mMouseButtonDoubleClicks[EMouseButtons::MOUSE_BUTTON_MIDDLE] = true;
#if VERBOSE_LOGGING
			Log::Info("Double Click!!");
#endif
		}
	}

	// KEYBOARD KEY
	else
	{
		mKeys[key] = true;
	}
}

void Input::UpdateKeyUp(KeyCode key)
{
	if (IsMouseKey(key))
	{
		const EMouseButtons mouseBtn = static_cast<EMouseButtons>(key);
		
		mMouseButtons[mouseBtn] = false;
		mMouseButtonDoubleClicks[mouseBtn] = false;
#if VERBOSE_LOGGING
		Log::Info("Mouse Key Up %x", key);
#endif
	}
	else
		mKeys[key] = false;
}

void Input::UpdateMousePos(long x, long y, short scroll)
{
	mMouseDelta[0] = static_cast<float>(std::max(-1l, std::min(x - mMousePosition[0], 1l)));
	mMouseDelta[1] = static_cast<float>(std::max(-1l, std::min(y - mMousePosition[1], 1l)));

	mMousePosition[0] = x;
	mMousePosition[1] = y;

#if defined(_DEBUG) && VERBOSE_LOGGING
	Log::Info("Mouse Delta: (%d, %d)\tMouse Position: (%d, %d)\tMouse Scroll: (%d)", 
		mMouseDelta[0], mMouseDelta[1],
		mMousePosition[0], mMousePosition[1],
		(int)scroll);
#endif

	mMouseScroll = scroll;
}

void Input::UpdateMousePos_Raw(int relativeX, int relativeY, short scroll, bool bMouseCaptured)
{
	if (bMouseCaptured)
	{
		//SetCursorPos(setting.width / 2, setting.height / 2);

		mMouseDelta[0] = static_cast<float>(relativeX);
		mMouseDelta[1] = static_cast<float>(relativeY);

		// unused for now
		mMousePosition[0] = 0;
		mMousePosition[1] = 0;

		mMouseScroll = scroll;

#if VERBOSE_LOGGING
		if (scroll != 0)
		{
			Log::Info("Scroll: %d", mMouseScroll);
		}
#endif
	}
}




bool Input::IsKeyDown(KeyCode key) const
{
	return mKeys[key] && !mbIgnoreInput;
}

bool Input::IsKeyDown(const char * key) const
{
	const KeyCode& code = KEY_MAP.at(key);
	return mKeys[code] && !mbIgnoreInput;
}

bool Input::IsKeyUp(const char * key) const
{
	const KeyCode& code = KEY_MAP.at(key);
	return (!mKeys[code] && mKeysPrevious[code]) && !mbIgnoreInput;
}

bool Input::IsKeyDown(const std::string& key) const
{
	const KeyCode& code = KEY_MAP.at(key.c_str());
	return mKeys[code] && !mbIgnoreInput;
}

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


int Input::MouseDeltaX() const { return !mbIgnoreInput ? (int)mMouseDelta[0] : 0; }
int Input::MouseDeltaY() const { return !mbIgnoreInput ? (int)mMouseDelta[1] : 0; }

bool Input::IsMouseDown(EMouseButtons mbtn) const
{
	return !mbIgnoreInput && mMouseButtons.at(mbtn);
}

bool Input::IsMouseDoubleClick(EMouseButtons mbtn) const
{
	return !mbIgnoreInput && mMouseButtonDoubleClicks.at(mbtn);
}

bool Input::IsMouseUp(EMouseButtons mbtn) const
{
	const bool bButtonUp = !mMouseButtons.at(mbtn) && mMouseButtonsPrevious.at(mbtn);
	return !mbIgnoreInput && bButtonUp;
}

bool Input::IsMouseTriggered(EMouseButtons mbtn) const
{
	const bool bButtonTriggered = mMouseButtons.at(mbtn) && !mMouseButtonsPrevious.at(mbtn);
	return !mbIgnoreInput && bButtonTriggered;
}

bool Input::IsMouseScrollUp() const
{
	return mMouseScroll > 0 && !mbIgnoreInput;
}

bool Input::IsMouseScrollDown() const
{
#if VERBOSE_LOGGING
	Log::Info("Input::IsMouseScrollDown() : scroll=%d", mMouseScroll);
#endif
	return mMouseScroll < 0 && !mbIgnoreInput;
}

bool Input::IsAnyMouseDown() const
{
	return 
		   mMouseButtons.at(EMouseButtons::MOUSE_BUTTON_LEFT)
		|| mMouseButtons.at(EMouseButtons::MOUSE_BUTTON_RIGHT)
		|| mMouseButtons.at(EMouseButtons::MOUSE_BUTTON_MIDDLE)

		|| mMouseButtonDoubleClicks.at(EMouseButtons::MOUSE_BUTTON_RIGHT)
		|| mMouseButtonDoubleClicks.at(EMouseButtons::MOUSE_BUTTON_MIDDLE)
		|| mMouseButtonDoubleClicks.at(EMouseButtons::MOUSE_BUTTON_LEFT)
	;
}


