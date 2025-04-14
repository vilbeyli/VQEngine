//	VQE
//	Copyright(C) 2025  - Volkan Ilbeyli
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

#include "Engine/Core/Types.h"
#include <dxgiformat.h>

namespace VQ_DXGI_UTILS
{
	size_t BitsPerPixel(DXGI_FORMAT fmt);

	//=====================================================================================-
	// return the byte size of a pixel (or block if block compressed)
	//=====================================================================================-
	size_t GetPixelByteSize(DXGI_FORMAT fmt);

	void MipImage(const void* pDataSrc, void* pDataDst, uint width, uint height, uint bytesPerPixel);
	void CopyPixels(const void* pData, void* pDest, uint stride, uint bytesWidth, uint height);
}