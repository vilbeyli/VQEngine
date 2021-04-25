// Modifications Copyright © 2020. Advanced Micro Devices, Inc. All Rights Reserved.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2016, Intel Corporation
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of
// the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File changes (yyyy-mm-dd)
// 2016-09-07: filip.strugar@intel.com: first commit
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "ffx_cacao.h"
#include "ffx_cacao_defines.h"

#include <assert.h>
#include <math.h>   // cos, sin
#include <string.h> // memcpy
#include <stdio.h>  // snprintf

#ifdef FFX_CACAO_ENABLE_D3D12
#include <d3dx12.h>
#endif

// Define symbol to enable DirectX debug markers created using Cauldron
// #define FFX_CACAO_ENABLE_CAULDRON_DEBUG

#define FFX_CACAO_ASSERT(exp) assert(exp)
#define FFX_CACAO_ARRAY_SIZE(xs) (sizeof(xs)/sizeof(xs[0]))
#define FFX_CACAO_COS(x) cosf(x)
#define FFX_CACAO_SIN(x) sinf(x)
#define FFX_CACAO_MIN(x, y) (((x) < (y)) ? (x) : (y))
#define FFX_CACAO_MAX(x, y) (((x) > (y)) ? (x) : (y))
#define FFX_CACAO_CLAMP(value, lower, upper) FFX_CACAO_MIN(FFX_CACAO_MAX(value, lower), upper)
#define FFX_CACAO_OFFSET_OF(T, member) (size_t)(&(((T*)0)->member))

#ifdef FFX_CACAO_ENABLE_D3D12
#include "PrecompiledShadersDXIL/CACAOPrepareDownsampledDepthsHalf.h"
#include "PrecompiledShadersDXIL/CACAOPrepareNativeDepthsHalf.h"

#include "PrecompiledShadersDXIL/CACAOPrepareDownsampledDepthsAndMips.h"
#include "PrecompiledShadersDXIL/CACAOPrepareNativeDepthsAndMips.h"

#include "PrecompiledShadersDXIL/CACAOPrepareDownsampledNormals.h"
#include "PrecompiledShadersDXIL/CACAOPrepareNativeNormals.h"

#include "PrecompiledShadersDXIL/CACAOPrepareDownsampledNormalsFromInputNormals.h"
#include "PrecompiledShadersDXIL/CACAOPrepareNativeNormalsFromInputNormals.h"

#include "PrecompiledShadersDXIL/CACAOPrepareDownsampledDepths.h"
#include "PrecompiledShadersDXIL/CACAOPrepareNativeDepths.h"

#include "PrecompiledShadersDXIL/CACAOGenerateQ0.h"
#include "PrecompiledShadersDXIL/CACAOGenerateQ1.h"
#include "PrecompiledShadersDXIL/CACAOGenerateQ2.h"
#include "PrecompiledShadersDXIL/CACAOGenerateQ3.h"
#include "PrecompiledShadersDXIL/CACAOGenerateQ3Base.h"

#include "PrecompiledShadersDXIL/CACAOGenerateImportanceMap.h"
#include "PrecompiledShadersDXIL/CACAOPostprocessImportanceMapA.h"
#include "PrecompiledShadersDXIL/CACAOPostprocessImportanceMapB.h"

#include "PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur1.h"
#include "PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur2.h"
#include "PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur3.h"
#include "PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur4.h"
#include "PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur5.h"
#include "PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur6.h"
#include "PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur7.h"
#include "PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur8.h"

#include "PrecompiledShadersDXIL/CACAOApply.h"
#include "PrecompiledShadersDXIL/CACAONonSmartApply.h"
#include "PrecompiledShadersDXIL/CACAONonSmartHalfApply.h"

#include "PrecompiledShadersDXIL/CACAOUpscaleBilateral5x5.h"
#include "PrecompiledShadersDXIL/CACAOUpscaleBilateral5x5Half.h"
#endif

#ifdef FFX_CACAO_ENABLE_VULKAN
// 16 bit versions
#include "PrecompiledShadersSPIRV/CACAOClearLoadCounter_16.h"

#include "PrecompiledShadersSPIRV/CACAOPrepareDownsampledDepthsHalf_16.h"
#include "PrecompiledShadersSPIRV/CACAOPrepareNativeDepthsHalf_16.h"

#include "PrecompiledShadersSPIRV/CACAOPrepareDownsampledDepthsAndMips_16.h"
#include "PrecompiledShadersSPIRV/CACAOPrepareNativeDepthsAndMips_16.h"

#include "PrecompiledShadersSPIRV/CACAOPrepareDownsampledNormals_16.h"
#include "PrecompiledShadersSPIRV/CACAOPrepareNativeNormals_16.h"

#include "PrecompiledShadersSPIRV/CACAOPrepareDownsampledNormalsFromInputNormals_16.h"
#include "PrecompiledShadersSPIRV/CACAOPrepareNativeNormalsFromInputNormals_16.h"

#include "PrecompiledShadersSPIRV/CACAOPrepareDownsampledDepths_16.h"
#include "PrecompiledShadersSPIRV/CACAOPrepareNativeDepths_16.h"

#include "PrecompiledShadersSPIRV/CACAOGenerateQ0_16.h"
#include "PrecompiledShadersSPIRV/CACAOGenerateQ1_16.h"
#include "PrecompiledShadersSPIRV/CACAOGenerateQ2_16.h"
#include "PrecompiledShadersSPIRV/CACAOGenerateQ3_16.h"
#include "PrecompiledShadersSPIRV/CACAOGenerateQ3Base_16.h"

#include "PrecompiledShadersSPIRV/CACAOGenerateImportanceMap_16.h"
#include "PrecompiledShadersSPIRV/CACAOPostprocessImportanceMapA_16.h"
#include "PrecompiledShadersSPIRV/CACAOPostprocessImportanceMapB_16.h"

#include "PrecompiledShadersSPIRV/CACAOEdgeSensitiveBlur1_16.h"
#include "PrecompiledShadersSPIRV/CACAOEdgeSensitiveBlur2_16.h"
#include "PrecompiledShadersSPIRV/CACAOEdgeSensitiveBlur3_16.h"
#include "PrecompiledShadersSPIRV/CACAOEdgeSensitiveBlur4_16.h"
#include "PrecompiledShadersSPIRV/CACAOEdgeSensitiveBlur5_16.h"
#include "PrecompiledShadersSPIRV/CACAOEdgeSensitiveBlur6_16.h"
#include "PrecompiledShadersSPIRV/CACAOEdgeSensitiveBlur7_16.h"
#include "PrecompiledShadersSPIRV/CACAOEdgeSensitiveBlur8_16.h"

#include "PrecompiledShadersSPIRV/CACAOApply_16.h"
#include "PrecompiledShadersSPIRV/CACAONonSmartApply_16.h"
#include "PrecompiledShadersSPIRV/CACAONonSmartHalfApply_16.h"

#include "PrecompiledShadersSPIRV/CACAOUpscaleBilateral5x5_16.h"
#include "PrecompiledShadersSPIRV/CACAOUpscaleBilateral5x5Half_16.h"

// 32 bit versions
#include "PrecompiledShadersSPIRV/CACAOClearLoadCounter_32.h"

#include "PrecompiledShadersSPIRV/CACAOPrepareDownsampledDepthsHalf_32.h"
#include "PrecompiledShadersSPIRV/CACAOPrepareNativeDepthsHalf_32.h"

#include "PrecompiledShadersSPIRV/CACAOPrepareDownsampledDepthsAndMips_32.h"
#include "PrecompiledShadersSPIRV/CACAOPrepareNativeDepthsAndMips_32.h"

#include "PrecompiledShadersSPIRV/CACAOPrepareDownsampledNormals_32.h"
#include "PrecompiledShadersSPIRV/CACAOPrepareNativeNormals_32.h"

#include "PrecompiledShadersSPIRV/CACAOPrepareDownsampledNormalsFromInputNormals_32.h"
#include "PrecompiledShadersSPIRV/CACAOPrepareNativeNormalsFromInputNormals_32.h"

#include "PrecompiledShadersSPIRV/CACAOPrepareDownsampledDepths_32.h"
#include "PrecompiledShadersSPIRV/CACAOPrepareNativeDepths_32.h"

#include "PrecompiledShadersSPIRV/CACAOGenerateQ0_32.h"
#include "PrecompiledShadersSPIRV/CACAOGenerateQ1_32.h"
#include "PrecompiledShadersSPIRV/CACAOGenerateQ2_32.h"
#include "PrecompiledShadersSPIRV/CACAOGenerateQ3_32.h"
#include "PrecompiledShadersSPIRV/CACAOGenerateQ3Base_32.h"

#include "PrecompiledShadersSPIRV/CACAOGenerateImportanceMap_32.h"
#include "PrecompiledShadersSPIRV/CACAOPostprocessImportanceMapA_32.h"
#include "PrecompiledShadersSPIRV/CACAOPostprocessImportanceMapB_32.h"

#include "PrecompiledShadersSPIRV/CACAOEdgeSensitiveBlur1_32.h"
#include "PrecompiledShadersSPIRV/CACAOEdgeSensitiveBlur2_32.h"
#include "PrecompiledShadersSPIRV/CACAOEdgeSensitiveBlur3_32.h"
#include "PrecompiledShadersSPIRV/CACAOEdgeSensitiveBlur4_32.h"
#include "PrecompiledShadersSPIRV/CACAOEdgeSensitiveBlur5_32.h"
#include "PrecompiledShadersSPIRV/CACAOEdgeSensitiveBlur6_32.h"
#include "PrecompiledShadersSPIRV/CACAOEdgeSensitiveBlur7_32.h"
#include "PrecompiledShadersSPIRV/CACAOEdgeSensitiveBlur8_32.h"

#include "PrecompiledShadersSPIRV/CACAOApply_32.h"
#include "PrecompiledShadersSPIRV/CACAONonSmartApply_32.h"
#include "PrecompiledShadersSPIRV/CACAONonSmartHalfApply_32.h"

#include "PrecompiledShadersSPIRV/CACAOUpscaleBilateral5x5_32.h"
#include "PrecompiledShadersSPIRV/CACAOUpscaleBilateral5x5Half_32.h"
#endif

#define MATRIX_ROW_MAJOR_ORDER 1
#define MAX_BLUR_PASSES 8

#ifdef FFX_CACAO_ENABLE_CAULDRON_DEBUG
#include <base/UserMarkers.h>

#define USER_MARKER(name) CAULDRON_DX12::UserMarker __marker(commandList, name)
#else
#define USER_MARKER(name)
#endif

typedef struct FfxCacaoConstants {
	float                   DepthUnpackConsts[2];
	float                   CameraTanHalfFOV[2];

	float                   NDCToViewMul[2];
	float                   NDCToViewAdd[2];

	float                   DepthBufferUVToViewMul[2];
	float                   DepthBufferUVToViewAdd[2];

	float                   EffectRadius;                           // world (viewspace) maximum size of the shadow
	float                   EffectShadowStrength;                   // global strength of the effect (0 - 5)
	float                   EffectShadowPow;
	float                   EffectShadowClamp;

	float                   EffectFadeOutMul;                       // effect fade out from distance (ex. 25)
	float                   EffectFadeOutAdd;                       // effect fade out to distance   (ex. 100)
	float                   EffectHorizonAngleThreshold;            // limit errors on slopes and caused by insufficient geometry tessellation (0.05 to 0.5)
	float                   EffectSamplingRadiusNearLimitRec;          // if viewspace pixel closer than this, don't enlarge shadow sampling radius anymore (makes no sense to grow beyond some distance, not enough samples to cover everything, so just limit the shadow growth; could be SSAOSettingsFadeOutFrom * 0.1 or less)

	float                   DepthPrecisionOffsetMod;
	float                   NegRecEffectRadius;                     // -1.0 / EffectRadius
	float                   LoadCounterAvgDiv;                      // 1.0 / ( halfDepthMip[SSAO_DEPTH_MIP_LEVELS-1].sizeX * halfDepthMip[SSAO_DEPTH_MIP_LEVELS-1].sizeY )
	float                   AdaptiveSampleCountLimit;

	float                   InvSharpness;
	int                     PassIndex;
	float                   BilateralSigmaSquared;
	float                   BilateralSimilarityDistanceSigma;

	float                   PatternRotScaleMatrices[5][4];

	float                   NormalsUnpackMul;
	float                   NormalsUnpackAdd;
	float                   DetailAOStrength;
	float                   Dummy0;

	float                   SSAOBufferDimensions[2];
	float                   SSAOBufferInverseDimensions[2];

	float                   DepthBufferDimensions[2];
	float                   DepthBufferInverseDimensions[2];

	int                     DepthBufferOffset[2];
	float                   PerPassFullResUVOffset[2];

	float                   InputOutputBufferDimensions[2];
	float                   InputOutputBufferInverseDimensions[2];

	float                   ImportanceMapDimensions[2];
	float                   ImportanceMapInverseDimensions[2];

	float                   DeinterleavedDepthBufferDimensions[2];
	float                   DeinterleavedDepthBufferInverseDimensions[2];

	float                   DeinterleavedDepthBufferOffset[2];
	float                   DeinterleavedDepthBufferNormalisedOffset[2];

	FfxCacaoMatrix4x4       NormalsWorldToViewspaceMatrix;
} FfxCacaoConstants;

typedef struct ScreenSizeInfo {
	uint32_t width;
	uint32_t height;
	uint32_t halfWidth;
	uint32_t halfHeight;
	uint32_t quarterWidth;
	uint32_t quarterHeight;
	uint32_t eighthWidth;
	uint32_t eighthHeight;
	uint32_t depthBufferWidth;
	uint32_t depthBufferHeight;
	uint32_t depthBufferHalfWidth;
	uint32_t depthBufferHalfHeight;
	uint32_t depthBufferQuarterWidth;
	uint32_t depthBufferQuarterHeight;
	uint32_t depthBufferOffsetX;
	uint32_t depthBufferOffsetY;
	uint32_t depthBufferHalfOffsetX;
	uint32_t depthBufferHalfOffsetY;
} ScreenSizeInfo;

typedef struct BufferSizeInfo {
	uint32_t inputOutputBufferWidth;
	uint32_t inputOutputBufferHeight;

	uint32_t ssaoBufferWidth;
	uint32_t ssaoBufferHeight;

	uint32_t depthBufferXOffset;
	uint32_t depthBufferYOffset;

	uint32_t depthBufferWidth;
	uint32_t depthBufferHeight;

	uint32_t deinterleavedDepthBufferXOffset;
	uint32_t deinterleavedDepthBufferYOffset;

	uint32_t deinterleavedDepthBufferWidth;
	uint32_t deinterleavedDepthBufferHeight;

	uint32_t importanceMapWidth;
	uint32_t importanceMapHeight;
} BufferSizeInfo;

static const FfxCacaoMatrix4x4 FFX_CACAO_IDENTITY_MATRIX = {
	{ { 1.0f, 0.0f, 0.0f, 0.0f },
	  { 0.0f, 1.0f, 0.0f, 0.0f },
	  { 0.0f, 0.0f, 1.0f, 0.0f },
	  { 0.0f, 0.0f, 0.0f, 1.0f } }
};

inline static uint32_t dispatchSize(uint32_t tileSize, uint32_t totalSize)
{
	return (totalSize + tileSize - 1) / tileSize;
}

static void updateConstants(FfxCacaoConstants* consts, FfxCacaoSettings* settings, BufferSizeInfo* bufferSizeInfo, const FfxCacaoMatrix4x4* proj, const FfxCacaoMatrix4x4* normalsToView)
{
	consts->BilateralSigmaSquared = settings->bilateralSigmaSquared;
	consts->BilateralSimilarityDistanceSigma = settings->bilateralSimilarityDistanceSigma;

	if (settings->generateNormals)
	{
		consts->NormalsWorldToViewspaceMatrix = FFX_CACAO_IDENTITY_MATRIX;
	}
	else
	{
		consts->NormalsWorldToViewspaceMatrix = *normalsToView;
	}

	// used to get average load per pixel; 9.0 is there to compensate for only doing every 9th InterlockedAdd in PSPostprocessImportanceMapB for performance reasons
	consts->LoadCounterAvgDiv = 9.0f / (float)(bufferSizeInfo->importanceMapWidth * bufferSizeInfo->importanceMapHeight * 255.0);

	float depthLinearizeMul = (MATRIX_ROW_MAJOR_ORDER) ? (-proj->elements[3][2]) : (-proj->elements[2][3]);           // float depthLinearizeMul = ( clipFar * clipNear ) / ( clipFar - clipNear );
	float depthLinearizeAdd = (MATRIX_ROW_MAJOR_ORDER) ? (proj->elements[2][2]) : (proj->elements[2][2]);           // float depthLinearizeAdd = clipFar / ( clipFar - clipNear );
	// correct the handedness issue. need to make sure this below is correct, but I think it is.
	if (depthLinearizeMul * depthLinearizeAdd < 0)
		depthLinearizeAdd = -depthLinearizeAdd;
	consts->DepthUnpackConsts[0] = depthLinearizeMul;
	consts->DepthUnpackConsts[1] = depthLinearizeAdd;

	float tanHalfFOVY = 1.0f / proj->elements[1][1];    // = tanf( drawContext.Camera.GetYFOV( ) * 0.5f );
	float tanHalfFOVX = 1.0F / proj->elements[0][0];    // = tanHalfFOVY * drawContext.Camera.GetAspect( );
	consts->CameraTanHalfFOV[0] = tanHalfFOVX;
	consts->CameraTanHalfFOV[1] = tanHalfFOVY;

	consts->NDCToViewMul[0] = consts->CameraTanHalfFOV[0] * 2.0f;
	consts->NDCToViewMul[1] = consts->CameraTanHalfFOV[1] * -2.0f;
	consts->NDCToViewAdd[0] = consts->CameraTanHalfFOV[0] * -1.0f;
	consts->NDCToViewAdd[1] = consts->CameraTanHalfFOV[1] * 1.0f;

	float ratio = ((float)bufferSizeInfo->inputOutputBufferWidth) / ((float)bufferSizeInfo->depthBufferWidth);
	float border = (1.0f - ratio) / 2.0f;
	for (int i = 0; i < 2; ++i)
	{
		consts->DepthBufferUVToViewMul[i] = consts->NDCToViewMul[i] / ratio;
		consts->DepthBufferUVToViewAdd[i] = consts->NDCToViewAdd[i] - consts->NDCToViewMul[i] * border / ratio;
	}

	consts->EffectRadius = FFX_CACAO_CLAMP(settings->radius, 0.0f, 100000.0f);
	consts->EffectShadowStrength = FFX_CACAO_CLAMP(settings->shadowMultiplier * 4.3f, 0.0f, 10.0f);
	consts->EffectShadowPow = FFX_CACAO_CLAMP(settings->shadowPower, 0.0f, 10.0f);
	consts->EffectShadowClamp = FFX_CACAO_CLAMP(settings->shadowClamp, 0.0f, 1.0f);
	consts->EffectFadeOutMul = -1.0f / (settings->fadeOutTo - settings->fadeOutFrom);
	consts->EffectFadeOutAdd = settings->fadeOutFrom / (settings->fadeOutTo - settings->fadeOutFrom) + 1.0f;
	consts->EffectHorizonAngleThreshold = FFX_CACAO_CLAMP(settings->horizonAngleThreshold, 0.0f, 1.0f);

	// 1.2 seems to be around the best trade off - 1.0 means on-screen radius will stop/slow growing when the camera is at 1.0 distance, so, depending on FOV, basically filling up most of the screen
	// This setting is viewspace-dependent and not screen size dependent intentionally, so that when you change FOV the effect stays (relatively) similar.
	float effectSamplingRadiusNearLimit = (settings->radius * 1.2f);

	// if the depth precision is switched to 32bit float, this can be set to something closer to 1 (0.9999 is fine)
	consts->DepthPrecisionOffsetMod = 0.9992f;

	// consts->RadiusDistanceScalingFunctionPow     = 1.0f - CLAMP( m_settings.RadiusDistanceScalingFunction, 0.0f, 1.0f );


	// Special settings for lowest quality level - just nerf the effect a tiny bit
	if (settings->qualityLevel <= FFX_CACAO_QUALITY_LOW)
	{
		//consts.EffectShadowStrength     *= 0.9f;
		effectSamplingRadiusNearLimit *= 1.50f;

		if (settings->qualityLevel == FFX_CACAO_QUALITY_LOWEST)
			consts->EffectRadius *= 0.8f;
	}

	effectSamplingRadiusNearLimit /= tanHalfFOVY; // to keep the effect same regardless of FOV

	consts->EffectSamplingRadiusNearLimitRec = 1.0f / effectSamplingRadiusNearLimit;

	consts->AdaptiveSampleCountLimit = settings->adaptiveQualityLimit;

	consts->NegRecEffectRadius = -1.0f / consts->EffectRadius;

	consts->InvSharpness = FFX_CACAO_CLAMP(1.0f - settings->sharpness, 0.0f, 1.0f);

	consts->DetailAOStrength = settings->detailShadowStrength;

	// set buffer size constants.
	consts->SSAOBufferDimensions[0] = (float)bufferSizeInfo->ssaoBufferWidth;
	consts->SSAOBufferDimensions[1] = (float)bufferSizeInfo->ssaoBufferHeight;
	consts->SSAOBufferInverseDimensions[0] = 1.0f / ((float)bufferSizeInfo->ssaoBufferWidth);
	consts->SSAOBufferInverseDimensions[1] = 1.0f / ((float)bufferSizeInfo->ssaoBufferHeight);

	consts->DepthBufferDimensions[0] = (float)bufferSizeInfo->depthBufferWidth;
	consts->DepthBufferDimensions[1] = (float)bufferSizeInfo->depthBufferHeight;
	consts->DepthBufferInverseDimensions[0] = 1.0f / ((float)bufferSizeInfo->depthBufferWidth);
	consts->DepthBufferInverseDimensions[1] = 1.0f / ((float)bufferSizeInfo->depthBufferHeight);

	consts->DepthBufferOffset[0] = bufferSizeInfo->depthBufferXOffset;
	consts->DepthBufferOffset[1] = bufferSizeInfo->depthBufferYOffset;

	consts->InputOutputBufferDimensions[0] = (float)bufferSizeInfo->inputOutputBufferWidth;
	consts->InputOutputBufferDimensions[1] = (float)bufferSizeInfo->inputOutputBufferHeight;
	consts->InputOutputBufferInverseDimensions[0] = 1.0f / ((float)bufferSizeInfo->inputOutputBufferWidth);
	consts->InputOutputBufferInverseDimensions[1] = 1.0f / ((float)bufferSizeInfo->inputOutputBufferHeight);

	consts->ImportanceMapDimensions[0] = (float)bufferSizeInfo->importanceMapWidth;
	consts->ImportanceMapDimensions[1] = (float)bufferSizeInfo->importanceMapHeight;
	consts->ImportanceMapInverseDimensions[0] = 1.0f / ((float)bufferSizeInfo->importanceMapWidth);
	consts->ImportanceMapInverseDimensions[1] = 1.0f / ((float)bufferSizeInfo->importanceMapHeight);

	consts->DeinterleavedDepthBufferDimensions[0] = (float)bufferSizeInfo->deinterleavedDepthBufferWidth;
	consts->DeinterleavedDepthBufferDimensions[1] = (float)bufferSizeInfo->deinterleavedDepthBufferHeight;
	consts->DeinterleavedDepthBufferInverseDimensions[0] = 1.0f / ((float)bufferSizeInfo->deinterleavedDepthBufferWidth);
	consts->DeinterleavedDepthBufferInverseDimensions[1] = 1.0f / ((float)bufferSizeInfo->deinterleavedDepthBufferHeight);

	consts->DeinterleavedDepthBufferOffset[0] = (float)bufferSizeInfo->deinterleavedDepthBufferXOffset;
	consts->DeinterleavedDepthBufferOffset[1] = (float)bufferSizeInfo->deinterleavedDepthBufferYOffset;
	consts->DeinterleavedDepthBufferNormalisedOffset[0] = ((float)bufferSizeInfo->deinterleavedDepthBufferXOffset) / ((float)bufferSizeInfo->deinterleavedDepthBufferWidth);
	consts->DeinterleavedDepthBufferNormalisedOffset[1] = ((float)bufferSizeInfo->deinterleavedDepthBufferYOffset) / ((float)bufferSizeInfo->deinterleavedDepthBufferHeight);

	if (!settings->generateNormals)
	{
		consts->NormalsUnpackMul = 2.0f;  // inputs->NormalsUnpackMul;
		consts->NormalsUnpackAdd = -1.0f; // inputs->NormalsUnpackAdd;
	}
	else
	{
		consts->NormalsUnpackMul = 2.0f;
		consts->NormalsUnpackAdd = -1.0f;
	}
}

static void updatePerPassConstants(FfxCacaoConstants* consts, FfxCacaoSettings* settings, BufferSizeInfo* bufferSizeInfo, int pass)
{
	consts->PerPassFullResUVOffset[0] = ((float)(pass % 2)) / (float)bufferSizeInfo->ssaoBufferWidth;
	consts->PerPassFullResUVOffset[1] = ((float)(pass / 2)) / (float)bufferSizeInfo->ssaoBufferHeight;

	consts->PassIndex = pass;

	float additionalAngleOffset = settings->temporalSupersamplingAngleOffset;  // if using temporal supersampling approach (like "Progressive Rendering Using Multi-frame Sampling" from GPU Pro 7, etc.)
	float additionalRadiusScale = settings->temporalSupersamplingRadiusOffset; // if using temporal supersampling approach (like "Progressive Rendering Using Multi-frame Sampling" from GPU Pro 7, etc.)
	const int subPassCount = 5;
	for (int subPass = 0; subPass < subPassCount; subPass++)
	{
		int a = pass;
		int b = subPass;

		int spmap[5]{ 0, 1, 4, 3, 2 };
		b = spmap[subPass];

		float ca, sa;
		float angle0 = ((float)a + (float)b / (float)subPassCount) * (3.1415926535897932384626433832795f) * 0.5f;
		// angle0 += additionalAngleOffset;

		ca = FFX_CACAO_COS(angle0);
		sa = FFX_CACAO_SIN(angle0);

		float scale = 1.0f + (a - 1.5f + (b - (subPassCount - 1.0f) * 0.5f) / (float)subPassCount) * 0.07f;
		// scale *= additionalRadiusScale;

		consts->PatternRotScaleMatrices[subPass][0] = scale * ca;
		consts->PatternRotScaleMatrices[subPass][1] = scale * -sa;
		consts->PatternRotScaleMatrices[subPass][2] = -scale * sa;
		consts->PatternRotScaleMatrices[subPass][3] = -scale * ca;
	}
}

