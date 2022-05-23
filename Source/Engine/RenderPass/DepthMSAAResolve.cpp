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

#include "DepthMSAAResolve.h"

#include "../GPUMarker.h"

DepthMSAAResolvePass::DepthMSAAResolvePass(VQRenderer& Renderer)
	: RenderPassBase(Renderer)
{
}

bool DepthMSAAResolvePass::Initialize()
{
	LoadRootSignatures();
	return true;
}

void DepthMSAAResolvePass::Destroy()
{
	DestroyRootSignatures();
}

void DepthMSAAResolvePass::OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const IRenderPassResourceCollection* pRscParameters)
{
	mSourceResolutionX = Width;
	mSourceResolutionY = Height;
}

void DepthMSAAResolvePass::OnDestroyWindowSizeDependentResources()
{
}

void DepthMSAAResolvePass::RecordCommands(const IRenderPassDrawParameters* pDrawParameters)
{
	const FDrawParameters* pParams = static_cast<const FDrawParameters*>(pDrawParameters);

	const bool bWriteOutDepth     = pParams->UAV_ResolvedDepth != INVALID_ID;
	const bool bWriteOutNormals   = pParams->UAV_ResolvedNormals != INVALID_ID;
	const bool bWriteOutRoughness = pParams->UAV_ResolvedRoughness != INVALID_ID;
	assert(bWriteOutDepth || bWriteOutNormals || bWriteOutRoughness);

#if _DEBUG
	// prevent potential {} initalization of IDs pointing to a valid resource (id==0)
	if (bWriteOutDepth)     assert(pParams->UAV_ResolvedDepth > 0);
	if (bWriteOutNormals)   assert(pParams->UAV_ResolvedNormals > 0);
	if (bWriteOutRoughness) assert(pParams->UAV_ResolvedRoughness > 0);
#endif

	// shorthands
	ID3D12GraphicsCommandList* pCmd = pParams->pCmd;
	DynamicBufferHeap* pCBufferHeap = pParams->pCBufferHeap;
	assert(pCmd);
	assert(pCBufferHeap);
	const int& W = mSourceResolutionX;
	const int& H = mSourceResolutionY;
	constexpr int NumSamples = 4; // TOOD: extend support to beyond MSAA4

	// PSO selection indices
	const int iDepth = bWriteOutDepth     ? 1 : 0;
	const int iNorml = bWriteOutNormals   ? 1 : 0;
	const int iRoghn = bWriteOutRoughness ? 1 : 0;

	// allocate and prep constant buffer
	struct FParams { uint params[4]; } CBuffer;
	CBuffer.params[0] = W;
	CBuffer.params[1] = H;
	CBuffer.params[2] = NumSamples;
	CBuffer.params[3] = 0xDEADC0DE;
	FParams* pConstBuffer = {};
	D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
	pCBufferHeap->AllocConstantBuffer(sizeof(FParams), (void**)&pConstBuffer, &cbAddr);
	*pConstBuffer = CBuffer;

	// record commands
	SCOPED_GPU_MARKER(pCmd, "MSAAResolve<Depth=%d, Normals=%d, Roughness=%d>"); // TODO: string formatting
	pCmd->SetPipelineState(mRenderer.GetPSO(mPSO[iDepth][iNorml][iRoghn]));
	pCmd->SetComputeRootSignature(mpRS);
	if (true/*always*/)     pCmd->SetComputeRootDescriptorTable(0, mRenderer.GetSRV(pParams->SRV_MSAADepth).GetGPUDescHandle());
	if (bWriteOutNormals)   pCmd->SetComputeRootDescriptorTable(1, mRenderer.GetSRV(pParams->SRV_MSAANormals).GetGPUDescHandle());
	if (bWriteOutRoughness) pCmd->SetComputeRootDescriptorTable(2, mRenderer.GetSRV(pParams->SRV_MSAARoughness).GetGPUDescHandle());
	if (bWriteOutDepth)     pCmd->SetComputeRootDescriptorTable(3, mRenderer.GetUAV(pParams->UAV_ResolvedDepth).GetGPUDescHandle());
	if (bWriteOutNormals)   pCmd->SetComputeRootDescriptorTable(4, mRenderer.GetUAV(pParams->UAV_ResolvedNormals).GetGPUDescHandle());
	if (bWriteOutRoughness) pCmd->SetComputeRootDescriptorTable(5, mRenderer.GetUAV(pParams->UAV_ResolvedRoughness).GetGPUDescHandle());
	pCmd->SetComputeRootConstantBufferView(6, cbAddr);

	constexpr int DispatchGroupDimensionX = 8;
	constexpr int DispatchGroupDimensionY = 8;
	const     int DispatchX = DIV_AND_ROUND_UP(W, DispatchGroupDimensionX);
	const     int DispatchY = DIV_AND_ROUND_UP(H, DispatchGroupDimensionY);
	constexpr int DispatchZ = 1;
	assert(DispatchX != 0);
	assert(DispatchY != 0);
	pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);
}

