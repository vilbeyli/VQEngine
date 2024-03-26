//	VQE
//	Copyright(C) 2024  - Volkan Ilbeyli
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

#include "OutlinePass.h"
#include "../../Renderer/Renderer.h"
#include "../Scene/Scene.h"
#include "../../Shaders/LightingConstantBufferData.h"
#include "../VQUtils/Source/utils.h"
#include "../GPUMarker.h"

#include <cassert>

using namespace DirectX;

OutlinePass::OutlinePass(VQRenderer& Renderer)
	: RenderPassBase(Renderer)
{
}

bool OutlinePass::Initialize()
{
	DSV = mRenderer.AllocateDSV();
	DSVMSAA = mRenderer.AllocateDSV();
	return true;
}

void OutlinePass::Destroy()
{
	mRenderer.DestroyDSV(DSVMSAA);
	mRenderer.DestroyDSV(DSV);
}

void OutlinePass::OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const IRenderPassResourceCollection* pRscParameters)
{
	mOutputResolutionX = Width;
	mOutputResolutionY = Height;

	{	// Scene depth stencil view
		TextureCreateDesc desc("OutlineStencil");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R24G8_TYPELESS // DXGI_FORMAT_D24_UNORM_S8_UINT
			, mOutputResolutionX
			, mOutputResolutionY
			, 1 // Array Size
			, 1 // MIP levels
			, 1 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
		);
		desc.ResourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		TEXPassOutputDepth = mRenderer.CreateTexture(desc);
		mRenderer.InitializeDSV(DSV, 0u, TEXPassOutputDepth);
	}
	{	// Scene depth stencil view
		TextureCreateDesc desc("OutlineStencilMSAA");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R24G8_TYPELESS // DXGI_FORMAT_D24_UNORM_S8_UINT
			, mOutputResolutionX
			, mOutputResolutionY
			, 1 // Array Size
			, 1 // MIP levels
			, 4 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
		);
		desc.ResourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		TEXPassOutputDepthMSAA4 = mRenderer.CreateTexture(desc);
		mRenderer.InitializeDSV(DSVMSAA, 0u, TEXPassOutputDepthMSAA4);
	}
}

void OutlinePass::OnDestroyWindowSizeDependentResources()
{
	mRenderer.DestroyTexture(TEXPassOutputDepth);
	mRenderer.DestroyTexture(TEXPassOutputDepthMSAA4);
}

void OutlinePass::RecordCommands(const IRenderPassDrawParameters* pDrawParameters)
{
	const FDrawParameters* pParams = static_cast<const FDrawParameters*>(pDrawParameters);
	assert(pParams);
	assert(pParams->pCmd);
	assert(pParams->pSceneView);
	assert(pParams->pMeshes);
	assert(pParams->pMaterials);
	assert(pParams->pCBufferHeap);
	assert(pParams->pRTVHandles);
	ID3D12GraphicsCommandList* pCmd = pParams->pCmd;
	const bool& bMSAA = pParams->bMSAA;
	const FSceneView& SceneView = *pParams->pSceneView;
	
	const ::DSV& dsv = mRenderer.GetDSV(bMSAA ? DSVMSAA : DSV);

	D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
	const float clearColor[] = { 0, 0, 0, 0 };
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsv.GetCPUDescHandle();

	std::vector< D3D12_GPU_VIRTUAL_ADDRESS> cbAddrs(SceneView.outlineRenderCommands.size());
	int iCB = 0;
	{
		SCOPED_GPU_MARKER(pCmd, "RenderStencil");
		pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA ? PSOOutlineStencilMSAA4Write : PSOOutlineStencilWrite));
		pCmd->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle);
		pCmd->OMSetStencilRef(1);

		pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__WireframeUnlit));
		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, nullptr);
		
		for (const FOutlineRenderCommand& cmd : SceneView.outlineRenderCommands)
		{
			FOutlineRenderCommand::FConstantBuffer* pCBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS& cbAddr = cbAddrs[iCB];
			pParams->pCBufferHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer)), (void**)(&pCBuffer), &cbAddr);
			memcpy(pCBuffer, &cmd.cb, sizeof(cmd.cb));

			const Mesh& mesh = pParams->pMeshes->at(cmd.meshID);
			const auto VBIBIDs = mesh.GetIABufferIDs();
			const uint32 NumIndices = mesh.GetNumIndices();
			const BufferID& VB_ID = VBIBIDs.first;
			const BufferID& IB_ID = VBIBIDs.second;
			const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
			const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

			pCmd->SetGraphicsRootConstantBufferView(0, cbAddrs[iCB++]);
			pCmd->IASetVertexBuffers(0, 1, &vb);
			pCmd->IASetIndexBuffer(&ib);
			pCmd->DrawIndexedInstanced(NumIndices, 1, 0, 0, 0);
		}
	}
	{
		SCOPED_GPU_MARKER(pCmd, "RenderOutline");
		pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA ? PSOOutlineStencilMSAA4Mask : PSOOutlineStencilMask));	
		pCmd->OMSetRenderTargets((UINT)pParams->pRTVHandles->size(), pParams->pRTVHandles->data(), FALSE, &dsvHandle);
		iCB = 0;
		for (const FOutlineRenderCommand& cmd : SceneView.outlineRenderCommands)
		{
			const Mesh& mesh = pParams->pMeshes->at(cmd.meshID);
			const auto VBIBIDs = mesh.GetIABufferIDs();
			const uint32 NumIndices = mesh.GetNumIndices();
			const BufferID& VB_ID = VBIBIDs.first;
			const BufferID& IB_ID = VBIBIDs.second;
			const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
			const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

			pCmd->SetGraphicsRootConstantBufferView(0, cbAddrs[iCB++]);
			pCmd->IASetVertexBuffers(0, 1, &vb);
			pCmd->IASetIndexBuffer(&ib);
			pCmd->DrawIndexedInstanced(NumIndices, 1, 0, 0, 0);
		}
	}
}

