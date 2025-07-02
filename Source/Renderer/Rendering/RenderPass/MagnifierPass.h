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
#include "Renderer/Resources/ResourceViews.h" // TODO: use IDs instead of SRV & IBV in draw params
#include <memory>

class DynamicBufferHeap;
struct FMagnifierParameters
{
	uint32_t    uImageWidth = 1;
	uint32_t    uImageHeight = 1;
	int         iMousePos[2] = { 0, 0 }; // in pixels, driven by ImGuiIO.MousePos.xy
	float       fBorderColorRGB[4] = { 1, 1, 1, 1 }; // Linear RGBA
	float       fMagnificationAmount = 6.0f;         // [1-...]
	float       fMagnifierScreenRadius = 0.35f;      // [0-1]
	mutable int iMagnifierOffset[2] = { 500, -500 }; // in pixels
};

class MagnifierPass : public RenderPassBase
{
public:

	struct FDrawParameters : public IRenderPassDrawParameters
	{
		ID3D12GraphicsCommandList* pCmd = nullptr;
		const FMagnifierParameters* pCBufferParams = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = 0;
		D3D12_CPU_DESCRIPTOR_HANDLE RTV = {};
		SRV SRVColorInput = {};
		IBV IndexBufferView = {};
	};
	struct FResourceCollection : public IRenderPassResourceCollection {};

public:

	MagnifierPass(VQRenderer& Renderer, bool bOutputsToSwapchainIn);
	MagnifierPass() = delete;
	virtual ~MagnifierPass() override {}

	virtual bool Initialize() override;
	virtual void Destroy() override;
	virtual void OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const IRenderPassResourceCollection* pRscParameters = nullptr) override;
	virtual void OnDestroyWindowSizeDependentResources() override;
	virtual void RecordCommands(const IRenderPassDrawParameters* pDrawParameters = nullptr) override;

	virtual std::vector<FPSOCreationTaskParameters> CollectPSOCreationParameters() override;

	static void KeepMagnifierOnScreen(FMagnifierParameters& params);
private:

	// Magnifier is usually one of the latest passes in the rendering pipeline.
	// This makes it a good candidate to be the pass that outputs to the swapchain
	// before the UI is drawn. For situations where the magnifier is not the last
	// pass, @bOutputsToSwapchain must be false in order to utilize the compute shaders.
	// Note that writing to swapchain requires pixel shaders.
	const bool bOutputsToSwapchain = true;

	PSO_ID PSOMagnifierPS = INVALID_ID;

	// resources for CS implementation, used when bOutputsToSwapchain==false
	PSO_ID PSOMagnifierCS    = INVALID_ID;
	TextureID TexPassOutput  = INVALID_ID;
	SRV_ID SRVPassOutput     = INVALID_ID;
	UAV_ID UAVPassOutput     = INVALID_ID;
	RTV_ID RTVPassOutput     = INVALID_ID;
	RTV_ID RTVPassOutputSRGB = INVALID_ID;
};

