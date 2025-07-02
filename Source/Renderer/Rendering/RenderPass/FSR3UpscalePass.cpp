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

#include "RenderPass.h"
#include "FSR3UpscalePass.h"
#include "Renderer.h"

#include "Libs/VQUtils/Include/Log.h"
#include "Engine/GPUMarker.h"

#include "ffx_api/dx12/ffx_api_dx12.h"
#include "ffx_api/ffx_upscale.h"
#include "ffx_api/ffx_upscale.hpp"

struct FSR3UpscalePass::ContextImpl
{
	ffxContext ctx = {};
};


FSR3UpscalePass::FSR3UpscalePass(VQRenderer& Renderer)
	: RenderPassBase(Renderer)
{
}

FSR3UpscalePass::~FSR3UpscalePass()
{
}

bool FSR3UpscalePass::Initialize()
{
	assert(!pImpl);
	pImpl = new ContextImpl();
	return pImpl != nullptr;
}

void FSR3UpscalePass::Destroy()
{
	assert(pImpl);
	if (pImpl)
	{
		delete pImpl;
		pImpl = nullptr;
	}
}

static void FSR3MessageCallback(uint type, const wchar_t* msg)
{
	switch (type)
	{
	case FFX_API_MESSAGE_TYPE_ERROR:
		Log::Error("[FFX_FSR3]: %ls", msg);
		break;
	case FFX_API_MESSAGE_TYPE_WARNING:
		Log::Warning("[FFX_FSR3]: %ls", msg);
		break;
	default:
		assert(false);
		break;
	}
}
void FSR3UpscalePass::OnCreateWindowSizeDependentResources(unsigned DisplayWidth, unsigned DisplayHeight, const IRenderPassResourceCollection* pRscParameters)
{
	assert(pImpl);
	if (!pImpl)
	{
		Log::Error("FSR3 Create: Memory not allocated for context");
		return;
	}

	ID3D12Device* pDevice = mRenderer.GetDevicePtr();
	assert(pDevice);

	const FResourceCollection* pResourceInput = static_cast<const FResourceCollection*>(pRscParameters);
	const uint RenderResolutionX = pResourceInput->fResolutionScale * DisplayWidth;
	const uint RenderResolutionY = pResourceInput->fResolutionScale * DisplayHeight;
	const FTexture* pTexture = mRenderer.GetTexture(pResourceInput->texColorInput);
	assert(pTexture);
	if (!pTexture)
	{
		Log::Error("FSR3 OnCreateWindowSizeDependentResources(): invalid input texture, ID=%d", pResourceInput->texColorInput);
		return;
	}

	ffxCreateBackendDX12Desc backendDesc{};
	backendDesc.device = pDevice;
	backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;

	ffxCreateContextDescUpscale createUpscaling = {};
	createUpscaling.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
	createUpscaling.header.pNext = &backendDesc.header;
	createUpscaling.maxUpscaleSize = { DisplayWidth, DisplayHeight };
	createUpscaling.maxRenderSize = { RenderResolutionX, RenderResolutionY };
	createUpscaling.flags = FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;
#if _DEBUG
	createUpscaling.flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
	createUpscaling.fpMessage = FSR3MessageCallback;
#endif

	ffxReturnCode_t retCode = ffxCreateContext(&pImpl->ctx, &createUpscaling.header, nullptr);
	bool bSucceeded = retCode == 0;
	if (!bSucceeded)
	{
		Log::Error("FSR3: Error (%d) creating ffxContext", retCode);
		texOutput = INVALID_ID;
		return;
	}

	FTextureRequest createDesc = {};
	createDesc.bCPUReadback = false;
	createDesc.bCubemap = pTexture->IsCubemap;
	createDesc.bGenerateMips = pTexture->MipCount > 1;
	createDesc.InitialState = D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	createDesc.Name = "FSR3_Output";
	createDesc.D3D12Desc.Dimension = D3D12_RESOURCE_DIMENSION::D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	createDesc.D3D12Desc.Format = pTexture->Format;
	createDesc.D3D12Desc.MipLevels = pTexture->MipCount;
	createDesc.D3D12Desc.DepthOrArraySize = pTexture->ArraySlices;
	createDesc.D3D12Desc.Height = DisplayHeight;
	createDesc.D3D12Desc.Width = DisplayWidth;
	createDesc.D3D12Desc.SampleDesc.Count = 1;
	createDesc.D3D12Desc.Flags = D3D12_RESOURCE_FLAGS::D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	texOutput = mRenderer.CreateTexture(createDesc);

	srvOutput = mRenderer.AllocateSRV(1);
	mRenderer.InitializeSRV(srvOutput, 0, texOutput);
}

void FSR3UpscalePass::OnDestroyWindowSizeDependentResources()
{
	assert(pImpl);
	if (!pImpl)
	{
		Log::Error("FSR3 Destroy: Memory not allocated for context");
		return; // don't crash
	}

	ffxReturnCode_t retCode = ffxDestroyContext(&pImpl->ctx, nullptr);
	bool bSucceeded = retCode == 0;
	if (!bSucceeded)
	{
		Log::Error("Error (%d) destroying ffxContext", retCode);
	}

	mRenderer.DestroySRV(srvOutput);
	mRenderer.DestroyTexture(texOutput);
}

