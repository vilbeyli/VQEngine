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
#include "../../Renderer/Renderer.h"
#include "../Scene/Scene.h"
#include "../../Shaders/LightingConstantBufferData.h"
#include "../VQUtils/Source/utils.h"
#include "../GPUMarker.h"

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
	mRenderer.DestroyDSV(DSVPassOutput);
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

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
		UINT64 byteAlignedSize = 0;
		mRenderer.GetDevicePtr()->GetCopyableFootprints(&desc.d3d12Desc, 0, 1, 0, &footprint, nullptr, nullptr, &byteAlignedSize);

		desc.TexName = "ObjectID_CPU_READBACK";
		desc.bCPUReadback = true;
		desc.ResourceState = D3D12_RESOURCE_STATE_COPY_DEST;
		desc.d3d12Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.d3d12Desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.d3d12Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.d3d12Desc.Height = 1;
		desc.d3d12Desc.Flags = D3D12_RESOURCE_FLAG_NONE;
		desc.d3d12Desc.Width = byteAlignedSize;
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
		Log::Error("GetMaterial() failed: Material not created. Did you call Scene::CreateMaterial()? (matID=%d)", ID);
		assert(false);
	}
	return pMats->at(ID);
}

static size_t GetPSO_Key(
	size_t iTess,
	size_t iDomain,
	size_t iPart,
	size_t iOutTopo,
	size_t iTessCullMode,
	size_t iAlpha)
{
	using namespace Tessellation;
	return iTess
		+ NUM_TESS_ENABLED * iDomain
		+ NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * iPart
		+ NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * NUM_PARTIT_OPTIONS * iOutTopo
		+ NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * NUM_PARTIT_OPTIONS * NUM_OUTTOP_OPTIONS * iTessCullMode
		+ NUM_TESS_ENABLED * NUM_DOMAIN_OPTIONS * NUM_PARTIT_OPTIONS * NUM_OUTTOP_OPTIONS * NUM_TESS_CULL_OPTIONS * iAlpha;
}
static size_t GetPSO_Key(const Material& mat, const VQRenderer& mRenderer)
{
	size_t iTess = 0; size_t iDomain = 0; size_t iPart = 0; size_t iOutTopo = 0; size_t iTessCull = 0;
	Tessellation::GetTessellationPSOConfig(mat.Tessellation, iTess, iDomain, iPart, iOutTopo, iTessCull);
	const size_t iAlpha = mat.IsAlphaMasked(mRenderer) ? 1 : 0;
	return GetPSO_Key(iTess, iDomain, iPart, iOutTopo, iTessCull, iAlpha);
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
	assert(pParams->pCBAddresses);
	assert(pParams->pCBufferHeap);
	const std::vector< D3D12_GPU_VIRTUAL_ADDRESS>& cbAddresses = *pParams->pCBAddresses;

	DynamicBufferHeap* pHeap = pParams->pCBufferHeap;
	ID3D12GraphicsCommandList* pCmd = pParams->pCmd;
	ID3D12GraphicsCommandList* pCmdCpy = static_cast<ID3D12GraphicsCommandList*>(pParams->pCmdCopy);
	auto pRscRT = mRenderer.GetTextureResource(TEXPassOutput);
	auto pRscCPU = mRenderer.GetTextureResource(TEXPassOutputCPUReadback);

	const DSV& dsv = mRenderer.GetDSV(DSVPassOutput);
	const RTV& rtv = mRenderer.GetRTV(RTVPassOutput);

	const float RenderResolutionX = static_cast<float>(pParams->pSceneView->SceneRTWidth);
	const float RenderResolutionY = static_cast<float>(pParams->pSceneView->SceneRTHeight);
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };

	D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
	const float clearColor[] = { 0, 0, 0, 0 };
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtv.GetCPUDescHandle();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsv.GetCPUDescHandle();

	if (pParams->bEnableAsyncCopy)
	{
		pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pRscRT,
			D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET));
	}

	pCmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, nullptr);
	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);

	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ZPrePass));

	// draw meshes
	int iCB = 0;
	for (const MeshRenderCommand_t& meshRenderCmd : pParams->pSceneView->meshRenderCommands)
	{
		using namespace VQ_SHADER_DATA;
		const Material& mat = GetMaterial(meshRenderCmd.matID, pParams->pMaterials);
		
		const uint32 NumIndices = meshRenderCmd.numIndices;
		const BufferID& VB_ID = meshRenderCmd.vertexIndexBuffer.first;
		const BufferID& IB_ID = meshRenderCmd.vertexIndexBuffer.second;
		const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
		const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);
		const uint32 NumInstances = (uint32)meshRenderCmd.matNormal.size();

		// select PSO
		const PSO_ID psoID = mapPSO.at(GetPSO_Key(mat, mRenderer));
		pCmd->SetPipelineState(mRenderer.GetPSO(psoID));

		// set cbuffers
		if (mat.Tessellation.bEnableTessellation)
		{
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr_Tsl;
			VQ_SHADER_DATA::TessellationParams* pCBuffer_Tessellation = nullptr;
			auto data = mat.GetTessellationCBufferData();
			pHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer_Tessellation)), (void**)(&pCBuffer_Tessellation), &cbAddr_Tsl);
			memcpy(pCBuffer_Tessellation, &data, sizeof(data));
			pCmd->SetGraphicsRootConstantBufferView(2, cbAddr_Tsl);
		}
		assert(iCB < cbAddresses.size());
		pCmd->SetGraphicsRootConstantBufferView(1, cbAddresses[iCB++]);
		pCmd->SetGraphicsRootConstantBufferView(4, pParams->cbPerView);

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

		// set IA-VB-IB
		D3D_PRIMITIVE_TOPOLOGY topo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		if (mat.Tessellation.bEnableTessellation)
		{
			topo = mat.Tessellation.Domain == ETessellationDomain::QUAD_PATCH
				? D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST
				: D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
		}
		pCmd->IASetPrimitiveTopology(topo);
		pCmd->IASetVertexBuffers(0, 1, &vb);
		pCmd->IASetIndexBuffer(&ib);
		
		pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
	}


	// transition output to copy source
	pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pRscRT,
		D3D12_RESOURCE_STATE_RENDER_TARGET, 
		(pParams->bEnableAsyncCopy ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_COPY_SOURCE)
	));
}

