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

/*! \file */

#pragma once

// In future it is planned that FidelityFX CACAO will allow SSAO generation at native resolution.
// However, at the current time the performance/image quality trade off is poor, and further optimisation
// work is being carried out. If you wish to experiment with native resolution SSAO generation enable this
// flag. Integrating FidelityFX CACAO into games with native resolution enabled is currently not recommended.
// #define FFX_CACAO_ENABLE_NATIVE_RESOLUTION

// #define FFX_CACAO_ENABLE_PROFILING
// #define FFX_CACAO_ENABLE_D3D12
// #define FFX_CACAO_ENABLE_VULKAN

#include <stdint.h>
#ifdef FFX_CACAO_ENABLE_D3D12
#include <d3d12.h>
#endif
#ifdef FFX_CACAO_ENABLE_VULKAN
#include <vulkan/vulkan.h>
#endif

typedef uint8_t FfxCacaoBool;
static const FfxCacaoBool FFX_CACAO_TRUE  = 1;
static const FfxCacaoBool FFX_CACAO_FALSE = 0;

/**
	The return codes for the API functions.
*/
typedef enum FfxCacaoStatus {
	FFX_CACAO_STATUS_OK               =  0,
	FFX_CACAO_STATUS_INVALID_ARGUMENT = -1,
	FFX_CACAO_STATUS_INVALID_POINTER  = -2,
	FFX_CACAO_STATUS_OUT_OF_MEMORY    = -3,
	FFX_CACAO_STATUS_FAILED           = -4,
} FfxCacaoStatus;

/**
	The quality levels that FidelityFX CACAO can generate SSAO at. This affects the number of samples taken for generating SSAO.
*/
typedef enum FfxCacaoQuality {
	FFX_CACAO_QUALITY_LOWEST  = 0,
	FFX_CACAO_QUALITY_LOW     = 1,
	FFX_CACAO_QUALITY_MEDIUM  = 2,
	FFX_CACAO_QUALITY_HIGH    = 3,
	FFX_CACAO_QUALITY_HIGHEST = 4,
} FfxCacaoQuality;

/**
	A structure representing a 4x4 matrix of floats. The matrix is stored in row major order in memory.
*/
typedef struct FfxCacaoMatrix4x4 {
	float elements[4][4];
} FfxCacaoMatrix4x4;

/**
	A structure for the settings used by FidelityFX CACAO. These settings may be updated with each draw call.
*/
typedef struct FfxCacaoSettings {
	float           radius;                            ///< [0.0,  ~ ] World (view) space size of the occlusion sphere.
	float           shadowMultiplier;                  ///< [0.0, 5.0] Effect strength linear multiplier
	float           shadowPower;                       ///< [0.5, 5.0] Effect strength pow modifier
	float           shadowClamp;                       ///< [0.0, 1.0] Effect max limit (applied after multiplier but before blur)
	float           horizonAngleThreshold;             ///< [0.0, 0.2] Limits self-shadowing (makes the sampling area less of a hemisphere, more of a spherical cone, to avoid self-shadowing and various artifacts due to low tessellation and depth buffer imprecision, etc.)
	float           fadeOutFrom;                       ///< [0.0,  ~ ] Distance to start start fading out the effect.
	float           fadeOutTo;                         ///< [0.0,  ~ ] Distance at which the effect is faded out.
	FfxCacaoQuality qualityLevel;                      ///<            Effect quality, affects number of taps etc
	float           adaptiveQualityLimit;              ///< [0.0, 1.0] (only for Quality Level 3)
	uint32_t        blurPassCount;                     ///< [  0,   8] Number of edge-sensitive smart blur passes to apply
	float           sharpness;                         ///< [0.0, 1.0] (How much to bleed over edges; 1: not at all, 0.5: half-half; 0.0: completely ignore edges)
	float           temporalSupersamplingAngleOffset;  ///< [0.0,  PI] Used to rotate sampling kernel; If using temporal AA / supersampling, suggested to rotate by ( (frame%3)/3.0*PI ) or similar. Kernel is already symmetrical, which is why we use PI and not 2*PI.
	float           temporalSupersamplingRadiusOffset; ///< [0.0, 2.0] Used to scale sampling kernel; If using temporal AA / supersampling, suggested to scale by ( 1.0f + (((frame%3)-1.0)/3.0)*0.1 ) or similar.
	float           detailShadowStrength;              ///< [0.0, 5.0] Used for high-res detail AO using neighboring depth pixels: adds a lot of detail but also reduces temporal stability (adds aliasing).
	FfxCacaoBool    generateNormals;                   ///< This option should be set to FFX_CACAO_TRUE if FidelityFX-CACAO should reconstruct a normal buffer from the depth buffer. It is required to be FFX_CACAO_TRUE if no normal buffer is provided.
	float           bilateralSigmaSquared;             ///< [0.0,  ~ ] Sigma squared value for use in bilateral upsampler giving Gaussian blur term. Should be greater than 0.0.
	float           bilateralSimilarityDistanceSigma;  ///< [0.0,  ~ ] Sigma squared value for use in bilateral upsampler giving similarity weighting for neighbouring pixels. Should be greater than 0.0.
} FfxCacaoSettings;

