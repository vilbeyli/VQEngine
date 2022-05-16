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

#include "ApplyReflections.h"

ApplyReflectionsPass::ApplyReflectionsPass(VQRenderer& Renderer)
	: RenderPassBase(Renderer)
{
}

bool ApplyReflectionsPass::Initialize()
{
	return true;
}

void ApplyReflectionsPass::Destroy()
{
}

void ApplyReflectionsPass::OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const IRenderPassResourceCollection* pRscParameters)
{

}

void ApplyReflectionsPass::OnDestroyWindowSizeDependentResources()
{
}

void ApplyReflectionsPass::RecordCommands(const IRenderPassDrawParameters* pDrawParameters)
{
	const FDrawParameters* pParams = static_cast<const FDrawParameters*>(pDrawParameters);
	assert(pParams);
	assert(pParams->pCmd);
	assert(pParams->pCBufferHeap);

	const bool bCompositeColor2 = pParams->SRVBoundingVolumes != INVALID_ID;

	ID3D12GraphicsCommandList* pCmd = pParams->pCmd;

	constexpr int NUM_THREADS_X = 8;
	constexpr int NUM_THREADS_Y = 8;
	const int DispatchX = DIV_AND_ROUND_UP(pParams->iSceneRTWidth , NUM_THREADS_X);
	const int DispatchY = DIV_AND_ROUND_UP(pParams->iSceneRTHeight, NUM_THREADS_Y);
	const int DispatchZ = 1;

	pCmd->SetPipelineState(mRenderer.GetPSO(PSOApplyReflectionsPass[bCompositeColor2 ? 1 : 0]));
	pCmd->SetComputeRootSignature(mRenderer.GetBuiltinRootSignature(bCompositeColor2 ? EBuiltinRootSignatures::CS__SRV2_UAV1_ROOTCBV1 : EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1));
	pCmd->SetComputeRootDescriptorTable(0, mRenderer.GetSRV(pParams->SRVReflectionRadiance).GetGPUDescHandle());
	if(bCompositeColor2) 
		pCmd->SetComputeRootDescriptorTable(1, mRenderer.GetSRV(pParams->SRVBoundingVolumes).GetGPUDescHandle());
	pCmd->SetComputeRootDescriptorTable(bCompositeColor2 ? 2 : 1, mRenderer.GetUAV(pParams->UAVSceneRadiance).GetGPUDescHandle());
	pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);
}

std::vector<FPSOCreationTaskParameters> ApplyReflectionsPass::CollectPSOCreationParameters()
{
	std::vector<FPSOCreationTaskParameters> PSODescs;
	{
		const std::wstring ShaderFilePath = VQRenderer::GetFullPathOfShader("ApplyReflections.hlsl");

		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "[PSO] ApplyReflections";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain", "cs_5_0" });
		psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1);
		PSODescs.push_back({ &PSOApplyReflectionsPass[0], psoLoadDesc});

		psoLoadDesc.PSOName = "[PSO] ApplyReflectionsAndBoundingVolumes";
		psoLoadDesc.D3D12ComputeDesc.pRootSignature = mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::CS__SRV2_UAV1_ROOTCBV1);
		psoLoadDesc.ShaderStageCompileDescs.back().Macros.push_back({ "COMPOSITE_BOUNDING_VOLUMES", "1" });
		PSODescs.push_back({ &PSOApplyReflectionsPass[1], psoLoadDesc});
	}

	return PSODescs;
}