using namespace Tessellation;
static constexpr size_t NUM_ALPHA_OPTIONS = 2; // opaque/alpha masked

std::vector<FPSOCreationTaskParameters> ObjectIDPass::CollectPSOCreationParameters()
{
	const std::wstring ShaderFilePath = VQRenderer::GetFullPathOfShader(L"ObjectID.hlsl");

	FPSODesc psoLoadDesc = {};
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
	psoDesc.NumRenderTargets = 1;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_SINT;
	psoDesc.SampleDesc.Count = 1;

	// count PSOs
	for(size_t iTess = 0    ; iTess     < NUM_TESS_ENABLED  ; ++iTess) 
	for(size_t iDomain = 0  ; iDomain   < NUM_DOMAIN_OPTIONS; ++iDomain) 
	for(size_t iPart = 0    ; iPart     < NUM_PARTIT_OPTIONS; ++iPart) 
	for(size_t iOutTopo = 0 ; iOutTopo  < NUM_OUTTOP_OPTIONS; ++iOutTopo) 
	for(size_t iTessCull = 0; iTessCull < NUM_TESS_CULL_OPTIONS; ++iTessCull)
	for(size_t iAlpha = 0   ; iAlpha    < NUM_ALPHA_OPTIONS ; ++iAlpha)
	{
		if (ShouldSkipTessellationVariant(iTess, iDomain, iPart, iOutTopo, iTessCull))
			continue;
		const size_t key = GetPSO_Key(iTess, iDomain, iPart, iOutTopo, iTessCull, iAlpha);
		mapPSO[key] = INVALID_ID;
	}
	
	std::vector<FPSOCreationTaskParameters> params(mapPSO.size());
	size_t iPSO = 0;
	for(size_t iTess = 0    ; iTess     < NUM_TESS_ENABLED  ; ++iTess) 
	for(size_t iDomain = 0  ; iDomain   < NUM_DOMAIN_OPTIONS; ++iDomain) 
	for(size_t iPart = 0    ; iPart     < NUM_PARTIT_OPTIONS; ++iPart) 
	for(size_t iOutTopo = 0 ; iOutTopo  < NUM_OUTTOP_OPTIONS; ++iOutTopo) 
	for(size_t iTessCull = 0; iTessCull < NUM_TESS_CULL_OPTIONS; ++iTessCull)
	for(size_t iAlpha = 0   ; iAlpha    < NUM_ALPHA_OPTIONS ; ++iAlpha)
	{
		if (ShouldSkipTessellationVariant(iTess, iDomain, iPart, iOutTopo, iTessCull))
			continue;

		const size_t key = GetPSO_Key(iTess, iDomain, iPart, iOutTopo, iTessCull, iAlpha);

		// PSO name
		std::string PSOName = "PSO_ObjectIDPass";
		if (iAlpha == 1) PSOName += "_AlphaMasked";
		if (iTess == 1)
		{
			AppendTessellationPSONameTokens(PSOName, iDomain, iPart, iOutTopo, iTessCull);
		}
		psoLoadDesc.PSOName = PSOName;

		// topology
		psoDesc.PrimitiveTopologyType = iTess == 1
			? D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH
			: D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		// shaders
		size_t NumShaders = 2; // VS-PS
		if (iTess == 1)
		{	                 // VS-HS-DS-PS | VS-HS-DS-GS-PS
			NumShaders += iTessCull == 0 ? 2 : 3;
		}
		psoLoadDesc.ShaderStageCompileDescs.resize(NumShaders);
		size_t iShader = 0;
		if (iTess == 1)
		{
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "VSMain_Tess", "vs_6_0" };
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "HSMain"     , "hs_6_0" };
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "DSMain"     , "ds_6_0" };
			if (iTessCull > 0)
				psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "GSMain" , "gs_6_0" };
		}
		else
		{
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "VSMain", "vs_6_0" };
		}
		psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "PSMain", "ps_6_0" };
		const size_t iPixelShader = iShader - 1;

		// macros
		const FShaderMacro InstancedDrawMacro = { "INSTANCED_DRAW", "1" };
		const FShaderMacro InstanceCountMacro = { "INSTANCE_COUNT", std::to_string(MAX_INSTANCE_COUNT__SCENE_MESHES) };
		if (iTess == 1)
		{
			AppendTessellationVSMacros(psoLoadDesc.ShaderStageCompileDescs[0/*VS*/].Macros, iDomain);
			AppendTessellationHSMacros(psoLoadDesc.ShaderStageCompileDescs[1/*HS*/].Macros, iDomain, iPart, iOutTopo, iTessCull);
			AppendTessellationDSMacros(psoLoadDesc.ShaderStageCompileDescs[2/*DS*/].Macros, iDomain, iOutTopo, iTessCull);
			if (iTessCull > 0)
				AppendTessellationGSMacros(psoLoadDesc.ShaderStageCompileDescs[3/*GS*/].Macros, iOutTopo, iTessCull);
		}
		for (FShaderStageCompileDesc& shdDesc : psoLoadDesc.ShaderStageCompileDescs)
		{
			shdDesc.Macros.push_back(InstancedDrawMacro);
			shdDesc.Macros.push_back(InstanceCountMacro);
			if (iAlpha == 1)
			{
				shdDesc.Macros.push_back({ "ENABLE_ALPHA_MASK", "1" });
			}
		}

		FPSOCreationTaskParameters& param = params[iPSO++];
		param.pID = &mapPSO[key];
		param.Desc = psoLoadDesc;
	}

	return params;
}

