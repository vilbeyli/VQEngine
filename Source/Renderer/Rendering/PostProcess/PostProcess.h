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

#include "Upscaling.h"
#include "Engine/Core/Types.h"

#include "Renderer/Rendering/HDR.h"

#define DISABLE_FIDELITYFX_CAS 1 // disable ffx cas but keep implementaiton around, now using fsr1 rcas

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
	MOTION_VECTORS,

	NUM_DRAW_MODES,
};

// TODO: separate post process options from parameters (shader data)
struct FPostProcessParameters
{
	enum EUpscalingAlgorithm
	{
		NONE = 0,
		FIDELITYFX_SUPER_RESOLUTION_1,
		FIDELITYFX_SUPER_RESOLUTION_3,

		NUM_UPSCALING_ALGORITHMS
	};
	enum ESharpeningAlgorithm
	{
		NO_SHARPENING = 0,
		FIDELITYFX_CAS, // FFX CAS

		NUM_SHARPENING_ALGORITHMS
	};

	struct FTonemapper
	{
		EColorSpace   ContentColorSpace = EColorSpace::REC_709;
		EDisplayCurve OutputDisplayCurve = EDisplayCurve::sRGB;
		float         DisplayReferenceBrightnessLevel = 200.0f;
		int           ToggleGammaCorrection = 1;
		float         UIHDRBrightness = 1.0f;
	};
	struct FBlurParams // Gaussian Blur Pass
	{ 
		int iImageSizeX;
		int iImageSizeY;
	};
	struct FVizualizationParams
	{
		int iDrawMode = 0;
		int iUnpackNormals = 0;
		float fInputStrength = 100.0f;
	};
#if !DISABLE_FIDELITYFX_CAS
	struct FFFXCAS
	{
		float CASSharpen = 0.8f;
		FFFXCAS() = default;
		FFFXCAS(const FFFXCAS& other) : CASSharpen(other.CASSharpen) { memcpy(CASConstantBlock, other.CASConstantBlock, sizeof(CASConstantBlock)); }
		void UpdateCASConstantBlock(uint InputWidth, uint InputHeight, uint OutputWidth, uint OutputHeight);

		unsigned CASConstantBlock[8];
	};
#endif
	
	using AMD_FSR1_Preset = AMD_FidelityFX_SuperResolution1::EPreset;
	using AMD_FSR3_Preset = AMD_FidelityFX_SuperResolution3::EPreset;
	using AMD_FSR1_ShaderParams = AMD_FidelityFX_SuperResolution1::FShaderParameters;
	//using AMD_FSR3_ShaderParams = AMD_FidelityFX_SuperResolution3::FShaderParameters;

	//-------------------------------------------------------------------------------------------
	//-------------------------------------------------------------------------------------------
	//-------------------------------------------------------------------------------------------
#if DISABLE_FIDELITYFX_CAS
	inline bool IsFFXCASEnabled() const { return false; }
#else
	inline bool IsFFXCASEnabled() const { return !this->IsFSR1Enabled() && this->bEnableCAS; }
#endif
	inline bool IsFSR1Enabled() const { return UpscalingAlgorithm == EUpscalingAlgorithm::FIDELITYFX_SUPER_RESOLUTION_1; }
	inline bool IsFSR3Enabled() const { return UpscalingAlgorithm == EUpscalingAlgorithm::FIDELITYFX_SUPER_RESOLUTION_3; }

	int SceneRTWidth = 0;
	int SceneRTHeight = 0;
	int DisplayResolutionWidth = 0;
	int DisplayResolutionHeight = 0;

	FTonemapper          TonemapperParams = {};
	FBlurParams          BlurParams       = {};

	EUpscalingAlgorithm  UpscalingAlgorithm = EUpscalingAlgorithm::FIDELITYFX_SUPER_RESOLUTION_1;
	AMD_FSR1_Preset      UpscalingQualityPresetEnum = AMD_FSR1_Preset::ULTRA_QUALITY;
	AMD_FSR1_ShaderParams FSR1ShaderParameters = {};
	
#if !DISABLE_FIDELITYFX_CAS
	FFFXCAS              FFXCASParams = {};
#endif
	float                ResolutionScale = 1.0f;
	float                Sharpness = 0.8f;

	EDrawMode            DrawModeEnum = EDrawMode::LIT_AND_POSTPROCESSED;
	FVizualizationParams VizParams = {};

#if !DISABLE_FIDELITYFX_CAS
	bool bEnableCAS = false;
#endif
	bool bEnableGaussianBlur = false;


	EUpscalingAlgorithm UpscalingAlgorithmLastValue;
};