void FSR3UpscalePass::RecordCommands(const IRenderPassDrawParameters* pDrawParameters)
{
	assert(pImpl);
	if (!pImpl)
	{
		Log::Error("FSR3 Dispatch: Memory not allocated for context");
		return; // don't crash
	}

	const Parameters* pParams = static_cast<const Parameters*>(pDrawParameters);
	assert(pParams->pCmd);

	const FResourceCollection& rsc = pParams->Resources;
	assert(rsc.texColorInput != INVALID_ID);
	assert(rsc.texDepthBuffer != INVALID_ID);
	assert(rsc.texMotionVectors != INVALID_ID);

	int RenderSizeX, RenderSizeY;
	mRenderer.GetTextureDimensions(rsc.texColorInput, RenderSizeX, RenderSizeY);
	assert(RenderSizeX > 0 && RenderSizeY > 0);

	int OutputSizeX, OutputSizeY;
	mRenderer.WaitForTexture(texOutput);
	mRenderer.GetTextureDimensions(texOutput, OutputSizeX, OutputSizeY);
	assert(OutputSizeX > 0 && OutputSizeY > 0);

	int MotionVectorScaleX, MotionVectorScaleY;
	mRenderer.GetTextureDimensions(rsc.texMotionVectors, MotionVectorScaleX, MotionVectorScaleY);
	assert(MotionVectorScaleX > 0 && MotionVectorScaleY > 0);

	/*
	struct ffxDispatchDescUpscale
	{
		ffxDispatchDescHeader      header;
		void* commandList;                ///< Command list to record upscaling rendering commands into.
		struct FfxApiResource      color;                      ///< Color buffer for the current frame (at render resolution).
		struct FfxApiResource      depth;                      ///< 32bit depth values for the current frame (at render resolution).
		struct FfxApiResource      motionVectors;              ///< 2-dimensional motion vectors (at render resolution if <c><i>FFX_FSR_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS</i></c> is not set).
		struct FfxApiResource      exposure;                   ///< Optional resource containing a 1x1 exposure value.
		struct FfxApiResource      reactive;                   ///< Optional resource containing alpha value of reactive objects in the scene.
		struct FfxApiResource      transparencyAndComposition; ///< Optional resource containing alpha value of special objects in the scene.
		struct FfxApiResource      output;                     ///< Output color buffer for the current frame (at presentation resolution).
		struct FfxApiFloatCoords2D jitterOffset;               ///< The subpixel jitter offset applied to the camera.
		struct FfxApiFloatCoords2D motionVectorScale;          ///< The scale factor to apply to motion vectors.
		struct FfxApiDimensions2D  renderSize;                 ///< The resolution that was used for rendering the input resources.
		struct FfxApiDimensions2D  upscaleSize;                ///< The resolution that the upscaler will upscale to (optional, assumed maxUpscaleSize otherwise).
		bool                       enableSharpening;           ///< Enable an additional sharpening pass.
		float                      sharpness;                  ///< The sharpness value between 0 and 1, where 0 is no additional sharpness and 1 is maximum additional sharpness.
		float                      frameTimeDelta;             ///< The time elapsed since the last frame (expressed in milliseconds).
		float                      preExposure;                ///< The pre exposure value (must be > 0.0f)
		bool                       reset;                      ///< A boolean value which when set to true, indicates the camera has moved discontinuously.
		float                      cameraNear;                 ///< The distance to the near plane of the camera.
		float                      cameraFar;                  ///< The distance to the far plane of the camera.
		float                      cameraFovAngleVertical;     ///< The camera angle field of view in the vertical direction (expressed in radians).
		float                      viewSpaceToMetersFactor;    ///< The scale factor to convert view space units to meters
		uint32_t                   flags;                      ///< Zero or a combination of values from FfxApiDispatchFsrUpscaleFlags.
	};
	*/
	ffxDispatchDescUpscale DispatchDescUpscale = {};
	DispatchDescUpscale.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
	DispatchDescUpscale.commandList = pParams->pCmd;

	// mandatory input textures
	DispatchDescUpscale.color         = ffxApiGetResourceDX12(mRenderer.GetTextureResource(rsc.texColorInput   ), FFX_API_RESOURCE_STATE_COMPUTE_READ, 0);
	DispatchDescUpscale.depth         = ffxApiGetResourceDX12(mRenderer.GetTextureResource(rsc.texDepthBuffer  ), FFX_API_RESOURCE_STATE_COMPUTE_READ, 0);
	DispatchDescUpscale.motionVectors = ffxApiGetResourceDX12(mRenderer.GetTextureResource(rsc.texMotionVectors), FFX_API_RESOURCE_STATE_COMPUTE_READ, 0);
	
	// optional input textures
	if(rsc.texExposure != INVALID_ID)
		DispatchDescUpscale.exposure = ffxApiGetResourceDX12(mRenderer.GetTextureResource(rsc.texExposure), FFX_API_RESOURCE_STATE_COMPUTE_READ, 0);
	if (rsc.texReactiveMask != INVALID_ID)
		DispatchDescUpscale.reactive = ffxApiGetResourceDX12(mRenderer.GetTextureResource(rsc.texReactiveMask), FFX_API_RESOURCE_STATE_COMPUTE_READ, 0);
	if(rsc.texTransparencyAndComposition != INVALID_ID)
		DispatchDescUpscale.transparencyAndComposition = ffxApiGetResourceDX12(mRenderer.GetTextureResource(rsc.texTransparencyAndComposition), FFX_API_RESOURCE_STATE_COMPUTE_READ, 0);
	
	// output texture
	DispatchDescUpscale.output = ffxApiGetResourceDX12(mRenderer.GetTextureResource(texOutput), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, 0);
	
	// params
	GetJitterXY(DispatchDescUpscale.jitterOffset.x, DispatchDescUpscale.jitterOffset.y, (uint)RenderSizeX, (uint)OutputSizeX, pParams->iFrame);
	DispatchDescUpscale.motionVectorScale.x = -MotionVectorScaleX; // - because MVs expected pointing from this frame to prev frame
	DispatchDescUpscale.motionVectorScale.y = -MotionVectorScaleY; // - because MVs expected pointing from this frame to prev frame

	DispatchDescUpscale.renderSize.width   = (uint)RenderSizeX;
	DispatchDescUpscale.renderSize.height  = (uint)RenderSizeY;
	DispatchDescUpscale.upscaleSize.width  = (uint)OutputSizeX;
	DispatchDescUpscale.upscaleSize.height = (uint)OutputSizeY;
	DispatchDescUpscale.enableSharpening = pParams->bEnableSharpening;
	DispatchDescUpscale.sharpness = pParams->fSharpness;

	DispatchDescUpscale.frameTimeDelta = pParams->fDeltaTimeMilliseconds;
	DispatchDescUpscale.preExposure = pParams->fPreExposure;
	DispatchDescUpscale.reset = pParams->bReset;
	DispatchDescUpscale.cameraNear = pParams->fCameraNear;
	DispatchDescUpscale.cameraFar = pParams->fCameraFar;
	DispatchDescUpscale.cameraFovAngleVertical = pParams->fCameraFoVAngleVerticalRadians;
	DispatchDescUpscale.viewSpaceToMetersFactor = pParams->fViewSpaceToMetersFactor;

	FfxApiDispatchFsrUpscaleFlags flags = {};
	/*
	FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW      
	FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB
	FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_PQ  
	*/
	DispatchDescUpscale.flags = flags;

	ffxReturnCode_t retCode = 0; 
	{
		SCOPED_GPU_MARKER(pParams->pCmd, "FSR3UpcalePass");
		retCode = ffxDispatch(&pImpl->ctx, &DispatchDescUpscale.header);
	}

	bool bSucceeded = retCode == 0;
	if (!bSucceeded)
	{
		Log::Error("FSR3: Error (%d) dispatching", retCode);
	}
}