#ifdef FFX_CACAO_ENABLE_PROFILING
// TIMESTAMP(name)
#define TIMESTAMPS \
	TIMESTAMP(BEGIN) \
	TIMESTAMP(PREPARE) \
	TIMESTAMP(BASE_SSAO_PASS) \
	TIMESTAMP(IMPORTANCE_MAP) \
	TIMESTAMP(GENERATE_SSAO) \
	TIMESTAMP(EDGE_SENSITIVE_BLUR) \
	TIMESTAMP(BILATERAL_UPSAMPLE) \
	TIMESTAMP(APPLY)

typedef enum TimestampID {
#define TIMESTAMP(name) TIMESTAMP_##name,
	TIMESTAMPS
#undef TIMESTAMP
	NUM_TIMESTAMPS
} TimestampID;

static const char *TIMESTAMP_NAMES[NUM_TIMESTAMPS] = {
#define TIMESTAMP(name) "FFX_CACAO_" #name,
	TIMESTAMPS
#undef TIMESTAMP
};

#define NUM_TIMESTAMP_BUFFERS 5
#endif

// =================================================================================
// DirectX 12
// =================================================================================

#ifdef FFX_CACAO_ENABLE_D3D12

static inline FfxCacaoStatus hresultToFfxCacaoStatus(HRESULT hr)
{
	switch (hr)
	{
	case E_FAIL: return FFX_CACAO_STATUS_FAILED;
	case E_INVALIDARG: return FFX_CACAO_STATUS_INVALID_ARGUMENT;
	case E_OUTOFMEMORY: return FFX_CACAO_STATUS_OUT_OF_MEMORY;
	case E_NOTIMPL: return FFX_CACAO_STATUS_INVALID_ARGUMENT;
	case S_FALSE: return FFX_CACAO_STATUS_OK;
	case S_OK: return FFX_CACAO_STATUS_OK;
	default: return FFX_CACAO_STATUS_FAILED;
	}
}

static inline void SetName(ID3D12Object* obj, const char* name)
{
	if (name == NULL)
	{
		return;
	}

	FFX_CACAO_ASSERT(obj != NULL);
	wchar_t buffer[1024];
	swprintf(buffer, FFX_CACAO_ARRAY_SIZE(buffer), L"%S", name);
	obj->SetName(buffer);
}

static inline size_t AlignOffset(size_t uOffset, size_t uAlign)
{
	return ((uOffset + (uAlign - 1)) & ~(uAlign - 1));
}

static size_t GetPixelByteSize(DXGI_FORMAT fmt)
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
		FFX_CACAO_ASSERT(0);
		break;
	}
	return 0;
}

// =================================================================================================
// GpuTimer implementation
// =================================================================================================

#ifdef FFX_CACAO_ENABLE_PROFILING
#define GPU_TIMER_MAX_VALUES_PER_FRAME (FFX_CACAO_ARRAY_SIZE(((FfxCacaoDetailedTiming*)0)->timestamps))

typedef struct D3D12Timestamp {
	TimestampID timestampID;
	uint64_t    value;
} D3D12Timestamp;

typedef struct GpuTimer {
	ID3D12Resource  *buffer;
	ID3D12QueryHeap *queryHeap;
	uint32_t         currentFrame;
	uint32_t         collectFrame;
	struct {
		uint32_t len;
		D3D12Timestamp timestamps[NUM_TIMESTAMPS];
	} timestampBuffers[NUM_TIMESTAMP_BUFFERS];

} GpuTimer;

static FfxCacaoStatus gpuTimerInit(GpuTimer* gpuTimer, ID3D12Device* device)
{
	memset(gpuTimer, 0, sizeof(*gpuTimer));

	D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
	queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	queryHeapDesc.Count = GPU_TIMER_MAX_VALUES_PER_FRAME * NUM_TIMESTAMP_BUFFERS;
	queryHeapDesc.NodeMask = 0;
	HRESULT hr = device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&gpuTimer->queryHeap));
	if (FAILED(hr))
	{
		return hresultToFfxCacaoStatus(hr);
	}

	hr = device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(sizeof(uint64_t) * NUM_TIMESTAMP_BUFFERS * GPU_TIMER_MAX_VALUES_PER_FRAME),
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&gpuTimer->buffer));
	if (FAILED(hr))
	{
		FFX_CACAO_ASSERT(gpuTimer->queryHeap);
		gpuTimer->queryHeap->Release();
		return hresultToFfxCacaoStatus(hr);
	}

	SetName(gpuTimer->buffer, "CACAO::GPUTimer::buffer");

	return FFX_CACAO_STATUS_OK;
}

static void gpuTimerDestroy(GpuTimer* gpuTimer)
{
	FFX_CACAO_ASSERT(gpuTimer->buffer);
	FFX_CACAO_ASSERT(gpuTimer->queryHeap);
	gpuTimer->buffer->Release();
	gpuTimer->queryHeap->Release();
}

static void gpuTimerStartFrame(GpuTimer* gpuTimer)
{
	uint32_t frame = gpuTimer->currentFrame = (gpuTimer->currentFrame + 1) % NUM_TIMESTAMP_BUFFERS;
	gpuTimer->timestampBuffers[frame].len = 0;

	uint32_t collectFrame = gpuTimer->collectFrame = (frame + 1) % NUM_TIMESTAMP_BUFFERS;

	uint32_t numMeasurements = gpuTimer->timestampBuffers[collectFrame].len;
	if (!numMeasurements)
	{
		return;
	}

	uint32_t start = GPU_TIMER_MAX_VALUES_PER_FRAME * collectFrame;
	uint32_t end = GPU_TIMER_MAX_VALUES_PER_FRAME * (collectFrame + 1);

	D3D12_RANGE readRange;
	readRange.Begin = start * sizeof(uint64_t);
	readRange.End = end * sizeof(uint64_t);
	uint64_t *timingsInTicks = NULL;
	gpuTimer->buffer->Map(0, &readRange, (void**)&timingsInTicks);

	for (uint32_t i = 0; i < numMeasurements; ++i)
	{
		gpuTimer->timestampBuffers[collectFrame].timestamps[i].value = timingsInTicks[start + i];
	}

	D3D12_RANGE writtenRange = {};
	writtenRange.Begin = 0;
	writtenRange.End = 0;
	gpuTimer->buffer->Unmap(0, &writtenRange);
}

static void gpuTimerGetTimestamp(GpuTimer* gpuTimer, ID3D12GraphicsCommandList* commandList, TimestampID timestampID)
{
	uint32_t frame = gpuTimer->currentFrame;
	uint32_t curTimestamp = gpuTimer->timestampBuffers[frame].len++;
	FFX_CACAO_ASSERT(curTimestamp < GPU_TIMER_MAX_VALUES_PER_FRAME);
	gpuTimer->timestampBuffers[frame].timestamps[curTimestamp].timestampID = timestampID;
	commandList->EndQuery(gpuTimer->queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, frame * GPU_TIMER_MAX_VALUES_PER_FRAME + curTimestamp);
}

static void gpuTimerEndFrame(GpuTimer* gpuTimer, ID3D12GraphicsCommandList* commandList)
{
	uint32_t frame = gpuTimer->currentFrame;
	uint32_t numTimestamps = gpuTimer->timestampBuffers[frame].len;
	commandList->ResolveQueryData(
		gpuTimer->queryHeap,
		D3D12_QUERY_TYPE_TIMESTAMP,
		frame * GPU_TIMER_MAX_VALUES_PER_FRAME,
		numTimestamps,
		gpuTimer->buffer,
		frame * GPU_TIMER_MAX_VALUES_PER_FRAME * sizeof(uint64_t));
}

static void gpuTimerCollectTimings(GpuTimer* gpuTimer, FfxCacaoDetailedTiming* timings)
{
	uint32_t frame = gpuTimer->collectFrame;
	uint32_t numTimestamps = timings->numTimestamps = gpuTimer->timestampBuffers[frame].len;

	uint64_t prevTimeTicks = gpuTimer->timestampBuffers[frame].timestamps[0].value;
	for (uint32_t i = 1; i < numTimestamps; ++i)
	{
		uint64_t thisTimeTicks = gpuTimer->timestampBuffers[frame].timestamps[i].value;
		FfxCacaoTimestamp *t = &timings->timestamps[i];
		t->label = TIMESTAMP_NAMES[gpuTimer->timestampBuffers[frame].timestamps[i].timestampID];
		t->ticks = thisTimeTicks - prevTimeTicks;
		prevTimeTicks = thisTimeTicks;
	}

	timings->timestamps[0].label = "FFX_CACAO_TOTAL";
	timings->timestamps[0].ticks = prevTimeTicks - gpuTimer->timestampBuffers[frame].timestamps[0].value;
}
#endif

// =================================================================================================
// CbvSrvUav implementation
// =================================================================================================

typedef struct CbvSrvUav {
	uint32_t                    size;
	uint32_t                    descriptorSize;
	D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor;
	D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptor;
	D3D12_CPU_DESCRIPTOR_HANDLE cpuVisibleCpuDescriptor;
} CbvSrvUav;

static D3D12_CPU_DESCRIPTOR_HANDLE cbvSrvUavGetCpu(CbvSrvUav* cbvSrvUav, uint32_t i)
{
	D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor = cbvSrvUav->cpuDescriptor;
	cpuDescriptor.ptr += i * cbvSrvUav->descriptorSize;
	return cpuDescriptor;
}

static D3D12_CPU_DESCRIPTOR_HANDLE cbvSrvUavGetCpuVisibleCpu(CbvSrvUav* cbvSrvUav, uint32_t i)
{
	D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor = cbvSrvUav->cpuVisibleCpuDescriptor;
	cpuDescriptor.ptr += i * cbvSrvUav->descriptorSize;
	return cpuDescriptor;
}

static D3D12_GPU_DESCRIPTOR_HANDLE cbvSrvUavGetGpu(CbvSrvUav* cbvSrvUav, uint32_t i)
{
	D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptor = cbvSrvUav->gpuDescriptor;
	gpuDescriptor.ptr += i * cbvSrvUav->descriptorSize;
	return gpuDescriptor;
}

// =================================================================================================
// CbvSrvUavHeap implementation
// =================================================================================================

typedef struct CbvSrvUavHeap {
	uint32_t              index;
	uint32_t              descriptorCount;
	uint32_t              descriptorElementSize;
	ID3D12DescriptorHeap *heap;
	ID3D12DescriptorHeap *cpuVisibleHeap;
} ResourceViewHeap;

static FfxCacaoStatus cbvSrvUavHeapInit(CbvSrvUavHeap* cbvSrvUavHeap, ID3D12Device* device, uint32_t descriptorCount)
{
	FFX_CACAO_ASSERT(cbvSrvUavHeap);
	FFX_CACAO_ASSERT(device);

	cbvSrvUavHeap->descriptorCount = descriptorCount;
	cbvSrvUavHeap->index = 0;

	cbvSrvUavHeap->descriptorElementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_DESCRIPTOR_HEAP_DESC descHeap;
	descHeap.NumDescriptors = descriptorCount;
	descHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	descHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	descHeap.NodeMask = 0;

	HRESULT hr = device->CreateDescriptorHeap(&descHeap, IID_PPV_ARGS(&cbvSrvUavHeap->heap));
	if (FAILED(hr))
	{
		return hresultToFfxCacaoStatus(hr);
	}

	SetName(cbvSrvUavHeap->heap, "FfxCacaoCbvSrvUavHeap");

	D3D12_DESCRIPTOR_HEAP_DESC cpuVisibleDescHeap;
	cpuVisibleDescHeap.NumDescriptors = descriptorCount;
	cpuVisibleDescHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cpuVisibleDescHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	cpuVisibleDescHeap.NodeMask = 0;

	hr = device->CreateDescriptorHeap(&cpuVisibleDescHeap, IID_PPV_ARGS(&cbvSrvUavHeap->cpuVisibleHeap));
	if (FAILED(hr))
	{
		FFX_CACAO_ASSERT(cbvSrvUavHeap->heap);
		cbvSrvUavHeap->heap->Release();
		return hresultToFfxCacaoStatus(hr);
	}

	SetName(cbvSrvUavHeap->cpuVisibleHeap, "FfxCacaoCbvSrvUavCpuVisibleHeap");
	return FFX_CACAO_STATUS_OK;
}

static void cbvSrvUavHeapDestroy(CbvSrvUavHeap* cbvSrvUavHeap)
{
	FFX_CACAO_ASSERT(cbvSrvUavHeap);
	FFX_CACAO_ASSERT(cbvSrvUavHeap->heap);
	FFX_CACAO_ASSERT(cbvSrvUavHeap->cpuVisibleHeap);
	cbvSrvUavHeap->heap->Release();
	cbvSrvUavHeap->cpuVisibleHeap->Release();
}

static void cbvSrvUavHeapAllocDescriptor(CbvSrvUavHeap* cbvSrvUavHeap, CbvSrvUav* cbvSrvUav, uint32_t size)
{
	FFX_CACAO_ASSERT(cbvSrvUavHeap);
	FFX_CACAO_ASSERT(cbvSrvUav);
	FFX_CACAO_ASSERT(cbvSrvUavHeap->index + size <= cbvSrvUavHeap->descriptorCount);

	D3D12_CPU_DESCRIPTOR_HANDLE cpuView = cbvSrvUavHeap->heap->GetCPUDescriptorHandleForHeapStart();
	cpuView.ptr += cbvSrvUavHeap->index * cbvSrvUavHeap->descriptorElementSize;

	D3D12_GPU_DESCRIPTOR_HANDLE gpuView = cbvSrvUavHeap->heap->GetGPUDescriptorHandleForHeapStart();
	gpuView.ptr += cbvSrvUavHeap->index * cbvSrvUavHeap->descriptorElementSize;

	D3D12_CPU_DESCRIPTOR_HANDLE cpuVisibleCpuView = cbvSrvUavHeap->cpuVisibleHeap->GetCPUDescriptorHandleForHeapStart();
	cpuVisibleCpuView.ptr += cbvSrvUavHeap->index * cbvSrvUavHeap->descriptorElementSize;

	cbvSrvUavHeap->index += size;

	cbvSrvUav->size = size;
	cbvSrvUav->descriptorSize = cbvSrvUavHeap->descriptorElementSize;
	cbvSrvUav->cpuDescriptor = cpuView;
	cbvSrvUav->gpuDescriptor = gpuView;
	cbvSrvUav->cpuVisibleCpuDescriptor = cpuVisibleCpuView;
}

// =================================================================================================
// ConstantBufferRing implementation
// =================================================================================================

typedef struct ConstantBufferRing {
	size_t             pageSize;
	size_t             totalSize;
	size_t             currentOffset;
	uint32_t           currentPage;
	uint32_t           numPages;
	char              *data;
	ID3D12Resource    *buffer;
} ConstantBufferRing;

static FfxCacaoStatus constantBufferRingInit(ConstantBufferRing* constantBufferRing, ID3D12Device* device, uint32_t numPages, size_t pageSize)
{
	FFX_CACAO_ASSERT(constantBufferRing);
	FFX_CACAO_ASSERT(device);

	pageSize = AlignOffset(pageSize, 256);
	size_t totalSize = numPages * pageSize;
	char *data = NULL;
	ID3D12Resource *buffer = NULL;

	HRESULT hr = device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(totalSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&buffer));
	if (FAILED(hr))
	{
		return hresultToFfxCacaoStatus(hr);
	}

	SetName(buffer, "DynamicBufferRing::m_pBuffer");

	buffer->Map(0, NULL, (void**)&data);

	constantBufferRing->pageSize = pageSize;
	constantBufferRing->totalSize = totalSize;
	constantBufferRing->currentOffset = 0;
	constantBufferRing->currentPage = 0;
	constantBufferRing->numPages = numPages;
	constantBufferRing->data = data;
	constantBufferRing->buffer = buffer;

	return FFX_CACAO_STATUS_OK;
}

static void constantBufferRingDestroy(ConstantBufferRing* constantBufferRing)
{
	FFX_CACAO_ASSERT(constantBufferRing);
	FFX_CACAO_ASSERT(constantBufferRing->buffer);
	constantBufferRing->buffer->Release();
}

static void constantBufferRingStartFrame(ConstantBufferRing* constantBufferRing)
{
	FFX_CACAO_ASSERT(constantBufferRing);
	constantBufferRing->currentPage = (constantBufferRing->currentPage + 1) % constantBufferRing->numPages;
	constantBufferRing->currentOffset = 0;
}

static void constantBufferRingAlloc(ConstantBufferRing* constantBufferRing, size_t size, void **data, D3D12_GPU_VIRTUAL_ADDRESS *bufferViewDesc)
{
	FFX_CACAO_ASSERT(constantBufferRing);
	size = AlignOffset(size, 256);
	FFX_CACAO_ASSERT(constantBufferRing->currentOffset + size <= constantBufferRing->pageSize);

	size_t memOffset = constantBufferRing->pageSize * constantBufferRing->currentPage + constantBufferRing->currentOffset;
	*data = constantBufferRing->data + memOffset;
	constantBufferRing->currentOffset += size;

	*bufferViewDesc = constantBufferRing->buffer->GetGPUVirtualAddress() + memOffset;
}

// =================================================================================================
// ComputeShader implementation
// =================================================================================================

typedef struct ComputeShader {
	ID3D12RootSignature  *rootSignature;
	ID3D12PipelineState  *pipelineState;
} ComputeShader;

static FfxCacaoStatus computeShaderInit(ComputeShader* computeShader, ID3D12Device* device, const char* name, const void* bytecode, size_t bytecodeLength, uint32_t uavTableSize, uint32_t srvTableSize, D3D12_STATIC_SAMPLER_DESC* staticSamplers, uint32_t numStaticSamplers)
{
	FFX_CACAO_ASSERT(computeShader);
	FFX_CACAO_ASSERT(device);
	FFX_CACAO_ASSERT(name);
	FFX_CACAO_ASSERT(bytecode);
	FFX_CACAO_ASSERT(staticSamplers);

	D3D12_SHADER_BYTECODE shaderByteCode = {};
	shaderByteCode.pShaderBytecode = bytecode;
	shaderByteCode.BytecodeLength = bytecodeLength;

	// Create root signature
	{
		CD3DX12_DESCRIPTOR_RANGE DescRange[4];
		CD3DX12_ROOT_PARAMETER RTSlot[4];

		// we'll always have a constant buffer
		int parameterCount = 0;
		DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
		RTSlot[parameterCount++].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

		// if we have a UAV table
		if (uavTableSize > 0)
		{
			DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, uavTableSize, 0);
			RTSlot[parameterCount].InitAsDescriptorTable(1, &DescRange[parameterCount], D3D12_SHADER_VISIBILITY_ALL);
			++parameterCount;
		}

		// if we have a SRV table
		if (srvTableSize > 0)
		{
			DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvTableSize, 0);
			RTSlot[parameterCount].InitAsDescriptorTable(1, &DescRange[parameterCount], D3D12_SHADER_VISIBILITY_ALL);
			++parameterCount;
		}

		// the root signature contains 3 slots to be used
		CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
		descRootSignature.NumParameters = parameterCount;
		descRootSignature.pParameters = RTSlot;
		descRootSignature.NumStaticSamplers = numStaticSamplers;
		descRootSignature.pStaticSamplers = staticSamplers;

		// deny uneccessary access to certain pipeline stages
		descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		ID3DBlob *outBlob, *errorBlob = NULL;

		HRESULT hr = D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &outBlob, &errorBlob);
		if (FAILED(hr))
		{
			return hresultToFfxCacaoStatus(hr);
		}

		if (errorBlob)
		{
			errorBlob->Release();
			if (outBlob)
			{
				outBlob->Release();
			}
			return FFX_CACAO_STATUS_FAILED;
		}

		hr = device->CreateRootSignature(0, outBlob->GetBufferPointer(), outBlob->GetBufferSize(), IID_PPV_ARGS(&computeShader->rootSignature));
		if (FAILED(hr))
		{
			outBlob->Release();
			return hresultToFfxCacaoStatus(hr);
		}

		char nameBuffer[1024] = "PostProcCS::m_pRootSignature::";
		strncat_s(nameBuffer, name, FFX_CACAO_ARRAY_SIZE(nameBuffer));
		SetName(computeShader->rootSignature, nameBuffer);

		outBlob->Release();
	}

	// Create pipeline state
	{
		D3D12_COMPUTE_PIPELINE_STATE_DESC descPso = {};
		descPso.CS = shaderByteCode;
		descPso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		descPso.pRootSignature = computeShader->rootSignature;
		descPso.NodeMask = 0;

		HRESULT hr = device->CreateComputePipelineState(&descPso, IID_PPV_ARGS(&computeShader->pipelineState));
		if (FAILED(hr))
		{
			computeShader->rootSignature->Release();
			return hresultToFfxCacaoStatus(hr);
		}

		char nameBuffer[1024] = "PostProcCS::m_pPipeline::";
		strncat_s(nameBuffer, name, FFX_CACAO_ARRAY_SIZE(nameBuffer));
		SetName(computeShader->rootSignature, nameBuffer);
	}

	return FFX_CACAO_STATUS_OK;
}

static void computeShaderDestroy(ComputeShader* computeShader)
{
	FFX_CACAO_ASSERT(computeShader);
	FFX_CACAO_ASSERT(computeShader->rootSignature);
	FFX_CACAO_ASSERT(computeShader->pipelineState);
	computeShader->rootSignature->Release();
	computeShader->pipelineState->Release();
}

static void computeShaderDraw(ComputeShader* computeShader, ID3D12GraphicsCommandList* commandList, D3D12_GPU_VIRTUAL_ADDRESS constantBuffer, CbvSrvUav *uavTable, CbvSrvUav *srvTable, uint32_t width, uint32_t height, uint32_t depth)
{
	FFX_CACAO_ASSERT(computeShader);
	FFX_CACAO_ASSERT(commandList);
	FFX_CACAO_ASSERT(uavTable);
	FFX_CACAO_ASSERT(srvTable);
	FFX_CACAO_ASSERT(computeShader->pipelineState);
	FFX_CACAO_ASSERT(computeShader->rootSignature);

	commandList->SetComputeRootSignature(computeShader->rootSignature);

	int params = 0;
	commandList->SetComputeRootConstantBufferView(params++, constantBuffer);
	if (uavTable)
	{
		commandList->SetComputeRootDescriptorTable(params++, uavTable->gpuDescriptor);
	}
	if (srvTable)
	{
		commandList->SetComputeRootDescriptorTable(params++, srvTable->gpuDescriptor);
	}

	commandList->SetPipelineState(computeShader->pipelineState);
	commandList->Dispatch(width, height, depth);
}

// =================================================================================================
// Texture implementation
// =================================================================================================

typedef struct Texture {
	ID3D12Resource *resource;
	DXGI_FORMAT     format;
	uint32_t        width;
	uint32_t        height;
	uint32_t        arraySize;
	uint32_t        mipMapCount;
} Texture;

static FfxCacaoStatus textureInit(Texture* texture, ID3D12Device* device, const char* name, const CD3DX12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue)
{
	FFX_CACAO_ASSERT(texture);
	FFX_CACAO_ASSERT(device);
	FFX_CACAO_ASSERT(name);
	FFX_CACAO_ASSERT(desc);

	HRESULT hr = device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		desc,
		initialState,
		clearValue,
		IID_PPV_ARGS(&texture->resource));
	if (FAILED(hr))
	{
		return hresultToFfxCacaoStatus(hr);
	}

	texture->format = desc->Format;
	texture->width = (uint32_t)desc->Width;
	texture->height = desc->Height;
	texture->arraySize = desc->DepthOrArraySize;
	texture->mipMapCount = desc->MipLevels;

	SetName(texture->resource, name);

	return FFX_CACAO_STATUS_OK;
}

static void textureDestroy(Texture* texture)
{
	FFX_CACAO_ASSERT(texture);
	FFX_CACAO_ASSERT(texture->resource);
	texture->resource->Release();
}

static void textureCreateSrvFromDesc(Texture* texture, uint32_t index, CbvSrvUav* srv, const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc)
{
	FFX_CACAO_ASSERT(texture);
	FFX_CACAO_ASSERT(srv);
	FFX_CACAO_ASSERT(srvDesc);

	ID3D12Device* device;
	texture->resource->GetDevice(__uuidof(*device), (void**)&device);

	device->CreateShaderResourceView(texture->resource, srvDesc, cbvSrvUavGetCpu(srv, index));
	device->CreateShaderResourceView(texture->resource, srvDesc, cbvSrvUavGetCpuVisibleCpu(srv, index));

	device->Release();
}

