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

#include "Renderer/Rendering/PostProcess/Upscaling.h"

#include "Renderer/Pipeline/PipelineStateObjects.h"


struct FSR3UpscalePass : public RenderPassBase
{
	struct FResourceCollection : public IRenderPassResourceCollection 
	{
		float fResolutionScale = 0.0f;
		TextureID texColorInput = INVALID_ID;
		TextureID texDepthBuffer = INVALID_ID;
		TextureID texMotionVectors = INVALID_ID;
		TextureID texExposure = INVALID_ID;

		bool bAllocateReactivityMaskTexture = false;
		bool bAllocateTransparencyAndCompositionMaskTexture = false;
	};
	struct Parameters : public IRenderPassDrawParameters
	{
		ID3D12GraphicsCommandList* pCmd = nullptr;

		bool bEnableSharpening = true;
		float fSharpness = 0.8f;
		float fDeltaTimeMilliseconds = 0.0f;
		float fCameraNear = 0.0f;
		float fCameraFar = 0.0f;
		float fCameraFoVAngleVerticalRadians = 0.0f;
		float fPreExposure = 1.0f;
		float fViewSpaceToMetersFactor = 1.0f;
		uint32 iFrame = 0;

		bool bUseGeneratedReactiveMask = false;
		float GeneratedReactiveMaskScale = 1.0f;
		float GeneratedReactiveMaskCutoffThreshold = 0.0f;
		float GeneratedReactiveMaskBinaryValue = 0.0f;

		FResourceCollection Resources;
	};

public:
	FSR3UpscalePass(VQRenderer& Renderer);
	~FSR3UpscalePass() override;

	bool Initialize() override;
	void Destroy() override;

	void OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const IRenderPassResourceCollection* pRscParameters = nullptr) override;
	void OnDestroyWindowSizeDependentResources() override;

	void RecordCommands(const IRenderPassDrawParameters* pDrawParameters = nullptr) override;

	std::vector<FPSOCreationTaskParameters> CollectPSOCreationParameters() override { return std::vector<FPSOCreationTaskParameters>(); }

	void GetJitterXY(float& OutPixelSpaceJitterX, float& OutPixelSpaceJitterY, uint RenderResolutionX, uint OutputResolutionX, size_t iFrame) const;

	inline void SetClearHistoryBuffers() { this->bClearHistoryBuffers = true; }


	enum EResources
	{
		ReactivityMask,
		TransparencyAndCompositionMask,
		Output
	};
	inline TextureID GetTextureID(EResources eRsc) const
	{
		switch (eRsc)
		{
		case EResources::ReactivityMask: return texReactivityMask;
		case EResources::TransparencyAndCompositionMask: return texTransparencyAndCompositionMask;
		case EResources::Output: return texOutput;
		}
		return INVALID_ID;
	}
	inline SRV_ID GetSRV_ID(EResources eRsc) const
	{
		switch (eRsc)
		{
		case EResources::Output: return srvOutput;
		}
		return INVALID_ID;
	}
	inline RTV_ID GetRTV_ID(EResources eRsc) const
	{
		switch (eRsc)
		{
		case EResources::TransparencyAndCompositionMask: return rtvTransparencyAndCompositionMask;
		}
		return INVALID_ID;
	}

private:
	bool bClearHistoryBuffers = false;

	// "reactivity" means how much influence the samples rendered for the 
	// current frame have over the production of the final upscaled image.
	// 
	// adjusts the accumulation balance.
	// pixelValue
	//   0: use the default FSR composition strategy
	//   1: alpha value for alpha-blended objects
	// recommended clamping the maximum reactive value to around 0.9
	TextureID texReactivityMask = INVALID_ID;

	// denotes the areas of other specialist rendering like raytraced reflections or 
	// animated textures which should be accounted for during the upscaling process. 
	// 
	// adjusts the pixel history protection mechanisms
	// pixelValue
	//   0: does not perform any additional modification to the lock for that pixel.
	//   1: the lock for that pixel should be completely removed
	TextureID texTransparencyAndCompositionMask = INVALID_ID;
	RTV_ID rtvTransparencyAndCompositionMask = INVALID_ID;

	TextureID texOutput = INVALID_ID;
	SRV_ID srvOutput = INVALID_ID;

	struct ContextImpl;
	ContextImpl* pImpl = nullptr;
};