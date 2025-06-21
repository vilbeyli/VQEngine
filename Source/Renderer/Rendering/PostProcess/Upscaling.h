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

#include "Engine/Core/Types.h"

#include "Renderer/Rendering/HDR.h"

#define DISABLE_FIDELITYFX_CAS 1 // disable ffx cas but keep implementaiton around, now using fsr1 rcas

class ID3D12Device;


namespace AMD_FidelityFX_SuperResolution1
{
	// AMD FidelityFX Super Resolution 1.0: Spatial Upscaling and RCAS

	enum EPreset
	{
		ULTRA_QUALITY = 0,
		QUALITY,
		BALANCED,
		PERFORMANCE,
		CUSTOM,

		NUM_FSR1_PRESET_OPTIONS
	};
	inline const char* GetVersionString() { return "1.0"; }
	inline float GetScreenPercentage(EPreset ePreset)
	{
		switch (ePreset)
		{
		case EPreset::ULTRA_QUALITY: return 0.77f;
		case EPreset::QUALITY      : return 0.67f;
		case EPreset::BALANCED     : return 0.58f;
		case EPreset::PERFORMANCE  : return 0.50f;
		}
		return 1.0f;
	}
	struct FShaderParameters
	{
		struct RCAS
		{
			unsigned RCASConstantBlock[4];
			float RCASSharpnessStops = 0.2f;

			float GetLinearSharpness() const;
			void  SetLinearSharpness(float Sharpness);

			void UpdateConstantBlock();
		};
		struct EASU
		{
			unsigned EASUConstantBlock[16];
			void UpdateConstantBlock(
				uint InputWidth, uint InputHeight,
				uint InputContainerWidth, uint InputContainerHeight,
				uint OutputWidth, uint OutputHeight
			);
		};
		EASU easu;
		RCAS rcas;
	};
}

namespace AMD_FidelityFX_SuperResolution3
{
	enum EPreset
	{
		NATIVE_AA = 0,
		QUALITY,
		BALANCED,
		PERFORMANCE,
		ULTRA_PERFORMANCE,
		CUSTOM,

		NUM_FSR3_PRESET_OPTIONS
	};
	inline const char* GetVersionString() { return "3.1.4"; }
	inline const char* GetPresetName(EPreset ePreset)
	{
		switch (ePreset)
		{
		case EPreset::NATIVE_AA        : return "Native AA";
		case EPreset::QUALITY          : return "Quality";
		case EPreset::BALANCED         : return "Balanced";
		case EPreset::PERFORMANCE      : return "Performance";
		case EPreset::ULTRA_PERFORMANCE: return "UltraPerformance";
		case EPreset::CUSTOM           : return "Custom";
		}
		return "Unknown Preset";
	}
	inline float GetScreenPercentage(EPreset ePreset)
	{
		switch (ePreset)
		{
		case EPreset::NATIVE_AA        : return 1.000f;
		case EPreset::QUALITY          : return 0.667f;
		case EPreset::BALANCED         : return 0.588f;
		case EPreset::PERFORMANCE      : return 0.500f;
		case EPreset::ULTRA_PERFORMANCE: return 0.333f;
		}
		return -1.0f;
	}
	struct FShaderParameters
	{

	};
	struct ContextImpl;
	struct Context
	{
		void Initialize(ID3D12Device* pDevice, uint DisplayWidth, uint DisplayHeight, uint RenderWidth, uint RenderHeight);
		void Destroy();

		ContextImpl* pImpl = nullptr;
	};
}