std::vector<FPSOCreationTaskParameters> OutlinePass::CollectPSOCreationParameters()
{
	std::vector<FPSOCreationTaskParameters> params;
	
	const std::wstring ShaderFilePath = VQRenderer::GetFullPathOfShader(L"Outline.hlsl");
	FShaderStageCompileDesc VSDesc{ ShaderFilePath, "VSMain", "vs_5_1" };
	FShaderStageCompileDesc PSDesc{ ShaderFilePath, "PSMain", "ps_5_1" };

	FPSODesc psoLoadDesc = {};
	psoLoadDesc.ShaderStageCompileDescs.push_back(VSDesc);
	psoLoadDesc.ShaderStageCompileDescs.push_back(PSDesc);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
	psoDesc.InputLayout = { };
	psoDesc.pRootSignature = mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__WireframeUnlit);
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 0;
	psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.StencilEnable = TRUE;
	psoDesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_KEEP;
	psoDesc.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_KEEP;
	psoDesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_KEEP;
	psoDesc.DepthStencilState.BackFace.StencilFailOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_KEEP;
	
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_LESS;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK::D3D12_DEPTH_WRITE_MASK_ALL;
	psoDesc.DepthStencilState.StencilReadMask = 0x00;
	psoDesc.DepthStencilState.StencilWriteMask = 0xFF;
	psoDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_ALWAYS;
	psoDesc.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_ALWAYS;
	psoDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_REPLACE;
	psoDesc.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_REPLACE;
	{
		psoLoadDesc.PSOName = "PSO_OutlineStencil";
		psoDesc.SampleDesc.Count = 1;
		params.push_back({ &PSOOutlineStencilWrite, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_OutlineStencil_MSAA4";
		psoDesc.SampleDesc.Count = 4;
		params.push_back({ &PSOOutlineStencilMSAA4Write, psoLoadDesc });
	}

	psoLoadDesc.ShaderStageCompileDescs[0].EntryPoint = "VSMainOutline";
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_ALWAYS;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK::D3D12_DEPTH_WRITE_MASK_ZERO;
	psoDesc.DepthStencilState.StencilWriteMask = 0x00;
	psoDesc.DepthStencilState.StencilReadMask = 0xFF;
	psoDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_NOT_EQUAL;
	psoDesc.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_NOT_EQUAL;
	psoDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_KEEP;
	psoDesc.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_KEEP;
	{
		psoLoadDesc.PSOName = "PSO_OutlineMask";
		psoDesc.SampleDesc.Count = 1;
		params.push_back({ &PSOOutlineStencilMask, psoLoadDesc });

		psoLoadDesc.PSOName = "PSO_OutlineMask_MSAA4";
		psoDesc.SampleDesc.Count = 4;
		params.push_back({ &PSOOutlineStencilMSAA4Mask, psoLoadDesc });
	}
	return params;
}
