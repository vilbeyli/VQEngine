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

#include "DXGIUtils.h"

#include "Engine/GPUMarker.h"

#include <algorithm>
#include <cassert>


namespace VQ_DXGI_UTILS
{
	size_t BitsPerPixel(DXGI_FORMAT fmt)
	{
		switch (fmt)
		{
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
		case DXGI_FORMAT_R32G32B32A32_UINT:
		case DXGI_FORMAT_R32G32B32A32_SINT:
			return 128;

		case DXGI_FORMAT_R32G32B32_TYPELESS:
		case DXGI_FORMAT_R32G32B32_FLOAT:
		case DXGI_FORMAT_R32G32B32_UINT:
		case DXGI_FORMAT_R32G32B32_SINT:
			return 96;

		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_UNORM:
		case DXGI_FORMAT_R16G16B16A16_UINT:
		case DXGI_FORMAT_R16G16B16A16_SNORM:
		case DXGI_FORMAT_R16G16B16A16_SINT:
		case DXGI_FORMAT_R32G32_TYPELESS:
		case DXGI_FORMAT_R32G32_FLOAT:
		case DXGI_FORMAT_R32G32_UINT:
		case DXGI_FORMAT_R32G32_SINT:
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
		case DXGI_FORMAT_Y416:
		case DXGI_FORMAT_Y210:
		case DXGI_FORMAT_Y216:
			return 64;

		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
		case DXGI_FORMAT_R10G10B10A2_UNORM:
		case DXGI_FORMAT_R10G10B10A2_UINT:
		case DXGI_FORMAT_R11G11B10_FLOAT:
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_UINT:
		case DXGI_FORMAT_R8G8B8A8_SNORM:
		case DXGI_FORMAT_R8G8B8A8_SINT:
		case DXGI_FORMAT_R16G16_TYPELESS:
		case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R16G16_UNORM:
		case DXGI_FORMAT_R16G16_UINT:
		case DXGI_FORMAT_R16G16_SNORM:
		case DXGI_FORMAT_R16G16_SINT:
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R32_UINT:
		case DXGI_FORMAT_R32_SINT:
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
		case DXGI_FORMAT_R8G8_B8G8_UNORM:
		case DXGI_FORMAT_G8R8_G8B8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		case DXGI_FORMAT_AYUV:
		case DXGI_FORMAT_Y410:
		case DXGI_FORMAT_YUY2:
			return 32;

		case DXGI_FORMAT_P010:
		case DXGI_FORMAT_P016:
			return 24;

		case DXGI_FORMAT_R8G8_TYPELESS:
		case DXGI_FORMAT_R8G8_UNORM:
		case DXGI_FORMAT_R8G8_UINT:
		case DXGI_FORMAT_R8G8_SNORM:
		case DXGI_FORMAT_R8G8_SINT:
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R16_FLOAT:
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM:
		case DXGI_FORMAT_R16_UINT:
		case DXGI_FORMAT_R16_SNORM:
		case DXGI_FORMAT_R16_SINT:
		case DXGI_FORMAT_B5G6R5_UNORM:
		case DXGI_FORMAT_B5G5R5A1_UNORM:
		case DXGI_FORMAT_A8P8:
		case DXGI_FORMAT_B4G4R4A4_UNORM:
			return 16;

		case DXGI_FORMAT_NV12:
		case DXGI_FORMAT_420_OPAQUE:
		case DXGI_FORMAT_NV11:
			return 12;

		case DXGI_FORMAT_R8_TYPELESS:
		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_R8_UINT:
		case DXGI_FORMAT_R8_SNORM:
		case DXGI_FORMAT_R8_SINT:
		case DXGI_FORMAT_A8_UNORM:
		case DXGI_FORMAT_AI44:
		case DXGI_FORMAT_IA44:
		case DXGI_FORMAT_P8:
			return 8;

		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return 8;

		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
			return 4;

		case DXGI_FORMAT_R1_UNORM:
			return 1;

		default:
			return 0;
		}
	}

