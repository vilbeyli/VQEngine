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

class ApplyReflectionsPass : public RenderPassBase
{
public:
	struct FResourceCollection : public IRenderPassResourceCollection {};
	struct FDrawParameters : public IRenderPassDrawParameters
	{
		ID3D12GraphicsCommandList* pCmd = nullptr;
		DynamicBufferHeap* pCBufferHeap = nullptr;
		SRV_ID SRVReflectionRadiance = INVALID_ID;
		UAV_ID UAVSceneRadiance      = INVALID_ID;
		int iSceneRTWidth = 0;
		int iSceneRTHeight = 0;
	};

	ApplyReflectionsPass(VQRenderer& Renderer);
	ApplyReflectionsPass() = delete;
	virtual ~ApplyReflectionsPass() override {}

	virtual bool Initialize() override;
	virtual void Destroy() override;
	virtual void OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const IRenderPassResourceCollection* pRscParameters = nullptr) override;
	virtual void OnDestroyWindowSizeDependentResources() override;
	virtual void RecordCommands(const IRenderPassDrawParameters* pDrawParameters = nullptr) override;

	virtual std::vector<FPSOCreationTaskParameters> CollectPSOCreationParameters() override;
private:
	PSO_ID PSOApplyReflectionsPass;
};