void FSR3UpscalePass::GetJitterXY(float& OutPixelSpaceJitterX, float& OutPixelSpaceJitterY, uint RenderResolutionX, uint OutputResolutionX, size_t iFrame) const
{
	OutPixelSpaceJitterX = 0.0f;
	OutPixelSpaceJitterY = 0.0f;

	const bool bContextInitialized = pImpl->ctx != nullptr;
	if (!bContextInitialized)
	{
		return;
	}

	ffxReturnCode_t ret = 0;
	int NumPhases = 0;
	{
		ffxQueryDescUpscaleGetJitterPhaseCount desc = {};
		desc.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
		desc.displayWidth = OutputResolutionX;
		desc.renderWidth = RenderResolutionX;
		desc.pOutPhaseCount = &NumPhases;

		ret = ffxQuery(&pImpl->ctx, &desc.header);
		if (ret != 0)
		{
			Log::Warning("ffxQuery for phase count unssuccessful");
			return;
		}
	}

	iFrame = iFrame % NumPhases;
	ffxQueryDescUpscaleGetJitterOffset desc;
	desc.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;
	desc.index = iFrame;
	desc.phaseCount = NumPhases;
	desc.pOutX = &OutPixelSpaceJitterX;
	desc.pOutY = &OutPixelSpaceJitterY;

	ret = ffxQuery(&pImpl->ctx, &desc.header);
	if (ret != 0)
	{
		Log::Warning("ffxQuery for jitter offset unssuccessful");
		return;
	}
	
	//Log::Info("ffx Jitter[%d/%d] {%.4f, %.4f}", iFrame, NumPhases, OutPixelSpaceJitterX, OutPixelSpaceJitterY);
}

float FSR3UpscalePass::GetMipBias(uint RenderResolutionX, uint OutputResolutionX)
{
	return std::log2(float(RenderResolutionX) / (float)OutputResolutionX) - 1.0f;
}