static void textureCreateSrv(Texture* texture, uint32_t index, CbvSrvUav* srv, int mipLevel, int arraySize, int firstArraySlice)
{
	FFX_CACAO_ASSERT(texture);
	FFX_CACAO_ASSERT(srv);

	D3D12_RESOURCE_DESC resourceDesc = texture->resource->GetDesc();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

	srvDesc.Format = resourceDesc.Format == DXGI_FORMAT_D32_FLOAT ? DXGI_FORMAT_R32_FLOAT : resourceDesc.Format;
	if (resourceDesc.SampleDesc.Count == 1)
	{
		if (resourceDesc.DepthOrArraySize == 1)
		{
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = (mipLevel == -1) ? 0 : mipLevel;
			srvDesc.Texture2D.MipLevels = (mipLevel == -1) ? texture->mipMapCount : 1;
			FFX_CACAO_ASSERT(arraySize == -1);
			FFX_CACAO_ASSERT(firstArraySlice == -1);
		}
		else
		{
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			srvDesc.Texture2DArray.MostDetailedMip = (mipLevel == -1) ? 0 : mipLevel;
			srvDesc.Texture2DArray.MipLevels = (mipLevel == -1) ? texture->mipMapCount : 1;
			srvDesc.Texture2DArray.FirstArraySlice = (firstArraySlice == -1) ? 0 : firstArraySlice;
			srvDesc.Texture2DArray.ArraySize = (arraySize == -1) ? resourceDesc.DepthOrArraySize : arraySize;
		}
	}
	else
	{
		if (resourceDesc.DepthOrArraySize == 1)
		{
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
			FFX_CACAO_ASSERT(mipLevel == -1);
			FFX_CACAO_ASSERT(arraySize == -1);
			FFX_CACAO_ASSERT(firstArraySlice == -1);
		}
		else
		{
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
			srvDesc.Texture2DMSArray.FirstArraySlice = (firstArraySlice == -1) ? 0 : firstArraySlice;
			srvDesc.Texture2DMSArray.ArraySize = (arraySize == -1) ? resourceDesc.DepthOrArraySize : arraySize;
			FFX_CACAO_ASSERT(mipLevel == -1);
		}
	}
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	textureCreateSrvFromDesc(texture, index, srv, &srvDesc);
}

static void textureCreateUavFromDesc(Texture* texture, uint32_t index, CbvSrvUav* uav, const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc)
{
	FFX_CACAO_ASSERT(texture);
	FFX_CACAO_ASSERT(uav);
	FFX_CACAO_ASSERT(uavDesc);

	ID3D12Device* device;
	texture->resource->GetDevice(__uuidof(*device), (void**)&device);

	device->CreateUnorderedAccessView(texture->resource, NULL, uavDesc, cbvSrvUavGetCpu(uav, index));
	device->CreateUnorderedAccessView(texture->resource, NULL, uavDesc, cbvSrvUavGetCpuVisibleCpu(uav, index));

	device->Release();
}

static void textureCreateUav(Texture* texture, uint32_t index, CbvSrvUav* uav, int mipLevel, int arraySize, int firstArraySlice)
{
	FFX_CACAO_ASSERT(texture);
	FFX_CACAO_ASSERT(uav);

	D3D12_RESOURCE_DESC resourceDesc = texture->resource->GetDesc();

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	uavDesc.Format = resourceDesc.Format;
	if (arraySize == -1)
	{
		FFX_CACAO_ASSERT(firstArraySlice == -1);
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = (mipLevel == -1) ? 0 : mipLevel;
	}
	else
	{
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		uavDesc.Texture2DArray.ArraySize = arraySize;
		uavDesc.Texture2DArray.FirstArraySlice = firstArraySlice;
		uavDesc.Texture2DArray.MipSlice = (mipLevel == -1) ? 0 : mipLevel;
	}

	textureCreateUavFromDesc(texture, index, uav, &uavDesc);
}

// =================================================================================================
// CACAO implementation
// =================================================================================================

struct FfxCacaoD3D12Context {
	FfxCacaoSettings   settings;
	FfxCacaoBool       useDownsampledSsao;

	ID3D12Device      *device;
	CbvSrvUavHeap      cbvSrvUavHeap;

#ifdef FFX_CACAO_ENABLE_PROFILING
	GpuTimer           gpuTimer;
#endif

	ConstantBufferRing constantBufferRing;
	BufferSizeInfo     bufferSizeInfo;
	ID3D12Resource    *outputResource;

	// ==========================================
	// Prepare shaders/resources

	ComputeShader prepareDownsampledDepthsAndMips;
	ComputeShader prepareNativeDepthsAndMips;

	ComputeShader prepareDownsampledNormals;
	ComputeShader prepareNativeNormals;

	ComputeShader prepareDownsampledNormalsFromInputNormals;
	ComputeShader prepareNativeNormalsFromInputNormals;

	ComputeShader prepareDownsampledDepths;
	ComputeShader prepareNativeDepths;

	ComputeShader prepareDownsampledDepthsHalf;
	ComputeShader prepareNativeDepthsHalf;

	CbvSrvUav prepareDepthsNormalsAndMipsInputs; // <-- this is just the depth source
	CbvSrvUav prepareDepthsAndMipsOutputs;
	CbvSrvUav prepareDepthsOutputs;
	CbvSrvUav prepareNormalsOutput;
	CbvSrvUav prepareNormalsFromInputNormalsInput;
	CbvSrvUav prepareNormalsFromInputNormalsOutput;

	// ==========================================
	// Generate SSAO shaders/resources

	ComputeShader generateSSAO[5];

	CbvSrvUav generateSSAOInputs[4];
	CbvSrvUav generateAdaptiveSSAOInputs[4];
	CbvSrvUav generateSSAOOutputsPing[4];
	CbvSrvUav generateSSAOOutputsPong[4];

	// ==========================================
	// Importance map generate/post process shaders/resources

	ComputeShader generateImportanceMap;
	ComputeShader postprocessImportanceMapA;
	ComputeShader postprocessImportanceMapB;

	CbvSrvUav generateImportanceMapInputs;
	CbvSrvUav generateImportanceMapOutputs;
	CbvSrvUav generateImportanceMapAInputs;
	CbvSrvUav generateImportanceMapAOutputs;
	CbvSrvUav generateImportanceMapBInputs;
	CbvSrvUav generateImportanceMapBOutputs;

	// ==========================================
	// De-interleave Blur shaders/resources

	ComputeShader edgeSensitiveBlur[8];

	CbvSrvUav edgeSensitiveBlurInput[4];
	CbvSrvUav edgeSensitiveBlurOutput[4];

	// ==========================================
	// Apply shaders/resources

	ComputeShader smartApply;
	ComputeShader nonSmartApply;
	ComputeShader nonSmartHalfApply;

	CbvSrvUav createOutputInputsPing;
	CbvSrvUav createOutputInputsPong;
	CbvSrvUav createOutputOutputs;

	// ==========================================
	// upscale shaders/resources

	ComputeShader upscaleBilateral5x5;
	ComputeShader upscaleBilateral5x5Half;

	CbvSrvUav bilateralUpscaleInputsPing;
	CbvSrvUav bilateralUpscaleInputsPong;
	CbvSrvUav bilateralUpscaleOutputs;

	// ==========================================
	// Intermediate buffers

	Texture deinterleavedDepths;
	Texture deinterleavedNormals;
	Texture ssaoBufferPing;
	Texture ssaoBufferPong;
	Texture importanceMap;
	Texture importanceMapPong;
	Texture loadCounter;

	CbvSrvUav loadCounterUav; // required for LoadCounter clear
};

static inline FfxCacaoD3D12Context* getAlignedD3D12ContextPointer(FfxCacaoD3D12Context* ptr)
{
	uintptr_t tmp = (uintptr_t)ptr;
	tmp = (tmp + alignof(FfxCacaoD3D12Context) - 1) & (~(alignof(FfxCacaoD3D12Context) - 1));
	return (FfxCacaoD3D12Context*)tmp;
}
#endif

#ifdef FFX_CACAO_ENABLE_VULKAN
// =================================================================================================
// CACAO vulkan implementation
// =================================================================================================

// DESCRIPTOR_SET_LAYOUT(name, num_inputs, num_outputs)
#define DESCRIPTOR_SET_LAYOUTS \
	DESCRIPTOR_SET_LAYOUT(CLEAR_LOAD_COUNTER,                 0, 1) \
	DESCRIPTOR_SET_LAYOUT(PREPARE_DEPTHS,                     1, 1) \
	DESCRIPTOR_SET_LAYOUT(PREPARE_DEPTHS_MIPS,                1, 4) \
	DESCRIPTOR_SET_LAYOUT(PREPARE_NORMALS,                    1, 1) \
	DESCRIPTOR_SET_LAYOUT(PREPARE_NORMALS_FROM_INPUT_NORMALS, 1, 1) \
	DESCRIPTOR_SET_LAYOUT(GENERATE,                           7, 1) \
	DESCRIPTOR_SET_LAYOUT(GENERATE_ADAPTIVE,                  7, 1) \
	DESCRIPTOR_SET_LAYOUT(GENERATE_IMPORTANCE_MAP,            1, 1) \
	DESCRIPTOR_SET_LAYOUT(POSTPROCESS_IMPORTANCE_MAP_A,       1, 1) \
	DESCRIPTOR_SET_LAYOUT(POSTPROCESS_IMPORTANCE_MAP_B,       1, 2) \
	DESCRIPTOR_SET_LAYOUT(EDGE_SENSITIVE_BLUR,                1, 1) \
	DESCRIPTOR_SET_LAYOUT(APPLY,                              1, 1) \
	DESCRIPTOR_SET_LAYOUT(BILATERAL_UPSAMPLE,                 4, 1)

typedef enum DescriptorSetLayoutID {
#define DESCRIPTOR_SET_LAYOUT(name, _num_inputs, _num_outputs) DSL_##name,
	DESCRIPTOR_SET_LAYOUTS
#undef DESCRIPTOR_SET_LAYOUT
	NUM_DESCRIPTOR_SET_LAYOUTS
} DescriptorSetLayoutID;

typedef struct DescriptorSetLayoutMetaData {
	uint32_t    numInputs;
	uint32_t    numOutputs;
	const char *name;
} DescriptorSetLayoutMetaData;

static const DescriptorSetLayoutMetaData DESCRIPTOR_SET_LAYOUT_META_DATA[NUM_DESCRIPTOR_SET_LAYOUTS] = {
#define DESCRIPTOR_SET_LAYOUT(name, num_inputs, num_outputs) { num_inputs, num_outputs, "FFX_CACAO_DSL_" #name },
	DESCRIPTOR_SET_LAYOUTS
#undef DESCRIPTOR_SET_LAYOUT
}; 

#define MAX_DESCRIPTOR_BINDINGS 32
// define all the data for compute shaders
// COMPUTE_SHADER(enum_name, pascal_case_name, descriptor_set)
#define COMPUTE_SHADERS \
	COMPUTE_SHADER(CLEAR_LOAD_COUNTER,                             ClearLoadCounter,                          CLEAR_LOAD_COUNTER) \
	\
	COMPUTE_SHADER(PREPARE_DOWNSAMPLED_DEPTHS,                     PrepareDownsampledDepths,                  PREPARE_DEPTHS) \
	COMPUTE_SHADER(PREPARE_NATIVE_DEPTHS,                          PrepareNativeDepths,                       PREPARE_DEPTHS) \
	COMPUTE_SHADER(PREPARE_DOWNSAMPLED_DEPTHS_AND_MIPS,            PrepareDownsampledDepthsAndMips,           PREPARE_DEPTHS_MIPS) \
	COMPUTE_SHADER(PREPARE_NATIVE_DEPTHS_AND_MIPS,                 PrepareNativeDepthsAndMips,                PREPARE_DEPTHS_MIPS) \
	COMPUTE_SHADER(PREPARE_DOWNSAMPLED_NORMALS,                    PrepareDownsampledNormals,                 PREPARE_NORMALS) \
	COMPUTE_SHADER(PREPARE_NATIVE_NORMALS,                         PrepareNativeNormals,                      PREPARE_NORMALS) \
	COMPUTE_SHADER(PREPARE_DOWNSAMPLED_NORMALS_FROM_INPUT_NORMALS, PrepareDownsampledNormalsFromInputNormals, PREPARE_NORMALS_FROM_INPUT_NORMALS) \
	COMPUTE_SHADER(PREPARE_NATIVE_NORMALS_FROM_INPUT_NORMALS,      PrepareNativeNormalsFromInputNormals,      PREPARE_NORMALS_FROM_INPUT_NORMALS) \
	COMPUTE_SHADER(PREPARE_DOWNSAMPLED_DEPTHS_HALF,                PrepareDownsampledDepthsHalf,              PREPARE_DEPTHS) \
	COMPUTE_SHADER(PREPARE_NATIVE_DEPTHS_HALF,                     PrepareNativeDepthsHalf,                   PREPARE_DEPTHS) \
	\
	COMPUTE_SHADER(GENERATE_Q0,                                    GenerateQ0,                                GENERATE) \
	COMPUTE_SHADER(GENERATE_Q1,                                    GenerateQ1,                                GENERATE) \
	COMPUTE_SHADER(GENERATE_Q2,                                    GenerateQ2,                                GENERATE) \
	COMPUTE_SHADER(GENERATE_Q3,                                    GenerateQ3,                                GENERATE_ADAPTIVE) \
	COMPUTE_SHADER(GENERATE_Q3_BASE,                               GenerateQ3Base,                            GENERATE) \
	\
	COMPUTE_SHADER(GENERATE_IMPORTANCE_MAP,                        GenerateImportanceMap,                     GENERATE_IMPORTANCE_MAP) \
	COMPUTE_SHADER(POSTPROCESS_IMPORTANCE_MAP_A,                   PostprocessImportanceMapA,                 POSTPROCESS_IMPORTANCE_MAP_A) \
	COMPUTE_SHADER(POSTPROCESS_IMPORTANCE_MAP_B,                   PostprocessImportanceMapB,                 POSTPROCESS_IMPORTANCE_MAP_B) \
	\
	COMPUTE_SHADER(EDGE_SENSITIVE_BLUR_1,                          EdgeSensitiveBlur1,                        EDGE_SENSITIVE_BLUR) \
	COMPUTE_SHADER(EDGE_SENSITIVE_BLUR_2,                          EdgeSensitiveBlur2,                        EDGE_SENSITIVE_BLUR) \
	COMPUTE_SHADER(EDGE_SENSITIVE_BLUR_3,                          EdgeSensitiveBlur3,                        EDGE_SENSITIVE_BLUR) \
	COMPUTE_SHADER(EDGE_SENSITIVE_BLUR_4,                          EdgeSensitiveBlur4,                        EDGE_SENSITIVE_BLUR) \
	COMPUTE_SHADER(EDGE_SENSITIVE_BLUR_5,                          EdgeSensitiveBlur5,                        EDGE_SENSITIVE_BLUR) \
	COMPUTE_SHADER(EDGE_SENSITIVE_BLUR_6,                          EdgeSensitiveBlur6,                        EDGE_SENSITIVE_BLUR) \
	COMPUTE_SHADER(EDGE_SENSITIVE_BLUR_7,                          EdgeSensitiveBlur7,                        EDGE_SENSITIVE_BLUR) \
	COMPUTE_SHADER(EDGE_SENSITIVE_BLUR_8,                          EdgeSensitiveBlur8,                        EDGE_SENSITIVE_BLUR) \
	\
	COMPUTE_SHADER(APPLY,                                          Apply,                                     APPLY) \
	COMPUTE_SHADER(NON_SMART_APPLY,                                NonSmartApply,                             APPLY) \
	COMPUTE_SHADER(NON_SMART_HALF_APPLY,                           NonSmartHalfApply,                         APPLY) \
	\
	COMPUTE_SHADER(UPSCALE_BILATERAL_5X5,                          UpscaleBilateral5x5,                       BILATERAL_UPSAMPLE) \
	COMPUTE_SHADER(UPSCALE_BILATERAL_5X5_HALF,                     UpscaleBilateral5x5Half,                   BILATERAL_UPSAMPLE)

typedef enum ComputeShaderID {
#define COMPUTE_SHADER(name, _pascal_name, _descriptor_set) CS_##name,
	COMPUTE_SHADERS
#undef COMPUTE_SHADER
	NUM_COMPUTE_SHADERS
} ComputeShaderID;

typedef struct ComputeShaderMetaData {
	const uint32_t        *shaderSpirv16;
	size_t                 spirv16Len;
	const uint32_t        *shaderSpirv32;
	size_t                 spirv32Len;
	const char            *name;
	DescriptorSetLayoutID  descriptorSetLayoutID;
	const char            *objectName;
} ComputeShaderMetaData;

static const ComputeShaderMetaData COMPUTE_SHADER_META_DATA[NUM_COMPUTE_SHADERS] = {
#define COMPUTE_SHADER(name, pascal_name, descriptor_set_layout) { (uint32_t*)CS##pascal_name##SPIRV16, FFX_CACAO_ARRAY_SIZE(CS##pascal_name##SPIRV16), (uint32_t*)CS##pascal_name##SPIRV32, FFX_CACAO_ARRAY_SIZE(CS##pascal_name##SPIRV32), "CS"#pascal_name, DSL_##descriptor_set_layout, "FFX_CACAO_CS_"#name },
	COMPUTE_SHADERS
#undef COMPUTE_SHADER
};

#define TEXTURES \
	TEXTURE(DEINTERLEAVED_DEPTHS,  deinterleavedDepthBufferWidth, deinterleavedDepthBufferHeight, VK_FORMAT_R16_SFLOAT,     4, 4) \
	TEXTURE(DEINTERLEAVED_NORMALS, ssaoBufferWidth,               ssaoBufferHeight,               VK_FORMAT_R8G8B8A8_SNORM, 4, 1) \
	TEXTURE(SSAO_BUFFER_PING,      ssaoBufferWidth,               ssaoBufferHeight,               VK_FORMAT_R8G8_UNORM,     4, 1) \
	TEXTURE(SSAO_BUFFER_PONG,      ssaoBufferWidth,               ssaoBufferHeight,               VK_FORMAT_R8G8_UNORM,     4, 1) \
	TEXTURE(IMPORTANCE_MAP,        importanceMapWidth,            importanceMapHeight,            VK_FORMAT_R8_UNORM,       1, 1) \
	TEXTURE(IMPORTANCE_MAP_PONG,   importanceMapWidth,            importanceMapHeight,            VK_FORMAT_R8_UNORM,       1, 1)

typedef enum TextureID {
#define TEXTURE(name, _width, _height, _format, _array_size, _num_mips) TEXTURE_##name,
	TEXTURES
#undef TEXTURE
	NUM_TEXTURES
} TextureID;

typedef struct TextureMetaData {
	size_t widthOffset;
	size_t heightOffset;
	VkFormat format;
	uint32_t arraySize;
	uint32_t numMips;
	const char *name;
} TextureMetaData;

static const TextureMetaData TEXTURE_META_DATA[NUM_TEXTURES] = {
#define TEXTURE(name, width, height, format, array_size, num_mips) { FFX_CACAO_OFFSET_OF(BufferSizeInfo, width), FFX_CACAO_OFFSET_OF(BufferSizeInfo, height), format, array_size, num_mips, "FFX_CACAO_" #name },
	TEXTURES
#undef TEXTURE
};

// SHADER_RESOURCE_VIEW(name, texture, view_dimension, most_detailed_mip, mip_levels, first_array_slice, array_size)
#define SHADER_RESOURCE_VIEWS \
	SHADER_RESOURCE_VIEW(DEINTERLEAVED_DEPTHS,    DEINTERLEAVED_DEPTHS,  VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 4, 0, 4) \
	SHADER_RESOURCE_VIEW(DEINTERLEAVED_DEPTHS_0,  DEINTERLEAVED_DEPTHS,  VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 4, 0, 1) \
	SHADER_RESOURCE_VIEW(DEINTERLEAVED_DEPTHS_1,  DEINTERLEAVED_DEPTHS,  VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 4, 1, 1) \
	SHADER_RESOURCE_VIEW(DEINTERLEAVED_DEPTHS_2,  DEINTERLEAVED_DEPTHS,  VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 4, 2, 1) \
	SHADER_RESOURCE_VIEW(DEINTERLEAVED_DEPTHS_3,  DEINTERLEAVED_DEPTHS,  VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 4, 3, 1) \
	SHADER_RESOURCE_VIEW(DEINTERLEAVED_NORMALS,   DEINTERLEAVED_NORMALS, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, 0, 4) \
	SHADER_RESOURCE_VIEW(IMPORTANCE_MAP,          IMPORTANCE_MAP,        VK_IMAGE_VIEW_TYPE_2D,       0, 1, 0, 1) \
	SHADER_RESOURCE_VIEW(IMPORTANCE_MAP_PONG,     IMPORTANCE_MAP_PONG,   VK_IMAGE_VIEW_TYPE_2D,       0, 1, 0, 1) \
	SHADER_RESOURCE_VIEW(SSAO_BUFFER_PING,        SSAO_BUFFER_PING,      VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, 0, 4) \
	SHADER_RESOURCE_VIEW(SSAO_BUFFER_PING_0,      SSAO_BUFFER_PING,      VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, 0, 1) \
	SHADER_RESOURCE_VIEW(SSAO_BUFFER_PING_1,      SSAO_BUFFER_PING,      VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, 1, 1) \
	SHADER_RESOURCE_VIEW(SSAO_BUFFER_PING_2,      SSAO_BUFFER_PING,      VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, 2, 1) \
	SHADER_RESOURCE_VIEW(SSAO_BUFFER_PING_3,      SSAO_BUFFER_PING,      VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, 3, 1) \
	SHADER_RESOURCE_VIEW(SSAO_BUFFER_PONG,        SSAO_BUFFER_PONG,      VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, 0, 4) \
	SHADER_RESOURCE_VIEW(SSAO_BUFFER_PONG_0,      SSAO_BUFFER_PONG,      VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, 0, 1) \
	SHADER_RESOURCE_VIEW(SSAO_BUFFER_PONG_1,      SSAO_BUFFER_PONG,      VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, 1, 1) \
	SHADER_RESOURCE_VIEW(SSAO_BUFFER_PONG_2,      SSAO_BUFFER_PONG,      VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, 2, 1) \
	SHADER_RESOURCE_VIEW(SSAO_BUFFER_PONG_3,      SSAO_BUFFER_PONG,      VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, 3, 1)

typedef enum ShaderResourceViewID {
#define SHADER_RESOURCE_VIEW(name, _texture, _view_dimension, _most_detailed_mip, _mip_levels, _first_array_slice, _array_size) SRV_##name,
	SHADER_RESOURCE_VIEWS
#undef SHADER_RESOURCE_VIEW
	NUM_SHADER_RESOURCE_VIEWS
} ShaderResourceViewID;

typedef struct ShaderResourceViewMetaData {
	TextureID       texture;
	VkImageViewType viewType;
	uint32_t        mostDetailedMip;
	uint32_t        mipLevels;
	uint32_t        firstArraySlice;
	uint32_t        arraySize;
} ShaderResourceViewMetaData;

static const ShaderResourceViewMetaData SRV_META_DATA[NUM_SHADER_RESOURCE_VIEWS] = {
#define SHADER_RESOURCE_VIEW(_name, texture, view_dimension, most_detailed_mip, mip_levels, first_array_slice, array_size) { TEXTURE_##texture, view_dimension, most_detailed_mip, mip_levels, first_array_slice, array_size },
	SHADER_RESOURCE_VIEWS
#undef SHADER_RESOURCE_VIEW
};

// UNORDERED_ACCESS_VIEW(name, texture, view_dimension, mip_slice, first_array_slice, array_size)
#define UNORDERED_ACCESS_VIEWS \
	UNORDERED_ACCESS_VIEW(DEINTERLEAVED_DEPTHS_MIP_0, DEINTERLEAVED_DEPTHS,  VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 0, 4) \
	UNORDERED_ACCESS_VIEW(DEINTERLEAVED_DEPTHS_MIP_1, DEINTERLEAVED_DEPTHS,  VK_IMAGE_VIEW_TYPE_2D_ARRAY, 1, 0, 4) \
	UNORDERED_ACCESS_VIEW(DEINTERLEAVED_DEPTHS_MIP_2, DEINTERLEAVED_DEPTHS,  VK_IMAGE_VIEW_TYPE_2D_ARRAY, 2, 0, 4) \
	UNORDERED_ACCESS_VIEW(DEINTERLEAVED_DEPTHS_MIP_3, DEINTERLEAVED_DEPTHS,  VK_IMAGE_VIEW_TYPE_2D_ARRAY, 3, 0, 4) \
	UNORDERED_ACCESS_VIEW(DEINTERLEAVED_NORMALS,      DEINTERLEAVED_NORMALS, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 0, 4) \
	UNORDERED_ACCESS_VIEW(IMPORTANCE_MAP,             IMPORTANCE_MAP,        VK_IMAGE_VIEW_TYPE_2D,       0, 0, 1) \
	UNORDERED_ACCESS_VIEW(IMPORTANCE_MAP_PONG,        IMPORTANCE_MAP_PONG,   VK_IMAGE_VIEW_TYPE_2D,       0, 0, 1) \
	UNORDERED_ACCESS_VIEW(SSAO_BUFFER_PING,           SSAO_BUFFER_PING,      VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 0, 4) \
	UNORDERED_ACCESS_VIEW(SSAO_BUFFER_PING_0,         SSAO_BUFFER_PING,      VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 0, 1) \
	UNORDERED_ACCESS_VIEW(SSAO_BUFFER_PING_1,         SSAO_BUFFER_PING,      VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, 1) \
	UNORDERED_ACCESS_VIEW(SSAO_BUFFER_PING_2,         SSAO_BUFFER_PING,      VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 2, 1) \
	UNORDERED_ACCESS_VIEW(SSAO_BUFFER_PING_3,         SSAO_BUFFER_PING,      VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 3, 1) \
	UNORDERED_ACCESS_VIEW(SSAO_BUFFER_PONG,           SSAO_BUFFER_PONG,      VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 0, 4) \
	UNORDERED_ACCESS_VIEW(SSAO_BUFFER_PONG_0,         SSAO_BUFFER_PONG,      VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 0, 1) \
	UNORDERED_ACCESS_VIEW(SSAO_BUFFER_PONG_1,         SSAO_BUFFER_PONG,      VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, 1) \
	UNORDERED_ACCESS_VIEW(SSAO_BUFFER_PONG_2,         SSAO_BUFFER_PONG,      VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 2, 1) \
	UNORDERED_ACCESS_VIEW(SSAO_BUFFER_PONG_3,         SSAO_BUFFER_PONG,      VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 3, 1)

typedef enum UnorderedAccessViewID {
#define UNORDERED_ACCESS_VIEW(name, _texture, _view_dimension, _mip_slice, _first_array_slice, _array_size) UAV_##name,
	UNORDERED_ACCESS_VIEWS
#undef UNORDERED_ACCESS_VIEW
	NUM_UNORDERED_ACCESS_VIEWS
} UnorderedAccessViewID;