	//--------------------------------------------------------------------------------------
	// return the byte size of a pixel (or block if block compressed)
	//--------------------------------------------------------------------------------------
	size_t GetPixelByteSize(DXGI_FORMAT fmt)
	{
		switch (fmt)
		{
		case(DXGI_FORMAT_R10G10B10A2_TYPELESS):
		case(DXGI_FORMAT_R10G10B10A2_UNORM):
		case(DXGI_FORMAT_R10G10B10A2_UINT):
		case(DXGI_FORMAT_R11G11B10_FLOAT):
		case(DXGI_FORMAT_R8G8B8A8_TYPELESS):
		case(DXGI_FORMAT_R8G8B8A8_UNORM):
		case(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB):
		case(DXGI_FORMAT_R8G8B8A8_UINT):
		case(DXGI_FORMAT_R8G8B8A8_SNORM):
		case(DXGI_FORMAT_R8G8B8A8_SINT):
		case(DXGI_FORMAT_B8G8R8A8_UNORM):
		case(DXGI_FORMAT_B8G8R8X8_UNORM):
		case(DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM):
		case(DXGI_FORMAT_B8G8R8A8_TYPELESS):
		case(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB):
		case(DXGI_FORMAT_B8G8R8X8_TYPELESS):
		case(DXGI_FORMAT_B8G8R8X8_UNORM_SRGB):
		case(DXGI_FORMAT_R16G16_TYPELESS):
		case(DXGI_FORMAT_R16G16_FLOAT):
		case(DXGI_FORMAT_R16G16_UNORM):
		case(DXGI_FORMAT_R16G16_UINT):
		case(DXGI_FORMAT_R16G16_SNORM):
		case(DXGI_FORMAT_R16G16_SINT):
		case(DXGI_FORMAT_R32_TYPELESS):
		case(DXGI_FORMAT_D32_FLOAT):
		case(DXGI_FORMAT_R32_FLOAT):
		case(DXGI_FORMAT_R32_UINT):
		case(DXGI_FORMAT_R32_SINT):
			return 4;

		case(DXGI_FORMAT_BC1_TYPELESS):
		case(DXGI_FORMAT_BC1_UNORM):
		case(DXGI_FORMAT_BC1_UNORM_SRGB):
		case(DXGI_FORMAT_BC4_TYPELESS):
		case(DXGI_FORMAT_BC4_UNORM):
		case(DXGI_FORMAT_BC4_SNORM):
		case(DXGI_FORMAT_R16G16B16A16_FLOAT):
		case(DXGI_FORMAT_R16G16B16A16_TYPELESS):
			return 8;

		case(DXGI_FORMAT_BC2_TYPELESS):
		case(DXGI_FORMAT_BC2_UNORM):
		case(DXGI_FORMAT_BC2_UNORM_SRGB):
		case(DXGI_FORMAT_BC3_TYPELESS):
		case(DXGI_FORMAT_BC3_UNORM):
		case(DXGI_FORMAT_BC3_UNORM_SRGB):
		case(DXGI_FORMAT_BC5_TYPELESS):
		case(DXGI_FORMAT_BC5_UNORM):
		case(DXGI_FORMAT_BC5_SNORM):
		case(DXGI_FORMAT_BC6H_TYPELESS):
		case(DXGI_FORMAT_BC6H_UF16):
		case(DXGI_FORMAT_BC6H_SF16):
		case(DXGI_FORMAT_BC7_TYPELESS):
		case(DXGI_FORMAT_BC7_UNORM):
		case(DXGI_FORMAT_BC7_UNORM_SRGB):
		case(DXGI_FORMAT_R32G32B32A32_FLOAT):
		case(DXGI_FORMAT_R32G32B32A32_TYPELESS):
			return 16;

		default:
			assert(0);
			break;
		}
		return 0;
	}

