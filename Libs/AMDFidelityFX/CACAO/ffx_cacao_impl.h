// Modifications Copyright © 2021. Advanced Micro Devices, Inc. All Rights Reserved.

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

#include "ffx_cacao.h"

// #define FFX_CACAO_ENABLE_PROFILING
// #define FFX_CACAO_ENABLE_D3D12
// #define FFX_CACAO_ENABLE_VULKAN

#ifdef FFX_CACAO_ENABLE_D3D12
#include <d3d12.h>
#endif
#ifdef FFX_CACAO_ENABLE_VULKAN
#include <vulkan/vulkan.h>
#endif

/**
	The return codes for the API functions.
*/
typedef enum FFX_CACAO_Status {
	FFX_CACAO_STATUS_OK               =  0,
	FFX_CACAO_STATUS_INVALID_ARGUMENT = -1,
	FFX_CACAO_STATUS_INVALID_POINTER  = -2,
	FFX_CACAO_STATUS_OUT_OF_MEMORY    = -3,
	FFX_CACAO_STATUS_FAILED           = -4,
} FFX_CACAO_Status;

#ifdef FFX_CACAO_ENABLE_D3D12
/**
	A struct containing all of the data used by FidelityFX-CACAO.
	A context corresponds to an ID3D12Device.
*/
typedef struct FFX_CACAO_D3D12Context FFX_CACAO_D3D12Context;

/**
	The parameters for creating a context.
*/
typedef struct FFX_CACAO_D3D12ScreenSizeInfo {
	uint32_t                          width;                ///< width of the input/output buffers
	uint32_t                          height;               ///< height of the input/output buffers
	ID3D12Resource                   *depthBufferResource;  ///< pointer to depth buffer ID3D12Resource
	D3D12_SHADER_RESOURCE_VIEW_DESC   depthBufferSrvDesc;   ///< depth buffer D3D12_SHADER_RESOURCE_VIEW_DESC
	ID3D12Resource                   *normalBufferResource; ///< optional pointer to normal buffer ID3D12Resource (leave as NULL if none is provided)
	D3D12_SHADER_RESOURCE_VIEW_DESC   normalBufferSrvDesc;  ///< normal buffer D3D12_SHADER_RESOURCE_VIEW_DESC
	ID3D12Resource                   *outputResource;       ///< pointer to output buffer ID3D12Resource
	D3D12_UNORDERED_ACCESS_VIEW_DESC  outputUavDesc;        ///< output buffer D3D12_UNORDERED_ACCESS_VIEW_DESC
	FFX_CACAO_Bool                      useDownsampledSsao;   ///< Whether SSAO should be generated at native resolution or half resolution. It is recommended to enable this setting for improved performance.
} FFX_CACAO_D3D12ScreenSizeInfo;
#endif

#ifdef FFX_CACAO_ENABLE_VULKAN
/**
	A struct containing all of the data used by FidelityFX-CACAO.
	A context corresponds to a VkDevice.
*/
typedef struct FFX_CACAO_VkContext FFX_CACAO_VkContext;

/**
	Miscellaneous flags for used for Vulkan context creation by FidelityFX-CACAO
 */
typedef enum FFX_CACAO_VkCreateFlagsBits {
	FFX_CACAO_VK_CREATE_USE_16_BIT        = 0x00000001, ///< Flag controlling whether 16-bit optimisations are enabled in shaders.
	FFX_CACAO_VK_CREATE_USE_DEBUG_MARKERS = 0x00000002, ///< Flag controlling whether debug markers should be used.
	FFX_CACAO_VK_CREATE_NAME_OBJECTS      = 0x00000004, ///< Flag controlling whether Vulkan objects should be named.
} FFX_CACAO_VkCreateFlagsBits;
typedef uint32_t FFX_CACAO_VkCreateFlags;

/**
	The parameters for creating a context.
*/
typedef struct FFX_CACAO_VkCreateInfo {
	VkPhysicalDevice                 physicalDevice; ///< The VkPhysicalDevice corresponding to the VkDevice in use
	VkDevice                         device;         ///< The VkDevice to use FFX CACAO with
	FFX_CACAO_VkCreateFlags            flags;          ///< Miscellaneous flags for context creation
} FFX_CACAO_VkCreateInfo;