typedef struct UnorderedAccessViewMetaData {
	TextureID       textureID;
	VkImageViewType viewType;
	uint32_t mostDetailedMip;
	uint32_t firstArraySlice;
	uint32_t arraySize;
} UnorderedAccessViewMetaData;

static const UnorderedAccessViewMetaData UAV_META_DATA[NUM_UNORDERED_ACCESS_VIEWS] = {
#define UNORDERED_ACCESS_VIEW(_name, texture, view_dimension, mip_slice, first_array_slice, array_size) { TEXTURE_##texture, view_dimension, mip_slice, first_array_slice, array_size },
	UNORDERED_ACCESS_VIEWS
#undef UNORDERED_ACCESS_VIEW
};

// DESCRIPTOR_SET(name, layout_name, pass)
#define DESCRIPTOR_SETS \
	DESCRIPTOR_SET(CLEAR_LOAD_COUNTER,                 CLEAR_LOAD_COUNTER,                 0) \
	DESCRIPTOR_SET(PREPARE_DEPTHS,                     PREPARE_DEPTHS,                     0) \
	DESCRIPTOR_SET(PREPARE_DEPTHS_MIPS,                PREPARE_DEPTHS_MIPS,                0) \
	DESCRIPTOR_SET(PREPARE_NORMALS,                    PREPARE_NORMALS,                    0) \
	DESCRIPTOR_SET(PREPARE_NORMALS_FROM_INPUT_NORMALS, PREPARE_NORMALS_FROM_INPUT_NORMALS, 0) \
	DESCRIPTOR_SET(GENERATE_ADAPTIVE_BASE_0,           GENERATE,                           0) \
	DESCRIPTOR_SET(GENERATE_ADAPTIVE_BASE_1,           GENERATE,                           1) \
	DESCRIPTOR_SET(GENERATE_ADAPTIVE_BASE_2,           GENERATE,                           2) \
	DESCRIPTOR_SET(GENERATE_ADAPTIVE_BASE_3,           GENERATE,                           3) \
	DESCRIPTOR_SET(GENERATE_0,                         GENERATE,                           0) \
	DESCRIPTOR_SET(GENERATE_1,                         GENERATE,                           1) \
	DESCRIPTOR_SET(GENERATE_2,                         GENERATE,                           2) \
	DESCRIPTOR_SET(GENERATE_3,                         GENERATE,                           3) \
	DESCRIPTOR_SET(GENERATE_ADAPTIVE_0,                GENERATE_ADAPTIVE,                  0) \
	DESCRIPTOR_SET(GENERATE_ADAPTIVE_1,                GENERATE_ADAPTIVE,                  1) \
	DESCRIPTOR_SET(GENERATE_ADAPTIVE_2,                GENERATE_ADAPTIVE,                  2) \
	DESCRIPTOR_SET(GENERATE_ADAPTIVE_3,                GENERATE_ADAPTIVE,                  3) \
	DESCRIPTOR_SET(GENERATE_IMPORTANCE_MAP,            GENERATE_IMPORTANCE_MAP,            0) \
	DESCRIPTOR_SET(POSTPROCESS_IMPORTANCE_MAP_A,       POSTPROCESS_IMPORTANCE_MAP_A,       0) \
	DESCRIPTOR_SET(POSTPROCESS_IMPORTANCE_MAP_B,       POSTPROCESS_IMPORTANCE_MAP_B,       0) \
	DESCRIPTOR_SET(EDGE_SENSITIVE_BLUR_0,              EDGE_SENSITIVE_BLUR,                0) \
	DESCRIPTOR_SET(EDGE_SENSITIVE_BLUR_1,              EDGE_SENSITIVE_BLUR,                1) \
	DESCRIPTOR_SET(EDGE_SENSITIVE_BLUR_2,              EDGE_SENSITIVE_BLUR,                2) \
	DESCRIPTOR_SET(EDGE_SENSITIVE_BLUR_3,              EDGE_SENSITIVE_BLUR,                3) \
	DESCRIPTOR_SET(APPLY_PING,                         APPLY,                              0) \
	DESCRIPTOR_SET(APPLY_PONG,                         APPLY,                              0) \
	DESCRIPTOR_SET(BILATERAL_UPSAMPLE_PING,            BILATERAL_UPSAMPLE,                 0) \
	DESCRIPTOR_SET(BILATERAL_UPSAMPLE_PONG,            BILATERAL_UPSAMPLE,                 0)

typedef enum DescriptorSetID {
#define DESCRIPTOR_SET(name, _layout_name, _pass) DS_##name,
	DESCRIPTOR_SETS
#undef DESCRIPTOR_SET
	NUM_DESCRIPTOR_SETS
} DescriptorSetID;

typedef struct DescriptorSetMetaData {
	DescriptorSetLayoutID descriptorSetLayoutID;
	uint32_t pass;
	const char *name;
} DescriptorSetMetaData;

static const DescriptorSetMetaData DESCRIPTOR_SET_META_DATA[NUM_DESCRIPTOR_SETS] = {
#define DESCRIPTOR_SET(name, layout_name, pass) { DSL_##layout_name, pass, "FFX_CACAO_DS_" #name },
	DESCRIPTOR_SETS
#undef DESCRIPTOR_SET
};

// INPUT_DESCRIPTOR(descriptor_set_name, srv_name, binding_num)
#define INPUT_DESCRIPTOR_BINDINGS \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_0,     DEINTERLEAVED_DEPTHS_0, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_0,     DEINTERLEAVED_NORMALS,  6) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_1,     DEINTERLEAVED_DEPTHS_1, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_1,     DEINTERLEAVED_NORMALS,  6) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_2,     DEINTERLEAVED_DEPTHS_2, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_2,     DEINTERLEAVED_NORMALS,  6) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_3,     DEINTERLEAVED_DEPTHS_3, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_3,     DEINTERLEAVED_NORMALS,  6) \
	\
	INPUT_DESCRIPTOR_BINDING(GENERATE_0,  DEINTERLEAVED_DEPTHS_0, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_0,  DEINTERLEAVED_NORMALS,  6) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_1,  DEINTERLEAVED_DEPTHS_1, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_1,  DEINTERLEAVED_NORMALS,  6) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_2,  DEINTERLEAVED_DEPTHS_2, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_2,  DEINTERLEAVED_NORMALS,  6) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_3,  DEINTERLEAVED_DEPTHS_3, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_3,  DEINTERLEAVED_NORMALS,  6) \
	\
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_0,  DEINTERLEAVED_DEPTHS_0, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_0,  IMPORTANCE_MAP,         3) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_0,  SSAO_BUFFER_PONG_0,     4) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_0,  DEINTERLEAVED_NORMALS,  6) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_1,  DEINTERLEAVED_DEPTHS_1, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_1,  IMPORTANCE_MAP,         3) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_1,  SSAO_BUFFER_PONG_1,     4) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_1,  DEINTERLEAVED_NORMALS,  6) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_2,  DEINTERLEAVED_DEPTHS_2, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_2,  IMPORTANCE_MAP,         3) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_2,  SSAO_BUFFER_PONG_2,     4) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_2,  DEINTERLEAVED_NORMALS,  6) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_3,  DEINTERLEAVED_DEPTHS_3, 0) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_3,  IMPORTANCE_MAP,         3) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_3,  SSAO_BUFFER_PONG_3,     4) \
	INPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_3,  DEINTERLEAVED_NORMALS,  6) \
	\
	INPUT_DESCRIPTOR_BINDING(GENERATE_IMPORTANCE_MAP,      SSAO_BUFFER_PONG,       0) \
	INPUT_DESCRIPTOR_BINDING(POSTPROCESS_IMPORTANCE_MAP_A, IMPORTANCE_MAP,         0) \
	INPUT_DESCRIPTOR_BINDING(POSTPROCESS_IMPORTANCE_MAP_B, IMPORTANCE_MAP_PONG,    0) \
	\
	INPUT_DESCRIPTOR_BINDING(EDGE_SENSITIVE_BLUR_0, SSAO_BUFFER_PING_0, 0) \
	INPUT_DESCRIPTOR_BINDING(EDGE_SENSITIVE_BLUR_1, SSAO_BUFFER_PING_1, 0) \
	INPUT_DESCRIPTOR_BINDING(EDGE_SENSITIVE_BLUR_2, SSAO_BUFFER_PING_2, 0) \
	INPUT_DESCRIPTOR_BINDING(EDGE_SENSITIVE_BLUR_3, SSAO_BUFFER_PING_3, 0) \
	\
	INPUT_DESCRIPTOR_BINDING(BILATERAL_UPSAMPLE_PING, SSAO_BUFFER_PING,     0) \
	INPUT_DESCRIPTOR_BINDING(BILATERAL_UPSAMPLE_PING, DEINTERLEAVED_DEPTHS, 3) \
	INPUT_DESCRIPTOR_BINDING(BILATERAL_UPSAMPLE_PONG, SSAO_BUFFER_PONG,     0) \
	INPUT_DESCRIPTOR_BINDING(BILATERAL_UPSAMPLE_PONG, DEINTERLEAVED_DEPTHS, 3) \
	\
	INPUT_DESCRIPTOR_BINDING(APPLY_PING, SSAO_BUFFER_PING, 0) \
	INPUT_DESCRIPTOR_BINDING(APPLY_PONG, SSAO_BUFFER_PONG, 0)

// need this to define NUM_INPUT_DESCRIPTOR_BINDINGS
typedef enum InputDescriptorBindingID {
#define INPUT_DESCRIPTOR_BINDING(descriptor_set_name, srv_name, _binding_num) INPUT_DESCRIPTOR_BINDING_##descriptor_set_name##_##srv_name,
	INPUT_DESCRIPTOR_BINDINGS
#undef INPUT_DESCRIPTOR_BINDING
	NUM_INPUT_DESCRIPTOR_BINDINGS
} InputDescriptorBindingID;

typedef struct InputDescriptorBindingMetaData {
	DescriptorSetID      descriptorID;
	ShaderResourceViewID srvID;
	uint32_t             bindingNumber;
} InputDescriptorBindingMetaData;

static const InputDescriptorBindingMetaData INPUT_DESCRIPTOR_BINDING_META_DATA[NUM_INPUT_DESCRIPTOR_BINDINGS] = {
#define INPUT_DESCRIPTOR_BINDING(descriptor_set_name, srv_name, binding_num) { DS_##descriptor_set_name, SRV_##srv_name, binding_num },
	INPUT_DESCRIPTOR_BINDINGS
#undef INPUT_DESCRIPTOR_BINDING
};

// OUTPUT_DESCRIPTOR(descriptor_set_name, uav_name, binding_num)
#define OUTPUT_DESCRIPTOR_BINDINGS \
	OUTPUT_DESCRIPTOR_BINDING(PREPARE_DEPTHS,                     DEINTERLEAVED_DEPTHS_MIP_0, 0) \
	OUTPUT_DESCRIPTOR_BINDING(PREPARE_DEPTHS_MIPS,                DEINTERLEAVED_DEPTHS_MIP_0, 0) \
	OUTPUT_DESCRIPTOR_BINDING(PREPARE_DEPTHS_MIPS,                DEINTERLEAVED_DEPTHS_MIP_1, 1) \
	OUTPUT_DESCRIPTOR_BINDING(PREPARE_DEPTHS_MIPS,                DEINTERLEAVED_DEPTHS_MIP_2, 2) \
	OUTPUT_DESCRIPTOR_BINDING(PREPARE_DEPTHS_MIPS,                DEINTERLEAVED_DEPTHS_MIP_3, 3) \
	OUTPUT_DESCRIPTOR_BINDING(PREPARE_NORMALS,                    DEINTERLEAVED_NORMALS,      0) \
	OUTPUT_DESCRIPTOR_BINDING(PREPARE_NORMALS_FROM_INPUT_NORMALS, DEINTERLEAVED_NORMALS,      0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_0,           SSAO_BUFFER_PONG_0,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_1,           SSAO_BUFFER_PONG_1,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_2,           SSAO_BUFFER_PONG_2,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_BASE_3,           SSAO_BUFFER_PONG_3,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_0,                         SSAO_BUFFER_PING_0,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_1,                         SSAO_BUFFER_PING_1,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_2,                         SSAO_BUFFER_PING_2,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_3,                         SSAO_BUFFER_PING_3,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_0,                SSAO_BUFFER_PING_0,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_1,                SSAO_BUFFER_PING_1,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_2,                SSAO_BUFFER_PING_2,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_ADAPTIVE_3,                SSAO_BUFFER_PING_3,         0) \
	OUTPUT_DESCRIPTOR_BINDING(GENERATE_IMPORTANCE_MAP,            IMPORTANCE_MAP,             0) \
	OUTPUT_DESCRIPTOR_BINDING(POSTPROCESS_IMPORTANCE_MAP_A,       IMPORTANCE_MAP_PONG,        0) \
	OUTPUT_DESCRIPTOR_BINDING(POSTPROCESS_IMPORTANCE_MAP_B,       IMPORTANCE_MAP,             0) \
	OUTPUT_DESCRIPTOR_BINDING(EDGE_SENSITIVE_BLUR_0,              SSAO_BUFFER_PONG_0,         0) \
	OUTPUT_DESCRIPTOR_BINDING(EDGE_SENSITIVE_BLUR_1,              SSAO_BUFFER_PONG_1,         0) \
	OUTPUT_DESCRIPTOR_BINDING(EDGE_SENSITIVE_BLUR_2,              SSAO_BUFFER_PONG_2,         0) \
	OUTPUT_DESCRIPTOR_BINDING(EDGE_SENSITIVE_BLUR_3,              SSAO_BUFFER_PONG_3,         0)

typedef enum OutputDescriptorBindingID {
#define OUTPUT_DESCRIPTOR_BINDING(descriptor_set_name, uav_name, _binding_num) OUTPUT_DESCRIPTOR_BINDING_##descriptor_set_name##_##uav_name,
	OUTPUT_DESCRIPTOR_BINDINGS
#undef OUTPUT_DESCRIPTOR_BINDING
	NUM_OUTPUT_DESCRIPTOR_BINDINGS
} OutputDescriptorBindingID;

typedef struct OutputDescriptorBindingMetaData {
	DescriptorSetID      descriptorID;
	UnorderedAccessViewID uavID;
	uint32_t              bindingNumber;
} OutputDescriptorBindingMetaData;

static const OutputDescriptorBindingMetaData OUTPUT_DESCRIPTOR_BINDING_META_DATA[NUM_OUTPUT_DESCRIPTOR_BINDINGS] = {
#define OUTPUT_DESCRIPTOR_BINDING(descriptor_set_name, uav_name, binding_num) { DS_##descriptor_set_name, UAV_##uav_name, binding_num },
	OUTPUT_DESCRIPTOR_BINDINGS
#undef OUTPUT_DESCRIPTOR_BINDING
};

#define NUM_BACK_BUFFERS 3
#define NUM_SAMPLERS 5
typedef struct FfxCacaoVkContext {
	FfxCacaoSettings   settings;
	FfxCacaoBool       useDownsampledSsao;
	BufferSizeInfo     bufferSizeInfo;

#ifdef FFX_CACAO_ENABLE_PROFILING
	VkQueryPool timestampQueryPool;
	uint32_t collectBuffer;
	struct {
		TimestampID timestamps[NUM_TIMESTAMPS];
		uint64_t    timings[NUM_TIMESTAMPS];
		uint32_t    numTimestamps;
	} timestampQueries[NUM_BACK_BUFFERS];
#endif

	VkPhysicalDevice                 physicalDevice;
	VkDevice                         device;
	PFN_vkCmdDebugMarkerBeginEXT     vkCmdDebugMarkerBegin;
	PFN_vkCmdDebugMarkerEndEXT       vkCmdDebugMarkerEnd;
	PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectName;


	VkDescriptorSetLayout descriptorSetLayouts[NUM_DESCRIPTOR_SET_LAYOUTS];
	VkPipelineLayout      pipelineLayouts[NUM_DESCRIPTOR_SET_LAYOUTS];

	VkShaderModule        computeShaders[NUM_COMPUTE_SHADERS];
	VkPipeline            computePipelines[NUM_COMPUTE_SHADERS];

	VkDescriptorSet       descriptorSets[NUM_BACK_BUFFERS][NUM_DESCRIPTOR_SETS];
	VkDescriptorPool      descriptorPool;

	VkSampler      samplers[NUM_SAMPLERS];

	VkImage        textures[NUM_TEXTURES];
	VkDeviceMemory textureMemory[NUM_TEXTURES];
	VkImageView    shaderResourceViews[NUM_SHADER_RESOURCE_VIEWS];
	VkImageView    unorderedAccessViews[NUM_UNORDERED_ACCESS_VIEWS];

	VkImage        loadCounter;
	VkDeviceMemory loadCounterMemory;
	VkImageView    loadCounterView;

	VkImage        output;

	uint32_t       currentConstantBuffer;
	VkBuffer       constantBuffer[NUM_BACK_BUFFERS][4];
	VkDeviceMemory constantBufferMemory[NUM_BACK_BUFFERS][4];
} FfxCacaoVkContext;

static inline FfxCacaoVkContext* getAlignedVkContextPointer(FfxCacaoVkContext* ptr)
{
	uintptr_t tmp = (uintptr_t)ptr;
	tmp = (tmp + alignof(FfxCacaoVkContext) - 1) & (~(alignof(FfxCacaoVkContext) - 1));
	return (FfxCacaoVkContext*)tmp;
}
#endif

// =================================================================================
// Interface
// =================================================================================

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef FFX_CACAO_ENABLE_D3D12
size_t ffxCacaoD3D12GetContextSize()
{
	return sizeof(FfxCacaoD3D12Context) + alignof(FfxCacaoD3D12Context) - 1;
}

FfxCacaoStatus ffxCacaoD3D12InitContext(FfxCacaoD3D12Context* context, ID3D12Device* device)
{
	if (context == NULL)
	{
		return FFX_CACAO_STATUS_INVALID_POINTER;
	}
	if (device == NULL)
	{
		return FFX_CACAO_STATUS_INVALID_POINTER;
	}
	context = getAlignedD3D12ContextPointer(context);

#define COMPUTE_SHADER_INIT(name, entryPoint, uavSize, srvSize) \
	errorStatus = computeShaderInit(&context->name, device, #entryPoint, entryPoint ## DXIL, sizeof(entryPoint ## DXIL), uavSize, srvSize, samplers, FFX_CACAO_ARRAY_SIZE(samplers)); \
	if (errorStatus) \
	{ \
		goto error_create_ ## entryPoint; \
	}
#define ERROR_COMPUTE_SHADER_DESTROY(name, entryPoint) \
	computeShaderDestroy(&context->name); \
error_create_ ## entryPoint:

	FfxCacaoStatus errorStatus = FFX_CACAO_STATUS_FAILED;

	context->device = device;
	CbvSrvUavHeap *cbvSrvUavHeap = &context->cbvSrvUavHeap;
	errorStatus = cbvSrvUavHeapInit(cbvSrvUavHeap, device, 256);
	if (errorStatus)
	{
		goto error_create_cbv_srv_uav_heap;
	}
	errorStatus = constantBufferRingInit(&context->constantBufferRing, device, 5, 1024 * 5);
	if (errorStatus)
	{
		goto error_create_constant_buffer_ring;
	}
#ifdef FFX_CACAO_ENABLE_PROFILING
	errorStatus = gpuTimerInit(&context->gpuTimer, device);
	if (errorStatus)
	{
		goto error_create_gpu_timer;
	}
#endif

	D3D12_STATIC_SAMPLER_DESC samplers[5] = { };

	samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	samplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	samplers[0].MinLOD = 0.0f;
	samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	samplers[0].MipLODBias = 0;
	samplers[0].MaxAnisotropy = 1;
	samplers[0].ShaderRegister = 0;
	samplers[0].RegisterSpace = 0;
	samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
	samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
	samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
	samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	samplers[1].MinLOD = 0.0f;
	samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
	samplers[1].MipLODBias = 0;
	samplers[1].MaxAnisotropy = 1;
	samplers[1].ShaderRegister = 1;
	samplers[1].RegisterSpace = 0;
	samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	samplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	samplers[2].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	samplers[2].MinLOD = 0.0f;
	samplers[2].MaxLOD = D3D12_FLOAT32_MAX;
	samplers[2].MipLODBias = 0;
	samplers[2].MaxAnisotropy = 1;
	samplers[2].ShaderRegister = 2;
	samplers[2].RegisterSpace = 0;
	samplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	samplers[3].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	samplers[3].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[3].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[3].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[3].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	samplers[3].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	samplers[3].MinLOD = 0.0f;
	samplers[3].MaxLOD = D3D12_FLOAT32_MAX;
	samplers[3].MipLODBias = 0;
	samplers[3].MaxAnisotropy = 1;
	samplers[3].ShaderRegister = 3;
	samplers[3].RegisterSpace = 0;
	samplers[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	samplers[4].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	samplers[4].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	samplers[4].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	samplers[4].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	samplers[4].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	samplers[4].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	samplers[4].MinLOD = 0.0f;
	samplers[4].MaxLOD = D3D12_FLOAT32_MAX;
	samplers[4].MipLODBias = 0;
	samplers[4].MaxAnisotropy = 1;
	samplers[4].ShaderRegister = 4;
	samplers[4].RegisterSpace = 0;
	samplers[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	// =====================================
	// Prepare shaders/resources

	COMPUTE_SHADER_INIT(prepareDownsampledDepthsHalf, CSPrepareDownsampledDepthsHalf, 1, 1);
	COMPUTE_SHADER_INIT(prepareNativeDepthsHalf, CSPrepareNativeDepthsHalf, 1, 1);

	COMPUTE_SHADER_INIT(prepareDownsampledDepthsAndMips, CSPrepareDownsampledDepthsAndMips, 4, 1);
	COMPUTE_SHADER_INIT(prepareNativeDepthsAndMips, CSPrepareNativeDepthsAndMips, 4, 1);

	COMPUTE_SHADER_INIT(prepareDownsampledNormals, CSPrepareDownsampledNormals, 1, 1);
	COMPUTE_SHADER_INIT(prepareNativeNormals, CSPrepareNativeNormals, 1, 1);

	COMPUTE_SHADER_INIT(prepareDownsampledNormalsFromInputNormals, CSPrepareDownsampledNormalsFromInputNormals, 1, 1);
	COMPUTE_SHADER_INIT(prepareNativeNormalsFromInputNormals, CSPrepareNativeNormalsFromInputNormals, 1, 1);

	COMPUTE_SHADER_INIT(prepareDownsampledDepths, CSPrepareDownsampledDepths, 1, 1);
	COMPUTE_SHADER_INIT(prepareNativeDepths, CSPrepareNativeDepths, 1, 1);

	cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->prepareDepthsAndMipsOutputs, 4);
	cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->prepareDepthsOutputs, 1);
	cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->prepareDepthsNormalsAndMipsInputs, 1);
	cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->prepareNormalsOutput, 1);
	cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->prepareNormalsFromInputNormalsInput, 1);
	cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->prepareNormalsFromInputNormalsOutput, 1);

	// =====================================
	// Generate SSAO shaders/resources

	COMPUTE_SHADER_INIT(generateSSAO[0], CSGenerateQ0, 1, 7);
	COMPUTE_SHADER_INIT(generateSSAO[1], CSGenerateQ1, 1, 7);
	COMPUTE_SHADER_INIT(generateSSAO[2], CSGenerateQ2, 1, 7);
	COMPUTE_SHADER_INIT(generateSSAO[3], CSGenerateQ3, 1, 7);
	COMPUTE_SHADER_INIT(generateSSAO[4], CSGenerateQ3Base, 2, 7);

	for (int i = 0; i < 4; ++i)
	{
		cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->generateSSAOInputs[i], 7);
		cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->generateAdaptiveSSAOInputs[i], 7);

		cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->generateSSAOOutputsPing[i], 1);
		cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->generateSSAOOutputsPong[i], 1);
	}

	// =====================================
	// Importance map shaders/resources

	COMPUTE_SHADER_INIT(generateImportanceMap, CSGenerateImportanceMap, 1, 1);
	COMPUTE_SHADER_INIT(postprocessImportanceMapA, CSPostprocessImportanceMapA, 1, 1);
	COMPUTE_SHADER_INIT(postprocessImportanceMapB, CSPostprocessImportanceMapB, 2, 1);

	cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->generateImportanceMapInputs, 1);
	cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->generateImportanceMapOutputs, 1);
	cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->generateImportanceMapAInputs, 1);
	cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->generateImportanceMapAOutputs, 1);
	cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->generateImportanceMapBInputs, 1);
	cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->generateImportanceMapBOutputs, 2);

	// =====================================
	// De-interleave Blur shaders/resources

	COMPUTE_SHADER_INIT(edgeSensitiveBlur[0], CSEdgeSensitiveBlur1, 1, 1);
	COMPUTE_SHADER_INIT(edgeSensitiveBlur[1], CSEdgeSensitiveBlur2, 1, 1);
	COMPUTE_SHADER_INIT(edgeSensitiveBlur[2], CSEdgeSensitiveBlur3, 1, 1);
	COMPUTE_SHADER_INIT(edgeSensitiveBlur[3], CSEdgeSensitiveBlur4, 1, 1);
	COMPUTE_SHADER_INIT(edgeSensitiveBlur[4], CSEdgeSensitiveBlur5, 1, 1);
	COMPUTE_SHADER_INIT(edgeSensitiveBlur[5], CSEdgeSensitiveBlur6, 1, 1);
	COMPUTE_SHADER_INIT(edgeSensitiveBlur[6], CSEdgeSensitiveBlur7, 1, 1);
	COMPUTE_SHADER_INIT(edgeSensitiveBlur[7], CSEdgeSensitiveBlur8, 1, 1);

	for (int i = 0; i < 4; ++i)
	{
		cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->edgeSensitiveBlurOutput[i], 1);
		cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->edgeSensitiveBlurInput[i], 1);
	}

	// =====================================
	// Apply shaders/resources

	COMPUTE_SHADER_INIT(smartApply, CSApply, 1, 1);
	COMPUTE_SHADER_INIT(nonSmartApply, CSNonSmartApply, 1, 1);
	COMPUTE_SHADER_INIT(nonSmartHalfApply, CSNonSmartHalfApply, 1, 1);

	cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->createOutputInputsPing, 1);
	cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->createOutputInputsPong, 1);
	cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->createOutputOutputs, 1);

	// =====================================
	// Upacale shaders/resources

	COMPUTE_SHADER_INIT(upscaleBilateral5x5, CSUpscaleBilateral5x5, 1, 4);
	COMPUTE_SHADER_INIT(upscaleBilateral5x5Half, CSUpscaleBilateral5x5Half, 1, 4);

	cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->bilateralUpscaleInputsPing, 4);
	cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->bilateralUpscaleInputsPong, 4);
	cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->bilateralUpscaleOutputs, 1);

	// =====================================
	// Misc

	errorStatus = textureInit(&context->loadCounter, device, "CACAO::m_loadCounter", &CD3DX12_RESOURCE_DESC::Tex1D(DXGI_FORMAT_R32_UINT, 1, 1, 1, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL);
	if (errorStatus)
	{
		goto error_create_load_counter_texture;
	}

	// create uav for load counter
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_R32_UINT;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
		uavDesc.Texture1D.MipSlice = 0;

		cbvSrvUavHeapAllocDescriptor(cbvSrvUavHeap, &context->loadCounterUav, 1); // required for clearing the load counter
		textureCreateUavFromDesc(&context->loadCounter, 0, &context->loadCounterUav, &uavDesc);
	}

	return FFX_CACAO_STATUS_OK;

