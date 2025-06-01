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
#include "Renderer/Renderer.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneViews.h"
#include "Shaders/LightingConstantBufferData.h"
#include "Libs/VQUtils/Source/utils.h"
#include "Engine/GPUMarker.h"

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
		FTextureRequest desc("ObjectIDDepth");
		desc.D3D12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R32_TYPELESS
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
		mRenderer.InitializeDSV(DSVPassOutput, 0u, TEXPassOutputDepth);
	}
	{ // Main render target view
		FTextureRequest desc("ObjectID");
		desc.D3D12Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_SINT
			, mOutputResolutionX
			, mOutputResolutionY
			, 1 // Array Size
			, 1 // MIP levels
			, 1 // MSAA SampleCount
			, 0 // MSAA SampleQuality
			, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
		);

		desc.InitialState = D3D12_RESOURCE_STATE_COMMON;
		TEXPassOutput = mRenderer.CreateTexture(desc);
		mRenderer.InitializeRTV(RTVPassOutput, 0u, TEXPassOutput);

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
		UINT64 byteAlignedSize = 0;
		mRenderer.GetDevicePtr()->GetCopyableFootprints(&desc.D3D12Desc, 0, 1, 0, &footprint, nullptr, nullptr, &byteAlignedSize);

		desc.Name = "ObjectID_CPU_READBACK";
		desc.bCPUReadback = true;
		desc.InitialState = D3D12_RESOURCE_STATE_COPY_DEST;
		desc.D3D12Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.D3D12Desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.D3D12Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.D3D12Desc.Height = 1;
		desc.D3D12Desc.Flags = D3D12_RESOURCE_FLAG_NONE;
		desc.D3D12Desc.Width = byteAlignedSize;
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
	uint8 iTess = 0; uint8 iDomain = 0; uint8 iPart = 0; uint8 iOutTopo = 0; uint8 iTessCull = 0;
	mat.GetTessellationPSOConfig(iTess, iDomain, iPart, iOutTopo, iTessCull);
	const size_t iAlpha = mat.IsAlphaMasked(mRenderer) ? 1 : 0;
	return GetPSO_Key(iTess, iDomain, iPart, iOutTopo, iTessCull, iAlpha);
}

PSO_ID ObjectIDPass::GetPSO_ID(
	size_t iTess,
	size_t iDomain,
	size_t iPart,
	size_t iOutTopo,
	size_t iTessCullMode,
	size_t iAlpha) const
{
	return mapPSO.at(GetPSO_Key(iTess, iDomain, iPart, iOutTopo, iTessCullMode, iAlpha));
}


