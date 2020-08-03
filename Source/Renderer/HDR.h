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

// Sources:
//
// - https://docs.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range
// - GDC 2018 - NVIDIA - HDR Ecosystems for Games: https://www.youtube.com/watch?v=OvLuQliiJlg
// - https://www.pyromuffin.com/2018/07/how-to-render-to-hdr-displays-on.html
// - https://software.intel.com/content/www/us/en/develop/blogs/detect-and-enable-hdr-with-microsoft-directx-11-and-directx-12.html
// - https://www.asawicki.info/news_1703_programming_hdr_monitor_support_in_direct3d
// - https://www.youtube.com/watch?v=9Upl31Mykrc : Linus Sebastian : Backlight Types As Fast As Possible
// - https://www.youtube.com/watch?v=tzm2XjcyKDQ : Linus Sebastian : HDR Standards Explained 
// - https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-24-importance-being-linear
// - https://app.spectracal.com/Documents/White%20Papers/HDR_Demystified.pdf


//
// # NOTES FROM NVD GDC2018 TALK
//
// # HDR Display Standards
// - HDMI
// - VESA - Display Port
// - SMPTE: Society of Motion Picture & Television
// - Intl. Telecom. Union (ITU) - Color Spaces

// # HDR10 
// - common term for popular HDR encoding standards tied to SMPTE/others
// - BT 2100 might be better term for generic usage
// - SMPTE 2084 transfer function <-- replacement for gamma2.2
// - BT 2020 color primaries
// - 10/12 bits-per-component signal
// - Encoding
//   - RGB 
//   - YCbCr 4:2:2 
//   - YCbCr 4:2:0 wire transfer (invisible to the dev)
// - Metadata (HDR10+)
//   - Static vs Dynamic
//   - SMPTE 2086/CEA Static Metadata
//     - Reference Primaries & WhitePoint
//     - Peak/Min Luminance
//     - Max Content Light Level
//     - Frame Avg Light Level
// - Dolby Vision
//   - Proprietart tech/branch
//   - Uses special encoding
//   - Supports dynamic metadata
//   - HDR10 is usually supported alongside Dolby Vision
// - DXGI
//   - Colorspace type: RGB (games) or YCbCr (video)
//   - Transfer/Gamma fuinctions 
//     - G22   : Gamma2.2                  | RGBA8_UNORM / RGB10A2_UNORM
//     - G10   : Linear ~ scRGB (no Gamma) | RGBA16_FLOAT
//     - G2084 : SMPTE 2084 (PQ Curve)     | RGBA8_UNORM / RGB10A2_UNORM
//   - Encoding range (Full / Studio)
//   - Siting / chroma subsampling (video)
//   - Color primaries (P709 / P2020)
//   - IDXGISwapChain4::SetHDRMetaData()

//------------------------------------------------------------------------------------------------------------------

#include <string>

enum EColorSpace
{
	REC_709 = 0,  // Also: sRGB
	REC_2020,     // Also: BT_2020
	DCI_P3,       // TODO: how do we handle this?
	
	NUM_COLOR_SPACES
};

enum EDisplayCurve
{
	  sRGB = 0     // The display expects an sRGB signal.
	, ST2084       // The display expects an HDR10 signal.
	, None         // The display expects a linear signal.
	, Linear = None

	, NUM_DISPLAY_CURVES
};

struct FDisplayHDRProfile
{
	std::string DisplayName;
	union 
	{
		float MinBrightness;
		float MinLuminance;
	};
	union
	{
		float MaxBrightness;
		float MaxLuminance;
	};
};

struct FSetHDRMetaDataParams
{
	EColorSpace ColorSpace = REC_709;
	float MaxOutputNits = 270.0f;
	float MinOutputNits = 0.01f;
	float MaxContentLightLevel = 2000.0f;
	float MaxFrameAverageLightLevel = 80.0f;
};
