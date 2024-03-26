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

#include "MagnifierPass.h"

MagnifierPass::MagnifierPass(VQRenderer& Renderer, bool bOutputsToSwapchainIn)
	: RenderPassBase(Renderer)
	, bOutputsToSwapchain(bOutputsToSwapchainIn)
{
}

bool MagnifierPass::Initialize()
{
	return true;
}

void MagnifierPass::Destroy()
{
}

void MagnifierPass::OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const IRenderPassResourceCollection* pRscParameters)
{
	if (!bOutputsToSwapchain)
	{
		// TODO: support for CS
	}
}

void MagnifierPass::OnDestroyWindowSizeDependentResources()
{
	if (!bOutputsToSwapchain)
	{
		if(TexPassOutput != INVALID_ID) mRenderer.DestroyTexture(TexPassOutput);
	}
}


void MagnifierPass::RecordCommands(const IRenderPassDrawParameters* pDrawParameters)
{
	const FDrawParameters* pParams = static_cast<const FDrawParameters*>(pDrawParameters);
	assert(pParams);
	assert(pParams->pCmd);
	assert(pParams->pCBufferHeap);
	assert(pParams->pCBufferParams);

	ID3D12GraphicsCommandList* pCmd = pParams->pCmd;
	assert(pCmd);

	const float W = static_cast<float>(pParams->pCBufferParams->uImageWidth);
	const float H = static_cast<float>(pParams->pCBufferParams->uImageHeight);

	if (bOutputsToSwapchain)
	{
		D3D12_VIEWPORT viewport{ 0.0f, 0.0f, W, H, 0.0f, 1.0f };
		D3D12_RECT     scissorsRect{ 0, 0, (LONG)W, (LONG)H };

		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		FMagnifierParameters* CB = nullptr;
		pParams->pCBufferHeap->AllocConstantBuffer(sizeof(FMagnifierParameters), (void**)&CB, &cbAddr);
		memcpy(CB, pParams->pCBufferParams.get(), sizeof(FMagnifierParameters));
		
		pCmd->SetPipelineState(mRenderer.GetPSO(PSOMagnifierPS));
		pCmd->OMSetRenderTargets(1, &pParams->RTV, FALSE, NULL);
		
		pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__FullScreenTriangle));
		pCmd->SetGraphicsRootDescriptorTable(0, pParams->SRVColorInput.GetGPUDescHandle());
		pCmd->SetGraphicsRootConstantBufferView(1, cbAddr);

		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmd->IASetVertexBuffers(0, 1, NULL);
		pCmd->IASetIndexBuffer(&pParams->IndexBufferView);

		pCmd->RSSetViewports(1, &viewport);
		pCmd->RSSetScissorRects(1, &scissorsRect);

		pCmd->DrawIndexedInstanced(3, 1, 0, 0, 0);

		return;
	}

	assert(false); // TODO: compute shader support
	pCmd->SetPipelineState(mRenderer.GetPSO(PSOMagnifierCS));
}

std::vector<FPSOCreationTaskParameters> MagnifierPass::CollectPSOCreationParameters()
{
	const std::wstring ShaderFilePath = VQRenderer::GetFullPathOfShader(L"Magnifier.hlsl");

	FPSODesc psoLoadDesc = {};
	psoLoadDesc.PSOName = "PSO_Magnifier";

	// Shader description
	psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_0" });
	psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_0" });

	D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
	psoDesc.InputLayout = { };
	psoDesc.pRootSignature = mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__FullScreenTriangle);
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;

	std::vector<FPSOCreationTaskParameters> params;
	params.push_back({ &PSOMagnifierPS, psoLoadDesc });

	return params;
}

void MagnifierPass::KeepMagnifierOnScreen(FMagnifierParameters& params)
{
	const int IMAGE_SIZE[2] = { static_cast<int>(params.uImageWidth), static_cast<int>(params.uImageHeight) };
	const int& W = IMAGE_SIZE[0];
	const int& H = IMAGE_SIZE[1];

	const int radiusInPixelsMagifier = static_cast<int>(params.fMagnifierScreenRadius * H);
	const int radiusInPixelsMagifiedArea = static_cast<int>(params.fMagnifierScreenRadius * H / params.fMagnificationAmount);

	const bool bCirclesAreOverlapping = radiusInPixelsMagifiedArea + radiusInPixelsMagifier > std::sqrt(params.iMagnifierOffset[0] * params.iMagnifierOffset[0] + params.iMagnifierOffset[1] * params.iMagnifierOffset[1]);


	if (bCirclesAreOverlapping) // don't let the two circles overlap
	{
		params.iMagnifierOffset[0] = radiusInPixelsMagifiedArea + radiusInPixelsMagifier + 1;
		params.iMagnifierOffset[1] = radiusInPixelsMagifiedArea + radiusInPixelsMagifier + 1;
	}

	for (int i = 0; i < 2; ++i) // try to move the magnified area to be fully on screen, if possible
	{
		const bool bMagnifierOutOfScreenRegion = params.iMousePos[i] + params.iMagnifierOffset[i] + radiusInPixelsMagifier > IMAGE_SIZE[i]
			|| params.iMousePos[i] + params.iMagnifierOffset[i] - radiusInPixelsMagifier < 0;
		if (bMagnifierOutOfScreenRegion)
		{
			if ( !(params.iMousePos[i] - params.iMagnifierOffset[i] + radiusInPixelsMagifier > IMAGE_SIZE[i]
				|| params.iMousePos[i] - params.iMagnifierOffset[i] - radiusInPixelsMagifier < 0))
			{
				// flip offset if possible
				params.iMagnifierOffset[i] = -params.iMagnifierOffset[i];
			}
			else
			{
				// otherwise clamp
				if (params.iMousePos[i] + params.iMagnifierOffset[i] + radiusInPixelsMagifier > IMAGE_SIZE[i])
					params.iMagnifierOffset[i] = IMAGE_SIZE[i] - params.iMousePos[i] - radiusInPixelsMagifier;
				if (params.iMousePos[i] + params.iMagnifierOffset[i] - radiusInPixelsMagifier < 0)
					params.iMagnifierOffset[i] = -params.iMousePos[i] + radiusInPixelsMagifier;
			}
		}
	}
}