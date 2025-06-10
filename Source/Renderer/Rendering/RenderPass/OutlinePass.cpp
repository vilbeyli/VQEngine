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
#include "Renderer/Renderer.h"
#include "Renderer/Pipeline/Tessellation.h"
#include "Renderer/Rendering/DrawData.h"
#include "Shaders/LightingConstantBufferData.h"
#include "Engine/GPUMarker.h"
#include "Libs/VQUtils/Include/utils.h"
#include "Engine/Scene/Mesh.h"
#include "Engine/Scene/Material.h"
#include "Libs/VQUtils/Include/Log.h"

#include <cassert>

using namespace DirectX;
using namespace VQ_SHADER_DATA;
using namespace Tessellation;

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
		FTextureRequest desc("OutlineStencil");
		desc.D3D12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R24G8_TYPELESS // DXGI_FORMAT_D24_UNORM_S8_UINT
			, mOutputResolutionX
			, mOutputResolutionY
			, 1 // Array Size
			, 1 // MIP levels
			, 1 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
		);
		desc.InitialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		TEXPassOutputDepth = mRenderer.CreateTexture(desc);
		mRenderer.InitializeDSV(DSV, 0u, TEXPassOutputDepth);
	}
	{	// Scene depth stencil view
		FTextureRequest desc("OutlineStencilMSAA");
		desc.D3D12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R24G8_TYPELESS // DXGI_FORMAT_D24_UNORM_S8_UINT
			, mOutputResolutionX
			, mOutputResolutionY
			, 1 // Array Size
			, 1 // MIP levels
			, 4 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
		);
		desc.InitialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		TEXPassOutputDepthMSAA4 = mRenderer.CreateTexture(desc);
		mRenderer.InitializeDSV(DSVMSAA, 0u, TEXPassOutputDepthMSAA4);
	}
}

void OutlinePass::OnDestroyWindowSizeDependentResources()
{
	mRenderer.DestroyTexture(TEXPassOutputDepth);
	mRenderer.DestroyTexture(TEXPassOutputDepthMSAA4);
}