	void MipImage(const void* pDataSrc, void* pDataDst, uint width, uint height, uint bytesPerPixel)
	{
		assert(pDataDst);
		assert(pDataSrc);

		SCOPED_CPU_MARKER("MipImage");

#define GetByte(color, component) (((color) >> (8 * (component))) & 0xff)
#define GetColor(ptr, x,y) (ptr[(x)+(y)*width])
#define SetColor(ptr, x,y, col) ptr[(x)+(y)*width/2]=col;

		int offsetsX[] = { 0,1,0,1 };
		int offsetsY[] = { 0,0,1,1 };
		assert(bytesPerPixel == 4 || bytesPerPixel == 16);
		
		if (bytesPerPixel == 4)
		{
			const uint32_t* pImgSrc = (const uint32_t*)pDataSrc;
			uint32_t* pImgDst = (uint32_t*)pDataDst;

			for (uint32_t y = 0; y < height; y += 2)
			{
				for (uint32_t x = 0; x < width; x += 2)
				{
					uint32_t ccc = 0;
					for (uint32_t c = 0; c < 4; c++)
					{
						uint32_t cc = 0;
						cc += GetByte(GetColor(pImgSrc, x + offsetsX[0], y + offsetsY[0]), 3 - c);
						cc += GetByte(GetColor(pImgSrc, x + offsetsX[1], y + offsetsY[1]), 3 - c);
						cc += GetByte(GetColor(pImgSrc, x + offsetsX[2], y + offsetsY[2]), 3 - c);
						cc += GetByte(GetColor(pImgSrc, x + offsetsX[3], y + offsetsY[3]), 3 - c);
						ccc = (ccc << 8) | (cc / 4); // ABGR
					}
					SetColor(pImgDst, x / 2, y / 2, ccc);
				}
			}
		}

		if (bytesPerPixel == 16)
		{
			using std::min;
			const float* pImgSrc = (const float*)pDataSrc;
			      float* pImgDst = (      float*)pDataDst;
			// each iteration handles 4 pixels from current level, writes out to a single pixel
			for (uint32_t y = 0; y < height; y += 2) // [0, 2, 4, ...]
			for (uint32_t x = 0; x < width ; x += 2) // [0, 2, 4, ...]
			{
				float rgb[4][3] = {}; // 4 samples of rgb
				for (uint smp = 0; smp < 4; ++smp)
				{
					for (int ch = 0; ch < 3; ++ch) // color channel ~ rgba, care for RGB only
						rgb[smp][ch] = pImgSrc[(x + offsetsX[smp]) * 4 + (y + offsetsY[smp]) * 4 * width + ch];
				}

				// filter: use min filter rather than interpolation
				float rgbFiltered[4];
				for (int ch = 0; ch < 3; ++ch)
					rgbFiltered[ch] = min(rgb[0][ch], min(rgb[1][ch], min(rgb[2][ch], rgb[3][ch])));
				rgbFiltered[3] = 1.0f;

				uint outX = x >> 1;
				uint outY = y >> 1;

				for (int ch = 0; ch < 4; ++ch)
					pImgDst[(outX * 4) + 4 * outY * (width >> 1) + ch] = rgbFiltered[ch];
			}
		}

#if 0
		// For cutouts we need we need to scale the alpha channel to match the coverage of the top MIP map
		// otherwise cutouts seem to get thinner when smaller mips are used
		// Credits: http://the-witness.net/news/2010/09/computing-alpha-mipmaps/
		//
		if (m_alphaTestCoverage < 1.0)
		{
			float ini = 0;
			float fin = 10;
			float mid;
			float alphaPercentage;
			int iter = 0;
			for (; iter < 50; iter++)
			{
				mid = (ini + fin) / 2;
				alphaPercentage = GetAlphaCoverage(width / 2, height / 2, mid, (int)(m_cutOff * 255));

				if (fabs(alphaPercentage - m_alphaTestCoverage) < .001)
					break;

				if (alphaPercentage > m_alphaTestCoverage)
					fin = mid;
				if (alphaPercentage < m_alphaTestCoverage)
					ini = mid;
			}
			ScaleAlpha(width / 2, height / 2, mid);
			//Trace(format("(%4i x %4i), %f, %f, %i\n", width, height, alphaPercentage, 1.0f, 0));       
		}
#endif
	}