static const FfxCacaoSettings FFX_CACAO_DEFAULT_SETTINGS = {
	/* radius                            */ 1.2f,
	/* shadowMultiplier                  */ 1.0f,
	/* shadowPower                       */ 1.50f,
	/* shadowClamp                       */ 0.98f,
	/* horizonAngleThreshold             */ 0.06f,
	/* fadeOutFrom                       */ 50.0f,
	/* fadeOutTo                         */ 300.0f,
	/* qualityLevel                      */ FFX_CACAO_QUALITY_HIGHEST,
	/* adaptiveQualityLimit              */ 0.45f,
	/* blurPassCount                     */ 2,
	/* sharpness                         */ 0.98f,
	/* temporalSupersamplingAngleOffset  */ 0.0f,
	/* temporalSupersamplingRadiusOffset */ 0.0f,
	/* detailShadowStrength              */ 0.5f,
	/* generateNormals                   */ FFX_CACAO_FALSE,
	/* bilateralSigmaSquared             */ 5.0f,
	/* bilateralSimilarityDistanceSigma  */ 0.01f,
};


#ifdef FFX_CACAO_ENABLE_D3D12
/**
	A struct containing all of the data used by FidelityFX-CACAO.
	A context corresponds to an ID3D12Device.
*/
typedef struct FfxCacaoD3D12Context FfxCacaoD3D12Context;

/**
	The parameters for creating a context.
*/
typedef struct FfxCacaoD3D12ScreenSizeInfo {
	uint32_t                          width;                ///< width of the input/output buffers
	uint32_t                          height;               ///< height of the input/output buffers
	ID3D12Resource                   *depthBufferResource;  ///< pointer to depth buffer ID3D12Resource
	D3D12_SHADER_RESOURCE_VIEW_DESC   depthBufferSrvDesc;   ///< depth buffer D3D12_SHADER_RESOURCE_VIEW_DESC
	ID3D12Resource                   *normalBufferResource; ///< optional pointer to normal buffer ID3D12Resource (leave as NULL if none is provided)
	D3D12_SHADER_RESOURCE_VIEW_DESC   normalBufferSrvDesc;  ///< normal buffer D3D12_SHADER_RESOURCE_VIEW_DESC
	ID3D12Resource                   *outputResource;       ///< pointer to output buffer ID3D12Resource
	D3D12_UNORDERED_ACCESS_VIEW_DESC  outputUavDesc;        ///< output buffer D3D12_UNORDERED_ACCESS_VIEW_DESC
#ifdef FFX_CACAO_ENABLE_NATIVE_RESOLUTION
	FfxCacaoBool                      useDownsampledSsao;   ///< Whether SSAO should be generated at native resolution or half resolution. It is recommended to enable this setting for improved performance.
#endif
} FfxCacaoD3D12ScreenSizeInfo;
#endif

#ifdef FFX_CACAO_ENABLE_VULKAN
/**
	A struct containing all of the data used by FidelityFX-CACAO.
	A context corresponds to a VkDevice.
*/
typedef struct FfxCacaoVkContext FfxCacaoVkContext;

/**
	Miscellaneous flags for used for Vulkan context creation by FidelityFX-CACAO
 */
typedef enum FfxCacaoVkCreateFlagsBits {
	FFX_CACAO_VK_CREATE_USE_16_BIT        = 0x00000001, ///< Flag controlling whether 16-bit optimisations are enabled in shaders.
	FFX_CACAO_VK_CREATE_USE_DEBUG_MARKERS = 0x00000002, ///< Flag controlling whether debug markers should be used.
	FFX_CACAO_VK_CREATE_NAME_OBJECTS      = 0x00000004, ///< Flag controlling whether Vulkan objects should be named.
} FfxCacaoVkCreateFlagsBits;
typedef uint32_t FfxCacaoVkCreateFlags;

/**
	The parameters for creating a context.
*/
typedef struct FfxCacaoVkCreateInfo {
	VkPhysicalDevice                 physicalDevice; ///< The VkPhysicalDevice corresponding to the VkDevice in use
	VkDevice                         device;         ///< The VkDevice to use FFX CACAO with
	FfxCacaoVkCreateFlags            flags;          ///< Miscellaneous flags for context creation
} FfxCacaoVkCreateInfo;

