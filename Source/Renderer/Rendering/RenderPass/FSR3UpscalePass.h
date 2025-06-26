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
		TextureID texReactiveMask = INVALID_ID;
		TextureID texTransparencyAndComposition = INVALID_ID;
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
		bool bReset = false;

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

	TextureID texOutput; // TODO: make private
private:

	struct ContextImpl;
	ContextImpl* pImpl = nullptr;
};