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

	// TODO: enable conditional resource consumption
	//if (pResourceInput->bAllocateReactivityMaskTexture)
	{
		createDesc.Name = "FSR3_ReactivityMask";
		createDesc.D3D12Desc.Width = RenderResolutionX;
		createDesc.D3D12Desc.Height = RenderResolutionY;
		createDesc.D3D12Desc.Format = DXGI_FORMAT_R16_FLOAT;
		texReactivityMask = mRenderer.CreateTexture(createDesc);
	}
	//if (pResourceInput->bAllocateTransparencyAndCompositionMaskTexture)
	{
		createDesc.Name = "FSR3_TransparencyAndCompositionMask";
		createDesc.D3D12Desc.Width = RenderResolutionX;
		createDesc.D3D12Desc.Height = RenderResolutionY;
		createDesc.D3D12Desc.Format = DXGI_FORMAT_R16_FLOAT;
		createDesc.D3D12Desc.Flags = D3D12_RESOURCE_FLAGS::D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		createDesc.InitialState = D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_RENDER_TARGET;
		texTransparencyAndCompositionMask = mRenderer.CreateTexture(createDesc);
	}

	srvOutput = mRenderer.AllocateSRV(1);
	mRenderer.InitializeSRV(srvOutput, 0, texOutput);

	if (pResourceInput->bAllocateReactivityMaskTexture) mRenderer.WaitForTexture(texReactivityMask);
	//if (pResourceInput->bAllocateTransparencyAndCompositionMaskTexture) 
	{
		rtvTransparencyAndCompositionMask = mRenderer.AllocateRTV(1);
		mRenderer.InitializeRTV(rtvTransparencyAndCompositionMask, 0, texTransparencyAndCompositionMask);
	}
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

	if (rtvTransparencyAndCompositionMask != INVALID_ID)
		//mRenderer.DestroyRTV(rtvTransparencyAndCompositionMask);

	mRenderer.DestroySRV(srvOutput);
	mRenderer.DestroyTexture(texOutput);
	
	if (texReactivityMask != INVALID_ID) mRenderer.DestroyTexture(texReactivityMask);
	if (texTransparencyAndCompositionMask != INVALID_ID) mRenderer.DestroyTexture(texTransparencyAndCompositionMask);
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

	ID3D12Resource* pRscColorInput = mRenderer.GetTextureResource(rsc.texColorInput);
	ID3D12Resource* pRscDepthBuffer = mRenderer.GetTextureResource(rsc.texDepthBuffer);
	ID3D12Resource* pRscMoVec = mRenderer.GetTextureResource(rsc.texMotionVectors);
	ID3D12Resource* pRscOutput = mRenderer.GetTextureResource(texOutput);
	assert(pRscColorInput);
	assert(pRscDepthBuffer);
	assert(pRscMoVec);
	assert(pRscOutput);
	ID3D12Resource* pRscExposure = rsc.texExposure == INVALID_ID ? nullptr : mRenderer.GetTextureResource(rsc.texExposure);
	ID3D12Resource* pRscReactiveMask = texReactivityMask == INVALID_ID ? nullptr : mRenderer.GetTextureResource(texReactivityMask);		
	ID3D12Resource* pRscTransparencyAndComposition = texTransparencyAndCompositionMask == INVALID_ID ? nullptr : mRenderer.GetTextureResource(texTransparencyAndCompositionMask);

	ffxReturnCode_t retCode = 0;
	if (pParams->bUseGeneratedReactiveMask)
	{
		SCOPED_GPU_MARKER(pParams->pCmd, "FSR3GenerateRactivityMaskPass");
		assert(pRscReactiveMask);

		ffxDispatchDescUpscaleGenerateReactiveMask GenReactiveMaskDesc = {};
		GenReactiveMaskDesc.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE_GENERATEREACTIVEMASK;
		GenReactiveMaskDesc.commandList = pParams->pCmd;
		GenReactiveMaskDesc.colorOpaqueOnly = ffxApiGetResourceDX12(pRscColorInput, FFX_API_RESOURCE_STATE_COMPUTE_READ, 0);
		GenReactiveMaskDesc.colorPreUpscale = ffxApiGetResourceDX12(pRscColorInput, FFX_API_RESOURCE_STATE_COMPUTE_READ, 0);
		GenReactiveMaskDesc.outReactive = ffxApiGetResourceDX12(pRscReactiveMask, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, 0);
		GenReactiveMaskDesc.renderSize.width = (uint)RenderSizeX;
		GenReactiveMaskDesc.renderSize.height = (uint)RenderSizeY;
		GenReactiveMaskDesc.scale = pParams->GeneratedReactiveMaskScale;
		GenReactiveMaskDesc.cutoffThreshold = pParams->GeneratedReactiveMaskCutoffThreshold;
		GenReactiveMaskDesc.binaryValue = pParams->GeneratedReactiveMaskBinaryValue;
		GenReactiveMaskDesc.flags;

		CD3DX12_RESOURCE_BARRIER barriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(pRscReactiveMask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		};
		pParams->pCmd->ResourceBarrier(_countof(barriers), barriers);

		retCode = ffxDispatch(&pImpl->ctx, &GenReactiveMaskDesc.header);

		barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(pRscReactiveMask, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		pParams->pCmd->ResourceBarrier(_countof(barriers), barriers);
	}
	{
		SCOPED_GPU_MARKER(pParams->pCmd, "FSR3UpcalePass");
		ffxDispatchDescUpscale DispatchDescUpscale = {};
		DispatchDescUpscale.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
		DispatchDescUpscale.commandList = pParams->pCmd;

		DispatchDescUpscale.color                      = ffxApiGetResourceDX12(pRscColorInput                , FFX_API_RESOURCE_STATE_COMPUTE_READ, 0);
		DispatchDescUpscale.depth                      = ffxApiGetResourceDX12(pRscDepthBuffer               , FFX_API_RESOURCE_STATE_COMPUTE_READ, 0);
		DispatchDescUpscale.motionVectors              = ffxApiGetResourceDX12(pRscMoVec                     , FFX_API_RESOURCE_STATE_COMPUTE_READ, 0);
		DispatchDescUpscale.exposure                   = ffxApiGetResourceDX12(pRscExposure                  , FFX_API_RESOURCE_STATE_COMPUTE_READ, 0);
		DispatchDescUpscale.reactive                   = ffxApiGetResourceDX12(pRscReactiveMask              , FFX_API_RESOURCE_STATE_COMPUTE_READ, 0);
		DispatchDescUpscale.transparencyAndComposition = ffxApiGetResourceDX12(pRscTransparencyAndComposition, FFX_API_RESOURCE_STATE_COMPUTE_READ, 0);
		DispatchDescUpscale.output                     = ffxApiGetResourceDX12(pRscOutput                    , FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, 0);
	
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
		DispatchDescUpscale.reset = this->bClearHistoryBuffers;
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

		retCode = ffxDispatch(&pImpl->ctx, &DispatchDescUpscale.header);
	}

	if (this->bClearHistoryBuffers)
	{
		this->bClearHistoryBuffers = false;
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

