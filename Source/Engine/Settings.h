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
enum EUpscalingAlgorithm : int
{
	NONE = 0,
	FIDELITYFX_SUPER_RESOLUTION_1,
	FIDELITYFX_SUPER_RESOLUTION_3,

	NUM_UPSCALING_ALGORITHMS
};
enum ESharpeningAlgorithm : int
{
	NO_SHARPENING = 0,
	FIDELITY_FX_CAS,
	FIDELITY_FX_RCAS,

	NUM_SHARPENING_ALGORITHMS
};
enum EAntiAliasingAlgorithm : int
{
	NO_ANTI_ALIASING = 0,
	MSAA4,
	FSR3_ANTI_ALIASING,

	NUM_ANTI_ALIASING_ALGORITHMS
};

struct FDisplaySettings
{
	unsigned short DisplayResolutionX = 1600;
	unsigned short DisplayResolutionY = 900;
	bool bVsync = false;
	bool bUseTripleBuffering = false;
};
struct FPostProcessingSettings
{
	using AMD_FSR1_Preset = AMD_FidelityFX_SuperResolution1::EPreset;
	using AMD_FSR3_Preset = AMD_FidelityFX_SuperResolution3::EPreset;

	// upscaling
	EUpscalingAlgorithm UpscalingAlgorithm = EUpscalingAlgorithm::FIDELITYFX_SUPER_RESOLUTION_3;
	AMD_FSR1_Preset FSR1UpscalingQualityEnum = AMD_FSR1_Preset::ULTRA_QUALITY;
	AMD_FSR3_Preset FSR3UpscalingQualityEnum = AMD_FSR3_Preset::NATIVE_AA;
	ESharpeningAlgorithm SharpeningAlgorithm = ESharpeningAlgorithm::FIDELITY_FX_RCAS;
	float Sharpness = 0.8f;

	bool EnableGaussianBlur = false;

	// tonemap
	float         UIHDRBrightness = 1.0f;
	float         DisplayReferenceBrightnessLevel = 200.0f;
	EColorSpace   ContentColorSpace = EColorSpace::REC_709;
	EDisplayCurve SDROutputDisplayCurve = EDisplayCurve::sRGB;
	EDisplayCurve HDROutputDisplayCurve = EDisplayCurve::Linear;
	bool          EnableGammaCorrection = true;
};
struct FDebugVisualizationSettings
{
	enum class EDrawMode : int
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

	EDrawMode DrawModeEnum = EDrawMode::LIT_AND_POSTPROCESSED;
	bool bUnpackNormals = false;
	float fInputStrength = 100.0f;
};
struct FRenderingSettings
{
	struct FFFX_SSSR_Options
	{
		bool  bEnableTemporalVarianceGuidedTracing = true;
		int   MaxTraversalIterations = 128;
		int   MostDetailedDepthHierarchyMipLevel = 0;
		int   MinTraversalOccupancy = 4;
		float DepthBufferThickness = 0.45f;
		float RoughnessThreshold = 0.2f;
		float TemporalStability = 0.25f;
		float TemporalVarianceThreshold = 0.0f;
		int   SamplesPerQuad = 1;
	};

	EAntiAliasingAlgorithm AntiAliasing = EAntiAliasingAlgorithm::MSAA4;
	EReflections Reflections = EReflections::REFLECTIONS_OFF;
	FFFX_SSSR_Options FFX_SSSR_Options;
	float RenderResolutionScale = 1.0f;
	short MaxFrameRate = -1; // -1: Auto (RefreshRate x 1.15) | 0: Unlimited | <int>: specified value
	short EnvironmentMapResolution = 256;
};

struct FGraphicsSettings
{
	FDisplaySettings Display;
	FRenderingSettings Rendering;
	FPostProcessingSettings PostProcessing;
	FDebugVisualizationSettings DebugVizualization;

	// command recording
	bool bEnableAsyncCopy = true;
	bool bEnableAsyncCompute = true;
	bool bUseSeparateSubmissionQueue = true;
	
	// =====================================================================================================================

	inline bool IsFSR1Enabled() const { return PostProcessing.UpscalingAlgorithm == EUpscalingAlgorithm::FIDELITYFX_SUPER_RESOLUTION_1; }
	inline bool IsFSR3Enabled() const { return PostProcessing.UpscalingAlgorithm == EUpscalingAlgorithm::FIDELITYFX_SUPER_RESOLUTION_3; }
	inline bool IsFFXCASEnabled() const { return false; } // TODO: handle RCAS (FSR1) vs CAS
	void Validate();
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
struct FEditorSettings
{
	float OutlineColor[4] = { 1.0f, 0.647f, 0.1f, 1.0f };
};
struct FEngineSettings
{
	FGraphicsSettings gfx;
	FEditorSettings Editor;

	FWindowSettings WndMain;
	FWindowSettings WndDebug;

	bool bShowDebugWindow = false;

	bool bAutomatedTestRun     = false;
	int NumAutomatedTestFrames = -1;
	
	char StartupScene[512];
};