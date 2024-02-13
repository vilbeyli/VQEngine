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
	CopyFence.Create(mRenderer.GetDevicePtr(), "ObjectID::CopyFence");
	return true;
}

void ObjectIDPass::Destroy()
{
	mRenderer.DestroyDSV(DSVPassOutput);
	CopyFence.Destroy();
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

		desc.ResourceState = D3D12_RESOURCE_STATE_COMMON;
		TEXPassOutput = mRenderer.CreateTexture(desc);
		mRenderer.InitializeRTV(RTVPassOutput, 0u, TEXPassOutput);
		//mRenderer.InitializeSRV(SRVPassOutput, 0u, TEXPassOutput);

		desc.TexName = "ObjectID_CPU_READBACK";
		desc.bCPUReadback = true;
		desc.ResourceState = D3D12_RESOURCE_STATE_COPY_DEST;
		desc.d3d12Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.d3d12Desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.d3d12Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.d3d12Desc.Width = mOutputResolutionX * mOutputResolutionY * 16; // 4B/p = 16B/rgba
		desc.d3d12Desc.Height = 1;
		desc.d3d12Desc.Flags = D3D12_RESOURCE_FLAG_NONE;
		TEXPassOutputCPUReadback = mRenderer.CreateTexture(desc);
	}
}

void ObjectIDPass::OnDestroyWindowSizeDependentResources()
{
	mRenderer.DestroyTexture(TEXPassOutputDepth);
	mRenderer.DestroyTexture(TEXPassOutput);
	mRenderer.DestroyTexture(TEXPassOutputCPUReadback);
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
	assert(pParams->pCmdCopy);
	assert(pParams->pSceneView);
	assert(pParams->pMeshes);
	assert(pParams->pMaterials);
	ID3D12GraphicsCommandList* pCmd = pParams->pCmd;
	ID3D12GraphicsCommandList* pCmdCpy = static_cast<ID3D12GraphicsCommandList*>(pParams->pCmdCopy);
	auto pRscRT = mRenderer.GetTextureResource(TEXPassOutput);
	auto pRscCPU = mRenderer.GetTextureResource(TEXPassOutputCPUReadback);
	
	//mRenderer
	const DSV& dsv = mRenderer.GetDSV(DSVPassOutput);
	const RTV& rtv = mRenderer.GetRTV(RTVPassOutput);

	D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtv.GetCPUDescHandle();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsv.GetCPUDescHandle();

#if OBJECTID_PASS__USE_ASYNC_COPY
	pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pRscRT,
		D3D12_RESOURCE_STATE_COMMON,  D3D12_RESOURCE_STATE_RENDER_TARGET));
#endif

	pCmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, nullptr);

	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	pCmd->SetPipelineState(mRenderer.GetPSO(PSOOpaque));
	pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ZPrePass));

	// draw meshes
	int iCB = 0;
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
		pCmd->SetGraphicsRootConstantBufferView(1, pParams->CBAddresses[iCB++]);
		pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
	}


	// transition output to copy source
	pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pRscRT,
		D3D12_RESOURCE_STATE_RENDER_TARGET, 
#if OBJECTID_PASS__USE_ASYNC_COPY
		D3D12_RESOURCE_STATE_COMMON
#else
		D3D12_RESOURCE_STATE_COPY_SOURCE
#endif
	));

#if OBJECTID_PASS__USE_ASYNC_COPY

	CommandQueue& GFXCmdQ = mRenderer.GetCommandQueue(CommandQueue::EType::GFX);
	CommandQueue& CPYCmdQ = mRenderer.GetCommandQueue(CommandQueue::EType::COPY);

	// EXECUTE and SIGNAL
	pCmd->Close();
	GFXCmdQ.pQueue->ExecuteCommandLists(1, (ID3D12CommandList*const*)&pCmd);
	CopyFence.Signal(GFXCmdQ.pQueue);

	CopyFence.WaitOnGPU(CPYCmdQ.pQueue); // wait for render target done

	// record copy command
	{
		//pCmdCpy->CopyResource(pRscCPU, pRscRT);
		D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
		srcLoc.pResource = pRscRT;
		srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		srcLoc.SubresourceIndex = 0; // Assuming copying from the first mip level

		D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
		dstLoc.pResource = pRscCPU;
		dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		mRenderer.GetDevicePtr()->GetCopyableFootprints(&pRscRT->GetDesc(), 0, 1, 0, &dstLoc.PlacedFootprint, nullptr, nullptr, nullptr);

		pCmdCpy->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
	}

	pCmdCpy->Close();
	CPYCmdQ.pQueue->ExecuteCommandLists(1, (ID3D12CommandList* const *)&pCmdCpy);
	
	CopyFence.Signal(CPYCmdQ.pQueue);

#else

	//pCmd->CopyResource(pRscCPU, pRscRT);
	D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
	srcLoc.pResource = pRscRT;
	srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	srcLoc.SubresourceIndex = 0; // Assuming copying from the first mip level

	D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
	dstLoc.pResource = pRscCPU;
	dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	mRenderer.GetDevicePtr()->GetCopyableFootprints(&pRscRT->GetDesc(), 0, 1, 0, &dstLoc.PlacedFootprint, nullptr, nullptr, nullptr);
	
	pCmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

	pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pRscRT,
		D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
#endif
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
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_LESS;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_SINT;
	psoDesc.SampleDesc.Count = 1;

	std::vector<FPSOCreationTaskParameters> params;
	params.push_back({ &PSOOpaque, psoLoadDesc });

	psoLoadDesc.ShaderStageCompileDescs[0].Macros.push_back({});
	psoLoadDesc.ShaderStageCompileDescs[1].Macros.push_back({});
	psoLoadDesc.PSOName = "PSO_ObjectIDPass_AlphaMasked";
	
	params.push_back({ &PSOAlphaMasked, psoLoadDesc });

	return params;
}

int4 ObjectIDPass::ReadBackPixel(const int2& screenCoords) const
{
	auto pRsc = mRenderer.GetTextureResource(TEXPassOutputCPUReadback);
	const int bytesPerPixel = 4 * 4; // 32bit/channel , RGBA

	unsigned char* pData = nullptr;
	D3D12_RANGE readRange = { 0, mOutputResolutionX * mOutputResolutionY * bytesPerPixel }; // Only request the read for the necessary range
	HRESULT hr = pRsc->Map(0, &readRange, reinterpret_cast<void**>(&pData));
	if (SUCCEEDED(hr))
	{
		const int pixelIndex = (screenCoords.y * mOutputResolutionX + screenCoords.x) * bytesPerPixel;
		const int r = pData[pixelIndex + 0];
		const int g = pData[pixelIndex + 4];
		const int b = pData[pixelIndex + 8];
		const int a = pData[pixelIndex + 12];

		pRsc->Unmap(0, nullptr);
		//Log::Info("\t[%d, %d] | [%.2f, %.2f] = (%d, %d, %d, %d)", pixelX, pixelY, uv.x, uv.y, r, g, b, a);
		return int4(r, g, b, a);
	}

	return int4(-1, -1, -1, -1);
}

void ObjectIDPass::WaitForCopyComplete() const
{
	CopyFence.WaitOnCPU(CopyFence.GetValue());
}