static const Material& GetMaterial(MaterialID ID, const std::unordered_map<MaterialID, Material>* pMats)
{
	if (pMats->find(ID) == pMats->end())
	{
		Log::Error("GetMaterial() failed: Material not created. Did you call Scene::CreateMaterial()? (matID=%d)", ID);
		assert(false);
	}
	return pMats->at(ID);
}
void OutlinePass::RecordCommands(const IRenderPassDrawParameters* pDrawParameters)
{
	const FDrawParameters* pParams = static_cast<const FDrawParameters*>(pDrawParameters);
	assert(pParams);
	assert(pParams->pCmd);
	assert(pParams->pSceneDrawData);
	assert(pParams->pCBufferHeap);
	assert(pParams->pRTVHandles);
	ID3D12GraphicsCommandList* pCmd = pParams->pCmd;
	DynamicBufferHeap* pHeap = pParams->pCBufferHeap;
	const bool& bMSAA = pParams->bMSAA;
	const FSceneDrawData& SceneDrawData = *pParams->pSceneDrawData;
	
	// early out & avoid changing state if we have no meshes to render
	if (SceneDrawData.outlineRenderParams.empty())
		return;

	const ::DSV& dsv = mRenderer.GetDSV(bMSAA ? DSVMSAA : DSV);

	D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
	const float clearColor[] = { 0, 0, 0, 0 };
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsv.GetCPUDescHandle();

	std::vector< D3D12_GPU_VIRTUAL_ADDRESS> cbAddrs(SceneDrawData.outlineRenderParams.size());
	std::vector< D3D12_GPU_VIRTUAL_ADDRESS> cbAddrsTess(SceneDrawData.outlineRenderParams.size());

	pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__OutlinePass));
	pCmd->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle);
	pCmd->OMSetStencilRef(1);
	pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, nullptr);

	const char* pszPassName[NUM_PASS_OPTIONS] = { "RenderStencil", "RenderOutline" };
	for (size_t iPass = 0; iPass < NUM_PASS_OPTIONS; ++iPass)
	{
		SCOPED_GPU_MARKER(pCmd, pszPassName[iPass]);
		if (iPass == 1)
		{
			pCmd->OMSetRenderTargets((UINT)pParams->pRTVHandles->size(), pParams->pRTVHandles->data(), FALSE, &dsvHandle);
		}

		int iCB = 0;
		for (const FOutlineRenderData& cmd : SceneDrawData.outlineRenderParams)
		{
			// set PSO
			const Material& mat = *cmd.pMaterial;
			const size_t iMSAA  = bMSAA ? 1 : 0;
			const size_t iAlpha = mat.IsAlphaMasked(mRenderer) ? 1 : 0;
			uint8 iTess = 0; uint8 iDomain = 0; uint8 iPart = 0; uint8 iOutTopo = 0; uint8 iTessCull = 0;
			mat.GetTessellationPSOConfig(iTess, iDomain, iPart, iOutTopo, iTessCull);
			const size_t key = Hash(iPass,
				iMSAA,
				iTess,
				iDomain,
				iPart,
				iOutTopo,
				iTessCull,
				iAlpha
			);
			const PSO_ID psoID = mapPSO.at(key);
			ID3D12PipelineState* pPSO = mRenderer.GetPSO(psoID);
			assert(pPSO);
			pCmd->SetPipelineState(pPSO);

			// set cbuffers
			D3D12_GPU_VIRTUAL_ADDRESS& cbAddr = cbAddrs[iCB];
			if (iPass == 0) // allocate only on the first go
			{
				FOutlineRenderData::FConstantBuffer* pCBuffer = {};
				pHeap->AllocConstantBuffer(sizeof(FOutlineRenderData::FConstantBuffer), (void**)(&pCBuffer), &cbAddr);
				memcpy(pCBuffer, &cmd.cb, sizeof(cmd.cb));
			}
			pCmd->SetGraphicsRootConstantBufferView(1, cbAddr);

			const bool bTessellationEnabled = mat.IsTessellationEnabled();
			if (bTessellationEnabled)
			{
				D3D12_GPU_VIRTUAL_ADDRESS& cbAddr_Tsl = cbAddrsTess[iCB];
				if (iPass == 0) // allocate only on the first go
				{
					VQ_SHADER_DATA::TessellationParams* pCBuffer_Tessellation = nullptr;
					auto data = mat.GetTessellationCBufferData();
					pHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer_Tessellation)), (void**)(&pCBuffer_Tessellation), &cbAddr_Tsl);
					memcpy(pCBuffer_Tessellation, &data, sizeof(data));
				}
				pCmd->SetGraphicsRootConstantBufferView(2, cbAddr_Tsl);
			}

			assert(iCB < cbAddrs.size());
			pCmd->SetGraphicsRootConstantBufferView(4, pParams->cbPerView);
			++iCB;

			// set textures
			if (mat.SRVMaterialMaps != INVALID_ID)
			{
				//const CBV_SRV_UAV& NullTex2DSRV = mRenderer.GetSRV(mResources_MainWnd.SRV_NullTexture2D);
				pCmd->SetGraphicsRootDescriptorTable(0, mRenderer.GetSRV(mat.SRVMaterialMaps).GetGPUDescHandle(0));
				if (mat.SRVHeightMap != INVALID_ID)
				{
					pCmd->SetGraphicsRootDescriptorTable(3, mRenderer.GetSRV(mat.SRVHeightMap).GetGPUDescHandle(0));
				}
			}

			// set input geometry
			const Mesh& mesh = *cmd.pMesh;
			const auto VBIBIDs = mesh.GetIABufferIDs();
			const uint32 NumIndices = mesh.GetNumIndices();
			const BufferID& VB_ID = VBIBIDs.first;
			const BufferID& IB_ID = VBIBIDs.second;
			const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
			const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);
			D3D_PRIMITIVE_TOPOLOGY topo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			if (bTessellationEnabled)
			{
				topo = mat.GetTessellationDomain() == ETessellationDomain::QUAD_PATCH
					? D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST
					: D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
			}
			pCmd->IASetPrimitiveTopology(topo);
			pCmd->IASetVertexBuffers(0, 1, &vb);
			pCmd->IASetIndexBuffer(&ib);

			// draw
			pCmd->DrawIndexedInstanced(NumIndices, 1, 0, 0, 0);
		}
	}
}