/**
	The parameters necessary when changing the screen size of FidelityFX CACAO.
*/
typedef struct FfxCacaoVkScreenSizeInfo {
	uint32_t                          width;                ///< width of the input/output buffers
	uint32_t                          height;               ///< height of the input/output buffers
	VkImageView                       depthView;            ///< An image view for the depth buffer, should be in layout VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL when used with FFX CACAO
	VkImageView                       normalsView;          ///< An optional image view for the normal buffer (may be VK_NULL_HANDLE). Should be in layout VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL when used with FFX CACAO
	VkImage                           output;               ///< An image for writing output from FFX CACAO, must have the same dimensions as the input
	VkImageView                       outputView;           ///< An image view corresponding to the output image.
#ifdef FFX_CACAO_ENABLE_NATIVE_RESOLUTION
	FfxCacaoBool                      useDownsampledSsao;   ///< Whether SSAO should be generated at native resolution or half resolution. It is recommended to enable this setting for improved performance.
#endif
} FfxCacaoVkScreenSizeInfo;
#endif

#ifdef FFX_CACAO_ENABLE_PROFILING
/**
	A timestamp. The label gives the name of the stage of the effect, and the ticks is the number of GPU ticks spent on that stage.
*/
typedef struct FfxCacaoTimestamp {
	const char *label; ///< name of timestamp stage
	uint64_t    ticks; ///< number of GPU ticks taken for stage
} FfxCacaoTimestamp;

/**
	An array of timestamps for detailed profiling information. The array timestamps contains numTimestamps entries.
	Entry 0 of the timestamps array is guaranteed to be the total time taken by the effect.
*/
typedef struct FfxCacaoDetailedTiming {
	uint32_t          numTimestamps;  ///< number of timetstamps in the array timestamps
	FfxCacaoTimestamp timestamps[32]; ///< array of timestamps for each FFX CACAO stage
} FfxCacaoDetailedTiming;
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef FFX_CACAO_ENABLE_D3D12
	/**
		Gets the size in bytes required by a context. This is to be used to allocate space for the context.
		For example:

		\code{.cpp}
		size_t ffxCacaoD3D12ContextSize = ffxCacaoD3D12GetContextSize();
		FfxCacaoD3D12Context *context = (FfxCacaoD3D12Context*)malloc(ffxCacaoD3D12GetContextSize);

		// ...

		ffxCacaoD3D12DestroyContext(context);
		free(context);
		\endcode

		\return The size in bytes of an FfxCacaoD3D12Context.
	*/
	size_t ffxCacaoD3D12GetContextSize();

	/**
		Initialises an FfxCacaoD3D12Context.

		\param context A pointer to the context to initialise.
		\param device A pointer to the D3D12 device.
		\return The corresponding error code.
	*/
	FfxCacaoStatus ffxCacaoD3D12InitContext(FfxCacaoD3D12Context* context, ID3D12Device* device);

	/**
		Destroys an FfxCacaoD3D12Context.

		\param context A pointer to the context to be destroyed.
		\return The corresponding error code.

		\note This function does not destroy screen size dependent resources, and must be called after ffxCacaoD3D12DestroyScreenSizeDependentResources.
	*/
	FfxCacaoStatus ffxCacaoD3D12DestroyContext(FfxCacaoD3D12Context* context);

	/**
		Initialises screen size dependent resources for the FfxCacaoD3D12Context.

		\param context A pointer to the FfxCacaoD3D12Context.
		\param info A pointer to an FfxCacaoD3D12ScreenSizeInfo struct containing screen size info.
		\return The corresponding error code.
	*/
	FfxCacaoStatus ffxCacaoD3D12InitScreenSizeDependentResources(FfxCacaoD3D12Context* context, const FfxCacaoD3D12ScreenSizeInfo* info);

	/**
		Destroys screen size dependent resources for the FfxCacaoD3D12Context.

		\param context A pointer to the FfxCacaoD3D12Context.
		\return The corresponding error code.
	*/
	FfxCacaoStatus ffxCacaoD3D12DestroyScreenSizeDependentResources(FfxCacaoD3D12Context* context);

	/**
		Update the settings of the FfxCacaoD3D12Context to those stored in the FfxCacaoSettings struct.

		\param context A pointer to the FfxCacaoD3D12Context to update.
		\param settings A pointer to the FfxCacaoSettings struct containing the new settings.
		\return The corresponding error code.
	*/
	FfxCacaoStatus ffxCacaoD3D12UpdateSettings(FfxCacaoD3D12Context* context, const FfxCacaoSettings* settings);

	/**
		Append commands for drawing FFX CACAO to the provided ID3D12GraphicsCommandList.

		\param context A pointer to the FfxCacaoD3D12Context.
		\param commandList A pointer to the ID3D12GraphicsCommandList to append commands to.
		\param proj A pointer to the projection matrix.
		\param normalsToView An optional pointer to a matrix for transforming normals to in the normal buffer to viewspace.
		\return The corresponding error code.
	*/
	FfxCacaoStatus ffxCacaoD3D12Draw(FfxCacaoD3D12Context* context, ID3D12GraphicsCommandList* commandList, const FfxCacaoMatrix4x4* proj, const FfxCacaoMatrix4x4* normalsToView);

