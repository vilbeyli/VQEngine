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

#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d12.h>
#include "Engine/Core/Types.h"
#include "Libs/VQUtils/Include/Log.h"
#include <cassert>
#include <cwchar>

#define KILOBYTE 1024ull
#define MEGABYTE (1024ull*KILOBYTE)
#define GIGABYTE (1024ull*MEGABYTE)
#define TERABYTE (1024ull*GIGABYTE)

template<class T>
T AlignOffset(const T& uOffset, const T& uAlign) { return ((uOffset + (uAlign - 1)) & ~(uAlign - 1)); }

template<class... Args>
void SetName(ID3D12Object* pObj, const char* format, Args&&... args)
{
	char bufName[240];
	sprintf_s(bufName, format, args...);

	wchar_t wBufName[240];
	size_t convertedChars = 0;
	mbstowcs_s(&convertedChars, wBufName, bufName, sizeof(wBufName) / sizeof(wchar_t));

	pObj->SetName(wBufName);
}

inline void ThrowIfFailed(HRESULT hr)
{
	if (FAILED(hr))
	{
		assert(false);// throw HrException(hr);
	}
}