error_create_load_counter_texture:

	ERROR_COMPUTE_SHADER_DESTROY(upscaleBilateral5x5Half, CSUpscaleBilateral5x5Half);
	ERROR_COMPUTE_SHADER_DESTROY(upscaleBilateral5x5, CSUpscaleBilateral5x5);

	ERROR_COMPUTE_SHADER_DESTROY(nonSmartHalfApply, CSNonSmartHalfApply);
	ERROR_COMPUTE_SHADER_DESTROY(nonSmartApply, CSNonSmartApply);
	ERROR_COMPUTE_SHADER_DESTROY(smartApply, CSApply);

	ERROR_COMPUTE_SHADER_DESTROY(edgeSensitiveBlur[7], CSEdgeSensitiveBlur8);
	ERROR_COMPUTE_SHADER_DESTROY(edgeSensitiveBlur[6], CSEdgeSensitiveBlur7);
	ERROR_COMPUTE_SHADER_DESTROY(edgeSensitiveBlur[5], CSEdgeSensitiveBlur6);
	ERROR_COMPUTE_SHADER_DESTROY(edgeSensitiveBlur[4], CSEdgeSensitiveBlur5);
	ERROR_COMPUTE_SHADER_DESTROY(edgeSensitiveBlur[3], CSEdgeSensitiveBlur4);
	ERROR_COMPUTE_SHADER_DESTROY(edgeSensitiveBlur[2], CSEdgeSensitiveBlur3);
	ERROR_COMPUTE_SHADER_DESTROY(edgeSensitiveBlur[1], CSEdgeSensitiveBlur2);
	ERROR_COMPUTE_SHADER_DESTROY(edgeSensitiveBlur[0], CSEdgeSensitiveBlur1);

	ERROR_COMPUTE_SHADER_DESTROY(postprocessImportanceMapB, CSPostprocessImportanceMapB);
	ERROR_COMPUTE_SHADER_DESTROY(postprocessImportanceMapA, CSPostprocessImportanceMapA);
	ERROR_COMPUTE_SHADER_DESTROY(generateImportanceMap, CSGenerateImportanceMap);

	ERROR_COMPUTE_SHADER_DESTROY(generateSSAO[4], CSGenerateQ3Base);
	ERROR_COMPUTE_SHADER_DESTROY(generateSSAO[3], CSGenerateQ3);
	ERROR_COMPUTE_SHADER_DESTROY(generateSSAO[2], CSGenerateQ2);
	ERROR_COMPUTE_SHADER_DESTROY(generateSSAO[1], CSGenerateQ1);
	ERROR_COMPUTE_SHADER_DESTROY(generateSSAO[0], CSGenerateQ0);

	ERROR_COMPUTE_SHADER_DESTROY(prepareNativeDepths, CSPrepareNativeDepths);
	ERROR_COMPUTE_SHADER_DESTROY(prepareDownsampledDepths, CSPrepareDownsampledDepths);

	ERROR_COMPUTE_SHADER_DESTROY(prepareNativeNormalsFromInputNormals, CSPrepareNativeNormalsFromInputNormals);
	ERROR_COMPUTE_SHADER_DESTROY(prepareDownsampledNormalsFromInputNormals, CSPrepareDownsampledNormalsFromInputNormals);

	ERROR_COMPUTE_SHADER_DESTROY(prepareNativeNormals, CSPrepareNativeNormals);
	ERROR_COMPUTE_SHADER_DESTROY(prepareDownsampledNormals, CSPrepareDownsampledNormals);

	ERROR_COMPUTE_SHADER_DESTROY(prepareNativeDepthsAndMips, CSPrepareNativeDepthsAndMips);
	ERROR_COMPUTE_SHADER_DESTROY(prepareDownsampledDepthsAndMips, CSPrepareDownsampledDepthsAndMips);

	ERROR_COMPUTE_SHADER_DESTROY(prepareNativeDepthsHalf, CSPrepareNativeDepthsHalf);
	ERROR_COMPUTE_SHADER_DESTROY(prepareDownsampledDepthsHalf, CSPrepareDownsampledDepthsHalf);

#ifdef FFX_CACAO_ENABLE_PROFILING
	gpuTimerDestroy(&context->gpuTimer);
error_create_gpu_timer:
#endif
	constantBufferRingDestroy(&context->constantBufferRing);
error_create_constant_buffer_ring:
	cbvSrvUavHeapDestroy(&context->cbvSrvUavHeap);
error_create_cbv_srv_uav_heap:

	return errorStatus;
	
#undef COMPUTE_SHADER_INIT
#undef ERROR_COMPUTE_SHADER_DESTROY
}

FfxCacaoStatus ffxCacaoD3D12DestroyContext(FfxCacaoD3D12Context* context)
{
	if (context == NULL)
	{
		return FFX_CACAO_STATUS_INVALID_POINTER;
	}
	context = getAlignedD3D12ContextPointer(context);

	textureDestroy(&context->loadCounter);

	computeShaderDestroy(&context->upscaleBilateral5x5Half);
	computeShaderDestroy(&context->upscaleBilateral5x5);

	computeShaderDestroy(&context->nonSmartHalfApply);
	computeShaderDestroy(&context->nonSmartApply);
	computeShaderDestroy(&context->smartApply);

	computeShaderDestroy(&context->edgeSensitiveBlur[7]);
	computeShaderDestroy(&context->edgeSensitiveBlur[6]);
	computeShaderDestroy(&context->edgeSensitiveBlur[5]);
	computeShaderDestroy(&context->edgeSensitiveBlur[4]);
	computeShaderDestroy(&context->edgeSensitiveBlur[3]);
	computeShaderDestroy(&context->edgeSensitiveBlur[2]);
	computeShaderDestroy(&context->edgeSensitiveBlur[1]);
	computeShaderDestroy(&context->edgeSensitiveBlur[0]);

	computeShaderDestroy(&context->postprocessImportanceMapB);
	computeShaderDestroy(&context->postprocessImportanceMapA);
	computeShaderDestroy(&context->generateImportanceMap);

	computeShaderDestroy(&context->generateSSAO[4]);
	computeShaderDestroy(&context->generateSSAO[3]);
	computeShaderDestroy(&context->generateSSAO[2]);
	computeShaderDestroy(&context->generateSSAO[1]);
	computeShaderDestroy(&context->generateSSAO[0]);

	computeShaderDestroy(&context->prepareNativeDepths);
	computeShaderDestroy(&context->prepareDownsampledDepths);

	computeShaderDestroy(&context->prepareNativeNormalsFromInputNormals);
	computeShaderDestroy(&context->prepareDownsampledNormalsFromInputNormals);

	computeShaderDestroy(&context->prepareNativeNormals);
	computeShaderDestroy(&context->prepareDownsampledNormals);

	computeShaderDestroy(&context->prepareNativeDepthsAndMips);
	computeShaderDestroy(&context->prepareDownsampledDepthsAndMips);

	computeShaderDestroy(&context->prepareNativeDepthsHalf);
	computeShaderDestroy(&context->prepareDownsampledDepthsHalf);

#ifdef FFX_CACAO_ENABLE_PROFILING
	gpuTimerDestroy(&context->gpuTimer);
#endif
	constantBufferRingDestroy(&context->constantBufferRing);
	cbvSrvUavHeapDestroy(&context->cbvSrvUavHeap);

	return FFX_CACAO_STATUS_OK;
}

FfxCacaoStatus ffxCacaoD3D12InitScreenSizeDependentResources(FfxCacaoD3D12Context* context, const FfxCacaoD3D12ScreenSizeInfo* info)
{
	if (context == NULL)
	{
		return FFX_CACAO_STATUS_INVALID_POINTER;
	}
	if (info == NULL)
	{
		return FFX_CACAO_STATUS_INVALID_POINTER;
	}
	context = getAlignedD3D12ContextPointer(context);

#ifdef FFX_CACAO_ENABLE_NATIVE_RESOLUTION
	FfxCacaoBool useDownsampledSsao = info->useDownsampledSsao;
#else
	FfxCacaoBool useDownsampledSsao = FFX_CACAO_TRUE;
#endif
	context->useDownsampledSsao = useDownsampledSsao;
	FfxCacaoStatus errorStatus;

#define TEXTURE_INIT(name, label, format, width, height, arraySize, mipLevels) \
	errorStatus = textureInit(&context->name, device, "CACAO::" #name, &CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, arraySize, mipLevels, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL); \
	if (errorStatus) \
	{ \
		goto error_create_texture_ ## label;\
	}
#define ERROR_TEXTURE_DESTROY(name, label) \
	textureDestroy(&context->name); \
error_create_texture_ ## label:


	ID3D12Device * device = context->device;

	uint32_t width = info->width;
	uint32_t height = info->height;
	uint32_t halfWidth = (width + 1) / 2;
	uint32_t halfHeight = (height + 1) / 2;
	uint32_t quarterWidth = (halfWidth + 1) / 2;
	uint32_t quarterHeight = (halfHeight + 1) / 2;
	uint32_t eighthWidth = (quarterWidth + 1) / 2;
	uint32_t eighthHeight = (quarterHeight + 1) / 2;

#if 1
	uint32_t depthBufferWidth = width;
	uint32_t depthBufferHeight = height;
	uint32_t depthBufferHalfWidth = halfWidth;
	uint32_t depthBufferHalfHeight = halfHeight;
	uint32_t depthBufferQuarterWidth = quarterWidth;
	uint32_t depthBufferQuarterHeight = quarterHeight;

	uint32_t depthBufferXOffset = 0;
	uint32_t depthBufferYOffset = 0;
	uint32_t depthBufferHalfXOffset = 0;
	uint32_t depthBufferHalfYOffset = 0;
	uint32_t depthBufferQuarterXOffset = 0;
	uint32_t depthBufferQuarterYOffset = 0;
#else
	uint32_t depthBufferWidth = info->depthBufferWidth;
	uint32_t depthBufferHeight = info->depthBufferHeight;
	uint32_t depthBufferHalfWidth = (depthBufferWidth + 1) / 2;
	uint32_t depthBufferHalfHeight = (depthBufferHeight + 1) / 2;
	uint32_t depthBufferQuarterWidth = (depthBufferHalfWidth + 1) / 2;
	uint32_t depthBufferQuarterHeight = (depthBufferHalfHeight + 1) / 2;

	uint32_t depthBufferXOffset = info->depthBufferXOffset;
	uint32_t depthBufferYOffset = info->depthBufferYOffset;
	uint32_t depthBufferHalfXOffset = (depthBufferXOffset + 1) / 2; // XXX - is this really right?
	uint32_t depthBufferHalfYOffset = (depthBufferYOffset + 1) / 2; // XXX - is this really right?
	uint32_t depthBufferQuarterXOffset = (depthBufferHalfXOffset + 1) / 2; // XXX - is this really right?
	uint32_t depthBufferQuarterYOffset = (depthBufferHalfYOffset + 1) / 2; // XXX - is this really right?
#endif

	BufferSizeInfo bsi = {};
	bsi.inputOutputBufferWidth = width;
	bsi.inputOutputBufferHeight = height;
	bsi.depthBufferXOffset = depthBufferXOffset;
	bsi.depthBufferYOffset = depthBufferYOffset;
	bsi.depthBufferWidth = depthBufferWidth;
	bsi.depthBufferHeight = depthBufferHeight;

	if (useDownsampledSsao)
	{
		bsi.ssaoBufferWidth = quarterWidth;
		bsi.ssaoBufferHeight = quarterHeight;
		bsi.deinterleavedDepthBufferXOffset = depthBufferQuarterXOffset;
		bsi.deinterleavedDepthBufferYOffset = depthBufferQuarterYOffset;
		bsi.deinterleavedDepthBufferWidth = depthBufferQuarterWidth;
		bsi.deinterleavedDepthBufferHeight = depthBufferQuarterHeight;
		bsi.importanceMapWidth = eighthWidth;
		bsi.importanceMapHeight = eighthHeight;
	}
	else
	{
		bsi.ssaoBufferWidth = halfWidth;
		bsi.ssaoBufferHeight = halfHeight;
		bsi.deinterleavedDepthBufferXOffset = depthBufferHalfXOffset;
		bsi.deinterleavedDepthBufferYOffset = depthBufferHalfYOffset;
		bsi.deinterleavedDepthBufferWidth = depthBufferHalfWidth;
		bsi.deinterleavedDepthBufferHeight = depthBufferHalfHeight;
		bsi.importanceMapWidth = quarterWidth;
		bsi.importanceMapHeight = quarterHeight;
	}
	
	context->bufferSizeInfo = bsi;

	// =======================================
	// allocate intermediate textures

	TEXTURE_INIT(deinterleavedDepths, deinterleaved_depths, DXGI_FORMAT_R16_FLOAT, bsi.deinterleavedDepthBufferWidth, bsi.deinterleavedDepthBufferHeight, 4, 4);
	TEXTURE_INIT(deinterleavedNormals, deinterleaved_normals, DXGI_FORMAT_R8G8B8A8_SNORM, bsi.ssaoBufferWidth, bsi.ssaoBufferHeight, 4, 1);

	TEXTURE_INIT(ssaoBufferPing, ssao_buffer_ping, DXGI_FORMAT_R8G8_UNORM, bsi.ssaoBufferWidth, bsi.ssaoBufferHeight, 4, 1);
	TEXTURE_INIT(ssaoBufferPong, ssao_buffer_pong, DXGI_FORMAT_R8G8_UNORM, bsi.ssaoBufferWidth, bsi.ssaoBufferHeight, 4, 1);

	TEXTURE_INIT(importanceMap, importance_map, DXGI_FORMAT_R8_UNORM, bsi.importanceMapWidth, bsi.importanceMapHeight, 1, 1);
	TEXTURE_INIT(importanceMapPong, importance_map_pong, DXGI_FORMAT_R8_UNORM, bsi.importanceMapWidth, bsi.importanceMapHeight, 1, 1);

	// =======================================
	// Init Prepare SRVs/UAVs

	for (int i = 0; i < 4; ++i)
	{
		textureCreateUav(&context->deinterleavedDepths, i, &context->prepareDepthsAndMipsOutputs, i, 4, 0);
	}
	textureCreateUav(&context->deinterleavedDepths, 0, &context->prepareDepthsOutputs, 0, 4, 0);
	textureCreateUav(&context->deinterleavedNormals, 0, &context->prepareNormalsOutput, 0, 4, 0);

	device->CreateShaderResourceView(info->depthBufferResource, &info->depthBufferSrvDesc, context->prepareDepthsNormalsAndMipsInputs.cpuDescriptor);

	textureCreateUav(&context->deinterleavedNormals, 0, &context->prepareNormalsFromInputNormalsOutput, 0, 4, 0);
	device->CreateShaderResourceView(info->normalBufferResource, &info->normalBufferSrvDesc, context->prepareNormalsFromInputNormalsInput.cpuDescriptor);

	// =======================================
	// Init Generate SSAO SRVs/UAVs

	for (int i = 0; i < 4; ++i)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R32_UINT;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture1D.MostDetailedMip = 0;
		srvDesc.Texture1D.MipLevels = 1;

		D3D12_SHADER_RESOURCE_VIEW_DESC zeroTextureSRVDesc = {};
		zeroTextureSRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
		zeroTextureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
		zeroTextureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		zeroTextureSRVDesc.Texture1D.MostDetailedMip = 0;
		zeroTextureSRVDesc.Texture1D.MipLevels = 1;

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_R32_UINT;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
		uavDesc.Texture1D.MipSlice = 0;

		textureCreateSrv(&context->deinterleavedDepths, 0, &context->generateSSAOInputs[i], -1, 1, i);
		textureCreateSrv(&context->deinterleavedNormals, 6, &context->generateSSAOInputs[i], 0, 4, 0);


		textureCreateSrv(&context->deinterleavedDepths, 0, &context->generateAdaptiveSSAOInputs[i], -1, 1, i);
		textureCreateSrvFromDesc(&context->loadCounter, 2, &context->generateAdaptiveSSAOInputs[i], &srvDesc);
		textureCreateSrv(&context->importanceMap, 3, &context->generateAdaptiveSSAOInputs[i], -1, -1, -1);
		textureCreateSrv(&context->ssaoBufferPong, 4, &context->generateAdaptiveSSAOInputs[i], -1, -1, -1);
		textureCreateSrv(&context->deinterleavedNormals, 6, &context->generateAdaptiveSSAOInputs[i], 0, 4, 0);

		textureCreateUav(&context->ssaoBufferPing, 0, &context->generateSSAOOutputsPing[i], 0, 1, i);

		textureCreateUav(&context->ssaoBufferPong, 0, &context->generateSSAOOutputsPong[i], 0, 1, i);

	}

	// =======================================
	// Init Generate/Postprocess Importance map SRVs/UAVs

	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_R32_UINT;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
		uavDesc.Texture1D.MipSlice = 0;

		textureCreateSrv(&context->ssaoBufferPong, 0, &context->generateImportanceMapInputs, -1, -1, -1);
		textureCreateUav(&context->importanceMap, 0, &context->generateImportanceMapOutputs, -1, -1, -1);

		textureCreateSrv(&context->importanceMap, 0, &context->generateImportanceMapAInputs, -1, -1, -1);
		textureCreateUav(&context->importanceMapPong, 0, &context->generateImportanceMapAOutputs, -1, -1, -1);

		textureCreateSrv(&context->importanceMapPong, 0, &context->generateImportanceMapBInputs, -1, -1, -1);
		textureCreateUav(&context->importanceMap, 0, &context->generateImportanceMapBOutputs, -1, -1, -1);
		textureCreateUavFromDesc(&context->loadCounter, 1, &context->generateImportanceMapBOutputs, &uavDesc);
	}

	// =======================================
	// Init De-interleave Blur SRVs/UAVs

	for (int i = 0; i < 4; ++i)
	{
		textureCreateSrv(&context->ssaoBufferPing, 0, &context->edgeSensitiveBlurInput[i], 0, 1, i);
		textureCreateUav(&context->ssaoBufferPong, 0, &context->edgeSensitiveBlurOutput[i], 0, 1, i);
	}

	// =======================================
	// Init apply SRVs/UAVs

	textureCreateSrv(&context->ssaoBufferPing, 0, &context->createOutputInputsPing, 0, 4, 0);
	textureCreateSrv(&context->ssaoBufferPong, 0, &context->createOutputInputsPong, 0, 4, 0);

	context->device->CreateUnorderedAccessView(info->outputResource, NULL, &info->outputUavDesc, context->createOutputOutputs.cpuDescriptor);
	context->device->CreateUnorderedAccessView(info->outputResource, NULL, &info->outputUavDesc, context->createOutputOutputs.cpuVisibleCpuDescriptor);

	// =======================================
	// Init upscale SRVs/UAVs

	textureCreateSrv(&context->ssaoBufferPing, 0, &context->bilateralUpscaleInputsPing, -1, -1, -1);
	context->device->CreateShaderResourceView(info->depthBufferResource, &info->depthBufferSrvDesc, cbvSrvUavGetCpu(&context->bilateralUpscaleInputsPing, 1));
	textureCreateSrv(&context->deinterleavedDepths, 3, &context->bilateralUpscaleInputsPing, 0, -1, -1);

	textureCreateSrv(&context->ssaoBufferPong, 0, &context->bilateralUpscaleInputsPong, -1, -1, -1);
	context->device->CreateShaderResourceView(info->depthBufferResource, &info->depthBufferSrvDesc, cbvSrvUavGetCpu(&context->bilateralUpscaleInputsPong, 1));
	textureCreateSrv(&context->deinterleavedDepths, 3, &context->bilateralUpscaleInputsPong, 0, -1, -1);

	context->device->CreateUnorderedAccessView(info->outputResource, NULL, &info->outputUavDesc, context->bilateralUpscaleOutputs.cpuDescriptor);

	// =======================================
	// Init debug SRVs/UAVs

	context->outputResource = info->outputResource;

	return FFX_CACAO_STATUS_OK;

	ERROR_TEXTURE_DESTROY(importanceMapPong, importance_map_pong);
	ERROR_TEXTURE_DESTROY(importanceMap, importance_map);

	ERROR_TEXTURE_DESTROY(ssaoBufferPong, ssao_buffer_pong);
	ERROR_TEXTURE_DESTROY(ssaoBufferPing, ssao_buffer_ping);

	ERROR_TEXTURE_DESTROY(deinterleavedNormals, deinterleaved_normals);
	ERROR_TEXTURE_DESTROY(deinterleavedDepths, deinterleaved_depths);

	return errorStatus;

#undef TEXTURE_INIT
#undef ERROR_TEXTURE_DESTROY
}

FfxCacaoStatus ffxCacaoD3D12DestroyScreenSizeDependentResources(FfxCacaoD3D12Context* context)
{
	if (context == NULL)
	{
		return FFX_CACAO_STATUS_INVALID_POINTER;
	}
	context = getAlignedD3D12ContextPointer(context);

	textureDestroy(&context->importanceMapPong);
	textureDestroy(&context->importanceMap);

	textureDestroy(&context->ssaoBufferPong);
	textureDestroy(&context->ssaoBufferPing);

	textureDestroy(&context->deinterleavedNormals);
	textureDestroy(&context->deinterleavedDepths);

	return FFX_CACAO_STATUS_OK;
}

FfxCacaoStatus ffxCacaoD3D12UpdateSettings(FfxCacaoD3D12Context* context, const FfxCacaoSettings* settings)
{
	if (context == NULL || settings == NULL)
	{
		return FFX_CACAO_STATUS_INVALID_POINTER;
	}
	context = getAlignedD3D12ContextPointer(context);

	memcpy(&context->settings, settings, sizeof(*settings));

	return FFX_CACAO_STATUS_OK;
}