size_t OutlinePass::Hash(size_t iPass, size_t iMSAA, size_t iTess, size_t iDomain, size_t iPart, size_t iOutTopo, size_t iTessCullMode, size_t iAlpha)
{
	return iPass
		+ NUM_PASS_OPTIONS * iMSAA
		+ NUM_PASS_OPTIONS * NUM_MSAA_OPTIONS * iTess
		+ NUM_PASS_OPTIONS * NUM_MSAA_OPTIONS * NUM_TESS_ENABLED * iDomain
		+ NUM_PASS_OPTIONS * NUM_MSAA_OPTIONS * NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * iPart
		+ NUM_PASS_OPTIONS * NUM_MSAA_OPTIONS * NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * NUM_PARTIT_OPTIONS * iOutTopo
		+ NUM_PASS_OPTIONS * NUM_MSAA_OPTIONS * NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * NUM_PARTIT_OPTIONS * NUM_OUTTOP_OPTIONS * iTessCullMode
		+ NUM_PASS_OPTIONS * NUM_MSAA_OPTIONS * NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * NUM_PARTIT_OPTIONS * NUM_OUTTOP_OPTIONS * NUM_TESS_CULL_OPTIONS * iAlpha;
}

std::vector<FPSOCreationTaskParameters> OutlinePass::CollectPSOCreationParameters()
{
	const std::wstring ShaderFilePath = VQRenderer::GetFullPathOfShader(L"Outline.hlsl");

	FPSODesc psoLoadDesc = {};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc = psoLoadDesc.D3D12GraphicsDesc;
	psoDesc.InputLayout = { };
	psoDesc.pRootSignature = mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__OutlinePass);
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.StencilEnable = TRUE;
	psoDesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_KEEP;
	psoDesc.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_KEEP;
	psoDesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_KEEP;
	psoDesc.DepthStencilState.BackFace.StencilFailOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_KEEP;

	const char* pszPSONameBases[NUM_PASS_OPTIONS] = { "PSO_OutlineStencil", "PSO_OutlineMask" };

	
	for(size_t iPass = 0    ; iPass     < NUM_PASS_OPTIONS  ; ++iPass)
	for(size_t iMSAA = 0    ; iMSAA     < NUM_MSAA_OPTIONS  ; ++iMSAA) 
	for(size_t iTess = 0    ; iTess     < NUM_TESS_ENABLED  ; ++iTess) 
	for(size_t iDomain = 0  ; iDomain   < NUM_DOMAIN_OPTIONS; ++iDomain) 
	for(size_t iPart = 0    ; iPart     < NUM_PARTIT_OPTIONS; ++iPart) 
	for(size_t iOutTopo = 0 ; iOutTopo  < NUM_OUTTOP_OPTIONS; ++iOutTopo) 
	for(size_t iTessCull = 0; iTessCull < NUM_TESS_CULL_OPTIONS; ++iTessCull)
	for(size_t iAlpha = 0   ; iAlpha    < NUM_ALPHA_OPTIONS ; ++iAlpha)
	{
		if (ShouldSkipTessellationVariant(iTess, iDomain, iPart, iOutTopo, iTessCull))
			continue;

		const size_t key = Hash(iPass, iMSAA, iTess, iDomain, iPart, iOutTopo, iTessCull, iAlpha);
		this->mapPSO[key] = INVALID_ID;
	}

	std::vector<FPSOCreationTaskParameters> params;
	for(size_t iPass = 0    ; iPass     < NUM_PASS_OPTIONS  ; ++iPass)
	for(size_t iMSAA = 0    ; iMSAA     < NUM_MSAA_OPTIONS  ; ++iMSAA) 
	for(size_t iTess = 0    ; iTess     < NUM_TESS_ENABLED  ; ++iTess) 
	for(size_t iDomain = 0  ; iDomain   < NUM_DOMAIN_OPTIONS; ++iDomain) 
	for(size_t iPart = 0    ; iPart     < NUM_PARTIT_OPTIONS; ++iPart) 
	for(size_t iOutTopo = 0 ; iOutTopo  < NUM_OUTTOP_OPTIONS; ++iOutTopo) 
	for(size_t iTessCull = 0; iTessCull < NUM_TESS_CULL_OPTIONS; ++iTessCull)
	for(size_t iAlpha = 0   ; iAlpha    < NUM_ALPHA_OPTIONS ; ++iAlpha)
	{
		if (ShouldSkipTessellationVariant(iTess, iDomain, iPart, iOutTopo, iTessCull))
			continue;

		const size_t key = Hash(iPass, iMSAA, iTess, iDomain, iPart, iOutTopo, iTessCull, iAlpha);

		// PSO name
		std::string PSOName = pszPSONameBases[iPass];
		if (iAlpha == 1) PSOName += "_AlphaMasked";
		if (iMSAA == 1) PSOName += "_MSAA4";
		if (iTess == 1)
		{
			AppendTessellationPSONameTokens(PSOName, iDomain, iPart, iOutTopo, iTessCull);
		}
		psoLoadDesc.PSOName = PSOName;

		// MSAA
		psoDesc.SampleDesc.Count = MSAA_SAMPLE_COUNTS[iMSAA];

		// DepthStencilState[iPass]
		switch (iPass)
		{
		case 0:
			psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_LESS;
			psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK::D3D12_DEPTH_WRITE_MASK_ALL;
			psoDesc.DepthStencilState.StencilReadMask = 0x00;
			psoDesc.DepthStencilState.StencilWriteMask = 0xFF;
			psoDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_ALWAYS;
			psoDesc.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_ALWAYS;
			psoDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_REPLACE;
			psoDesc.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_REPLACE;
			break;
		case 1:
			psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_ALWAYS;
			psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK::D3D12_DEPTH_WRITE_MASK_ZERO;
			psoDesc.DepthStencilState.StencilWriteMask = 0x00;
			psoDesc.DepthStencilState.StencilReadMask = 0xFF;
			psoDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_NOT_EQUAL;
			psoDesc.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC::D3D12_COMPARISON_FUNC_NOT_EQUAL;
			psoDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_KEEP;
			psoDesc.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP::D3D12_STENCIL_OP_KEEP;
			break;
		}

		// topology
		psoDesc.PrimitiveTopologyType = iTess == 1 ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		// render targets
		psoDesc.NumRenderTargets = iPass == 0 ? 0 : 1;
		if (psoDesc.NumRenderTargets > 0)
		{
			psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		}

		// shaders 
		assert(iPass <= 1);
		size_t NumShaders = 1 + iPass; // VS-PS
		if (iTess == 1)
		{	                 // VS-HS-DS-PS | VS-HS-DS-GS-PS
			NumShaders += iTessCull == 0 ? 2 : 3;
		}
		psoLoadDesc.ShaderStageCompileDescs.resize(NumShaders);
		size_t iShader = 0;
		if (iTess == 1)
		{
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "VSMain_Tess", EShaderStage::VS, EShaderModel::SM6_1 };
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "HSMain"     , EShaderStage::HS, EShaderModel::SM6_1 };
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "DSMain"     , EShaderStage::DS, EShaderModel::SM6_1 };
			if (iTessCull > 0)
				psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "GSMain" , EShaderStage::GS, EShaderModel::SM6_1 };
		}
		else
		{
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "VSMain"     , EShaderStage::VS, EShaderModel::SM6_1};
		}
		if (iPass == 1)
		{
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "PSMain"     , EShaderStage::PS, EShaderModel::SM6_1 };
		}
		const size_t iPixelShader = iShader - 1;

		// macros: per-stage
		if (iTess == 1)
		{
			AppendTessellationVSMacros(psoLoadDesc.ShaderStageCompileDescs[0/*VS*/].Macros, iDomain);
			AppendTessellationHSMacros(psoLoadDesc.ShaderStageCompileDescs[1/*HS*/].Macros, iDomain, iPart, iOutTopo, iTessCull);
			AppendTessellationDSMacros(psoLoadDesc.ShaderStageCompileDescs[2/*DS*/].Macros, iDomain, iOutTopo, iTessCull);
			if (iTessCull > 0)
			{
				AppendTessellationGSMacros(psoLoadDesc.ShaderStageCompileDescs[3/*GS*/].Macros, iOutTopo, iTessCull);
			}
		}
		
		// macros: all stages
		const FShaderMacro InstancedDrawMacro = FShaderMacro::CreateShaderMacro("INSTANCED_DRAW", "%d", RENDER_INSTANCED_SCENE_MESHES);
		const FShaderMacro InstanceCountMacro = FShaderMacro::CreateShaderMacro("INSTANCE_COUNT", "%d", MAX_INSTANCE_COUNT__SCENE_MESHES);
		const FShaderMacro OutlineMacro = { "OUTLINE_PASS", "1" };
		const FShaderMacro AlphaMaskMacro = { "ENABLE_ALPHA_MASK", "1" };
		for (FShaderStageCompileDesc& shdDesc : psoLoadDesc.ShaderStageCompileDescs) 
		{
			shdDesc.Macros.push_back(InstancedDrawMacro);
			shdDesc.Macros.push_back(InstanceCountMacro);
			if (iAlpha == 1) { shdDesc.Macros.push_back(AlphaMaskMacro); }
			if (iPass  == 1) { shdDesc.Macros.push_back(OutlineMacro); }
		}
	
		params.push_back({ &this->mapPSO.at(key), psoLoadDesc });
	}
	return params;
}
