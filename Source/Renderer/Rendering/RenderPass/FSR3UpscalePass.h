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
	struct Parameters : public IRenderPassDrawParameters
	{

	};

	struct FResourceCollection : public IRenderPassResourceCollection 
	{

	};

public:
	FSR3UpscalePass(VQRenderer& Renderer);
	~FSR3UpscalePass() override;

	bool Initialize() override;
	void Destroy() override;

	void OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const IRenderPassResourceCollection* pRscParameters = nullptr) override;
	void OnDestroyWindowSizeDependentResources() override;

	void RecordCommands(const IRenderPassDrawParameters* pDrawParameters = nullptr) override;

	std::vector<FPSOCreationTaskParameters> CollectPSOCreationParameters() override;

private:
	AMD_FidelityFX_SuperResolution3::Context Context;
};