#if FFX_CACAO_ENABLE_PROFILING
	/**
		Get detailed performance timings from the previous frame.

		\param context A pointer to the FfxCacaoD3D12Context.
		\param timings A pointer to an FfxCacaoDetailedTiming struct to fill in with detailed timings.
		\result The corresponding error code.
	*/
	FfxCacaoStatus ffxCacaoD3D12GetDetailedTimings(FfxCacaoD3D12Context* context, FfxCacaoDetailedTiming* timings);
#endif
#endif

#ifdef FFX_CACAO_ENABLE_VULKAN
	/**
		Gets the size in bytes required by a Vulkan context. This is to be used to allocate space for the context.
		For example:

		\code{.cpp}
		size_t ffxCacaoVkContextSize = ffxCacaoVkGetContextSize();
		FfxCacaoVkContext *context = (FfxCacaoVkContext*)malloc(ffxCacaoVkGetContextSize);

		// ...

		ffxCacaoVkDestroyContext(context);
		free(context);
		\endcode

		\return The size in bytes of an FfxCacaoVkContext.
	*/
	size_t ffxCacaoVkGetContextSize();

	/**
		Initialises an FfxCacaoVkContext.

		\param context A pointer to the context to initialise.
		\param info A pointer to an FfxCacaoVkCreateInfo struct with parameters such as the vulkan device.
		\return The corresponding error code.
	*/
	FfxCacaoStatus ffxCacaoVkInitContext(FfxCacaoVkContext* context, const FfxCacaoVkCreateInfo *info);

	/**
		Destroys an FfxCacaoVkContext.

		\param context A pointer to the context to be destroyed.
		\return The corresponding error code.

		\note This function does not destroy screen size dependent resources, and must be called after ffxCacaoVkDestroyScreenSizeDependentResources.
	*/
	FfxCacaoStatus ffxCacaoVkDestroyContext(FfxCacaoVkContext* context);

	/**
		Initialises screen size dependent resources for the FfxCacaoVkContext.

		\param context A pointer to the FfxCacaoVkContext.
		\param info A pointer to an FfxCacaoVkScreenSizeInfo struct containing screen size info.
		\return The corresponding error code.
	*/
	FfxCacaoStatus ffxCacaoVkInitScreenSizeDependentResources(FfxCacaoVkContext* context, const FfxCacaoVkScreenSizeInfo* info);

	/**
		Destroys screen size dependent resources for the FfxCacaoVkContext.

		\param context A pointer to the FfxCacaoVkContext.
		\return The corresponding error code.
	*/
	FfxCacaoStatus ffxCacaoVkDestroyScreenSizeDependentResources(FfxCacaoVkContext* context);

	/**
		Update the settings of the FfxCacaoVkContext to those stored in the FfxCacaoSettings struct.

		\param context A pointer to the FfxCacaoVkContext to update.
		\param settings A pointer to the FfxCacaoSettings struct containing the new settings.
		\return The corresponding error code.
	*/
	FfxCacaoStatus ffxCacaoVkUpdateSettings(FfxCacaoVkContext* context, const FfxCacaoSettings* settings);

	/**
		Append commands for drawing FFX CACAO to the provided VkCommandBuffer.

		\param context A pointer to the FfxCacaoVkContext.
		\param commandList The VkCommandBuffer to append commands to.
		\param proj A pointer to the projection matrix.
		\param normalsToView An optional pointer to a matrix for transforming normals to in the normal buffer to viewspace.
		\return The corresponding error code.
	*/
	FfxCacaoStatus ffxCacaoVkDraw(FfxCacaoVkContext* context, VkCommandBuffer commandList, const FfxCacaoMatrix4x4* proj, const FfxCacaoMatrix4x4* normalsToView);

#ifdef FFX_CACAO_ENABLE_PROFILING
	/**
		Get detailed performance timings from the previous frame.

		\param context A pointer to the FfxCacaoVkContext.
		\param timings A pointer to an FfxCacaoDetailedTiming struct to fill in with detailed timings.
		\result The corresponding error code.
	*/
	FfxCacaoStatus ffxCacaoVkGetDetailedTimings(FfxCacaoVkContext* context, FfxCacaoDetailedTiming* timings);
#endif
#endif

#ifdef __cplusplus
}
#endif