/**
	The parameters necessary when changing the screen size of FidelityFX CACAO.
*/
typedef struct FFX_CACAO_VkScreenSizeInfo {
	uint32_t                          width;                ///< width of the input/output buffers
	uint32_t                          height;               ///< height of the input/output buffers
	VkImageView                       depthView;            ///< An image view for the depth buffer, should be in layout VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL when used with FFX CACAO
	VkImageView                       normalsView;          ///< An optional image view for the normal buffer (may be VK_NULL_HANDLE). Should be in layout VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL when used with FFX CACAO
	VkImage                           output;               ///< An image for writing output from FFX CACAO, must have the same dimensions as the input
	VkImageView                       outputView;           ///< An image view corresponding to the output image.
	FFX_CACAO_Bool                      useDownsampledSsao;   ///< Whether SSAO should be generated at native resolution or half resolution. It is recommended to enable this setting for improved performance.
} FFX_CACAO_VkScreenSizeInfo;
#endif

#ifdef FFX_CACAO_ENABLE_PROFILING
/**
	A timestamp. The label gives the name of the stage of the effect, and the ticks is the number of GPU ticks spent on that stage.
*/
typedef struct FFX_CACAO_Timestamp {
	const char *label; ///< name of timestamp stage
	uint64_t    ticks; ///< number of GPU ticks taken for stage
} FFX_CACAO_Timestamp;

/**
	An array of timestamps for detailed profiling information. The array timestamps contains numTimestamps entries.
	Entry 0 of the timestamps array is guaranteed to be the total time taken by the effect.
*/
typedef struct FFX_CACAO_DetailedTiming {
	uint32_t          numTimestamps;  ///< number of timetstamps in the array timestamps
	FFX_CACAO_Timestamp timestamps[32]; ///< array of timestamps for each FFX CACAO stage
} FFX_CACAO_DetailedTiming;
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
		size_t FFX_CACAO_D3D12ContextSize = ffxCacaoD3D12GetContextSize();
		FFX_CACAO_D3D12Context *context = (FFX_CACAO_D3D12Context*)malloc(FFX_CACAO_D3D12GetContextSize);

		// ...

		FFX_CACAO_D3D12DestroyContext(context);
		free(context);
		\endcode

		\return The size in bytes of an FFX_CACAO_D3D12Context.
	*/
	size_t FFX_CACAO_D3D12GetContextSize();

	/**
		Initialises an FFX_CACAO_D3D12Context.

		\param context A pointer to the context to initialise.
		\param device A pointer to the D3D12 device.
		\return The corresponding error code.
	*/
	FFX_CACAO_Status FFX_CACAO_D3D12InitContext(FFX_CACAO_D3D12Context* context, ID3D12Device* device);

	/**
		Destroys an FFX_CACAO_D3D12Context.

		\param context A pointer to the context to be destroyed.
		\return The corresponding error code.

		\note This function does not destroy screen size dependent resources, and must be called after FFX_CACAO_D3D12DestroyScreenSizeDependentResources.
	*/
	FFX_CACAO_Status FFX_CACAO_D3D12DestroyContext(FFX_CACAO_D3D12Context* context);

	/**
		Initialises screen size dependent resources for the FFX_CACAO_D3D12Context.

		\param context A pointer to the FFX_CACAO_D3D12Context.
		\param info A pointer to an FFX_CACAO_D3D12ScreenSizeInfo struct containing screen size info.
		\return The corresponding error code.
	*/
	FFX_CACAO_Status FFX_CACAO_D3D12InitScreenSizeDependentResources(FFX_CACAO_D3D12Context* context, const FFX_CACAO_D3D12ScreenSizeInfo* info);

	/**
		Destroys screen size dependent resources for the FFX_CACAO_D3D12Context.

		\param context A pointer to the FFX_CACAO_D3D12Context.
		\return The corresponding error code.
	*/
	FFX_CACAO_Status FFX_CACAO_D3D12DestroyScreenSizeDependentResources(FFX_CACAO_D3D12Context* context);

	/**
		Update the settings of the FFX_CACAO_D3D12Context to those stored in the FFX_CACAO_Settings struct.

		\param context A pointer to the FFX_CACAO_D3D12Context to update.
		\param settings A pointer to the FFX_CACAO_Settings struct containing the new settings.
		\return The corresponding error code.
	*/
	FFX_CACAO_Status FFX_CACAO_D3D12UpdateSettings(FFX_CACAO_D3D12Context* context, const FFX_CACAO_Settings* settings);

	/**
		Append commands for drawing FFX CACAO to the provided ID3D12GraphicsCommandList.

		\param context A pointer to the FFX_CACAO_D3D12Context.
		\param commandList A pointer to the ID3D12GraphicsCommandList to append commands to.
		\param proj A pointer to the projection matrix.
		\param normalsToView An optional pointer to a matrix for transforming normals to in the normal buffer to viewspace.
		\return The corresponding error code.
	*/
	FFX_CACAO_Status FFX_CACAO_D3D12Draw(FFX_CACAO_D3D12Context* context, ID3D12GraphicsCommandList* commandList, const FFX_CACAO_Matrix4x4* proj, const FFX_CACAO_Matrix4x4* normalsToView);