	// https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-error
	const char* GetDXGIError(HRESULT hr)
	{
		switch (hr)
		{
		case DXGI_ERROR_ACCESS_DENIED: return "You tried to use a resource to which you did not have the required access privileges. This error is most typically caused when you write to a shared resource with read-only access.";
		 case DXGI_ERROR_ACCESS_LOST: return "The desktop duplication interface is invalid. The desktop duplication interface typically becomes invalid when a different type of image is displayed on the desktop.";
		 case DXGI_ERROR_ALREADY_EXISTS: return "The desired element already exists. This is returned by DXGIDeclareAdapterRemovalSupport if it is not the first time that the function is called.";
		 case DXGI_ERROR_CANNOT_PROTECT_CONTENT: return "DXGI can't provide content protection on the swap chain. This error is typically caused by an older driver, or when you use a swap chain that is incompatible with content protection.";
		 case DXGI_ERROR_DEVICE_HUNG: return "The application's device failed due to badly formed commands sent by the application. This is an design-time issue that should be investigated and fixed.";
		 case DXGI_ERROR_DEVICE_REMOVED: return "The video card has been physically removed from the system, or a driver upgrade for the video card has occurred. The application should destroy and recreate the device. For help debugging the problem, call ID3D10Device::GetDeviceRemovedReason.";
		 case DXGI_ERROR_DEVICE_RESET: return "The device failed due to a badly formed command. This is a run-time issue; The application should destroy and recreate the device.";
		 case DXGI_ERROR_DRIVER_INTERNAL_ERROR: return "The driver encountered a problem and was put into the device removed state.";
		 case DXGI_ERROR_FRAME_STATISTICS_DISJOINT: return "An event (for example, a power cycle) interrupted the gathering of presentation statistics.";
		 case DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE: return "The application attempted to acquire exclusive ownership of an output, but failed because some other application (or device within the application) already acquired ownership.";
		 case DXGI_ERROR_INVALID_CALL: return "The application provided invalid parameter data; this must be debugged and fixed before the application is released.";
		 case DXGI_ERROR_MORE_DATA: return "The buffer supplied by the application is not big enough to hold the requested data.";
		 case DXGI_ERROR_NAME_ALREADY_EXISTS: return "The supplied name of a resource in a call to IDXGIResource1::CreateSharedHandle is already associated with some other resource.";
		 case DXGI_ERROR_NONEXCLUSIVE: return "A global counter resource is in use, and the Direct3D device can't currently use the counter resource.";
		 case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE: return "The resource or request is not currently available, but it might become available later.";
		 case DXGI_ERROR_NOT_FOUND: return "When calling IDXGIObject::GetPrivateData, the GUID passed in is not recognized as one previously passed to IDXGIObject::SetPrivateData or IDXGIObject::SetPrivateDataInterface. When calling IDXGIFactory::EnumAdapters or IDXGIAdapter::EnumOutputs, the enumerated ordinal is out of range.";
		 case DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED: return "Reserved";
		 case DXGI_ERROR_REMOTE_OUTOFMEMORY: return "Reserved";
		 case DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE: return "The DXGI output (monitor) to which the swap chain content was restricted is now disconnected or changed.";
		 case DXGI_ERROR_SDK_COMPONENT_MISSING: return "The operation depends on an SDK component that is missing or mismatched.";
		 case DXGI_ERROR_SESSION_DISCONNECTED: return "The Remote Desktop Services session is currently disconnected.";
		 case DXGI_ERROR_UNSUPPORTED: return "The requested functionality is not supported by the device or the driver.";
		 case DXGI_ERROR_WAIT_TIMEOUT: return "The time-out interval elapsed before the next desktop frame was available.";
		 case DXGI_ERROR_WAS_STILL_DRAWING: return "The GPU was busy at the moment when a call was made to perform an operation, and did not execute or schedule the operation.";
		 case S_OK: return "The method succeeded without an error.";
		}
		return "UNKNOWN_DXGI_ERROR";
	}

	void CopyPixels(const void* pData, void* pDest, uint32_t stride, uint32_t bytesWidth, uint32_t height)
	{
		for (uint32_t y = 0; y < height; y++)
		{
			memcpy((char*)pDest + y * stride, (char*)pData + y * bytesWidth, bytesWidth);
		}
	}
}