int4 ObjectIDPass::ReadBackPixel(const int2& screenCoords) const
{
	auto pRsc = mRenderer.GetTextureResource(TEXPassOutputCPUReadback);
	const int bytesPerChannel = 4; // 32bit/channel
	const int numChannels = 4; // RGBA
	const int bytesPerPixel = bytesPerChannel * numChannels;
	const size_t numPixels = mOutputResolutionX * mOutputResolutionY;

	unsigned int* pData = nullptr;
	D3D12_RANGE readRange = { 0, numPixels * bytesPerPixel }; // Only request the read for the necessary range
	HRESULT hr = pRsc->Map(0, &readRange, reinterpret_cast<void**>(&pData));
	if (SUCCEEDED(hr) && screenCoords.y >= 0 && screenCoords.x >= 0)
	{
		const int pixelIndex = (screenCoords.y * mOutputResolutionX + screenCoords.x) * bytesPerChannel;
		const int r = pData[pixelIndex + 0]; 
		const int g = pData[pixelIndex + 1];
		const int b = pData[pixelIndex + 2];
		const int a = pData[pixelIndex + 3];

		pRsc->Unmap(0, nullptr);
		//Log::Info("\t[%d, %d] | [%.2f, %.2f] = (%d, %d, %d, %d)", pixelX, pixelY, uv.x, uv.y, r, g, b, a);
		return int4(r, g, b, a);
	}

	return int4(-1, -1, -1, -1);
}

ID3D12Resource* ObjectIDPass::GetGPUTextureResource() const { return mRenderer.GetTextureResource(TEXPassOutput); }
ID3D12Resource* ObjectIDPass::GetCPUTextureResource() const { return mRenderer.GetTextureResource(TEXPassOutputCPUReadback); }