FfxCacaoStatus ffxCacaoD3D12Draw(FfxCacaoD3D12Context* context, ID3D12GraphicsCommandList* commandList, const FfxCacaoMatrix4x4* proj, const FfxCacaoMatrix4x4* normalsToView)
{
	if (context == NULL || commandList == NULL || proj == NULL)
	{
		return FFX_CACAO_STATUS_INVALID_POINTER;
	}
	context = getAlignedD3D12ContextPointer(context);

	
#ifdef FFX_CACAO_ENABLE_PROFILING
#define GET_TIMESTAMP(name) gpuTimerGetTimestamp(&context->gpuTimer, commandList, TIMESTAMP_##name)
#else
#define GET_TIMESTAMP(name)
#endif
	BufferSizeInfo *bsi = &context->bufferSizeInfo;


	USER_MARKER("FidelityFX CACAO");

	constantBufferRingStartFrame(&context->constantBufferRing);

#ifdef FFX_CACAO_ENABLE_PROFILING
	gpuTimerStartFrame(&context->gpuTimer);
#endif

	GET_TIMESTAMP(BEGIN);

	// set the descriptor heaps
	{
		ID3D12DescriptorHeap *descriptorHeaps[] = { context->cbvSrvUavHeap.heap };
		commandList->SetDescriptorHeaps(FFX_CACAO_ARRAY_SIZE(descriptorHeaps), descriptorHeaps);
	}

	// clear load counter
	{
		UINT clearValue[] = { 0, 0, 0, 0 };
		commandList->ClearUnorderedAccessViewUint(context->loadCounterUav.gpuDescriptor, context->loadCounterUav.cpuVisibleCpuDescriptor, context->loadCounter.resource, clearValue, 0, NULL);
	}

	// move this to initialisation
	D3D12_GPU_VIRTUAL_ADDRESS cbCACAOHandle;
	FfxCacaoConstants *pCACAOConsts;
	D3D12_GPU_VIRTUAL_ADDRESS cbCACAOPerPassHandle[4];
	FfxCacaoConstants *pPerPassConsts[4];

	// upload constant buffers
	{
		constantBufferRingAlloc(&context->constantBufferRing, sizeof(*pCACAOConsts), (void**)&pCACAOConsts, &cbCACAOHandle);
		updateConstants(pCACAOConsts, &context->settings, bsi, proj, normalsToView);

		for (int i = 0; i < 4; ++i)
		{
			constantBufferRingAlloc(&context->constantBufferRing, sizeof(*pPerPassConsts[0]), (void**)&pPerPassConsts[i], &cbCACAOPerPassHandle[i]);
			updateConstants(pPerPassConsts[i], &context->settings, bsi, proj, normalsToView);
			updatePerPassConstants(pPerPassConsts[i], &context->settings, &context->bufferSizeInfo, i);
		}
	}

	// prepare depths, normals and mips
	{
		USER_MARKER("Prepare downsampled depths, normals and mips");


		switch (context->settings.qualityLevel)
		{
		case FFX_CACAO_QUALITY_LOWEST: {
			uint32_t dispatchWidth = dispatchSize(PREPARE_DEPTHS_HALF_WIDTH, bsi->deinterleavedDepthBufferWidth);
			uint32_t dispatchHeight = dispatchSize(PREPARE_DEPTHS_HALF_HEIGHT, bsi->deinterleavedDepthBufferHeight);
			ComputeShader *prepareDepthsHalf = context->useDownsampledSsao ? &context->prepareDownsampledDepthsHalf : &context->prepareNativeDepthsHalf;
			computeShaderDraw(prepareDepthsHalf, commandList, cbCACAOHandle, &context->prepareDepthsOutputs, &context->prepareDepthsNormalsAndMipsInputs, dispatchWidth, dispatchHeight, 1);
			break;
		}
		case FFX_CACAO_QUALITY_LOW: {
			uint32_t dispatchWidth = dispatchSize(PREPARE_DEPTHS_WIDTH, bsi->deinterleavedDepthBufferWidth);
			uint32_t dispatchHeight = dispatchSize(PREPARE_DEPTHS_HEIGHT, bsi->deinterleavedDepthBufferHeight);
			ComputeShader *prepareDepths = context->useDownsampledSsao ? &context->prepareDownsampledDepths : &context->prepareNativeDepths;
			computeShaderDraw(prepareDepths, commandList, cbCACAOHandle, &context->prepareDepthsOutputs, &context->prepareDepthsNormalsAndMipsInputs, dispatchWidth, dispatchHeight, 1);
			break;
		}
		default: {
			uint32_t dispatchWidth = dispatchSize(PREPARE_DEPTHS_AND_MIPS_WIDTH, bsi->deinterleavedDepthBufferWidth);
			uint32_t dispatchHeight = dispatchSize(PREPARE_DEPTHS_AND_MIPS_HEIGHT, bsi->deinterleavedDepthBufferHeight);
			ComputeShader *prepareDepthsAndMips = context->useDownsampledSsao ? &context->prepareDownsampledDepthsAndMips : &context->prepareNativeDepthsAndMips;
			computeShaderDraw(prepareDepthsAndMips, commandList, cbCACAOHandle, &context->prepareDepthsAndMipsOutputs, &context->prepareDepthsNormalsAndMipsInputs, dispatchWidth, dispatchHeight, 1);
			break;
		}
		}

		if (context->settings.generateNormals)
		{
			uint32_t dispatchWidth = dispatchSize(PREPARE_NORMALS_WIDTH, bsi->ssaoBufferWidth);
			uint32_t dispatchHeight = dispatchSize(PREPARE_NORMALS_HEIGHT, bsi->ssaoBufferHeight);
			ComputeShader *prepareNormals = context->useDownsampledSsao ? &context->prepareDownsampledNormals : &context->prepareNativeNormals;
			computeShaderDraw(prepareNormals, commandList, cbCACAOHandle, &context->prepareNormalsOutput, &context->prepareDepthsNormalsAndMipsInputs, dispatchWidth, dispatchHeight, 1);
		}
		else
		{
			uint32_t dispatchWidth = dispatchSize(PREPARE_NORMALS_FROM_INPUT_NORMALS_WIDTH, bsi->ssaoBufferWidth);
			uint32_t dispatchHeight = dispatchSize(PREPARE_NORMALS_FROM_INPUT_NORMALS_HEIGHT, bsi->ssaoBufferHeight);
			ComputeShader *prepareNormalsFromInputNormals = context->useDownsampledSsao ? &context->prepareDownsampledNormalsFromInputNormals : &context->prepareNativeNormalsFromInputNormals;
			computeShaderDraw(prepareNormalsFromInputNormals, commandList, cbCACAOHandle, &context->prepareNormalsFromInputNormalsOutput, &context->prepareNormalsFromInputNormalsInput, dispatchWidth, dispatchHeight, 1);
		}

		GET_TIMESTAMP(PREPARE);
	}

	// deinterleaved depths and normals are now read only resources, also used in the next stage
	{
		D3D12_RESOURCE_BARRIER resourceBarriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(context->deinterleavedDepths.resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(context->deinterleavedNormals.resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		};
		commandList->ResourceBarrier(FFX_CACAO_ARRAY_SIZE(resourceBarriers), resourceBarriers);
	}

	// base pass for highest quality setting
	if (context->settings.qualityLevel == FFX_CACAO_QUALITY_HIGHEST)
	{
		USER_MARKER("Generate High Quality Base Pass");

		// SSAO
		{
			USER_MARKER("SSAO");

			for (int pass = 0; pass < 4; ++pass)
			{
				CbvSrvUav *inputs = &context->generateSSAOInputs[pass];
				uint32_t dispatchWidth = dispatchSize(GENERATE_WIDTH, bsi->ssaoBufferWidth);
				uint32_t dispatchHeight = dispatchSize(GENERATE_WIDTH, bsi->ssaoBufferHeight);
				computeShaderDraw(&context->generateSSAO[4], commandList, cbCACAOPerPassHandle[pass], &context->generateSSAOOutputsPong[pass], inputs, dispatchWidth, dispatchHeight, 1);
			}
			GET_TIMESTAMP(BASE_SSAO_PASS);
		}

		// results written by base pass are now a reaad only resource, used in next stage
		commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(context->ssaoBufferPong.resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

		// generate importance map
		{
			USER_MARKER("Importance Map");

			CD3DX12_RESOURCE_BARRIER barriers[2];
			UINT barrierCount;

			uint32_t dispatchWidth = dispatchSize(IMPORTANCE_MAP_WIDTH, bsi->importanceMapWidth);
			uint32_t dispatchHeight = dispatchSize(IMPORTANCE_MAP_HEIGHT, bsi->importanceMapHeight);

			computeShaderDraw(&context->generateImportanceMap, commandList, cbCACAOHandle, &context->generateImportanceMapOutputs, &context->generateImportanceMapInputs, dispatchWidth, dispatchHeight, 1);

			barrierCount = 0;
			barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(context->importanceMap.resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			commandList->ResourceBarrier(barrierCount, barriers);

			computeShaderDraw(&context->postprocessImportanceMapA, commandList, cbCACAOHandle, &context->generateImportanceMapAOutputs, &context->generateImportanceMapAInputs, dispatchWidth, dispatchHeight, 1);

			barrierCount = 0;
			barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(context->importanceMap.resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(context->importanceMapPong.resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			commandList->ResourceBarrier(barrierCount, barriers);

			computeShaderDraw(&context->postprocessImportanceMapB, commandList, cbCACAOHandle, &context->generateImportanceMapBOutputs, &context->generateImportanceMapBInputs, dispatchWidth, dispatchHeight, 1);

			barrierCount = 0;
			barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(context->importanceMap.resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(context->loadCounter.resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			commandList->ResourceBarrier(barrierCount, barriers);

			GET_TIMESTAMP(IMPORTANCE_MAP);
		}
	}

	int blurPassCount = context->settings.blurPassCount;
	blurPassCount = FFX_CACAO_CLAMP(blurPassCount, 0, MAX_BLUR_PASSES);
	
	// main ssao generation
	{
		USER_MARKER("Generate SSAO");

		ComputeShader *generate = &context->generateSSAO[FFX_CACAO_MAX(0, context->settings.qualityLevel - 1)];
		for (int pass = 0; pass < 4; ++pass)
		{
			if (context->settings.qualityLevel == FFX_CACAO_QUALITY_LOWEST && (pass == 1 || pass == 2))
			{
				continue;
			}

			CbvSrvUav *input = context->settings.qualityLevel == FFX_CACAO_QUALITY_HIGHEST ? &context->generateAdaptiveSSAOInputs[pass] : &context->generateSSAOInputs[pass];
			CbvSrvUav *output = &context->generateSSAOOutputsPing[pass]; // blurPassCount == 0 ? &context->generateSSAOOutputsPing[pass] : &context->generateSSAOOutputsPong[pass];

			uint32_t dispatchWidth = dispatchSize(GENERATE_WIDTH, bsi->ssaoBufferWidth);
			uint32_t dispatchHeight = dispatchSize(GENERATE_WIDTH, bsi->ssaoBufferHeight);
			computeShaderDraw(generate, commandList, cbCACAOPerPassHandle[pass], output, input, dispatchWidth, dispatchHeight, 1);
		}

		GET_TIMESTAMP(GENERATE_SSAO);
	}
	
	// de-interleaved blur
	if (blurPassCount)
	{
		// only need to transition pong to writable if we didn't already use it in the base pass
		CD3DX12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(context->ssaoBufferPing.resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(context->ssaoBufferPong.resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		};
		commandList->ResourceBarrier(context->settings.qualityLevel == FFX_CACAO_QUALITY_HIGHEST ? 2 : 1, barriers);

		USER_MARKER("Deinterleaved blur");

		for (int pass = 0; pass < 4; ++pass)
		{
			if (context->settings.qualityLevel == FFX_CACAO_QUALITY_LOWEST && (pass == 1 || pass == 2))
			{
				continue;
			}

			uint32_t w = 4 * BLUR_WIDTH - 2 * blurPassCount;
			uint32_t h = 3 * BLUR_HEIGHT - 2 * blurPassCount;
			uint32_t blurPassIndex = blurPassCount - 1;
			uint32_t dispatchWidth = dispatchSize(w, bsi->ssaoBufferWidth);
			uint32_t dispatchHeight = dispatchSize(h, bsi->ssaoBufferHeight);
			computeShaderDraw(&context->edgeSensitiveBlur[blurPassIndex], commandList, cbCACAOPerPassHandle[pass], &context->edgeSensitiveBlurOutput[pass], &context->edgeSensitiveBlurInput[pass], dispatchWidth, dispatchHeight, 1);
		}

		GET_TIMESTAMP(EDGE_SENSITIVE_BLUR);

		commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(context->ssaoBufferPong.resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	}
	else
	{
		commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(context->ssaoBufferPing.resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	}


	if (context->useDownsampledSsao)
	{
		USER_MARKER("Upscale");

		CbvSrvUav *inputs = blurPassCount ? &context->bilateralUpscaleInputsPong : &context->bilateralUpscaleInputsPing;
		ComputeShader *upscaler = context->settings.qualityLevel == FFX_CACAO_QUALITY_LOWEST ? &context->upscaleBilateral5x5Half : &context->upscaleBilateral5x5;
		uint32_t dispatchWidth = dispatchSize(2 * BILATERAL_UPSCALE_WIDTH, bsi->inputOutputBufferWidth);
		uint32_t dispatchHeight = dispatchSize(2 * BILATERAL_UPSCALE_HEIGHT, bsi->inputOutputBufferHeight);
		computeShaderDraw(upscaler, commandList, cbCACAOHandle, &context->bilateralUpscaleOutputs, inputs, dispatchWidth, dispatchHeight, 1);

		GET_TIMESTAMP(BILATERAL_UPSAMPLE);
	}
	else
	{
		USER_MARKER("Create Output");
		CbvSrvUav *inputs = blurPassCount ? &context->createOutputInputsPong : &context->createOutputInputsPing;
		uint32_t dispatchWidth = dispatchSize(APPLY_WIDTH, bsi->inputOutputBufferWidth);
		uint32_t dispatchHeight = dispatchSize(APPLY_HEIGHT, bsi->inputOutputBufferHeight);
		switch (context->settings.qualityLevel)
		{
		case FFX_CACAO_QUALITY_LOWEST:
			computeShaderDraw(&context->nonSmartHalfApply, commandList, cbCACAOHandle, &context->createOutputOutputs, inputs, dispatchWidth, dispatchHeight, 1);
			break;
		case FFX_CACAO_QUALITY_LOW:
			computeShaderDraw(&context->nonSmartApply, commandList, cbCACAOHandle, &context->createOutputOutputs, inputs, dispatchWidth, dispatchHeight, 1);
			break;
		default:
			computeShaderDraw(&context->smartApply, commandList, cbCACAOHandle, &context->createOutputOutputs, inputs, dispatchWidth, dispatchHeight, 1);
			break;
		}
		GET_TIMESTAMP(APPLY);
	}

	// end frame resource barrier
	{
		uint32_t numBarriers = 0;
		D3D12_RESOURCE_BARRIER resourceBarriers[10] = {};
		resourceBarriers[numBarriers++] = CD3DX12_RESOURCE_BARRIER::Transition(context->deinterleavedDepths.resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		resourceBarriers[numBarriers++] = CD3DX12_RESOURCE_BARRIER::Transition(context->deinterleavedNormals.resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		resourceBarriers[numBarriers++] = CD3DX12_RESOURCE_BARRIER::Transition(context->outputResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
		resourceBarriers[numBarriers++] = CD3DX12_RESOURCE_BARRIER::Transition(context->ssaoBufferPing.resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		if (context->settings.qualityLevel == FFX_CACAO_QUALITY_HIGHEST || blurPassCount)
		{
			resourceBarriers[numBarriers++] = CD3DX12_RESOURCE_BARRIER::Transition(context->ssaoBufferPong.resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
		if (context->settings.qualityLevel == FFX_CACAO_QUALITY_HIGHEST)
		{
			resourceBarriers[numBarriers++] = CD3DX12_RESOURCE_BARRIER::Transition(context->importanceMap.resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			resourceBarriers[numBarriers++] = CD3DX12_RESOURCE_BARRIER::Transition(context->importanceMapPong.resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			resourceBarriers[numBarriers++] = CD3DX12_RESOURCE_BARRIER::Transition(context->loadCounter.resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
		commandList->ResourceBarrier(numBarriers, resourceBarriers);
	}

#ifdef FFX_CACAO_ENABLE_PROFILING
	gpuTimerEndFrame(&context->gpuTimer, commandList);
#endif

	return FFX_CACAO_STATUS_OK;

#undef GET_TIMESTAMP
}

#ifdef FFX_CACAO_ENABLE_PROFILING
FfxCacaoStatus ffxCacaoD3D12GetDetailedTimings(FfxCacaoD3D12Context* context, FfxCacaoDetailedTiming* timings)
{
	if (context == NULL || timings == NULL)
	{
		return FFX_CACAO_STATUS_INVALID_POINTER;
	}
	context = getAlignedD3D12ContextPointer(context);

	gpuTimerCollectTimings(&context->gpuTimer, timings);

	return FFX_CACAO_STATUS_OK;
}
#endif
#endif

#ifdef FFX_CACAO_ENABLE_VULKAN
inline static void setObjectName(VkDevice device, FfxCacaoVkContext* context, VkObjectType type, uint64_t handle, const char* name)
{
	if (!context->vkSetDebugUtilsObjectName)
	{
		return;
	}

	VkDebugUtilsObjectNameInfoEXT info = {};
	info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	info.pNext = NULL;
	info.objectType = type;
	info.objectHandle = handle;
	info.pObjectName = name;

	VkResult result = context->vkSetDebugUtilsObjectName(device, &info);
	FFX_CACAO_ASSERT(result == VK_SUCCESS);
}

inline static uint32_t getBestMemoryHeapIndex(VkPhysicalDevice physicalDevice,  VkMemoryRequirements memoryRequirements, VkMemoryPropertyFlags desiredProperties)
{
	VkPhysicalDeviceMemoryProperties memoryProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

	uint32_t chosenMemoryTypeIndex = VK_MAX_MEMORY_TYPES;
	for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
	{
		uint32_t typeBit = 1 << i;
		// can we allocate to memory of this type
		if (memoryRequirements.memoryTypeBits & typeBit)
		{
			VkMemoryType currentMemoryType = memoryProperties.memoryTypes[i];
			// do we want to allocate to memory of this type
			if ((currentMemoryType.propertyFlags & desiredProperties) == desiredProperties)
			{
				chosenMemoryTypeIndex = i;
				break;
			}
		}
	}
	return chosenMemoryTypeIndex;
}

size_t ffxCacaoVkGetContextSize()
{
	return sizeof(FfxCacaoVkContext) + alignof(FfxCacaoVkContext) - 1;
}

FfxCacaoStatus ffxCacaoVkInitContext(FfxCacaoVkContext* context, const FfxCacaoVkCreateInfo* info)
{
	if (context == NULL)
	{
		return FFX_CACAO_STATUS_INVALID_POINTER;
	}
	if (info == NULL)
	{
		return FFX_CACAO_STATUS_INVALID_POINTER;
	}
	context = getAlignedVkContextPointer(context);
	memset(context, 0, sizeof(*context));

	VkDevice device = info->device;
	VkPhysicalDevice physicalDevice = info->physicalDevice;
	VkResult result;
	FfxCacaoBool use16Bit = info->flags & FFX_CACAO_VK_CREATE_USE_16_BIT ? FFX_CACAO_TRUE : FFX_CACAO_FALSE;
	FfxCacaoStatus errorStatus = FFX_CACAO_STATUS_FAILED;

	context->device = device;
	context->physicalDevice = physicalDevice;

	if (info->flags & FFX_CACAO_VK_CREATE_USE_DEBUG_MARKERS)
	{
		context->vkCmdDebugMarkerBegin = (PFN_vkCmdDebugMarkerBeginEXT)vkGetDeviceProcAddr(device, "vkCmdDebugMarkerBeginEXT");
		context->vkCmdDebugMarkerEnd = (PFN_vkCmdDebugMarkerEndEXT)vkGetDeviceProcAddr(device, "vkCmdDebugMarkerEndEXT");
	}
	if (info->flags & FFX_CACAO_VK_CREATE_USE_DEBUG_MARKERS)
	{
		context->vkSetDebugUtilsObjectName = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT");
	}

	uint32_t numSamplersInited = 0;
	uint32_t numDescriptorSetLayoutsInited = 0;
	uint32_t numPipelineLayoutsInited = 0;
	uint32_t numShaderModulesInited = 0;
	uint32_t numPipelinesInited = 0;
	uint32_t numConstantBackBuffersInited = 0;

	VkSampler samplers[NUM_SAMPLERS];
	{
		VkSamplerCreateInfo samplerCreateInfo = {};
		samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerCreateInfo.pNext = NULL;
		samplerCreateInfo.flags = 0;
		samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
		samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
		samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerCreateInfo.mipLodBias = 0.0f;
		samplerCreateInfo.anisotropyEnable = VK_FALSE;
		samplerCreateInfo.compareEnable = VK_FALSE;
		samplerCreateInfo.minLod = -1000.0f;
		samplerCreateInfo.maxLod = 1000.0f;
		samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;

		result = vkCreateSampler(device, &samplerCreateInfo, NULL, &samplers[numSamplersInited]);
		if (result != VK_SUCCESS)
		{
			goto error_init_samplers;
		}
		setObjectName(device, context, VK_OBJECT_TYPE_SAMPLER, (uint64_t)samplers[numSamplersInited], "FFX_CACAO_POINT_CLAMP_SAMPLER");
		++numSamplersInited;

		samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;

		result = vkCreateSampler(device, &samplerCreateInfo, NULL, &samplers[numSamplersInited]);
		if (result != VK_SUCCESS)
		{
			goto error_init_samplers;
		}
		setObjectName(device, context, VK_OBJECT_TYPE_SAMPLER, (uint64_t)samplers[numSamplersInited], "FFX_CACAO_POINT_MIRROR_SAMPLER");
		++numSamplersInited;

		samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
		samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
		samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

		result = vkCreateSampler(device, &samplerCreateInfo, NULL, &samplers[numSamplersInited]);
		if (result != VK_SUCCESS)
		{
			goto error_init_samplers;
		}
		setObjectName(device, context, VK_OBJECT_TYPE_SAMPLER, (uint64_t)samplers[numSamplersInited], "FFX_CACAO_LINEAR_CLAMP_SAMPLER");
		++numSamplersInited;

		samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
		samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
		samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

		result = vkCreateSampler(device, &samplerCreateInfo, NULL, &samplers[numSamplersInited]);
		if (result != VK_SUCCESS)
		{
			goto error_init_samplers;
		}
		setObjectName(device, context, VK_OBJECT_TYPE_SAMPLER, (uint64_t)samplers[numSamplersInited], "FFX_CACAO_VIEWSPACE_DEPTH_TAP_SAMPLER");
		++numSamplersInited;

		samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

		result = vkCreateSampler(device, &samplerCreateInfo, NULL, &samplers[numSamplersInited]);
		if (result != VK_SUCCESS)
		{
			goto error_init_samplers;
		}
		setObjectName(device, context, VK_OBJECT_TYPE_SAMPLER, (uint64_t)samplers[numSamplersInited], "FFX_CACAO_ZERO_TEXTURE_SAMPLER");
		++numSamplersInited;

		for (uint32_t i = 0; i < FFX_CACAO_ARRAY_SIZE(samplers); ++i)
		{
			context->samplers[i] = samplers[i];
		}
	}

	// create descriptor set layouts
	for ( ; numDescriptorSetLayoutsInited < NUM_DESCRIPTOR_SET_LAYOUTS; ++numDescriptorSetLayoutsInited)
	{
		VkDescriptorSetLayout descriptorSetLayout;
		DescriptorSetLayoutMetaData dslMetaData = DESCRIPTOR_SET_LAYOUT_META_DATA[numDescriptorSetLayoutsInited];

		VkDescriptorSetLayoutBinding bindings[MAX_DESCRIPTOR_BINDINGS] = {};
		uint32_t numBindings = 0;
		for (uint32_t samplerBinding = 0; samplerBinding < FFX_CACAO_ARRAY_SIZE(samplers); ++samplerBinding)
		{
			VkDescriptorSetLayoutBinding binding = {};
			binding.binding = samplerBinding;
			binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
			binding.descriptorCount = 1;
			binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			binding.pImmutableSamplers = &samplers[samplerBinding];
			bindings[numBindings++] = binding;
		}

		// constant buffer binding
		{
			VkDescriptorSetLayoutBinding binding = {};
			binding.binding = 10;
			binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			binding.descriptorCount = 1;
			binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			binding.pImmutableSamplers = NULL;
			bindings[numBindings++] = binding;
		}

		for (uint32_t inputBinding = 0; inputBinding < dslMetaData.numInputs; ++inputBinding)
		{
			VkDescriptorSetLayoutBinding binding = {};
			binding.binding = 20 + inputBinding;
			binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			binding.descriptorCount = 1;
			binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			binding.pImmutableSamplers = NULL;
			bindings[numBindings++] = binding;
		}

		for (uint32_t outputBinding = 0; outputBinding < dslMetaData.numOutputs; ++outputBinding)
		{
			VkDescriptorSetLayoutBinding binding = {};
			binding.binding = 30 + outputBinding; // g_PrepareDepthsOut register(u0)
			binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			binding.descriptorCount = 1;
			binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			binding.pImmutableSamplers = NULL;
			bindings[numBindings++] = binding;
		}

		VkDescriptorSetLayoutCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		info.pNext = NULL;
		info.flags = 0;
		info.bindingCount = numBindings;
		info.pBindings = bindings;

		result = vkCreateDescriptorSetLayout(device, &info, NULL, &descriptorSetLayout);
		if (result != VK_SUCCESS)
		{
			goto error_init_descriptor_set_layouts;
		}
		setObjectName(device, context, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)descriptorSetLayout, dslMetaData.name);

		context->descriptorSetLayouts[numDescriptorSetLayoutsInited] = descriptorSetLayout;
	}

	// create pipeline layouts
	for ( ; numPipelineLayoutsInited < NUM_DESCRIPTOR_SET_LAYOUTS; ++numPipelineLayoutsInited)
	{
		VkPipelineLayout pipelineLayout;

		DescriptorSetLayoutMetaData dslMetaData = DESCRIPTOR_SET_LAYOUT_META_DATA[numPipelineLayoutsInited];

		VkPipelineLayoutCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		info.pNext = NULL;
		info.flags = 0;
		info.setLayoutCount = 1;
		info.pSetLayouts = &context->descriptorSetLayouts[numPipelineLayoutsInited];
		info.pushConstantRangeCount = 0;
		info.pPushConstantRanges = NULL;

		result = vkCreatePipelineLayout(device, &info, NULL, &pipelineLayout);
		if (result != VK_SUCCESS)
		{
			goto error_init_pipeline_layouts;
		}
		setObjectName(device, context, VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)pipelineLayout, dslMetaData.name);

		context->pipelineLayouts[numPipelineLayoutsInited] = pipelineLayout;
	}

	for ( ; numShaderModulesInited < NUM_COMPUTE_SHADERS; ++numShaderModulesInited)
	{
		VkShaderModule shaderModule;
		ComputeShaderMetaData csMetaData = COMPUTE_SHADER_META_DATA[numShaderModulesInited];

		VkShaderModuleCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		info.pNext = 0;
		info.flags = 0;
		if (use16Bit)
		{
			info.codeSize = csMetaData.spirv16Len;
			info.pCode = csMetaData.shaderSpirv16;
		}
		else
		{
			info.codeSize = csMetaData.spirv32Len;
			info.pCode = csMetaData.shaderSpirv32;
		}

		result = vkCreateShaderModule(device, &info, NULL, &shaderModule);
		if (result != VK_SUCCESS)
		{
			goto error_init_shader_modules;
		}
		setObjectName(device, context, VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)shaderModule, csMetaData.objectName);

		context->computeShaders[numShaderModulesInited] = shaderModule;
	}

	for ( ; numPipelinesInited < NUM_COMPUTE_SHADERS; ++numPipelinesInited)
	{
		VkPipeline pipeline;
		ComputeShaderMetaData csMetaData = COMPUTE_SHADER_META_DATA[numPipelinesInited];

		VkPipelineShaderStageCreateInfo stageInfo = {};
		stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stageInfo.pNext = NULL;
		stageInfo.flags = 0;
		stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stageInfo.module = context->computeShaders[numPipelinesInited];
		stageInfo.pName = csMetaData.name;
		stageInfo.pSpecializationInfo = NULL;

		VkComputePipelineCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		info.pNext = NULL;
		info.flags = 0;
		info.stage = stageInfo;
		info.layout = context->pipelineLayouts[csMetaData.descriptorSetLayoutID];
		info.basePipelineHandle = VK_NULL_HANDLE;
		info.basePipelineIndex = 0;

		result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline);
		if (result != VK_SUCCESS)
		{
			goto error_init_pipelines;
		}
		setObjectName(device, context, VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipeline, csMetaData.objectName);

		context->computePipelines[numPipelinesInited] = pipeline;
	}

	// create descriptor pool
	{
		VkDescriptorPool descriptorPool;

		VkDescriptorPoolSize poolSizes[4] = {};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLER;
		poolSizes[0].descriptorCount = NUM_BACK_BUFFERS * NUM_DESCRIPTOR_SETS * 5;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		poolSizes[1].descriptorCount = NUM_BACK_BUFFERS * NUM_DESCRIPTOR_SETS * 7;
		poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		poolSizes[2].descriptorCount = NUM_BACK_BUFFERS * NUM_DESCRIPTOR_SETS * 4;
		poolSizes[3].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[3].descriptorCount = NUM_BACK_BUFFERS * NUM_DESCRIPTOR_SETS * 1;

		VkDescriptorPoolCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		info.pNext = NULL;
		info.flags = 0;
		info.maxSets = NUM_BACK_BUFFERS * NUM_DESCRIPTOR_SETS;
		info.poolSizeCount = FFX_CACAO_ARRAY_SIZE(poolSizes);
		info.pPoolSizes = poolSizes;

		result = vkCreateDescriptorPool(device, &info, NULL, &descriptorPool);
		if (result != VK_SUCCESS)
		{
			goto error_init_descriptor_pool;
		}
		setObjectName(device, context, VK_OBJECT_TYPE_DESCRIPTOR_POOL, (uint64_t)descriptorPool, "FFX_CACAO_DESCRIPTOR_POOL");

		context->descriptorPool = descriptorPool;
	}

	// allocate descriptor sets
	{
		VkDescriptorSetLayout descriptorSetLayouts[NUM_DESCRIPTOR_SETS];
		for (uint32_t i = 0; i < NUM_DESCRIPTOR_SETS; ++i) {
			descriptorSetLayouts[i] = context->descriptorSetLayouts[DESCRIPTOR_SET_META_DATA[i].descriptorSetLayoutID];
		}

		for (uint32_t i = 0; i < NUM_BACK_BUFFERS; ++i) {
			VkDescriptorSetAllocateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			info.pNext = NULL;
			info.descriptorPool = context->descriptorPool;
			info.descriptorSetCount = FFX_CACAO_ARRAY_SIZE(descriptorSetLayouts); // FFX_CACAO_ARRAY_SIZE(context->descriptorSetLayouts);
			info.pSetLayouts = descriptorSetLayouts; // context->descriptorSetLayouts;

			result = vkAllocateDescriptorSets(device, &info, context->descriptorSets[i]);
			if (result != VK_SUCCESS)
			{
				goto error_allocate_descriptor_sets;
			}
		}

		char name[1024];
		for (uint32_t j = 0; j < NUM_BACK_BUFFERS; ++j) {
			for (uint32_t i = 0; i < NUM_DESCRIPTOR_SETS; ++i) {
				DescriptorSetMetaData dsMetaData = DESCRIPTOR_SET_META_DATA[i];
				snprintf(name, FFX_CACAO_ARRAY_SIZE(name), "%s_%u", dsMetaData.name, j);
				setObjectName(device, context, VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)context->descriptorSets[j][i], name);
			}
		}
	}

	// assign memory to constant buffers
	for ( ; numConstantBackBuffersInited < NUM_BACK_BUFFERS; ++numConstantBackBuffersInited)
	{
		for (uint32_t j = 0; j < 4; ++j)
		{
			VkBuffer buffer = context->constantBuffer[numConstantBackBuffersInited][j];

			VkBufferCreateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			info.pNext = NULL;
			info.flags = 0;
			info.size = sizeof(FfxCacaoConstants);
			info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			info.queueFamilyIndexCount = 0;
			info.pQueueFamilyIndices = NULL;

			result = vkCreateBuffer(device, &info, NULL, &buffer);
			if (result != VK_SUCCESS)
			{
				goto error_init_constant_buffers;
			}
			char name[1024];
			snprintf(name, FFX_CACAO_ARRAY_SIZE(name), "FFX_CACAO_CONSTANT_BUFFER_PASS_%u_BACK_BUFFER_%u", j, numConstantBackBuffersInited);
			setObjectName(device, context, VK_OBJECT_TYPE_BUFFER, (uint64_t)buffer, name);

			VkMemoryRequirements memoryRequirements;
			vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);

			uint32_t chosenMemoryTypeIndex = getBestMemoryHeapIndex(physicalDevice, memoryRequirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			if (chosenMemoryTypeIndex == VK_MAX_MEMORY_TYPES)
			{
				vkDestroyBuffer(device, buffer, NULL);
				goto error_init_constant_buffers;
			}

			VkMemoryAllocateInfo allocationInfo = {};
			allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocationInfo.pNext = NULL;
			allocationInfo.allocationSize = memoryRequirements.size;
			allocationInfo.memoryTypeIndex = chosenMemoryTypeIndex;

			VkDeviceMemory memory;
			result = vkAllocateMemory(device, &allocationInfo, NULL, &memory);
			if (result != VK_SUCCESS)
			{
				vkDestroyBuffer(device, buffer, NULL);
				goto error_init_constant_buffers;
			}

			result = vkBindBufferMemory(device, buffer, memory, 0);
			if (result != VK_SUCCESS)
			{
				vkDestroyBuffer(device, buffer, NULL);
				goto error_init_constant_buffers;
			}

			context->constantBufferMemory[numConstantBackBuffersInited][j] = memory;
			context->constantBuffer[numConstantBackBuffersInited][j] = buffer;
		}
	}

	// create load counter VkImage
	{
		VkImage image = VK_NULL_HANDLE;

		VkImageCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		info.pNext = NULL;
		info.flags = 0;
		info.imageType = VK_IMAGE_TYPE_1D;
		info.format = VK_FORMAT_R32_UINT;
		info.extent.width = 1;
		info.extent.height = 1;
		info.extent.depth = 1;
		info.mipLevels = 1;
		info.arrayLayers = 1;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.queueFamilyIndexCount = 0;
		info.pQueueFamilyIndices = NULL;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		result = vkCreateImage(device, &info, NULL, &image);
		if (result != VK_SUCCESS)
		{
			goto error_init_load_counter_image;
		}

		setObjectName(device, context, VK_OBJECT_TYPE_IMAGE, (uint64_t)image, "FFX_CACAO_LOAD_COUNTER");

		VkMemoryRequirements memoryRequirements;
		vkGetImageMemoryRequirements(device, image, &memoryRequirements);

		uint32_t chosenMemoryTypeIndex = getBestMemoryHeapIndex(physicalDevice, memoryRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (chosenMemoryTypeIndex == VK_MAX_MEMORY_TYPES)
		{
			vkDestroyImage(device, image, NULL);
			goto error_init_load_counter_image;
		}

		VkMemoryAllocateInfo allocationInfo = {};
		allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocationInfo.pNext = NULL;
		allocationInfo.allocationSize = memoryRequirements.size;
		allocationInfo.memoryTypeIndex = chosenMemoryTypeIndex;

		VkDeviceMemory memory;
		result = vkAllocateMemory(device, &allocationInfo, NULL, &memory);
		if (result != VK_SUCCESS)
		{
			vkDestroyImage(device, image, NULL);
			goto error_init_load_counter_image;
		}

		result = vkBindImageMemory(device, image, memory, 0);
		if (result != VK_SUCCESS)
		{
			vkDestroyImage(device, image, NULL);
			goto error_init_load_counter_image;
		}

		context->loadCounter = image;
		context->loadCounterMemory = memory;
	}

	// create load counter view
	{
		VkImageView imageView;

		VkImageViewCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.pNext = NULL;
		info.flags = 0;
		info.image = context->loadCounter;
		info.viewType = VK_IMAGE_VIEW_TYPE_1D;
		info.format = VK_FORMAT_R32_UINT;
		info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		info.subresourceRange.baseMipLevel = 0;
		info.subresourceRange.levelCount = 1;
		info.subresourceRange.baseArrayLayer = 0;
		info.subresourceRange.layerCount = 1;

		result = vkCreateImageView(device, &info, NULL, &imageView);
		if (result != VK_SUCCESS)
		{
			goto error_init_load_counter_view;
		}

		context->loadCounterView = imageView;
	}

#ifdef FFX_CACAO_ENABLE_PROFILING
	// create timestamp query pool
	{
		VkQueryPool queryPool = VK_NULL_HANDLE;

		VkQueryPoolCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		info.pNext = NULL;
		info.flags = 0;
		info.queryType = VK_QUERY_TYPE_TIMESTAMP;
		info.queryCount = NUM_TIMESTAMPS * NUM_BACK_BUFFERS;

		result = vkCreateQueryPool(device, &info, NULL, &queryPool);
		if (result != VK_SUCCESS)
		{
			goto error_init_query_pool;
		}

		context->timestampQueryPool = queryPool;
	}
#endif

	return FFX_CACAO_STATUS_OK;

#ifdef FFX_CACAO_ENABLE_PROFILING
	vkDestroyQueryPool(device, context->timestampQueryPool, NULL);
error_init_query_pool:
#endif

	vkDestroyImageView(device, context->loadCounterView, NULL);
error_init_load_counter_view:
	vkDestroyImage(device, context->loadCounter, NULL);
	vkFreeMemory(device, context->loadCounterMemory, NULL);
error_init_load_counter_image:

error_init_constant_buffers:
	for (uint32_t i = 0; i < numConstantBackBuffersInited; ++i)
	{
		for (uint32_t j = 0; j < 4; ++j)
		{
			vkDestroyBuffer(device, context->constantBuffer[i][j], NULL);
			vkFreeMemory(device, context->constantBufferMemory[i][j], NULL);
		}
	}
	
error_allocate_descriptor_sets:
	vkDestroyDescriptorPool(device, context->descriptorPool, NULL);
error_init_descriptor_pool:

error_init_pipelines:
	for (uint32_t i = 0; i < numPipelinesInited; ++i)
	{
		vkDestroyPipeline(device, context->computePipelines[i], NULL);
	}

error_init_shader_modules:
	for (uint32_t i = 0; i < numShaderModulesInited; ++i)
	{
		vkDestroyShaderModule(device, context->computeShaders[i], NULL);
	}

error_init_pipeline_layouts:
	for (uint32_t i = 0; i < numPipelineLayoutsInited; ++i)
	{
		vkDestroyPipelineLayout(device, context->pipelineLayouts[i], NULL);
	}

error_init_descriptor_set_layouts:
	for (uint32_t i = 0; i < numDescriptorSetLayoutsInited; ++i)
	{
		vkDestroyDescriptorSetLayout(device, context->descriptorSetLayouts[i], NULL);
	}


error_init_samplers:
	for (uint32_t i = 0; i < numSamplersInited; ++i)
	{
		vkDestroySampler(device, context->samplers[i], NULL);
	}

	return errorStatus;
}

FfxCacaoStatus ffxCacaoVkDestroyContext(FfxCacaoVkContext* context)
{
	if (context == NULL)
	{
		return FFX_CACAO_STATUS_INVALID_POINTER;
	}
	context = getAlignedVkContextPointer(context);

	VkDevice device = context->device;

#ifdef FFX_CACAO_ENABLE_PROFILING
	vkDestroyQueryPool(device, context->timestampQueryPool, NULL);
#endif

	vkDestroyImageView(device, context->loadCounterView, NULL);
	vkDestroyImage(device, context->loadCounter, NULL);
	vkFreeMemory(device, context->loadCounterMemory, NULL);

	for (uint32_t i = 0; i < NUM_BACK_BUFFERS; ++i)
	{
		for (uint32_t j = 0; j < 4; ++j)
		{
			vkDestroyBuffer(device, context->constantBuffer[i][j], NULL);
			vkFreeMemory(device, context->constantBufferMemory[i][j], NULL);
		}
	}

	vkDestroyDescriptorPool(device, context->descriptorPool, NULL);

	for (uint32_t i = 0; i < NUM_COMPUTE_SHADERS; ++i)
	{
		vkDestroyPipeline(device, context->computePipelines[i], NULL);
	}

	for (uint32_t i = 0; i < NUM_COMPUTE_SHADERS; ++i)
	{
		vkDestroyShaderModule(device, context->computeShaders[i], NULL);
	}

	for (uint32_t i = 0; i < NUM_DESCRIPTOR_SET_LAYOUTS; ++i)
	{
		vkDestroyPipelineLayout(device, context->pipelineLayouts[i], NULL);
	}

	for(uint32_t i = 0; i < NUM_DESCRIPTOR_SET_LAYOUTS; ++i)
	{
		vkDestroyDescriptorSetLayout(device, context->descriptorSetLayouts[i], NULL);
	}


	for (uint32_t i = 0; i < FFX_CACAO_ARRAY_SIZE(context->samplers); ++i)
	{
		vkDestroySampler(device, context->samplers[i], NULL);
	}

	return FFX_CACAO_STATUS_OK;
}

FfxCacaoStatus ffxCacaoVkInitScreenSizeDependentResources(FfxCacaoVkContext* context, const FfxCacaoVkScreenSizeInfo* info)
{
	if (context == NULL)
	{
		return FFX_CACAO_STATUS_INVALID_POINTER;
	}
	if (info == NULL)
	{
		return FFX_CACAO_STATUS_INVALID_POINTER;
	}
	context = getAlignedVkContextPointer(context);

#ifdef FFX_CACAO_ENABLE_NATIVE_RESOLUTION
	FfxCacaoBool useDownsampledSsao = info->useDownsampledSsao;
#else
	FfxCacaoBool useDownsampledSsao = FFX_CACAO_TRUE;
#endif
	context->useDownsampledSsao = useDownsampledSsao;
	context->output = info->output;

	VkDevice device = context->device;
	VkPhysicalDevice physicalDevice = context->physicalDevice;
	VkPhysicalDeviceMemoryProperties memoryProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
	VkResult result;

	uint32_t width = info->width;
	uint32_t height = info->height;
	uint32_t halfWidth = (width + 1) / 2;
	uint32_t halfHeight = (height + 1) / 2;
	uint32_t quarterWidth = (halfWidth + 1) / 2;
	uint32_t quarterHeight = (halfHeight + 1) / 2;
	uint32_t eighthWidth = (quarterWidth + 1) / 2;
	uint32_t eighthHeight = (quarterHeight + 1) / 2;

	uint32_t depthBufferWidth = width;
	uint32_t depthBufferHeight = height;
	uint32_t depthBufferHalfWidth = halfWidth;
	uint32_t depthBufferHalfHeight = halfHeight;
	uint32_t depthBufferQuarterWidth = quarterWidth;
	uint32_t depthBufferQuarterHeight = quarterHeight;

	uint32_t depthBufferXOffset = 0;
	uint32_t depthBufferYOffset = 0;
	uint32_t depthBufferHalfXOffset = 0;
	uint32_t depthBufferHalfYOffset = 0;
	uint32_t depthBufferQuarterXOffset = 0;
	uint32_t depthBufferQuarterYOffset = 0;

	BufferSizeInfo bsi = {};
	bsi.inputOutputBufferWidth = width;
	bsi.inputOutputBufferHeight = height;
	bsi.depthBufferXOffset = depthBufferXOffset;
	bsi.depthBufferYOffset = depthBufferYOffset;
	bsi.depthBufferWidth = depthBufferWidth;
	bsi.depthBufferHeight = depthBufferHeight;

	if (useDownsampledSsao)
	{
		bsi.ssaoBufferWidth = quarterWidth;
		bsi.ssaoBufferHeight = quarterHeight;
		bsi.deinterleavedDepthBufferXOffset = depthBufferQuarterXOffset;
		bsi.deinterleavedDepthBufferYOffset = depthBufferQuarterYOffset;
		bsi.deinterleavedDepthBufferWidth = depthBufferQuarterWidth;
		bsi.deinterleavedDepthBufferHeight = depthBufferQuarterHeight;
		bsi.importanceMapWidth = eighthWidth;
		bsi.importanceMapHeight = eighthHeight;
	}
	else
	{
		bsi.ssaoBufferWidth = halfWidth;
		bsi.ssaoBufferHeight = halfHeight;
		bsi.deinterleavedDepthBufferXOffset = depthBufferHalfXOffset;
		bsi.deinterleavedDepthBufferYOffset = depthBufferHalfYOffset;
		bsi.deinterleavedDepthBufferWidth = depthBufferHalfWidth;
		bsi.deinterleavedDepthBufferHeight = depthBufferHalfHeight;
		bsi.importanceMapWidth = quarterWidth;
		bsi.importanceMapHeight = quarterHeight;
	}

	context->bufferSizeInfo = bsi;

	FfxCacaoStatus errorStatus = FFX_CACAO_STATUS_FAILED;
	uint32_t numTextureImagesInited = 0;
	uint32_t numTextureMemoriesInited = 0;
	uint32_t numSrvsInited = 0;
	uint32_t numUavsInited = 0;

	// create images for textures
	for ( ; numTextureImagesInited < NUM_TEXTURES; ++numTextureImagesInited)
	{
		TextureMetaData metaData = TEXTURE_META_DATA[numTextureImagesInited];
		VkImage image = VK_NULL_HANDLE;

		VkImageCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		info.pNext = NULL;
		info.flags = 0;
		info.imageType = VK_IMAGE_TYPE_2D;
		info.format = metaData.format;
		info.extent.width = *(uint32_t*)((uint8_t*)&bsi + metaData.widthOffset);
		info.extent.height = *(uint32_t*)((uint8_t*)&bsi + metaData.heightOffset);
		info.extent.depth = 1;
		info.mipLevels = metaData.numMips;
		info.arrayLayers = metaData.arraySize;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.queueFamilyIndexCount = 0;
		info.pQueueFamilyIndices = NULL;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		result = vkCreateImage(device, &info, NULL, &image);
		if (result != VK_SUCCESS)
		{
			goto error_init_texture_images;
		}

		setObjectName(device, context, VK_OBJECT_TYPE_IMAGE, (uint64_t)image, metaData.name);

		context->textures[numTextureImagesInited] = image;
	}

	// allocate memory for textures
	for ( ; numTextureMemoriesInited < NUM_TEXTURES; ++numTextureMemoriesInited)
	{
		VkImage image = context->textures[numTextureMemoriesInited];

		VkMemoryRequirements memoryRequirements;
		vkGetImageMemoryRequirements(device, image, &memoryRequirements);

		uint32_t chosenMemoryTypeIndex = getBestMemoryHeapIndex(physicalDevice, memoryRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (chosenMemoryTypeIndex == VK_MAX_MEMORY_TYPES)
		{
			goto error_init_texture_memories;
		}

		VkMemoryAllocateInfo allocationInfo = {};
		allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocationInfo.pNext = NULL;
		allocationInfo.allocationSize = memoryRequirements.size;
		allocationInfo.memoryTypeIndex = chosenMemoryTypeIndex;

		VkDeviceMemory memory;
		result = vkAllocateMemory(device, &allocationInfo, NULL, &memory);
		if (result != VK_SUCCESS)
		{
			goto error_init_texture_memories;
		}

		result = vkBindImageMemory(device, image, memory, 0);
		if (result != VK_SUCCESS)
		{
			vkFreeMemory(device, memory, NULL);
			goto  error_init_texture_memories;
		}

		context->textureMemory[numTextureMemoriesInited] = memory;
	}

	// create srv image views
	for ( ; numSrvsInited < NUM_SHADER_RESOURCE_VIEWS; ++numSrvsInited)
	{
		VkImageView imageView;
		ShaderResourceViewMetaData srvMetaData = SRV_META_DATA[numSrvsInited];

		VkImageViewCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.pNext = NULL;
		info.flags = 0;
		info.image = context->textures[srvMetaData.texture];
		info.viewType = srvMetaData.viewType;
		info.format = TEXTURE_META_DATA[srvMetaData.texture].format;
		info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		info.subresourceRange.baseMipLevel = srvMetaData.mostDetailedMip;
		info.subresourceRange.levelCount = srvMetaData.mipLevels;
		info.subresourceRange.baseArrayLayer = srvMetaData.firstArraySlice;
		info.subresourceRange.layerCount = srvMetaData.arraySize;

		result = vkCreateImageView(device, &info, NULL, &imageView);
		if (result != VK_SUCCESS)
		{
			goto error_init_srvs;
		}

		context->shaderResourceViews[numSrvsInited] = imageView;
	}

	// create uav image views
	for ( ; numUavsInited < NUM_UNORDERED_ACCESS_VIEWS; ++numUavsInited)
	{
		VkImageView imageView;
		UnorderedAccessViewMetaData uavMetaData = UAV_META_DATA[numUavsInited];

		VkImageViewCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.pNext = NULL;
		info.flags = 0;
		info.image = context->textures[uavMetaData.textureID];
		info.viewType = uavMetaData.viewType;
		info.format = TEXTURE_META_DATA[uavMetaData.textureID].format;
		info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		info.subresourceRange.baseMipLevel = uavMetaData.mostDetailedMip;
		info.subresourceRange.levelCount = 1;
		info.subresourceRange.baseArrayLayer = uavMetaData.firstArraySlice;
		info.subresourceRange.layerCount = uavMetaData.arraySize;

		result = vkCreateImageView(device, &info, NULL, &imageView);
		if (result != VK_SUCCESS)
		{
			goto error_init_uavs;
		}

		context->unorderedAccessViews[numUavsInited] = imageView;
	}

	// update descriptor sets from table
	for (uint32_t i = 0; i < NUM_BACK_BUFFERS; ++i) {
		VkDescriptorImageInfo  imageInfos[NUM_INPUT_DESCRIPTOR_BINDINGS + NUM_OUTPUT_DESCRIPTOR_BINDINGS] = {};
		VkDescriptorImageInfo *curImageInfo = imageInfos;
		VkWriteDescriptorSet   writes[NUM_INPUT_DESCRIPTOR_BINDINGS + NUM_OUTPUT_DESCRIPTOR_BINDINGS] = {};
		VkWriteDescriptorSet  *curWrite = writes;
		
		// write input descriptor bindings
		for (uint32_t j = 0; j < NUM_INPUT_DESCRIPTOR_BINDINGS; ++j)
		{
			InputDescriptorBindingMetaData bindingMetaData = INPUT_DESCRIPTOR_BINDING_META_DATA[j];

			curImageInfo->sampler = VK_NULL_HANDLE;
			curImageInfo->imageView = context->shaderResourceViews[bindingMetaData.srvID];
			curImageInfo->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			curWrite->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			curWrite->pNext = NULL;
			curWrite->dstSet = context->descriptorSets[i][bindingMetaData.descriptorID];
			curWrite->dstBinding = 20 + bindingMetaData.bindingNumber;
			curWrite->descriptorCount = 1;
			curWrite->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			curWrite->pImageInfo = curImageInfo;

			++curWrite; ++curImageInfo;
		}

		// write output descriptor bindings
		for (uint32_t j = 0; j < NUM_OUTPUT_DESCRIPTOR_BINDINGS; ++j)
		{
			OutputDescriptorBindingMetaData bindingMetaData = OUTPUT_DESCRIPTOR_BINDING_META_DATA[j];

			curImageInfo->sampler = VK_NULL_HANDLE;
			curImageInfo->imageView = context->unorderedAccessViews[bindingMetaData.uavID];
			curImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;

			curWrite->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			curWrite->pNext = VK_NULL_HANDLE;
			curWrite->dstSet = context->descriptorSets[i][bindingMetaData.descriptorID];
			curWrite->dstBinding = 30 + bindingMetaData.bindingNumber;
			curWrite->descriptorCount = 1;
			curWrite->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			curWrite->pImageInfo = curImageInfo;

			++curWrite; ++curImageInfo;
		}

		vkUpdateDescriptorSets(device, FFX_CACAO_ARRAY_SIZE(writes), writes, 0, NULL);
	}

	// update descriptor sets with inputs
	for (uint32_t i = 0; i < NUM_BACK_BUFFERS; ++i) {
#define MAX_NUM_MISC_INPUT_DESCRIPTORS 32

		VkDescriptorImageInfo imageInfos[MAX_NUM_MISC_INPUT_DESCRIPTORS] = {};
		VkWriteDescriptorSet writes[MAX_NUM_MISC_INPUT_DESCRIPTORS] = {};

		for (uint32_t i = 0; i < FFX_CACAO_ARRAY_SIZE(writes); ++i)
		{
			VkDescriptorImageInfo *imageInfo = imageInfos + i;
			VkWriteDescriptorSet *write = writes + i;

			imageInfo->sampler = VK_NULL_HANDLE;

			write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write->pNext = NULL;
			write->descriptorCount = 1;
			write->pImageInfo = imageInfo;
		}

		uint32_t cur = 0;

		// register(t0) -> 20
		// register(u0) -> 30
		imageInfos[cur].imageView = info->depthView;
		imageInfos[cur].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		writes[cur].dstSet = context->descriptorSets[i][DS_PREPARE_DEPTHS];
		writes[cur].dstBinding = 20;
		writes[cur].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		++cur;

		imageInfos[cur].imageView = info->depthView;
		imageInfos[cur].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		writes[cur].dstSet = context->descriptorSets[i][DS_PREPARE_DEPTHS_MIPS];
		writes[cur].dstBinding = 20;
		writes[cur].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		++cur;

		imageInfos[cur].imageView = info->depthView;
		imageInfos[cur].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		writes[cur].dstSet = context->descriptorSets[i][DS_PREPARE_NORMALS];
		writes[cur].dstBinding = 20;
		writes[cur].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		++cur;

		imageInfos[cur].imageView = info->depthView;
		imageInfos[cur].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		writes[cur].dstSet = context->descriptorSets[i][DS_BILATERAL_UPSAMPLE_PING];
		writes[cur].dstBinding = 21;
		writes[cur].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		++cur;

		imageInfos[cur].imageView = info->depthView;
		imageInfos[cur].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		writes[cur].dstSet = context->descriptorSets[i][DS_BILATERAL_UPSAMPLE_PONG];
		writes[cur].dstBinding = 21;
		writes[cur].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		++cur;

		imageInfos[cur].imageView = info->outputView;
		imageInfos[cur].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		writes[cur].dstSet = context->descriptorSets[i][DS_BILATERAL_UPSAMPLE_PING];
		writes[cur].dstBinding = 30;
		writes[cur].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		++cur;

		imageInfos[cur].imageView = info->outputView;
		imageInfos[cur].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		writes[cur].dstSet = context->descriptorSets[i][DS_BILATERAL_UPSAMPLE_PONG];
		writes[cur].dstBinding = 30;
		writes[cur].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		++cur;

		imageInfos[cur].imageView = info->outputView;
		imageInfos[cur].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		writes[cur].dstSet = context->descriptorSets[i][DS_APPLY_PING];
		writes[cur].dstBinding = 30;
		writes[cur].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		++cur;

		imageInfos[cur].imageView = info->outputView;
		imageInfos[cur].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		writes[cur].dstSet = context->descriptorSets[i][DS_APPLY_PONG];
		writes[cur].dstBinding = 30;
		writes[cur].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		++cur;

		imageInfos[cur].imageView = context->loadCounterView;
		imageInfos[cur].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		writes[cur].dstSet = context->descriptorSets[i][DS_POSTPROCESS_IMPORTANCE_MAP_B];
		writes[cur].dstBinding = 31;
		writes[cur].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		++cur;

		imageInfos[cur].imageView = context->loadCounterView;
		imageInfos[cur].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		writes[cur].dstSet = context->descriptorSets[i][DS_CLEAR_LOAD_COUNTER];
		writes[cur].dstBinding = 30;
		writes[cur].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		++cur;

		for (uint32_t pass = 0; pass < 4; ++pass)
		{
			imageInfos[cur].imageView = context->loadCounterView;
			imageInfos[cur].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			writes[cur].dstSet = context->descriptorSets[i][(DescriptorSetID)(DS_GENERATE_ADAPTIVE_0 + pass)];
			writes[cur].dstBinding = 22;
			writes[cur].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			++cur;
		}

		if (info->normalsView) {
			imageInfos[cur].imageView = info->normalsView;
			imageInfos[cur].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			writes[cur].dstSet = context->descriptorSets[i][DS_PREPARE_NORMALS_FROM_INPUT_NORMALS];
			writes[cur].dstBinding = 20;
			writes[cur].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			++cur;
		}

		FFX_CACAO_ASSERT(cur <= MAX_NUM_MISC_INPUT_DESCRIPTORS);
		vkUpdateDescriptorSets(device, cur, writes, 0, NULL);
	}

	// update descriptor sets with constant buffers
	for (uint32_t i = 0; i < NUM_BACK_BUFFERS; ++i) {
		VkDescriptorBufferInfo  bufferInfos[NUM_DESCRIPTOR_SETS] = {};
		VkDescriptorBufferInfo *curBufferInfo = bufferInfos;
		VkWriteDescriptorSet    writes[NUM_DESCRIPTOR_SETS] = {};
		VkWriteDescriptorSet   *curWrite = writes;

		for (uint32_t j = 0; j < NUM_DESCRIPTOR_SETS; ++j)
		{
			DescriptorSetMetaData dsMetaData = DESCRIPTOR_SET_META_DATA[j];

			curBufferInfo->buffer = context->constantBuffer[i][dsMetaData.pass];
			curBufferInfo->offset = 0;
			curBufferInfo->range = VK_WHOLE_SIZE;

			curWrite->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			curWrite->pNext = NULL;
			curWrite->dstSet = context->descriptorSets[i][j];
			curWrite->dstBinding = 10;
			curWrite->dstArrayElement = 0;
			curWrite->descriptorCount = 1;
			curWrite->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			curWrite->pBufferInfo = curBufferInfo;

			++curWrite;
			++curBufferInfo;
		}

		vkUpdateDescriptorSets(device, FFX_CACAO_ARRAY_SIZE(writes), writes, 0, NULL);
	}

	return FFX_CACAO_STATUS_OK;

error_init_uavs:
	for (uint32_t i = 0; i < numUavsInited; ++i)
	{
		vkDestroyImageView(device, context->unorderedAccessViews[i], NULL);
	}

error_init_srvs:
	for (uint32_t i = 0; i < numSrvsInited; ++i)
	{
		vkDestroyImageView(device, context->shaderResourceViews[i], NULL);
	}

error_init_texture_memories:
	for (uint32_t i = 0; i < numTextureMemoriesInited; ++i)
	{
		vkFreeMemory(device, context->textureMemory[i], NULL);
	}

error_init_texture_images:
	for (uint32_t i = 0; i < numTextureImagesInited; ++i)
	{
		vkDestroyImage(device, context->textures[i], NULL);
	}

	return errorStatus;
}

FfxCacaoStatus ffxCacaoVkDestroyScreenSizeDependentResources(FfxCacaoVkContext* context)
{
	if (context == NULL)
	{
		return FFX_CACAO_STATUS_INVALID_POINTER;
	}
	context = getAlignedVkContextPointer(context);

	VkDevice device = context->device;

	for (uint32_t i = 0; i < NUM_UNORDERED_ACCESS_VIEWS; ++i)
	{
		vkDestroyImageView(device, context->unorderedAccessViews[i], NULL);
	}

	for (uint32_t i = 0; i < NUM_SHADER_RESOURCE_VIEWS; ++i)
	{
		vkDestroyImageView(device, context->shaderResourceViews[i], NULL);
	}

	for (uint32_t i = 0; i < NUM_TEXTURES; ++i)
	{
		vkFreeMemory(device, context->textureMemory[i], NULL);
	}

	for (uint32_t i = 0; i < NUM_TEXTURES; ++i)
	{
		vkDestroyImage(device, context->textures[i], NULL);
	}

	return FFX_CACAO_STATUS_OK;
}

FfxCacaoStatus ffxCacaoVkUpdateSettings(FfxCacaoVkContext* context, const FfxCacaoSettings* settings)
{
	if (context == NULL || settings == NULL)
	{
		return FFX_CACAO_STATUS_INVALID_POINTER;
	}
	context = getAlignedVkContextPointer(context);

	memcpy(&context->settings, settings, sizeof(*settings));

	return FFX_CACAO_STATUS_OK;
}

static inline void computeDispatch(FfxCacaoVkContext* context, VkCommandBuffer cb, DescriptorSetID ds, ComputeShaderID cs, uint32_t width, uint32_t height)
{
	DescriptorSetLayoutID dsl = DESCRIPTOR_SET_META_DATA[ds].descriptorSetLayoutID;
	vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, context->pipelineLayouts[dsl], 0, 1, &context->descriptorSets[context->currentConstantBuffer][ds], 0, NULL);
	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, context->computePipelines[cs]);
	vkCmdDispatch(cb, width, height, 1);
}

typedef struct BarrierList
{
	uint32_t len;
	VkImageMemoryBarrier barriers[32];
} BarrierList;

static inline void pushBarrier(BarrierList* barrierList, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkAccessFlags srcAccessFlags, VkAccessFlags dstAccessFlags)
{
	FFX_CACAO_ASSERT(barrierList->len < FFX_CACAO_ARRAY_SIZE(barrierList->barriers));
	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.pNext = NULL;
	barrier.srcAccessMask = srcAccessFlags;
	barrier.dstAccessMask = dstAccessFlags;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
	barrier.image = image;
	barrierList->barriers[barrierList->len++] = barrier;
}

static inline void beginDebugMarker(FfxCacaoVkContext* context, VkCommandBuffer cb, const char* name)
{
	if (context->vkCmdDebugMarkerBegin)
	{
		VkDebugMarkerMarkerInfoEXT info = {};
		info.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
		info.pNext = NULL;
		info.pMarkerName = name;
		info.color[0] = 1.0f;
		info.color[1] = 0.0f;
		info.color[2] = 0.0f;
		info.color[3] = 1.0f;

		context->vkCmdDebugMarkerBegin(cb, &info);
	}
}

static inline void endDebugMarker(FfxCacaoVkContext* context, VkCommandBuffer cb)
{
	if (context->vkCmdDebugMarkerEnd)
	{
		context->vkCmdDebugMarkerEnd(cb);
	}
}

FfxCacaoStatus ffxCacaoVkDraw(FfxCacaoVkContext* context, VkCommandBuffer cb, const FfxCacaoMatrix4x4* proj, const FfxCacaoMatrix4x4* normalsToView)
{
	if (context == NULL || cb == VK_NULL_HANDLE || proj == NULL)
	{
		return FFX_CACAO_STATUS_INVALID_POINTER;
	}
	context = getAlignedVkContextPointer(context);

	FfxCacaoSettings *settings = &context->settings;
	BufferSizeInfo *bsi = &context->bufferSizeInfo;
	VkDevice device = context->device;
	VkDescriptorSet *ds = context->descriptorSets[context->currentConstantBuffer];
	VkImage *tex = context->textures;
	VkResult result;
	BarrierList barrierList;

	uint32_t curBuffer = context->currentConstantBuffer;
	curBuffer = (curBuffer + 1) % NUM_BACK_BUFFERS;
	context->currentConstantBuffer = curBuffer;
#ifdef FFX_CACAO_ENABLE_PROFILING
	{
		uint32_t collectBuffer = context->collectBuffer = (curBuffer + 1) % NUM_BACK_BUFFERS;
		if (uint32_t numQueries = context->timestampQueries[collectBuffer].numTimestamps)
		{
			uint32_t offset = collectBuffer * NUM_TIMESTAMPS;
			vkGetQueryPoolResults(device, context->timestampQueryPool, offset, numQueries, numQueries * sizeof(uint64_t), context->timestampQueries[collectBuffer].timings, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
		}
	}
#endif

	beginDebugMarker(context, cb, "FidelityFX CACAO");

	// update constant buffer

	for (uint32_t i = 0; i < 4; ++i)
	{
		VkDeviceMemory memory = context->constantBufferMemory[curBuffer][i];
		void *data = NULL;
		result = vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &data);
		FFX_CACAO_ASSERT(result == VK_SUCCESS);
		updateConstants((FfxCacaoConstants*)data, settings, bsi, proj, normalsToView);
		updatePerPassConstants((FfxCacaoConstants*)data, settings, bsi, i);
		vkUnmapMemory(device, memory);
	}

#ifdef FFX_CACAO_ENABLE_PROFILING
	uint32_t queryPoolOffset = curBuffer * NUM_TIMESTAMPS;
	uint32_t numTimestamps = 0;
	vkCmdResetQueryPool(cb, context->timestampQueryPool, queryPoolOffset, NUM_TIMESTAMPS);
#define GET_TIMESTAMP(name) \
		context->timestampQueries[curBuffer].timestamps[numTimestamps] = TIMESTAMP_##name; \
		vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, context->timestampQueryPool, queryPoolOffset + numTimestamps++);
#else
#define GET_TIMESTAMP(name)
#endif
	
	GET_TIMESTAMP(BEGIN)

	barrierList.len = 0;
	pushBarrier(&barrierList, tex[TEXTURE_DEINTERLEAVED_DEPTHS], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT);
	pushBarrier(&barrierList, tex[TEXTURE_DEINTERLEAVED_NORMALS], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT);
	pushBarrier(&barrierList, tex[TEXTURE_SSAO_BUFFER_PING], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT);
	pushBarrier(&barrierList, tex[TEXTURE_SSAO_BUFFER_PONG], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT);
	pushBarrier(&barrierList, tex[TEXTURE_IMPORTANCE_MAP], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT);
	pushBarrier(&barrierList, tex[TEXTURE_IMPORTANCE_MAP_PONG], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT);
	pushBarrier(&barrierList, context->loadCounter, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT);
	vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, barrierList.len, barrierList.barriers);

	// prepare depths, normals and mips
	{
		beginDebugMarker(context, cb, "Prepare downsampled depths, normals and mips");

		// clear load counter
		computeDispatch(context, cb, DS_CLEAR_LOAD_COUNTER, CS_CLEAR_LOAD_COUNTER, 1, 1);

		switch (context->settings.qualityLevel)
		{
		case FFX_CACAO_QUALITY_LOWEST: {
			uint32_t dispatchWidth = dispatchSize(PREPARE_DEPTHS_HALF_WIDTH, bsi->deinterleavedDepthBufferWidth);
			uint32_t dispatchHeight = dispatchSize(PREPARE_DEPTHS_HALF_HEIGHT, bsi->deinterleavedDepthBufferHeight);
			ComputeShaderID csPrepareDepthsHalf = context->useDownsampledSsao ? CS_PREPARE_DOWNSAMPLED_DEPTHS_HALF : CS_PREPARE_NATIVE_DEPTHS_HALF;
			computeDispatch(context, cb, DS_PREPARE_DEPTHS, csPrepareDepthsHalf, dispatchWidth, dispatchHeight);
			break;
		}
		case FFX_CACAO_QUALITY_LOW: {
			uint32_t dispatchWidth = dispatchSize(PREPARE_DEPTHS_WIDTH, bsi->deinterleavedDepthBufferWidth);
			uint32_t dispatchHeight = dispatchSize(PREPARE_DEPTHS_HEIGHT, bsi->deinterleavedDepthBufferHeight);
			ComputeShaderID csPrepareDepths = context->useDownsampledSsao ? CS_PREPARE_DOWNSAMPLED_DEPTHS : CS_PREPARE_NATIVE_DEPTHS;
			computeDispatch(context, cb, DS_PREPARE_DEPTHS, csPrepareDepths, dispatchWidth, dispatchHeight);
			break;
		}
		default: {
			uint32_t dispatchWidth = dispatchSize(PREPARE_DEPTHS_AND_MIPS_WIDTH, bsi->deinterleavedDepthBufferWidth);
			uint32_t dispatchHeight = dispatchSize(PREPARE_DEPTHS_AND_MIPS_HEIGHT, bsi->deinterleavedDepthBufferHeight);
			ComputeShaderID csPrepareDepthsAndMips = context->useDownsampledSsao ? CS_PREPARE_DOWNSAMPLED_DEPTHS_AND_MIPS : CS_PREPARE_NATIVE_DEPTHS_AND_MIPS;
			computeDispatch(context, cb, DS_PREPARE_DEPTHS_MIPS, csPrepareDepthsAndMips, dispatchWidth, dispatchHeight);
			break;
		}
		}

		if (context->settings.generateNormals)
		{
			uint32_t dispatchWidth = dispatchSize(PREPARE_NORMALS_WIDTH, bsi->ssaoBufferWidth);
			uint32_t dispatchHeight = dispatchSize(PREPARE_NORMALS_HEIGHT, bsi->ssaoBufferHeight);
			ComputeShaderID csPrepareNormals = context->useDownsampledSsao ? CS_PREPARE_DOWNSAMPLED_NORMALS : CS_PREPARE_NATIVE_NORMALS;
			computeDispatch(context, cb, DS_PREPARE_NORMALS, csPrepareNormals, dispatchWidth, dispatchHeight);
		}
		else
		{
			uint32_t dispatchWidth = dispatchSize(PREPARE_NORMALS_FROM_INPUT_NORMALS_WIDTH, bsi->ssaoBufferWidth);
			uint32_t dispatchHeight = dispatchSize(PREPARE_NORMALS_FROM_INPUT_NORMALS_HEIGHT, bsi->ssaoBufferHeight);
			ComputeShaderID csPrepareNormalsFromInputNormals = context->useDownsampledSsao ? CS_PREPARE_DOWNSAMPLED_NORMALS_FROM_INPUT_NORMALS : CS_PREPARE_NATIVE_NORMALS_FROM_INPUT_NORMALS;
			computeDispatch(context, cb, DS_PREPARE_NORMALS_FROM_INPUT_NORMALS, csPrepareNormalsFromInputNormals, dispatchWidth, dispatchHeight);
		}

		endDebugMarker(context, cb);
		GET_TIMESTAMP(PREPARE)
	}

	barrierList.len = 0;
	pushBarrier(&barrierList, tex[TEXTURE_DEINTERLEAVED_DEPTHS], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
	pushBarrier(&barrierList, tex[TEXTURE_DEINTERLEAVED_NORMALS], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
	pushBarrier(&barrierList, context->loadCounter, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT);
	vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, barrierList.len, barrierList.barriers);

	// base pass for highest quality setting
	if (context->settings.qualityLevel == FFX_CACAO_QUALITY_HIGHEST)
	{
		beginDebugMarker(context, cb, "Generate High Quality Base Pass");

		// SSAO
		{
			beginDebugMarker(context, cb, "Base SSAO");

			uint32_t dispatchWidth = dispatchSize(GENERATE_WIDTH, bsi->ssaoBufferWidth);
			uint32_t dispatchHeight = dispatchSize(GENERATE_WIDTH, bsi->ssaoBufferHeight);

			for (int pass = 0; pass < 4; ++pass)
			{
				computeDispatch(context, cb, (DescriptorSetID)(DS_GENERATE_ADAPTIVE_BASE_0 + pass), CS_GENERATE_Q3_BASE, dispatchWidth, dispatchHeight);
			}

			endDebugMarker(context, cb);
		}

		GET_TIMESTAMP(BASE_SSAO_PASS)

		barrierList.len = 0;
		pushBarrier(&barrierList, tex[TEXTURE_SSAO_BUFFER_PONG], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
		vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, barrierList.len, barrierList.barriers);

		// generate importance map
		{
			beginDebugMarker(context, cb, "Importance Map");

			uint32_t dispatchWidth = dispatchSize(IMPORTANCE_MAP_WIDTH, bsi->importanceMapWidth);
			uint32_t dispatchHeight = dispatchSize(IMPORTANCE_MAP_HEIGHT, bsi->importanceMapHeight);

			computeDispatch(context, cb, DS_GENERATE_IMPORTANCE_MAP, CS_GENERATE_IMPORTANCE_MAP, dispatchWidth, dispatchHeight);

			barrierList.len = 0;
			pushBarrier(&barrierList, tex[TEXTURE_IMPORTANCE_MAP], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
			vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, barrierList.len, barrierList.barriers);

			computeDispatch(context, cb, DS_POSTPROCESS_IMPORTANCE_MAP_A, CS_POSTPROCESS_IMPORTANCE_MAP_A, dispatchWidth, dispatchHeight);

			barrierList.len = 0;
			pushBarrier(&barrierList, tex[TEXTURE_IMPORTANCE_MAP], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
			pushBarrier(&barrierList, tex[TEXTURE_IMPORTANCE_MAP_PONG], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
			vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, barrierList.len, barrierList.barriers);

			computeDispatch(context, cb, DS_POSTPROCESS_IMPORTANCE_MAP_B, CS_POSTPROCESS_IMPORTANCE_MAP_B, dispatchWidth, dispatchHeight);

			endDebugMarker(context, cb);
		}

		endDebugMarker(context, cb);
		GET_TIMESTAMP(IMPORTANCE_MAP)

		barrierList.len = 0;
		pushBarrier(&barrierList, tex[TEXTURE_IMPORTANCE_MAP], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
		pushBarrier(&barrierList, context->loadCounter, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);
		vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, barrierList.len, barrierList.barriers);
	}

	// main ssao generation
	{
		beginDebugMarker(context, cb, "Generate SSAO");

		uint32_t dispatchWidth = dispatchSize(GENERATE_WIDTH, bsi->ssaoBufferWidth);
		uint32_t dispatchHeight = dispatchSize(GENERATE_WIDTH, bsi->ssaoBufferHeight);

		ComputeShaderID generateCS = (ComputeShaderID)(CS_GENERATE_Q0 + FFX_CACAO_MAX(0, context->settings.qualityLevel - 1));
		for (int pass = 0; pass < 4; ++pass)
		{
			if (context->settings.qualityLevel == FFX_CACAO_QUALITY_LOWEST && (pass == 1 || pass == 2))
			{
				continue;
			}

			DescriptorSetID descriptorSetID = context->settings.qualityLevel == FFX_CACAO_QUALITY_HIGHEST ? DS_GENERATE_ADAPTIVE_0 : DS_GENERATE_0;
			descriptorSetID = (DescriptorSetID)(descriptorSetID + pass);

			computeDispatch(context, cb, descriptorSetID, generateCS, dispatchWidth, dispatchHeight);
		}

		endDebugMarker(context, cb);
		GET_TIMESTAMP(GENERATE_SSAO)
	}

	uint32_t blurPassCount = context->settings.blurPassCount;
	blurPassCount = FFX_CACAO_CLAMP(blurPassCount, 0, MAX_BLUR_PASSES);

	// de-interleaved blur
	if (blurPassCount)
	{
		barrierList.len = 0;
		pushBarrier(&barrierList, tex[TEXTURE_SSAO_BUFFER_PING], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
		pushBarrier(&barrierList, tex[TEXTURE_SSAO_BUFFER_PONG], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT);
		vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, barrierList.len, barrierList.barriers);

		beginDebugMarker(context, cb, "Deinterleaved Blur");

		uint32_t w = 4 * BLUR_WIDTH - 2 * blurPassCount;
		uint32_t h = 3 * BLUR_HEIGHT - 2 * blurPassCount;
		uint32_t dispatchWidth = dispatchSize(w, bsi->ssaoBufferWidth);
		uint32_t dispatchHeight = dispatchSize(h, bsi->ssaoBufferHeight);

		for (int pass = 0; pass < 4; ++pass)
		{
			if (context->settings.qualityLevel == FFX_CACAO_QUALITY_LOWEST && (pass == 1 || pass == 2))
			{
				continue;
			}

			ComputeShaderID blurShaderID = (ComputeShaderID)(CS_EDGE_SENSITIVE_BLUR_1 + blurPassCount - 1);
			DescriptorSetID descriptorSetID = (DescriptorSetID)(DS_EDGE_SENSITIVE_BLUR_0 + pass);
			computeDispatch(context, cb, descriptorSetID, blurShaderID, dispatchWidth, dispatchHeight);
		}

		endDebugMarker(context, cb);
		GET_TIMESTAMP(EDGE_SENSITIVE_BLUR)

		barrierList.len = 0;
		pushBarrier(&barrierList, tex[TEXTURE_SSAO_BUFFER_PONG], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
		pushBarrier(&barrierList, context->output, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT);
		vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, barrierList.len, barrierList.barriers);
	}
	else
	{
		barrierList.len = 0;
		pushBarrier(&barrierList, tex[TEXTURE_SSAO_BUFFER_PING], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
		pushBarrier(&barrierList, context->output, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT);
		vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, barrierList.len, barrierList.barriers);
	}


	if (context->useDownsampledSsao)
	{
		beginDebugMarker(context, cb, "Bilateral Upsample");

		uint32_t dispatchWidth = dispatchSize(2 * BILATERAL_UPSCALE_WIDTH, bsi->inputOutputBufferWidth);
		uint32_t dispatchHeight = dispatchSize(2 * BILATERAL_UPSCALE_HEIGHT, bsi->inputOutputBufferHeight);

		DescriptorSetID descriptorSetID = blurPassCount ? DS_BILATERAL_UPSAMPLE_PONG : DS_BILATERAL_UPSAMPLE_PING;
		ComputeShaderID upscaler = context->settings.qualityLevel == FFX_CACAO_QUALITY_LOWEST ? CS_UPSCALE_BILATERAL_5X5_HALF : CS_UPSCALE_BILATERAL_5X5;

		computeDispatch(context, cb, descriptorSetID, upscaler, dispatchWidth, dispatchHeight);

		endDebugMarker(context, cb);
		GET_TIMESTAMP(BILATERAL_UPSAMPLE)
	}
	else
	{
		beginDebugMarker(context, cb, "Reinterleave");

		uint32_t dispatchWidth = dispatchSize(APPLY_WIDTH, bsi->inputOutputBufferWidth);
		uint32_t dispatchHeight = dispatchSize(APPLY_HEIGHT, bsi->inputOutputBufferHeight);

		DescriptorSetID descriptorSetID = blurPassCount ? DS_APPLY_PONG : DS_APPLY_PING;

		switch (context->settings.qualityLevel)
		{
		case FFX_CACAO_QUALITY_LOWEST:
			computeDispatch(context, cb, descriptorSetID, CS_NON_SMART_HALF_APPLY, dispatchWidth, dispatchHeight);
			break;
		case FFX_CACAO_QUALITY_LOW:
			computeDispatch(context, cb, descriptorSetID, CS_NON_SMART_APPLY, dispatchWidth, dispatchHeight);
			break;
		default:
			computeDispatch(context, cb, descriptorSetID, CS_APPLY, dispatchWidth, dispatchHeight);
			break;
		}

		endDebugMarker(context, cb);
		GET_TIMESTAMP(APPLY)
	}
	
	endDebugMarker(context, cb);

	barrierList.len = 0;
	pushBarrier(&barrierList, context->output, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
	vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, barrierList.len, barrierList.barriers);

#ifdef FFX_CACAO_ENABLE_PROFILING
	context->timestampQueries[curBuffer].numTimestamps = numTimestamps;
#endif

	return FFX_CACAO_STATUS_OK;
}

#ifdef FFX_CACAO_ENABLE_PROFILING
FfxCacaoStatus ffxCacaoVkGetDetailedTimings(FfxCacaoVkContext* context, FfxCacaoDetailedTiming* timings)
{
	if (context == NULL || timings == NULL)
	{
		return FFX_CACAO_STATUS_INVALID_POINTER;
	}
	context = getAlignedVkContextPointer(context);

	uint32_t bufferIndex = context->collectBuffer;
	uint32_t numTimestamps = context->timestampQueries[bufferIndex].numTimestamps;
	uint64_t prevTime = context->timestampQueries[bufferIndex].timings[0];
	for (uint32_t i = 1; i < numTimestamps; ++i)
	{
		TimestampID timestampID = context->timestampQueries[bufferIndex].timestamps[i];
		timings->timestamps[i].label = TIMESTAMP_NAMES[timestampID];
		uint64_t time = context->timestampQueries[bufferIndex].timings[i];
		timings->timestamps[i].ticks = time - prevTime;
		prevTime = time;
	}
	timings->timestamps[0].label = "FFX_CACAO_TOTAL";
	timings->timestamps[0].ticks = prevTime - context->timestampQueries[bufferIndex].timings[0];
	timings->numTimestamps = numTimestamps;
	
	return FFX_CACAO_STATUS_OK;
}
#endif
#endif

#ifdef __cplusplus
}
#endif
