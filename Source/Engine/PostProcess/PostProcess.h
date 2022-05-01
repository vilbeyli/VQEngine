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

#include "../Core/Types.h"

#include "../../Renderer/HDR.h"

// fwd decl
struct FUIState;

enum class EDrawMode
{
	LIT_AND_POSTPROCESSED = 0,
	//WIREFRAME,    // TODO: add support
	//NO_MATERIALS, // TODO: add support

	DEPTH,
	NORMALS,
	ROUGHNESS,
	METALLIC,
	AO,
	ALBEDO,
	REFLECTIONS,

	NUM_DRAW_MODES,
};
struct FPostProcessParameters
{
	struct FTonemapper
	{
		EColorSpace   ContentColorSpace = EColorSpace::REC_709;
		EDisplayCurve OutputDisplayCurve = EDisplayCurve::sRGB;
		float         DisplayReferenceBrightnessLevel = 200.0f;
		int           ToggleGammaCorrection = 1;
		float         UIHDRBrightness = 1.0f;
	};
	struct FFFXCAS
	{
		float CASSharpen = 0.8f;
		FFFXCAS() = default;
		FFFXCAS(const FFFXCAS& other) : CASSharpen(other.CASSharpen) { memcpy(CASConstantBlock, other.CASConstantBlock, sizeof(CASConstantBlock)); }
		void UpdateCASConstantBlock(uint InputWidth, uint InputHeight, uint OutputWidth, uint OutputHeight);
		
		unsigned CASConstantBlock[8];
	};
	struct FFSR_EASU
	{
		enum EPresets
		{
			ULTRA_QUALITY = 0,
			QUALITY,
			BALANCED,
			PERFORMANCE,
			CUSTOM,

			NUM_FSR_PRESET_OPTIONS
		};

		float GetScreenPercentage() const;
		FFSR_EASU();
		FFSR_EASU(const FFSR_EASU& other);
		void UpdateEASUConstantBlock(uint InputWidth          , uint InputHeight,
			                         uint InputContainerWidth , uint InputContainerHeight,
			                         uint OutputWidth         , uint OutputHeight);
		
		EPresets SelectedFSRPreset;
		float    fCustomScaling;

		unsigned EASUConstantBlock[16];
	};
	struct FFSR_RCAS
	{
		FFSR_RCAS();
		FFSR_RCAS(const FFSR_RCAS& other);
		float GetLinearSharpness() const;
		void  SetLinearSharpness(float Sharpness);
		void UpdateRCASConstantBlock();

		unsigned RCASConstantBlock[4];
		float RCASSharpnessStops = 0.2f;
	};
	struct FBlurParams // Gaussian Blur Pass
	{ 
		int iImageSizeX;
		int iImageSizeY;
	};

	//-------------------------------------------------------------------------------------------
	//-------------------------------------------------------------------------------------------
	//-------------------------------------------------------------------------------------------

	inline bool IsFFXCASEnabled() const { return !this->bEnableFSR && this->bEnableCAS; }
	inline bool IsFSREnabled() const { return this->bEnableFSR; }

	int SceneRTWidth = 0;
	int SceneRTHeight = 0;
	int DisplayResolutionWidth = 0;
	int DisplayResolutionHeight = 0;

	FTonemapper TonemapperParams = {};
	FBlurParams BlurParams       = {};
	FFFXCAS     FFXCASParams     = {};
	FFSR_RCAS   FFSR_RCASParams  = {};
	FFSR_EASU   FFSR_EASUParams  = {};
	EDrawMode   eDrawMode = EDrawMode::LIT_AND_POSTPROCESSED;

	bool bVisualization_UnpackNormals = false;
	bool bEnableCAS = false;
	bool bEnableFSR = false;
	bool bEnableGaussianBlur = false;
};
