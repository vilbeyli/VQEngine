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

class DepthMSAAResolvePass : public RenderPassBase
{
public:
	struct FResourceCollection : public IRenderPassResourceCollection {};
	struct FDrawParameters : public IRenderPassDrawParameters
	{
		ID3D12GraphicsCommandList* pCmd = nullptr;
		DynamicBufferHeap* pCBufferHeap = nullptr;
		SRV_ID SRV_MSAADepth         = INVALID_ID;
		SRV_ID SRV_MSAANormals       = INVALID_ID;
		SRV_ID SRV_MSAARoughness     = INVALID_ID;
		UAV_ID UAV_ResolvedDepth     = INVALID_ID;
		UAV_ID UAV_ResolvedNormals   = INVALID_ID;
		UAV_ID UAV_ResolvedRoughness = INVALID_ID;
	};

	DepthMSAAResolvePass(VQRenderer& Renderer);
	DepthMSAAResolvePass() = delete;
	virtual ~DepthMSAAResolvePass() override {}

	virtual bool Initialize() override;
	virtual void Destroy() override;
	virtual void OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const IRenderPassResourceCollection* pRscParameters = nullptr) override;
	virtual void OnDestroyWindowSizeDependentResources() override;
	virtual void RecordCommands(const IRenderPassDrawParameters* pDrawParameters = nullptr) override;

	virtual std::vector<FPSOCreationTaskParameters> CollectPSOCreationParameters() override;

private:
	void LoadRootSignatures();
	void DestroyRootSignatures();

private:
	PSO_ID mPSO[2][2][2];
	ID3D12RootSignature* mpRS = nullptr;
	int mSourceResolutionX = 0;
	int mSourceResolutionY = 0;
};

