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

#include "Renderer/Rendering/PostProcess/Upscaling.h"

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 
// GRAPHICS SETTINGS
// 
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
enum EDisplayMode : unsigned char
{
	WINDOWED = 0,
	BORDERLESS_FULLSCREEN,
	EXCLUSIVE_FULLSCREEN,

	NUM_DISPLAY_MODES
};
enum EReflections : unsigned char
{
	REFLECTIONS_OFF,
	SCREEN_SPACE_REFLECTIONS__FFX,
	RAY_TRACED_REFLECTIONS,

	NUM_REFLECTION_SETTINGS
};
enum EUpscalingAlgorithm : unsigned char
{
	NONE = 0,
	FIDELITYFX_SUPER_RESOLUTION_1,
	FIDELITYFX_SUPER_RESOLUTION_3,

	NUM_UPSCALING_ALGORITHMS
};
enum ESharpeningAlgorithm : unsigned char
{
	NO_SHARPENING = 0,
	FIDELITYFX_CAS,

	NUM_SHARPENING_ALGORITHMS
};
enum EAntiAliasingAlgorithm : unsigned char
{
	NO_ANTI_ALIASING = 0,
	MSAA4,
	FSR3_ANTI_ALIASING,

	NUM_ANTI_ALIASING_ALGORITHMS
};

struct FGraphicsSettings
{
	using AMD_FSR1_Preset = AMD_FidelityFX_SuperResolution1::EPreset;
	using AMD_FSR3_Preset = AMD_FidelityFX_SuperResolution3::EPreset;

	// display
	unsigned short DisplayResolutionX = 1600;
	unsigned short DisplayResolutionY = 900;
	bool bVsync              = false;
	bool bUseTripleBuffering = false;

	// rendering
	EAntiAliasingAlgorithm AntiAliasing = EAntiAliasingAlgorithm::MSAA4;
	EReflections Reflections = EReflections::REFLECTIONS_OFF;
	float RenderResolutionScale = 1.0f;
	short MaxFrameRate = -1; // -1: Auto (RefreshRate x 1.15) | 0: Unlimited | <int>: specified value
	short EnvironmentMapResolution = 256;

	// post processing
	EUpscalingAlgorithm UpscalingAlgorithm = EUpscalingAlgorithm::NONE;
	AMD_FSR1_Preset FSR1UpscalingQualityEnum = AMD_FSR1_Preset::ULTRA_QUALITY;
	AMD_FSR3_Preset FSR3UpscalingQualityEnum = AMD_FSR3_Preset::NATIVE_AA;
	float Sharpness = 0.8f;

	// tonemap
	float         UIHDRBrightness = 1.0f;
	float         DisplayReferenceBrightnessLevel = 200.0f;
	EColorSpace   ContentColorSpace = EColorSpace::REC_709;
	EDisplayCurve OutputDisplayCurve = EDisplayCurve::sRGB;
	bool          EnableGammaCorrection = true;

	// command recording
	bool bEnableAsyncCopy = true;
	bool bEnableAsyncCompute = true;
	bool bUseSeparateSubmissionQueue = true;
	
	// =====================================================================================================================

	inline bool IsFSR1Enabled() const { return UpscalingAlgorithm == EUpscalingAlgorithm::FIDELITYFX_SUPER_RESOLUTION_1; }
	inline bool IsFSR3Enabled() const { return UpscalingAlgorithm == EUpscalingAlgorithm::FIDELITYFX_SUPER_RESOLUTION_3; }
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 
// WINDOW SETTINGS
// 
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
struct FWindowSettings
{
	int Width                 = -1;
	int Height                = -1;
	EDisplayMode DisplayMode  = EDisplayMode::WINDOWED;
	unsigned PreferredDisplay = 0;
	char Title[64]            = "";
	bool bEnableHDR           = false;

	inline bool IsDisplayModeFullscreen() const { return DisplayMode == EDisplayMode::EXCLUSIVE_FULLSCREEN || DisplayMode == EDisplayMode::BORDERLESS_FULLSCREEN; }
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// 
// ENGINE SETTINGS
// 
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
struct FEngineSettings
{
	FGraphicsSettings gfx;

	FWindowSettings WndMain;
	FWindowSettings WndDebug;

	bool bShowDebugWindow = false;

	bool bAutomatedTestRun     = false;
	int NumAutomatedTestFrames = -1;
	
	char StartupScene[512];
};