std::vector<FPSOCreationTaskParameters> DepthMSAAResolvePass::CollectPSOCreationParameters()
{
	std::vector<FPSOCreationTaskParameters> params;
	for (int iDpth = 0; iDpth < 2; ++iDpth)
	for (int iNrml = 0; iNrml < 2; ++iNrml)
	for (int iRghn = 0; iRghn < 2; ++iRghn)
	{
		if (iDpth == 0 && iNrml == 0 && iRghn == 0) 
			continue; // we always want an output, don't compile this permutation

		FPSOCreationTaskParameters param = {};
		param.pID = &mPSO[iDpth][iNrml][iRghn];

		FShaderStageCompileDesc shaderDesc = {};
		shaderDesc.EntryPoint = "CSMain";
		shaderDesc.FilePath = VQRenderer::GetFullPathOfShader("DepthResolve.hlsl");
		shaderDesc.ShaderModel = "cs_5_0";
		shaderDesc.Macros =
		{
			  {"OUTPUT_DEPTH"    , (iDpth ? "1" : "0")}
			, {"OUTPUT_NORMALS"  , (iNrml ? "1" : "0")}
			, {"OUTPUT_ROUGHNESS", (iRghn ? "1" : "0")}
		};

		FPSODesc& psoDesc = param.Desc;
		psoDesc.PSOName = "DepthMSAAResolveCS" 
			+ std::string(iDpth ? "_Depth" : "") 
			+ std::string(iNrml ? "_Normals" : "") 
			+ std::string(iRghn ? "_Roughness" : "");
		psoDesc.ShaderStageCompileDescs = { shaderDesc };
		D3D12_COMPUTE_PIPELINE_STATE_DESC& desc = psoDesc.D3D12ComputeDesc;
		desc.pRootSignature = mpRS;
		desc.NodeMask = 0;
		desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		params.push_back(param);
	}

	return params;
}

void DepthMSAAResolvePass::LoadRootSignatures()
{
	ID3D12Device* pDevice = mRenderer.GetDevicePtr();

	//==============================RootSignature============================================
	{
		constexpr UINT NUM_SRV = 3;
		constexpr UINT NUM_UAV = 3;
		constexpr UINT NUM_RSC_VIEWS = NUM_SRV + NUM_UAV;

		CD3DX12_ROOT_PARAMETER RTSlot[NUM_RSC_VIEWS+1] = {};
		CD3DX12_DESCRIPTOR_RANGE DescRange[NUM_RSC_VIEWS] = {};
		
		D3D12_DESCRIPTOR_RANGE_TYPE t[2] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV , D3D12_DESCRIPTOR_RANGE_TYPE_UAV };
		for (int i = 0; i < NUM_RSC_VIEWS; ++i)
		{
			const int numViewsOfThisType = NUM_RSC_VIEWS >> 1;
			const int iView = i / (numViewsOfThisType); // 0: srv, 1:uav
			DescRange[i].Init(t[iView], 1, i % numViewsOfThisType, 0, 0);
			RTSlot[i].InitAsDescriptorTable(1, &DescRange[i]);
		}
		RTSlot[NUM_RSC_VIEWS].InitAsConstantBufferView(0);

		CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
		descRootSignature.NumParameters = _countof(RTSlot);
		descRootSignature.pParameters = RTSlot;
		descRootSignature.NumStaticSamplers = 0;
		descRootSignature.pStaticSamplers = nullptr;
		descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		ID3DBlob* pOutBlob = nullptr;
		ID3DBlob* pErrorBlob = nullptr;
		HRESULT hr = D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob);
		ThrowIfFailed(
			pDevice->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&mpRS))
		);
		SetName(mpRS, "DepthMSAAResolve");

		pOutBlob->Release();
		if (pErrorBlob)
			pErrorBlob->Release();
	}
}

void DepthMSAAResolvePass::DestroyRootSignatures()
{
	if (mpRS) mpRS->Release();
}
