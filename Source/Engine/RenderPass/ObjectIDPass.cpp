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

#include "ObjectIDPass.h"
#include "../Scene/Scene.h"
#include "../../Shaders/LightingConstantBufferData.h"
#include "../VQUtils/Source/utils.h"

#include <cassert>

using namespace DirectX;

ObjectIDPass::ObjectIDPass(VQRenderer& Renderer)
	: RenderPassBase(Renderer)
{
}

bool ObjectIDPass::Initialize()
{
	RTVPassOutput = mRenderer.AllocateRTV();
	DSVPassOutput = mRenderer.AllocateDSV();
	return true;
}

void ObjectIDPass::Destroy()
{
}

void ObjectIDPass::OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const IRenderPassResourceCollection* pRscParameters)
{
	mOutputResolutionX = Width;
	mOutputResolutionY = Height;

	{	// Scene depth stencil view
		TextureCreateDesc desc("ObjectIDDepth");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R32_TYPELESS
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
		mRenderer.InitializeDSV(DSVPassOutput, 0u, TEXPassOutputDepth);
	}
	{ // Main render target view
		TextureCreateDesc desc("ObjectID");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_SINT
			, mOutputResolutionX
			, mOutputResolutionY
			, 1 // Array Size
			, 1 // MIP levels
			, 1 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
		);

		desc.ResourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;
		TEXPassOutput = mRenderer.CreateTexture(desc);
		mRenderer.InitializeRTV(RTVPassOutput, 0u, TEXPassOutput);
		//mRenderer.InitializeSRV(SRVPassOutput, 0u, TEXPassOutput);
	}
}

void ObjectIDPass::OnDestroyWindowSizeDependentResources()
{
	mRenderer.DestroyTexture(TEXPassOutputDepth);
	mRenderer.DestroyTexture(TEXPassOutput);
}


static const Material& GetMaterial(MaterialID ID, const std::unordered_map<MaterialID, Material>* pMats)
{
	if (pMats->find(ID) == pMats->end())
	{
		Log::Error("Material not created. Did you call Scene::CreateMaterial()? (matID=%d)", ID);
		assert(false);
	}
	return pMats->at(ID);
}

void ObjectIDPass::RecordCommands(const IRenderPassDrawParameters* pDrawParameters)
{
	const FDrawParameters* pParams = static_cast<const FDrawParameters*>(pDrawParameters);
	assert(pParams);
	assert(pParams->pCmd);
	assert(pParams->pCBufferHeap);
	assert(pParams->pSceneView);
	assert(pParams->pMeshes);
	assert(pParams->pMaterials);
	ID3D12GraphicsCommandList* pCmd = pParams->pCmd;

	const DSV& dsv = mRenderer.GetDSV(DSVPassOutput);
	const RTV& rtv = mRenderer.GetRTV(RTVPassOutput);

	D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtv.GetCPUDescHandle();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsv.GetCPUDescHandle();

	pCmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, nullptr);

	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	pCmd->SetPipelineState(mRenderer.GetPSO(PSOOpaque));
	pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ZPrePass));

	// draw meshes
	for (const MeshRenderCommand_t& meshRenderCmd : pParams->pSceneView->meshRenderCommands)
	{
		using namespace VQ_SHADER_DATA;

		if (pParams->pMeshes->find(meshRenderCmd.meshID) == pParams->pMeshes->end())
		{
			Log::Warning("MeshID=%d couldn't be found", meshRenderCmd.meshID);
			continue; // skip drawing this mesh
		}
		
		const Material& mat = GetMaterial(meshRenderCmd.matID, pParams->pMaterials);
		const Mesh& mesh = pParams->pMeshes->at(meshRenderCmd.meshID);
		const auto VBIBIDs = mesh.GetIABufferIDs();
		const uint32 NumIndices = mesh.GetNumIndices();
		const BufferID& VB_ID = VBIBIDs.first;
		const BufferID& IB_ID = VBIBIDs.second;
		const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
		const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

		const uint32 NumInstances = (uint32)meshRenderCmd.matNormal.size();

		// set textures
		if (mat.SRVMaterialMaps != INVALID_ID)
		{
			pCmd->SetGraphicsRootDescriptorTable(0, mRenderer.GetSRV(mat.SRVMaterialMaps).GetGPUDescHandle(0));
			//pCmd->SetGraphicsRootDescriptorTable(4, mRenderer.GetSRV(mat.SRVMaterialMaps).GetGPUDescHandle(0));
		}

		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmd->IASetVertexBuffers(0, 1, &vb);
		pCmd->IASetIndexBuffer(&ib);
		pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
	}
}

std::vector<FPSOCreationTaskParameters> ObjectIDPass::CollectPSOCreationParameters()
{
	const std::wstring ShaderFilePath = VQRenderer::GetFullPathOfShader(L"ObjectID.hlsl");

	FPSODesc psoLoadDesc = {};
	psoLoadDesc.PSOName = "PSO_ObjectIDPass";

	// Shader description
	psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_5_0" });
	psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_5_0" });

	D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
	psoDesc.InputLayout = { };
	psoDesc.pRootSignature = mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ZPrePass);
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = true;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK::D3D12_DEPTH_WRITE_MASK_ALL;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_SINT;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_LESS;

	std::vector<FPSOCreationTaskParameters> params;
	params.push_back({ &PSOOpaque, psoLoadDesc });

	psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({});
	psoLoadDesc.ShaderStageCompileDescs[1].Macros.push_back({});
	psoLoadDesc.PSOName = "PSO_ObjectIDPass_AlphaMasked";
	
	params.push_back({ &PSOAlphaMasked, psoLoadDesc });

	return params;
}