#if FFX_CACAO_ENABLE_PROFILING
	/**
		Get detailed performance timings from the previous frame.

		\param context A pointer to the FFX_CACAO_D3D12Context.
		\param timings A pointer to an FFX_CACAO_DetailedTiming struct to fill in with detailed timings.
		\result The corresponding error code.
	*/
	FFX_CACAO_Status FFX_CACAO_D3D12GetDetailedTimings(FFX_CACAO_D3D12Context* context, FFX_CACAO_DetailedTiming* timings);
#endif
#endif

#ifdef FFX_CACAO_ENABLE_VULKAN
	/**
		Gets the size in bytes required by a Vulkan context. This is to be used to allocate space for the context.
		For example:

		\code{.cpp}
		size_t FFX_CACAO_VkContextSize = ffxCacaoVkGetContextSize();
		FFX_CACAO_VkContext *context = (FFX_CACAO_VkContext*)malloc(FFX_CACAO_VkGetContextSize);

		// ...

		FFX_CACAO_VkDestroyContext(context);
		free(context);
		\endcode

		\return The size in bytes of an FFX_CACAO_VkContext.
	*/
	size_t FFX_CACAO_VkGetContextSize();

	/**
		Initialises an FFX_CACAO_VkContext.

		\param context A pointer to the context to initialise.
		\param info A pointer to an FFX_CACAO_VkCreateInfo struct with parameters such as the vulkan device.
		\return The corresponding error code.
	*/
	FFX_CACAO_Status FFX_CACAO_VkInitContext(FFX_CACAO_VkContext* context, const FFX_CACAO_VkCreateInfo *info);

	/**
		Destroys an FFX_CACAO_VkContext.

		\param context A pointer to the context to be destroyed.
		\return The corresponding error code.

		\note This function does not destroy screen size dependent resources, and must be called after FFX_CACAO_VkDestroyScreenSizeDependentResources.
	*/
	FFX_CACAO_Status FFX_CACAO_VkDestroyContext(FFX_CACAO_VkContext* context);

	/**
		Initialises screen size dependent resources for the FFX_CACAO_VkContext.

		\param context A pointer to the FFX_CACAO_VkContext.
		\param info A pointer to an FFX_CACAO_VkScreenSizeInfo struct containing screen size info.
		\return The corresponding error code.
	*/
	FFX_CACAO_Status FFX_CACAO_VkInitScreenSizeDependentResources(FFX_CACAO_VkContext* context, const FFX_CACAO_VkScreenSizeInfo* info);

	/**
		Destroys screen size dependent resources for the FFX_CACAO_VkContext.

		\param context A pointer to the FFX_CACAO_VkContext.
		\return The corresponding error code.
	*/
	FFX_CACAO_Status FFX_CACAO_VkDestroyScreenSizeDependentResources(FFX_CACAO_VkContext* context);

	/**
		Update the settings of the FFX_CACAO_VkContext to those stored in the FFX_CACAO_Settings struct.

		\param context A pointer to the FFX_CACAO_VkContext to update.
		\param settings A pointer to the FFX_CACAO_Settings struct containing the new settings.
		\return The corresponding error code.
	*/
	FFX_CACAO_Status FFX_CACAO_VkUpdateSettings(FFX_CACAO_VkContext* context, const FFX_CACAO_Settings* settings);

	/**
		Append commands for drawing FFX CACAO to the provided VkCommandBuffer.

		\param context A pointer to the FFX_CACAO_VkContext.
		\param commandList The VkCommandBuffer to append commands to.
		\param proj A pointer to the projection matrix.
		\param normalsToView An optional pointer to a matrix for transforming normals to in the normal buffer to viewspace.
		\return The corresponding error code.
	*/
	FFX_CACAO_Status FFX_CACAO_VkDraw(FFX_CACAO_VkContext* context, VkCommandBuffer commandList, const FFX_CACAO_Matrix4x4* proj, const FFX_CACAO_Matrix4x4* normalsToView);

#ifdef FFX_CACAO_ENABLE_PROFILING
	/**
		Get detailed performance timings from the previous frame.

		\param context A pointer to the FFX_CACAO_VkContext.
		\param timings A pointer to an FFX_CACAO_DetailedTiming struct to fill in with detailed timings.
		\result The corresponding error code.
	*/
	FFX_CACAO_Status FFX_CACAO_VkGetDetailedTimings(FFX_CACAO_VkContext* context, FFX_CACAO_DetailedTiming* timings);
#endif
#endif

#ifdef __cplusplus
}
#endif