void ObjectIDPass::RecordCommands(const IRenderPassDrawParameters* pDrawParameters)
{
	const FDrawParameters* pParams = static_cast<const FDrawParameters*>(pDrawParameters);
	assert(pParams);
	assert(pParams->pCmd);
	assert(pParams->pCmdCopy);
	assert(pParams->pSceneView);
	assert(pParams->pSceneDrawData);

	ID3D12GraphicsCommandList* pCmd = pParams->pCmd;
	ID3D12GraphicsCommandList* pCmdCpy = static_cast<ID3D12GraphicsCommandList*>(pParams->pCmdCopy);
	const bool bEmptyDrawList = pParams->pSceneDrawData->mainViewDrawParams.empty();

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
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(pRscRT, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
		pCmd->ResourceBarrier(1, &barrier);
	}

	pCmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, nullptr);
	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);

	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ZPrePass));
	pCmd->SetGraphicsRootConstantBufferView(4, pParams->cbPerView);

	// draw meshes
	int iCB = 0;
	PSO_ID psoPrev = INVALID_ID;
	BufferID vbPrev = INVALID_ID;
	BufferID ibPrev = INVALID_ID;
	SRV_ID matSRVPrev = INVALID_ID;
	SRV_ID heightSRVPrev = INVALID_ID;
	D3D_PRIMITIVE_TOPOLOGY topoPrev = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
	for (const FInstancedDrawParameters& meshRenderCmd : pParams->pSceneDrawData->mainViewDrawParams)
	{
		const size_t iAlpha = meshRenderCmd.PackedMaterialConfig & 0x1;
		
		// select PSO
		size_t iTess = 0; size_t iDomain = 0; size_t iPart = 0; size_t iOutTopo = 0; size_t iTessCull = 0;
		meshRenderCmd.UnpackTessellationConfig(iTess, iDomain, iPart, iOutTopo, iTessCull);
		const PSO_ID psoID = this->GetPSO_ID(iTess, iDomain, iPart, iOutTopo, iTessCull, iAlpha);
		if (psoPrev != psoID)
		{
			pCmd->SetPipelineState(mRenderer.GetPSO(psoID));
		}

		// set cbuffers
		pCmd->SetGraphicsRootConstantBufferView(1, meshRenderCmd.cbAddr);
		if (meshRenderCmd.cbAddr_Tessellation)
		{
			pCmd->SetGraphicsRootConstantBufferView(2, meshRenderCmd.cbAddr_Tessellation);
		}

		// set textures
		if (meshRenderCmd.SRVMaterialMaps != matSRVPrev)
		{
			pCmd->SetGraphicsRootDescriptorTable(0, mRenderer.GetSRV(meshRenderCmd.SRVMaterialMaps).GetGPUDescHandle(0));
		}
		if (meshRenderCmd.SRVHeightMap != heightSRVPrev)
		{
			pCmd->SetGraphicsRootDescriptorTable(3, mRenderer.GetSRV(meshRenderCmd.SRVHeightMap).GetGPUDescHandle(0));
		}

		// set IA-VB-IB
		if (topoPrev != meshRenderCmd.IATopology)
		{
			pCmd->IASetPrimitiveTopology(meshRenderCmd.IATopology);
		}
		if (vbPrev != meshRenderCmd.VB)
		{
			const VBV& vb = mRenderer.GetVertexBufferView(meshRenderCmd.VB);
			pCmd->IASetVertexBuffers(0, 1, &vb);
		}
		if (ibPrev != meshRenderCmd.IB)
		{
			const IBV& ib = mRenderer.GetIndexBufferView(meshRenderCmd.IB);
			pCmd->IASetIndexBuffer(&ib);
		}

		// draw
		pCmd->DrawIndexedInstanced(meshRenderCmd.numIndices, meshRenderCmd.numInstances, 0, 0, 0);

		ibPrev = meshRenderCmd.IB;
		vbPrev = meshRenderCmd.VB;
		topoPrev = meshRenderCmd.IATopology;
		psoPrev = psoID;
		matSRVPrev = meshRenderCmd.SRVMaterialMaps;
		heightSRVPrev = meshRenderCmd.SRVHeightMap;
	}


	// transition output to copy source
	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(pRscRT, 
		D3D12_RESOURCE_STATE_RENDER_TARGET, (pParams->bEnableAsyncCopy 
			? D3D12_RESOURCE_STATE_COMMON
			: D3D12_RESOURCE_STATE_COPY_SOURCE
	));
	pCmd->ResourceBarrier(1, &barrier);
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
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "VSMain_Tess", EShaderStage::VS, EShaderModel::SM6_0 };
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "HSMain"     , EShaderStage::HS, EShaderModel::SM6_0 };
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "DSMain"     , EShaderStage::DS, EShaderModel::SM6_0 };
			if (iTessCull > 0)
				psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "GSMain" , EShaderStage::GS, EShaderModel::SM6_0 };
		}
		else
		{
			psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "VSMain", EShaderStage::VS, EShaderModel::SM6_0 };
		}
		psoLoadDesc.ShaderStageCompileDescs[iShader++] = FShaderStageCompileDesc{ ShaderFilePath, "PSMain", EShaderStage::PS, EShaderModel::SM6_0 };
		const size_t iPixelShader = iShader - 1;

		// macros
		const FShaderMacro InstancedDrawMacro = { "INSTANCED_DRAW", "1" };
		const FShaderMacro InstanceCountMacro = FShaderMacro::CreateShaderMacro("INSTANCE_COUNT", "%d", MAX_INSTANCE_COUNT__SCENE_MESHES);
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

int4 ObjectIDPass::ReadBackPixel(const int2& screenCoords, HWND hwnd) const
{
	mRenderer.WaitCopyFenceOnCPU(hwnd);

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