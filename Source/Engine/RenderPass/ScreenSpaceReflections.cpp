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

#include "ScreenSpaceReflections.h"

#include "../../Renderer/Libs/D3DX12/d3dx12.h"
#include "../../Renderer/Renderer.h"
#include "../GPUMarker.h"

/**********************************************************************
Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
********************************************************************/
namespace _1spp
{
#include "../Libs/AMDFidelityFX/SSSR/samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp.cpp"
}
struct FBlueNoiseSamplerStateCPU
{
	std::int32_t const (&sobolBuffer)[256 * 256];
	std::int32_t const (&rankingTileBuffer)[128 * 128 * 8];
	std::int32_t const (&scramblingTileBuffer)[128 * 128 * 8];
};
static const FBlueNoiseSamplerStateCPU g_blueNoiseSamplerState = { _1spp::sobol_256spp_256d,  _1spp::rankingTile,  _1spp::scramblingTile };

constexpr int BLUE_NOISE_TEXTURE_DIM = 128;

constexpr bool DONT_USE_ARRAY_VIEW   = false;
constexpr bool DONT_USE_CUBEMAP_VIEW = false;

ScreenSpaceReflectionsPass::ScreenSpaceReflectionsPass(VQRenderer& Renderer)
	: RenderPassBase(Renderer)
	, mpCommandSignature(nullptr)
	, iBuffer(0)
{}

ScreenSpaceReflectionsPass::~ScreenSpaceReflectionsPass()
{
	// TODO: decide how we want to clean up leaks.
	// 
	//OnDestroyWindowSizeDependentResources();
	//DestroyResources();
}

bool ScreenSpaceReflectionsPass::Initialize()
{
	CreateResources();
	LoadRootSignatures();
	AllocateResourceViews();
	return true;
}

void ScreenSpaceReflectionsPass::Destroy()
{
	DestroyResources();
	for (const auto& pr : this->mSubpassRootSignatureLookup)
	{
		if (pr.second) pr.second->Release();
	}
}

constexpr uint32_t ELEMENT_BYTE_SIZE = 4;

void ScreenSpaceReflectionsPass::OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const IRenderPassResourceCollection* pRscParameters)
{
	const uint64 NumPixels = Width * Height;
	const FResourceParameters* pParams = static_cast<const FResourceParameters*>(pRscParameters);

	//==============================Create Tile Classification-related buffers============================================
	{
		TextureCreateDesc desc("FFX_SSSR - Ray List");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Buffer(NumPixels * ELEMENT_BYTE_SIZE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS); // 4B - single uint variable
		desc.ResourceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		TexRayList = mRenderer.CreateTexture(desc);
		desc.TexName = "FFX_DNSR - Denoiser Tile List"; 
		TexDenoiserTileList = mRenderer.CreateTexture(desc);
	}
	//==============================Create denoising-related resources==============================
	{
		const UINT Widt8  = DIV_AND_ROUND_UP(Width , 8u);
		const UINT Heigh8 = DIV_AND_ROUND_UP(Height, 8u);
		enum EDescs
		{
			RADIANCE = 0,
			AVERAGE_RADIANCE,
			VARIANCE,
			SAMPLE_COUNT,
			DEPTH_HISTORY,
			NORMAL_HISTORY,
			ROUGHNESS_TEXTURE,

			NUM_DESCS
		};
		std::array<CD3DX12_RESOURCE_DESC, NUM_DESCS> descs =
		{
			  CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, Width, Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
			, CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R11G11B10_FLOAT   , Widt8, Heigh8, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
			, CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16_FLOAT         , Width, Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
			, CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16_FLOAT         , Width, Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
			, CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_FLOAT         , Width, Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
			, CD3DX12_RESOURCE_DESC::Tex2D(pParams->NormalBufferFormat   , Width, Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
			, CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8_UNORM          , Width, Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
		};
		const D3D12_RESOURCE_STATES rscStateSRV = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		const D3D12_RESOURCE_STATES rscStateUAV = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		std::vector<TextureCreateDesc> texCreateDescs =
		{
			  TextureCreateDesc("Reflection Denoiser - Extracted Roughness Texture"        , descs[EDescs::ROUGHNESS_TEXTURE], rscStateSRV)
			, TextureCreateDesc("Reflection Denoiser - Depth Buffer History"               , descs[EDescs::DEPTH_HISTORY]    , rscStateSRV)
			, TextureCreateDesc("Reflection Denoiser - Normal Buffer History"              , descs[EDescs::NORMAL_HISTORY]   , rscStateSRV)
			, TextureCreateDesc("Reflection Denoiser - Extracted Roughness History Texture", descs[EDescs::ROUGHNESS_TEXTURE], rscStateSRV)
			, TextureCreateDesc("Reflection Denoiser - Radiance 0"                         , descs[EDescs::RADIANCE]         , rscStateSRV)
			, TextureCreateDesc("Reflection Denoiser - Radiance 1"                         , descs[EDescs::RADIANCE]         , rscStateSRV)
			, TextureCreateDesc("Reflection Denoiser - Variance 0"                         , descs[EDescs::VARIANCE]         , rscStateSRV)
			, TextureCreateDesc("Reflection Denoiser - Variance 1"                         , descs[EDescs::VARIANCE]         , rscStateSRV)
			, TextureCreateDesc("Reflection Denoiser - SampleCount 0"                      , descs[EDescs::SAMPLE_COUNT]     , rscStateSRV)
			, TextureCreateDesc("Reflection Denoiser - SampleCount 1"                      , descs[EDescs::SAMPLE_COUNT]     , rscStateSRV)
			, TextureCreateDesc("Reflection Denoiser - Average Radiance 0"                 , descs[EDescs::AVERAGE_RADIANCE] , rscStateSRV)
			, TextureCreateDesc("Reflection Denoiser - Average Radiance 1"                 , descs[EDescs::AVERAGE_RADIANCE] , rscStateSRV)
			, TextureCreateDesc("Reflection Denoiser - Reprojected Radiance"               , descs[EDescs::RADIANCE]         , rscStateSRV)
		};

		int i = 0;
		std::vector<TextureID*> pTexIDs = GetWindowSizeDependentDenoisingTextures();
		assert(pTexIDs.size() == texCreateDescs.size());
		for(const TextureCreateDesc& desc : texCreateDescs)
		{
			*pTexIDs[i++] = mRenderer.CreateTexture(desc);
		}
	}
	iBuffer = 0;
	bClearHistoryBuffers = true;
	MatPreviousViewProjection = DirectX::XMMatrixIdentity();

	InitializeResourceViews(pParams);
}

void ScreenSpaceReflectionsPass::OnDestroyWindowSizeDependentResources()
{
	std::vector<TextureID*> pTexIDs = GetWindowSizeDependentDenoisingTextures();
	for (TextureID* pID : pTexIDs)
	{
		if (*pID != INVALID_ID) mRenderer.DestroyTexture(*pID);
	}
	
	if (TexRayList          != INVALID_ID) mRenderer.DestroyTexture(TexRayList);
	if (TexDenoiserTileList != INVALID_ID) mRenderer.DestroyTexture(TexDenoiserTileList);
}


static void CopyToTexture(ID3D12GraphicsCommandList* cl, ID3D12Resource* source, ID3D12Resource* target, UINT32 width, UINT32 height)
{
	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = source;
	src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	src.SubresourceIndex = 0;

	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = target;
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = 0;

	D3D12_BOX srcBox = {};
	srcBox.left = 0;
	srcBox.top = 0;
	srcBox.front = 0;
	srcBox.right = width;
	srcBox.bottom = height;
	srcBox.back = 1;

	cl->CopyTextureRegion(&dst, 0, 0, 0, &src, &srcBox);
}


void ScreenSpaceReflectionsPass::RecordCommands(const IRenderPassDrawParameters* pDrawParameters)
{
	const FDrawParameters* pParams = static_cast<const FDrawParameters*>(pDrawParameters);
	assert(pParams);
	assert(pParams->pCmd);
	assert(pParams->pCBufferHeap);
	assert(pParams->TexDepthHierarchy != INVALID_ID);
	assert(pParams->TexNormals != INVALID_ID);

	const int W = pParams->ffxCBuffer.bufferDimensions[0]; // render width
	const int H = pParams->ffxCBuffer.bufferDimensions[1]; // render height
	ID3D12GraphicsCommandList* pCmd    = pParams->pCmd;
	DynamicBufferHeap* pCBHeap         = pParams->pCBufferHeap;
	ID3D12Resource* pRscDepthHierarchy = mRenderer.GetTextureResource(pParams->TexDepthHierarchy);
	ID3D12Resource* pRscNormals        = mRenderer.GetTextureResource(pParams->TexNormals);

	ID3D12Resource* pRscRoughnessExtract = mRenderer.GetTextureResource(TexExtractedRoughness);
	ID3D12Resource* pRscRoughnessHistory = mRenderer.GetTextureResource(TexRoughnessHistory);
	ID3D12Resource* pRscDepthHistory     = mRenderer.GetTextureResource(TexDepthHistory);
	ID3D12Resource* pRscNormalsHistory   = mRenderer.GetTextureResource(TexNormalsHistory);

	//Set Constantbuffer data
	FFX_SSSRConstants* pCB_CPU = nullptr;
	D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
	if (!pCBHeap->AllocConstantBuffer(sizeof(FFX_SSSRConstants), (void**)&pCB_CPU, &cbAddr))
	{
		Log::Warning("AllocConstantBuffer() failed for ScreenSpaceReflectionsPass::RecordCommands!");
	}
	*pCB_CPU = pParams->ffxCBuffer;
	pCB_CPU->prevViewProjection = MatPreviousViewProjection;

	ID3D12DescriptorHeap* ppHeaps[] = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };
	pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	// Ensure that the ray list is in UA state
	{
		SCOPED_GPU_MARKER(pCmd, "TransitionRayListResources");
		D3D12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexRayList)                    , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexDenoiserTileList)           , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(pRscRoughnessExtract                                        , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexReflectionDenoiserBlueNoise), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthHierarchy                                          , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexRadiance[iBuffer]), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		};
		pCmd->ResourceBarrier(_countof(barriers), barriers);
	}

	if (this->bClearHistoryBuffers)
	{
		this->ClearHistoryBuffers(pCmd);
		this->bClearHistoryBuffers = false;
	}
	
	{
		SCOPED_GPU_MARKER(pCmd, "FFX DNSR ClassifyTiles");
		pCmd->SetComputeRootSignature(mSubpassRootSignatureLookup.at(ESubpass::CLASSIFY_TILES));
		pCmd->SetComputeRootDescriptorTable(0, mRenderer.GetSRV(SRVClassifyTilesInputs[iBuffer]).GetGPUDescHandle());
		pCmd->SetComputeRootDescriptorTable(1, mRenderer.GetSRV(pParams->SRVEnvironmentSpecularIrradianceCubemap).GetGPUDescHandle());
		pCmd->SetComputeRootDescriptorTable(2, mRenderer.GetSRV(pParams->SRVBRDFIntegrationLUT).GetGPUDescHandle());
		pCmd->SetComputeRootConstantBufferView(3, cbAddr);
		pCmd->SetPipelineState(mRenderer.GetPSO(PSOClassifyTilesPass));
		const UINT DISPATCH_X = DIV_AND_ROUND_UP(W, 8u);
		const UINT DISPATCH_Y = DIV_AND_ROUND_UP(H, 8u);
		const UINT DISPATCH_Z = 1;
		pCmd->Dispatch(DISPATCH_X, DISPATCH_Y, DISPATCH_Z);
	}

	// At the same time prepare the blue noise texture for intersection
	{
		SCOPED_GPU_MARKER(pCmd, "FFX DNSR PrepareBlueNoise");
		pCmd->SetComputeRootSignature(mSubpassRootSignatureLookup.at(ESubpass::BLUE_NOISE));
		pCmd->SetComputeRootDescriptorTable(0, mRenderer.GetSRV(SRVBlueNoisePassInputs[iBuffer]).GetGPUDescHandle());
		pCmd->SetComputeRootConstantBufferView(1, cbAddr);
		pCmd->SetPipelineState(mRenderer.GetPSO(PSOBlueNoisePass));
		const UINT DISPATCH_X = BLUE_NOISE_TEXTURE_DIM / 8;
		const UINT DISPATCH_Y = BLUE_NOISE_TEXTURE_DIM / 8;
		const UINT DISPATCH_Z = 1;
		pCmd->Dispatch(DISPATCH_X, DISPATCH_Y, DISPATCH_Z);
	}

	// gpuTimer.GetTimeStamp(pCommandList, "FFX DNSR ClassifyTiles + PrepareBlueNoise");

	// Ensure that the tile classification pass finished
	{
		D3D12_RESOURCE_BARRIER barriers[] = {
				CD3DX12_RESOURCE_BARRIER::UAV       (mRenderer.GetTextureResource(TexRayCounter)),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexRayList)                     , D3D12_RESOURCE_STATE_UNORDERED_ACCESS         , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexDenoiserTileList)            , D3D12_RESOURCE_STATE_UNORDERED_ACCESS         , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexIntersectionPassIndirectArgs), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT        , D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				CD3DX12_RESOURCE_BARRIER::Transition(pRscRoughnessExtract                                         , D3D12_RESOURCE_STATE_UNORDERED_ACCESS         , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexReflectionDenoiserBlueNoise) , D3D12_RESOURCE_STATE_UNORDERED_ACCESS         , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		};
		pCmd->ResourceBarrier(_countof(barriers), barriers);
	}

	{
		SCOPED_GPU_MARKER(pCmd, "FFX SSSR PrepareIndirectArgs");
		pCmd->SetComputeRootSignature(mSubpassRootSignatureLookup.at(ESubpass::PREPARE_INDIRECT_ARGS));
		pCmd->SetComputeRootDescriptorTable(0, mRenderer.GetUAV(UAVPrepareIndirectArgsPass[iBuffer]).GetGPUDescHandle());
		pCmd->SetPipelineState(mRenderer.GetPSO(PSOPrepareIndirectArgsPass));
		pCmd->Dispatch(1, 1, 1);
		// gpuTimer.GetTimeStamp(pCommandList, "FFX SSSR PrepareIndirectArgs");
	}

	// Ensure that the arguments are written
	{
		D3D12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexIntersectionPassIndirectArgs),	D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
		};
		pCmd->ResourceBarrier(_countof(barriers), barriers);
	}

	{
		SCOPED_GPU_MARKER(pCmd, "FFX SSSR Intersection");
		pCmd->SetComputeRootSignature(mSubpassRootSignatureLookup.at(ESubpass::INTERSECTION));
		pCmd->SetComputeRootDescriptorTable(0, mRenderer.GetSRV(SRVIntersectionInputs[iBuffer]).GetGPUDescHandle());
		pCmd->SetComputeRootDescriptorTable(1, mRenderer.GetSRV(pParams->SRVEnvironmentSpecularIrradianceCubemap).GetGPUDescHandle());
		pCmd->SetComputeRootDescriptorTable(2, mRenderer.GetSRV(pParams->SRVBRDFIntegrationLUT).GetGPUDescHandle());
		pCmd->SetComputeRootConstantBufferView(3, cbAddr);
		pCmd->SetPipelineState(mRenderer.GetPSO(PSOIntersectPass));
		pCmd->ExecuteIndirect(mpCommandSignature, 1, mRenderer.GetTextureResource(TexIntersectionPassIndirectArgs), 0, nullptr, 0);
		//gpuTimer.GetTimeStamp(pCommandList, "FFX SSSR Intersection");
	}
	
	constexpr bool showIntersectResult = false; // TODO: visualization mode support
	if constexpr (showIntersectResult)
	{
		// Ensure that the intersection pass is done.
		{
			D3D12_RESOURCE_BARRIER barriers[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexRadiance[iBuffer]), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			};
			pCmd->ResourceBarrier(_countof(barriers), barriers);
		}
	}
	else
	{
		// Ensure that the intersection pass is done.
		{
			D3D12_RESOURCE_BARRIER barriers[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexReprojectedRadiance) , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexAvgRadiance[iBuffer]), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexVariance[iBuffer])   , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexRadiance[iBuffer])   , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexSampleCount[iBuffer]), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			};
			pCmd->ResourceBarrier(_countof(barriers), barriers);
		}

		// Reproject pass
		{
			SCOPED_GPU_MARKER(pCmd, "FFX DNSR Reproject");
			pCmd->SetComputeRootSignature(mSubpassRootSignatureLookup.at(ESubpass::REPROJECT));
			pCmd->SetComputeRootDescriptorTable(0, mRenderer.GetSRV(SRVReprojectPassInputs[iBuffer]).GetGPUDescHandle());
			pCmd->SetComputeRootConstantBufferView(1, cbAddr);
			pCmd->SetPipelineState(mRenderer.GetPSO(PSOReprojectPass));
			pCmd->ExecuteIndirect(mpCommandSignature, 1, mRenderer.GetTextureResource(TexIntersectionPassIndirectArgs), 12, nullptr, 0);
			//gpuTimer.GetTimeStamp(pCommandList, "FFX DNSR Reproject");
		}

		// Ensure that the Reproject pass is done
		{
			D3D12_RESOURCE_BARRIER barriers[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexAvgRadiance[iBuffer])    , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexVariance[iBuffer])       , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexSampleCount[iBuffer])    , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexRadiance[1 - iBuffer])   , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexVariance[1 - iBuffer])   , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexSampleCount[1 - iBuffer]), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			};
			pCmd->ResourceBarrier(_countof(barriers), barriers);
		}

		// Prefilter pass
		{
			SCOPED_GPU_MARKER(pCmd, "FFX DNSR Prefilter");
			pCmd->SetComputeRootSignature(mSubpassRootSignatureLookup.at(ESubpass::PREFILTER));
			pCmd->SetComputeRootDescriptorTable(0, mRenderer.GetSRV(SRVPrefilterPassInputs[iBuffer]).GetGPUDescHandle());
			pCmd->SetComputeRootConstantBufferView(1, cbAddr);
			pCmd->SetPipelineState(mRenderer.GetPSO(PSOPrefilterPass));
			pCmd->ExecuteIndirect(mpCommandSignature, 1, mRenderer.GetTextureResource(TexIntersectionPassIndirectArgs), 12, nullptr, 0);
			//gpuTimer.GetTimeStamp(pCommandList, "FFX DNSR Prefilter");
		}

		// Ensure that the Prefilter pass is done
		{
			D3D12_RESOURCE_BARRIER barriers[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexRadiance[1 - iBuffer])   , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexReprojectedRadiance)     , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexVariance[1 - iBuffer])   , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexSampleCount[1 - iBuffer]), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexRadiance[iBuffer])       , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexVariance[iBuffer])       , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexSampleCount[iBuffer])    , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			};
			pCmd->ResourceBarrier(_countof(barriers), barriers);
		}

		// Temporal accumulation passes
		{
			SCOPED_GPU_MARKER(pCmd, "FFX DNSR Resolve Temporal");
			pCmd->SetComputeRootSignature(mSubpassRootSignatureLookup.at(ESubpass::RESOLVE_TEMPORAL));
			pCmd->SetComputeRootDescriptorTable(0, mRenderer.GetSRV(SRVTemporalResolveInputs[iBuffer]).GetGPUDescHandle());
			pCmd->SetComputeRootConstantBufferView(1, cbAddr);
			pCmd->SetPipelineState(mRenderer.GetPSO(PSOResolveTemporalPass));
			pCmd->ExecuteIndirect(mpCommandSignature, 1, mRenderer.GetTextureResource(TexIntersectionPassIndirectArgs), 12, nullptr, 0);
			//gpuTimer.GetTimeStamp(pCommandList, "FFX DNSR Resolve Temporal");
		}

		// Ensure that the temporal accumulation finished
		{
			D3D12_RESOURCE_BARRIER barriers[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexRadiance[iBuffer])   , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexVariance[iBuffer])   , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(TexSampleCount[iBuffer]), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			};
			pCmd->ResourceBarrier(_countof(barriers), barriers);
		}

		// Also, copy the depth buffer for the next frame. This is optional if the engine already keeps a copy around. 
		{
			SCOPED_GPU_MARKER(pCmd, "CopyHistoryTextures");
			{
				D3D12_RESOURCE_BARRIER barriers[] = {
					CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthHierarchy  , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(pRscNormals         , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthHistory    , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
					CD3DX12_RESOURCE_BARRIER::Transition(pRscNormalsHistory  , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
					CD3DX12_RESOURCE_BARRIER::Transition(pRscRoughnessHistory, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
					CD3DX12_RESOURCE_BARRIER::Transition(pRscRoughnessExtract, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
				};
				pCmd->ResourceBarrier(_countof(barriers), barriers);
			}

			CopyToTexture(pCmd, pRscDepthHierarchy  , pRscDepthHistory    , W, H);
			CopyToTexture(pCmd, pRscNormals         , pRscNormalsHistory  , W, H);
			CopyToTexture(pCmd, pRscRoughnessExtract, pRscRoughnessHistory, W, H);

			{
				D3D12_RESOURCE_BARRIER barriers[] = {
					CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthHierarchy  , D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS/*D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE*/),
					CD3DX12_RESOURCE_BARRIER::Transition(pRscNormals         , D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(pRscRoughnessExtract, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthHistory    , D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(pRscNormalsHistory  , D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(pRscRoughnessHistory, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				};
				pCmd->ResourceBarrier(_countof(barriers), barriers);
			}
		}
	}

	iBuffer = 1 - iBuffer;
	MatPreviousViewProjection = pParams->ffxCBuffer.view * pParams->ffxCBuffer.projection;
}


std::vector<FPSOCreationTaskParameters> ScreenSpaceReflectionsPass::CollectPSOCreationParameters() /*override*/
{
	const bool bUseFP16Types = mRenderer.GetDeviceCapabilities().bSupportsFP16;
	if (!bUseFP16Types)
	{
		Log::Warning("------------------------------------------------------------------------------------------");
		Log::Warning("ScreenSpaceReflectionsPass / FidelityFX SSSR requires Native16BitShaderOps, but the current GPU doesn't support it.");
		Log::Warning("Reflections may look incorrect or buggy.");
		Log::Warning("------------------------------------------------------------------------------------------");
	}

	std::vector<FPSOCreationTaskParameters> PSODescs;
	{
		const std::wstring ShaderFilePath = VQRenderer::GetFullPathOfShader("ScreenSpaceReflections/ClassifyReflectionTiles.hlsl");
		FPSODesc psoLoadDesc = {}; //"-enable-16bit-types -T cs_6_2 /Zi /Zss"
		psoLoadDesc.PSOName = "[PSO] Reflection Denoiser - ClassifyReflectionTiles";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain", "cs_6_2", {}, bUseFP16Types });
		psoLoadDesc.D3D12ComputeDesc.pRootSignature = mSubpassRootSignatureLookup.at(ESubpass::CLASSIFY_TILES);
		psoLoadDesc.D3D12ComputeDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		psoLoadDesc.D3D12ComputeDesc.NodeMask = 0;
		PSODescs.push_back(FPSOCreationTaskParameters{ &PSOClassifyTilesPass, psoLoadDesc });
	}
	{
		const std::wstring ShaderFilePath = VQRenderer::GetFullPathOfShader("ScreenSpaceReflections/PrepareBlueNoiseTexture.hlsl");
		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "[PSO] Reflection Denoiser - Prepare Blue Noise Texture";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain", "cs_6_2", {}, bUseFP16Types });
		psoLoadDesc.D3D12ComputeDesc.pRootSignature = mSubpassRootSignatureLookup.at(ESubpass::BLUE_NOISE);
		psoLoadDesc.D3D12ComputeDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		psoLoadDesc.D3D12ComputeDesc.NodeMask = 0;
		PSODescs.push_back(FPSOCreationTaskParameters{ &PSOBlueNoisePass, psoLoadDesc });
	}
	{
		const std::wstring ShaderFilePath = VQRenderer::GetFullPathOfShader("ScreenSpaceReflections/Reproject.hlsl");
		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "[PSO] Reflection Denoiser - Reproject";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain", "cs_6_2", {}, bUseFP16Types });
		psoLoadDesc.D3D12ComputeDesc.pRootSignature = mSubpassRootSignatureLookup.at(ESubpass::REPROJECT);
		psoLoadDesc.D3D12ComputeDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		psoLoadDesc.D3D12ComputeDesc.NodeMask = 0;
		PSODescs.push_back(FPSOCreationTaskParameters{ &PSOReprojectPass, psoLoadDesc });
	}
	{
		const std::wstring ShaderFilePath = VQRenderer::GetFullPathOfShader("ScreenSpaceReflections/Prefilter.hlsl");
		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "[PSO] Reflection Denoiser - Prefilter";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain", "cs_6_2", {}, bUseFP16Types });
		psoLoadDesc.D3D12ComputeDesc.pRootSignature = mSubpassRootSignatureLookup.at(ESubpass::PREFILTER);
		psoLoadDesc.D3D12ComputeDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		psoLoadDesc.D3D12ComputeDesc.NodeMask = 0;
		PSODescs.push_back(FPSOCreationTaskParameters{ &PSOPrefilterPass, psoLoadDesc });
	}
	{
		const std::wstring ShaderFilePath = VQRenderer::GetFullPathOfShader("ScreenSpaceReflections/ResolveTemporal.hlsl");
		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "[PSO] Reflection Denoiser - Temporal Resolve";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain", "cs_6_2", {}, bUseFP16Types });
		psoLoadDesc.D3D12ComputeDesc.pRootSignature = mSubpassRootSignatureLookup.at(ESubpass::RESOLVE_TEMPORAL);
		psoLoadDesc.D3D12ComputeDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		psoLoadDesc.D3D12ComputeDesc.NodeMask = 0;
		PSODescs.push_back(FPSOCreationTaskParameters{ &PSOResolveTemporalPass, psoLoadDesc });
	}
	{
		const std::wstring ShaderFilePath = VQRenderer::GetFullPathOfShader("ScreenSpaceReflections/Intersect.hlsl");
		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "[PSO] SSSR - Intersection";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain", "cs_6_2", {}, bUseFP16Types });
		psoLoadDesc.D3D12ComputeDesc.pRootSignature = mSubpassRootSignatureLookup.at(ESubpass::INTERSECTION);
		psoLoadDesc.D3D12ComputeDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		psoLoadDesc.D3D12ComputeDesc.NodeMask = 0;
		PSODescs.push_back(FPSOCreationTaskParameters{ &PSOIntersectPass, psoLoadDesc });
	}
	{
		const std::wstring ShaderFilePath = VQRenderer::GetFullPathOfShader("ScreenSpaceReflections/PrepareIndirectArgs.hlsl");
		FPSODesc psoLoadDesc = {};
		psoLoadDesc.PSOName = "[PSO] PrepareIndirectArgs";
		psoLoadDesc.ShaderStageCompileDescs.push_back(FShaderStageCompileDesc{ ShaderFilePath, "CSMain", "cs_6_2", {}, bUseFP16Types });
		psoLoadDesc.D3D12ComputeDesc.pRootSignature = mSubpassRootSignatureLookup.at(ESubpass::PREPARE_INDIRECT_ARGS);
		psoLoadDesc.D3D12ComputeDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		psoLoadDesc.D3D12ComputeDesc.NodeMask = 0;
		PSODescs.push_back(FPSOCreationTaskParameters{ &PSOPrepareIndirectArgsPass, psoLoadDesc });
	}
	return PSODescs;
}

SRV_ID ScreenSpaceReflectionsPass::GetPassOutputSRV(int iOutput) const
{
	// once the commands are recorded, ibuffer will flip. 
	// This assumes is called after RecordCommands() GetPassOutputSRV(), hence ^1
	return SRVScreenSpaceReflectionOutput[iBuffer ^ 1]; 
}

void ScreenSpaceReflectionsPass::ClearHistoryBuffers(ID3D12GraphicsCommandList* pCmd)
{
	// TODO: fix dx12/dxgi errors with UAV clearing
	UINT clear[4] = { 0,0,0,0 };
	for (int i = 0; i < 2; ++i)
	{
		const UAV& uav = mRenderer.GetUAV(UAVTemporalResolveOutputs[i]);
		std::vector<ID3D12Resource*> pRsc = 
		{
			mRenderer.GetTextureResource(TexRadiance[i])
			, mRenderer.GetTextureResource(TexVariance[i])
			, mRenderer.GetTextureResource(TexSampleCount[i])
		};
		for (int rsc = 0; rsc < pRsc.size(); ++rsc)
		{
			pCmd->ClearUnorderedAccessViewUint(uav.GetGPUDescHandle(rsc), uav.GetCPUDescHandle(rsc), pRsc[rsc], clear, 0, nullptr);
		}
	}

	for (int i = 0; i < 2; ++i)
	{
		const UAV& uav = mRenderer.GetUAV(UAVReprojectPassOutputs[0]);
		pCmd->ClearUnorderedAccessViewUint(uav.GetGPUDescHandle(1), uav.GetCPUDescHandle(1), mRenderer.GetTextureResource(TexAvgRadiance[i]), clear, 0, nullptr);
		if(i == 0)
		{
			pCmd->ClearUnorderedAccessViewUint(uav.GetGPUDescHandle(), uav.GetCPUDescHandle(), mRenderer.GetTextureResource(TexReprojectedRadiance), clear, 0, nullptr);
		}
	}
}





// ==============================================================================================================================
// ==============================================================================================================================
// ==============================================================================================================================

std::vector<TextureID*> ScreenSpaceReflectionsPass::GetWindowSizeDependentDenoisingTextures()
{
	return {
	  &TexExtractedRoughness
	, &TexDepthHistory
	, &TexNormalsHistory
	, &TexRoughnessHistory
	, &TexRadiance[0]
	, &TexRadiance[1]
	, &TexVariance[0]
	, &TexVariance[1]
	, &TexSampleCount[0]
	, &TexSampleCount[1]
	, &TexAvgRadiance[0]
	, &TexAvgRadiance[1]
	, &TexReprojectedRadiance
	};
}

inline D3D12_STATIC_SAMPLER_DESC InitLinearSampler(uint32_t shader_register)
{
	D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	samplerDesc.MinLOD = 0.0f;
	samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	samplerDesc.MipLODBias = 0;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.ShaderRegister = shader_register;
	samplerDesc.RegisterSpace = 0;
	samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; // Compute
	return samplerDesc;
}
void ScreenSpaceReflectionsPass::LoadRootSignatures()
{
	ID3D12Device* pDevice = mRenderer.GetDevicePtr();
	// TODO: register RSs with the Renderer??

	D3D12_STATIC_SAMPLER_DESC samplerDescs[] = { InitLinearSampler(0) }; // g_linear_sampler || used as EnvMapSampler

	//==============================RootSignature ClassifyReflectionTilesPass============================================
	{
		const UINT srvCount = 4;
		const UINT uavCount = 5;

		CD3DX12_ROOT_PARAMETER RTSlot[4] = {};

		int parameterCount = 0;
		CD3DX12_DESCRIPTOR_RANGE DescRange_3[1] = {};
		CD3DX12_DESCRIPTOR_RANGE DescRange_1[2] = {};
		CD3DX12_DESCRIPTOR_RANGE DescRange_2[1] = {};
		{
			//Param 0
			int rangeCount = 0;
			DescRange_1[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvCount, 0, 0, 0);
			DescRange_1[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, uavCount, 0, 0, srvCount);
			RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange_1[0], D3D12_SHADER_VISIBILITY_ALL);
		}

		// Param 1
		{
			int rangeCount = 0;
			int space = 1;
			DescRange_3[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, space, 0);
			RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange_3[0], D3D12_SHADER_VISIBILITY_ALL);
		}		
		
		// Param 2
		{
			int rangeCount = 0;
			int space = 1;
			DescRange_2[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6, space, 0);
			RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange_2[0], D3D12_SHADER_VISIBILITY_ALL);
		}

		//Param 3
		RTSlot[parameterCount++].InitAsConstantBufferView(0);

		D3D12_STATIC_SAMPLER_DESC samplerDescs[] = { InitLinearSampler(0) }; // g_linear_sampler
		CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
		descRootSignature.NumParameters = parameterCount;
		descRootSignature.pParameters = RTSlot;
		descRootSignature.NumStaticSamplers = _countof(samplerDescs);
		descRootSignature.pStaticSamplers = samplerDescs;
		// deny uneccessary access to certain pipeline stages
		descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		ID3DBlob* pOutBlob = nullptr;
		ID3DBlob* pErrorBlob = nullptr;
		HRESULT hr = D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob);
		ThrowIfFailed(
			pDevice->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&mSubpassRootSignatureLookup[ESubpass::CLASSIFY_TILES]))
		);
		SetName(mSubpassRootSignatureLookup.at(ESubpass::CLASSIFY_TILES), "Reflection Denoiser - ClassifyTiles Rootsignature");

		pOutBlob->Release();
		if (pErrorBlob)
			pErrorBlob->Release();
	}
	//==============================RootSignature BlueNoisePass============================================
	{
		const UINT srvCount = 3;
		const UINT uavCount = 1;

		CD3DX12_ROOT_PARAMETER RTSlot[3] = {};

		int parameterCount = 0;
		CD3DX12_DESCRIPTOR_RANGE DescRange[3] = {};
		{
			//Param 0
			int rangeCount = 0;
			DescRange[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvCount, 0, 0, 0);
			DescRange[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, uavCount, 0, 0, srvCount);
			RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange[0], D3D12_SHADER_VISIBILITY_ALL);
		}
		//Param 1
		RTSlot[parameterCount++].InitAsConstantBufferView(0);

		CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
		descRootSignature.NumParameters = parameterCount;
		descRootSignature.pParameters = RTSlot;
		descRootSignature.NumStaticSamplers = 0;
		descRootSignature.pStaticSamplers = nullptr;
		// deny uneccessary access to certain pipeline stages   
		descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		ID3DBlob* pOutBlob = nullptr;
		ID3DBlob* pErrorBlob = nullptr;
		ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
		ThrowIfFailed(
			pDevice->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&mSubpassRootSignatureLookup[ESubpass::BLUE_NOISE]))
		);
		SetName(mSubpassRootSignatureLookup[ESubpass::BLUE_NOISE], "Reflection Denoiser - Prepare Blue Noise Texture Root Signature");

		pOutBlob->Release();
		if (pErrorBlob)
			pErrorBlob->Release();
	}
	//==============================RootSignature ReprojectPass============================================
	{
		const UINT srvCount = 14;
		const UINT uavCount = 4;

		CD3DX12_ROOT_PARAMETER RTSlot[3] = {};

		int parameterCount = 0;
		CD3DX12_DESCRIPTOR_RANGE DescRange[3] = {};
		{
			//Param 0
			int rangeCount = 0;
			DescRange[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvCount, 0, 0, 0);
			DescRange[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, uavCount, 0, 0, srvCount);
			RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange[0], D3D12_SHADER_VISIBILITY_ALL);
		}
		//Param 1
		RTSlot[parameterCount++].InitAsConstantBufferView(0);

		D3D12_STATIC_SAMPLER_DESC samplerDescs[] = { InitLinearSampler(0) }; // g_linear_sampler

		CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
		descRootSignature.NumParameters = parameterCount;
		descRootSignature.pParameters = RTSlot;
		descRootSignature.NumStaticSamplers = _countof(samplerDescs);
		descRootSignature.pStaticSamplers = samplerDescs;
		// deny uneccessary access to certain pipeline stages   
		descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		ID3DBlob* pOutBlob = nullptr;
		ID3DBlob* pErrorBlob = nullptr;
		ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
		ThrowIfFailed(
			pDevice->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&mSubpassRootSignatureLookup[ESubpass::REPROJECT]))
		);
		SetName(mSubpassRootSignatureLookup[ESubpass::REPROJECT], "Reflection Denoiser - Reproject Root Signature");

		pOutBlob->Release();
		if (pErrorBlob)
			pErrorBlob->Release();
	}
	//==============================RootSignature PrefilterPass============================================
	{
		const UINT srvCount = 8;
		const UINT uavCount = 3;
		CD3DX12_ROOT_PARAMETER RTSlot[2] = {};

		int parameterCount = 0;
		CD3DX12_DESCRIPTOR_RANGE DescRange[2] = {};
		{
			//Param 0
			int rangeCount = 0;
			DescRange[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvCount, 0, 0, 0);
			DescRange[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, uavCount, 0, 0, srvCount);
			RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange[0], D3D12_SHADER_VISIBILITY_ALL);
		}
		//Param 1
		RTSlot[parameterCount++].InitAsConstantBufferView(0);

		D3D12_STATIC_SAMPLER_DESC samplerDescs[] = { InitLinearSampler(0) }; // g_linear_sampler

		CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
		descRootSignature.NumParameters = parameterCount;
		descRootSignature.pParameters = RTSlot;
		descRootSignature.NumStaticSamplers = _countof(samplerDescs);
		descRootSignature.pStaticSamplers = samplerDescs;
		// deny uneccessary access to certain pipeline stages   
		descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		ID3DBlob* pOutBlob = nullptr;
		ID3DBlob* pErrorBlob = nullptr;
		ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
		ThrowIfFailed(
			pDevice->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&mSubpassRootSignatureLookup[ESubpass::PREFILTER]))
		);
		SetName(mSubpassRootSignatureLookup[ESubpass::PREFILTER], "Reflection Denoiser - Prefilter Root Signature");

		pOutBlob->Release();
		if (pErrorBlob)
			pErrorBlob->Release();
	}
	//==============================RootSignature ResolveTemporalPass============================================
	{
		const UINT srvCount = 7;
		const UINT uavCount = 3;

		CD3DX12_ROOT_PARAMETER RTSlot[3] = {};

		int parameterCount = 0;
		CD3DX12_DESCRIPTOR_RANGE DescRange[3] = {};
		{
			//Param 0
			int rangeCount = 0;
			DescRange[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvCount, 0, 0, 0);
			DescRange[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, uavCount, 0, 0, srvCount);
			RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange[0], D3D12_SHADER_VISIBILITY_ALL);
		}
		//Param 1
		RTSlot[parameterCount++].InitAsConstantBufferView(0);

		CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
		descRootSignature.NumParameters = parameterCount;
		descRootSignature.pParameters = RTSlot;
		descRootSignature.NumStaticSamplers = _countof(samplerDescs);
		descRootSignature.pStaticSamplers = samplerDescs;
		// deny uneccessary access to certain pipeline stages   
		descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		ID3DBlob* pOutBlob = nullptr;
		ID3DBlob* pErrorBlob = nullptr;
		ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
		ThrowIfFailed(
			pDevice->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&mSubpassRootSignatureLookup[ESubpass::RESOLVE_TEMPORAL]))
		);
		SetName(mSubpassRootSignatureLookup[ESubpass::RESOLVE_TEMPORAL], "Reflection Denoiser - Temporal Resolve Root Signature");

		pOutBlob->Release();
		if (pErrorBlob)
			pErrorBlob->Release();
	}
	//==============================RootSignature IntersectionPass============================================
	{
		const UINT srvCount = 6;
		const UINT uavCount = 2;

		CD3DX12_ROOT_PARAMETER RTSlot[4] = {};

		int parameterCount = 0;
		CD3DX12_DESCRIPTOR_RANGE DescRange_1[3] = {};
		CD3DX12_DESCRIPTOR_RANGE DescRange_2[1] = {};
		CD3DX12_DESCRIPTOR_RANGE DescRange_3[1] = {};
		CD3DX12_DESCRIPTOR_RANGE DescRange_4[1] = {};
		{
			//Param 0
			int rangeCount = 0;
			DescRange_1[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvCount, 0, 0, 0);
			DescRange_1[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, uavCount, 0, 0, srvCount);
			RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange_1[0], D3D12_SHADER_VISIBILITY_ALL);
		}
		
		// Param 1
		{
			int rangeCount = 0;
			int space = 1;
			DescRange_3[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, space, 0);
			RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange_3[0], D3D12_SHADER_VISIBILITY_ALL);
		}
		// Param 2
		{
			int rangeCount = 0;
			int space = 1;
			DescRange_4[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, space, 0);
			RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange_4[0], D3D12_SHADER_VISIBILITY_ALL);
		}

		//Param 3
		RTSlot[parameterCount++].InitAsConstantBufferView(0);
		
		CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
		descRootSignature.NumParameters = parameterCount;
		descRootSignature.pParameters = RTSlot;
		descRootSignature.NumStaticSamplers = _countof(samplerDescs);
		descRootSignature.pStaticSamplers = samplerDescs;
		// deny uneccessary access to certain pipeline stages   
		descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		ID3DBlob* pOutBlob = nullptr;
		ID3DBlob* pErrorBlob = nullptr;
		ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
		ThrowIfFailed(
			pDevice->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&mSubpassRootSignatureLookup[ESubpass::INTERSECTION]))
		);
		SetName(mSubpassRootSignatureLookup[ESubpass::INTERSECTION], "SSSR - Intersection Root Signature");

		pOutBlob->Release();
		if (pErrorBlob)
			pErrorBlob->Release();
	}
	//==============================RootSignature SetupPrepareIndirectArgsPass============================================
	{
		const UINT srvCount = 0;
		const UINT uavCount = 2;

		CD3DX12_ROOT_PARAMETER RTSlot[1] = {};

		int parameterCount = 0;
		CD3DX12_DESCRIPTOR_RANGE DescRange[1] = {};
		{
			int rangeCount = 0;
			DescRange[rangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, uavCount, 0, 0, srvCount);
			RTSlot[parameterCount++].InitAsDescriptorTable(rangeCount, &DescRange[0], D3D12_SHADER_VISIBILITY_ALL);
		}

		CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
		descRootSignature.NumParameters = parameterCount;
		descRootSignature.pParameters = RTSlot;
		descRootSignature.NumStaticSamplers = 0;
		descRootSignature.pStaticSamplers = nullptr;
		descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		ID3DBlob* pOutBlob = nullptr;
		ID3DBlob* pErrorBlob = nullptr;
		ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
		ThrowIfFailed(
			pDevice->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&mSubpassRootSignatureLookup[ESubpass::PREPARE_INDIRECT_ARGS]))
		);
		SetName(mSubpassRootSignatureLookup[ESubpass::PREPARE_INDIRECT_ARGS], "PrepareIndirectArgs Rootsignature");

		pOutBlob->Release();
		if (pErrorBlob)
			pErrorBlob->Release();
	}
}

void ScreenSpaceReflectionsPass::CreateResources()
{
	//==============================Create Tile Classification-related buffers============================================
	{
		const unsigned NumCounters = 4; // ray and tile counters, 4 in total
		TextureCreateDesc desc("FFX_SSSR - Ray Counter");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Buffer(NumCounters * ELEMENT_BYTE_SIZE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS); // 4B - single uint variable
		desc.ResourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		TexRayCounter = mRenderer.CreateTexture(desc);
	}
	//==============================Create PrepareIndirectArgs-related buffers============================================
	{
		TextureCreateDesc desc("FFX_SSSR - Intersect Indirect Args");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Buffer(6ull * ELEMENT_BYTE_SIZE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		desc.ResourceState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
		TexIntersectionPassIndirectArgs = mRenderer.CreateTexture(desc);
	}
	//==============================Command Signature==========================================
	{
		D3D12_INDIRECT_ARGUMENT_DESC dispatch = {};
		dispatch.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

		D3D12_COMMAND_SIGNATURE_DESC desc = {};
		desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
		desc.NodeMask = 0;
		desc.NumArgumentDescs = 1;
		desc.pArgumentDescs = &dispatch;

		ThrowIfFailed(mRenderer.GetDevicePtr()->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&mpCommandSignature)));
	}
	//==============================Blue Noise buffers============================================
	{
		TextureCreateDesc desc("FFX_SSSR - Sobol Buffer");
		desc.pData = (void*)&g_blueNoiseSamplerState.sobolBuffer;
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Buffer(ELEMENT_BYTE_SIZE * _countof(g_blueNoiseSamplerState.sobolBuffer), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		TexBlueNoiseSobolBuffer = mRenderer.CreateTexture(desc);
	}
	{
		TextureCreateDesc desc("FFX_SSSR - Ranking Tile Buffer");
		desc.pData = (void*)&g_blueNoiseSamplerState.rankingTileBuffer;
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Buffer(ELEMENT_BYTE_SIZE * _countof(g_blueNoiseSamplerState.rankingTileBuffer), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		TexBlueNoiseRankingTileBuffer = mRenderer.CreateTexture(desc);
	}
	{
		TextureCreateDesc desc("FFX_SSSR - Scrambling Tile Buffer");
		desc.pData = (void*)&g_blueNoiseSamplerState.scramblingTileBuffer;
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Buffer(ELEMENT_BYTE_SIZE * _countof(g_blueNoiseSamplerState.scramblingTileBuffer), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		TexBlueNoiseScramblingTileBuffer = mRenderer.CreateTexture(desc);
	}
	{			
		TextureCreateDesc desc("Reflection Denoiser - Blue Noise Texture");
		desc.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8_UNORM, BLUE_NOISE_TEXTURE_DIM, BLUE_NOISE_TEXTURE_DIM, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		desc.ResourceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		TexReflectionDenoiserBlueNoise = mRenderer.CreateTexture(desc);
	}
}

void ScreenSpaceReflectionsPass::DestroyResources()
{
	if(mpCommandSignature)
		mpCommandSignature->Release();
	mpCommandSignature = nullptr;

	if(TexRayCounter                    != INVALID_ID) mRenderer.DestroyTexture(TexRayCounter);
	if(TexIntersectionPassIndirectArgs  != INVALID_ID) mRenderer.DestroyTexture(TexIntersectionPassIndirectArgs);
	if(TexBlueNoiseSobolBuffer          != INVALID_ID) mRenderer.DestroyTexture(TexBlueNoiseSobolBuffer);
	if(TexBlueNoiseRankingTileBuffer    != INVALID_ID) mRenderer.DestroyTexture(TexBlueNoiseRankingTileBuffer);
	if(TexBlueNoiseScramblingTileBuffer != INVALID_ID) mRenderer.DestroyTexture(TexBlueNoiseScramblingTileBuffer);
	if(TexReflectionDenoiserBlueNoise   != INVALID_ID) mRenderer.DestroyTexture(TexReflectionDenoiserBlueNoise);
}

void ScreenSpaceReflectionsPass::AllocateResourceViews()
{
	for (size_t i = 0; i < 2; i++)
	{
		SRVScreenSpaceReflectionOutput[i] = mRenderer.AllocateSRV();
		//==============================ClassifyTiles==========================================
		{
			const UINT srvCount = 4;
			const UINT uavCount = 5;
			SRVClassifyTilesInputs[i] = mRenderer.AllocateSRV(srvCount);
			UAVClassifyTilesOutputs[i] = mRenderer.AllocateUAV(uavCount);
			
			//m_pResourceViewHeaps->AllocSamplerDescriptor(1, &shaderpass.descriptorTables_Sampler[i]);
		}
		//==============================PrepareIndirectArgs==========================================
		{
			const UINT srvCount = 0;
			const UINT uavCount = 2;
			UAVPrepareIndirectArgsPass[i] = mRenderer.AllocateUAV(uavCount);
		}
		//==============================Intersection==========================================
		{
			const UINT srvCount = 6;
			const UINT uavCount = 2;
			SRVIntersectionInputs[i] = mRenderer.AllocateSRV(srvCount);
			UAVIntersectionOutputs[i] = mRenderer.AllocateUAV(uavCount);

			//m_pResourceViewHeaps->AllocSamplerDescriptor(1, &shaderpass.descriptorTables_Sampler[i]);
		}
		//==============================ResolveTemporal==========================================
		{
			const UINT srvCount = 7;
			const UINT uavCount = 3;
			SRVTemporalResolveInputs[i] = mRenderer.AllocateSRV(srvCount);
			UAVTemporalResolveOutputs[i] = mRenderer.AllocateUAV(uavCount);
		}
		//==============================Prefilter==========================================
		{
			const UINT srvCount = 8;
			const UINT uavCount = 3;
			SRVPrefilterPassInputs[i] = mRenderer.AllocateSRV(srvCount);
			UAVPrefilterPassOutputs[i] = mRenderer.AllocateUAV(uavCount);
		}
		//==============================Reproject==========================================
		{
			const UINT srvCount = 14;
			const UINT uavCount = 4;
			SRVReprojectPassInputs[i] = mRenderer.AllocateSRV(srvCount);
			UAVReprojectPassOutputs[i] = mRenderer.AllocateUAV(uavCount);
		}
		//==============================PrepareBlueNoise=============================================
		{
			const UINT srvCount = 3;
			const UINT uavCount = 1;
			SRVBlueNoisePassInputs[i] = mRenderer.AllocateSRV(srvCount);
			UAVBlueNoisePassOutputs[i] = mRenderer.AllocateUAV(uavCount);
		}
	}
}

void ScreenSpaceReflectionsPass::DeallocateResourceViews()
{
	// empty function : renderer currently allocates-and-forgets the resource view memory.
	// this should be a useful entry point once descriptor memory management is implemented.
	
	// TODO: deallocate resource views
}

void ScreenSpaceReflectionsPass::InitializeResourceViews(const FResourceParameters* pParams)
{
	assert(pParams->TexHierarchicalDepthBuffer);
	assert(pParams->TexMotionVectors);
	assert(pParams->TexNormals);
	assert(pParams->TexSceneColor);
	assert(pParams->TexSceneColorRoughness);

	for (size_t i = 0; i < 2; i++)
	{
		//==============================ClassifyTiles==========================================
		{
			mRenderer.InitializeSRV(SRVClassifyTilesInputs[i], 0, pParams->TexSceneColorRoughness    , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVClassifyTilesInputs[i], 1, pParams->TexHierarchicalDepthBuffer, DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVClassifyTilesInputs[i], 2, TexVariance[1-i]                   , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVClassifyTilesInputs[i], 3, pParams->TexNormals                , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);

			mRenderer.InitializeUAVForBuffer(UAVClassifyTilesOutputs[i], 0, TexRayList           , DXGI_FORMAT_R32_UINT);
			mRenderer.InitializeUAVForBuffer(UAVClassifyTilesOutputs[i], 1, TexRayCounter        , DXGI_FORMAT_R32_UINT);
			mRenderer.InitializeUAV         (UAVClassifyTilesOutputs[i], 2, TexRadiance[i]       );
			mRenderer.InitializeUAV         (UAVClassifyTilesOutputs[i], 3, TexExtractedRoughness);
			mRenderer.InitializeUAVForBuffer(UAVClassifyTilesOutputs[i], 4, TexDenoiserTileList  , DXGI_FORMAT_R32_UINT);
		}
		//==============================PrepareBlueNoise=============================================
		{
			mRenderer.InitializeSRVForBuffer(SRVBlueNoisePassInputs [i], 0, TexBlueNoiseSobolBuffer         , DXGI_FORMAT_R32_UINT);
			mRenderer.InitializeSRVForBuffer(SRVBlueNoisePassInputs [i], 1, TexBlueNoiseRankingTileBuffer   , DXGI_FORMAT_R32_UINT);
			mRenderer.InitializeSRVForBuffer(SRVBlueNoisePassInputs [i], 2, TexBlueNoiseScramblingTileBuffer, DXGI_FORMAT_R32_UINT);
			mRenderer.InitializeUAV(UAVBlueNoisePassOutputs[i], 0, TexReflectionDenoiserBlueNoise);
		}
		//==============================PrepareIndirectArgs==========================================
		{
			mRenderer.InitializeUAVForBuffer(UAVPrepareIndirectArgsPass[i], 0, TexRayCounter                  , DXGI_FORMAT_R32_UINT);
			mRenderer.InitializeUAVForBuffer(UAVPrepareIndirectArgsPass[i], 1, TexIntersectionPassIndirectArgs, DXGI_FORMAT_R32_UINT);
		}
		//==============================Intersection==========================================
		{
			mRenderer.InitializeSRV(SRVIntersectionInputs[i], 0, pParams->TexSceneColor             , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVIntersectionInputs[i], 1, pParams->TexHierarchicalDepthBuffer, DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVIntersectionInputs[i], 2, pParams->TexNormals                , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVIntersectionInputs[i], 3, TexExtractedRoughness              , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVIntersectionInputs[i], 4, TexReflectionDenoiserBlueNoise     , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRVForBuffer(SRVIntersectionInputs[i], 5, TexRayList                , DXGI_FORMAT_R32_UINT);

			mRenderer.InitializeUAV         (UAVIntersectionOutputs[i], 0, TexRadiance[i]);
			mRenderer.InitializeUAVForBuffer(UAVIntersectionOutputs[i], 1, TexRayCounter, DXGI_FORMAT_R32_UINT);

			//m_pDevice->GetDevice()->CreateSampler(&m_environmentMapSamplerDesc, table_sampler.GetCPU(0));
		}
		//==============================Reproject==========================================
		{
			mRenderer.InitializeSRV(SRVReprojectPassInputs[i], 0 , pParams->TexHierarchicalDepthBuffer, DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVReprojectPassInputs[i], 1 , TexExtractedRoughness    , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVReprojectPassInputs[i], 2 , pParams->TexNormals      , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVReprojectPassInputs[i], 3 , TexDepthHistory          , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVReprojectPassInputs[i], 4 , TexRoughnessHistory      , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVReprojectPassInputs[i], 5 , TexNormalsHistory        , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVReprojectPassInputs[i], 6 , TexRadiance[i]           , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVReprojectPassInputs[i], 7 , TexRadiance[1-i]         , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVReprojectPassInputs[i], 8 , pParams->TexMotionVectors, DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVReprojectPassInputs[i], 9 , TexAvgRadiance[1-i]      , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVReprojectPassInputs[i], 10, TexVariance[1-i]         , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVReprojectPassInputs[i], 11, TexSampleCount[1-i]      , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVReprojectPassInputs[i], 12, TexReflectionDenoiserBlueNoise, DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRVForBuffer(SRVReprojectPassInputs[i], 13, TexDenoiserTileList  , DXGI_FORMAT_R32_UINT);

			mRenderer.InitializeUAV(UAVReprojectPassOutputs[i], 0, TexReprojectedRadiance);
			mRenderer.InitializeUAV(UAVReprojectPassOutputs[i], 1, TexAvgRadiance[i]);
			mRenderer.InitializeUAV(UAVReprojectPassOutputs[i], 2, TexVariance[i]);
			mRenderer.InitializeUAV(UAVReprojectPassOutputs[i], 3, TexSampleCount[i]);
		}
		//==============================Prefilter==========================================
		{
			mRenderer.InitializeSRV(SRVPrefilterPassInputs[i], 0, pParams->TexHierarchicalDepthBuffer, DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVPrefilterPassInputs[i], 1, TexExtractedRoughness              , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVPrefilterPassInputs[i], 2, pParams->TexNormals                , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVPrefilterPassInputs[i], 3, TexAvgRadiance[i]                  , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVPrefilterPassInputs[i], 4, TexRadiance[i]                     , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVPrefilterPassInputs[i], 5, TexVariance[i]                     , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVPrefilterPassInputs[i], 6, TexSampleCount[i]                  , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRVForBuffer(SRVPrefilterPassInputs[i], 7, TexDenoiserTileList       , DXGI_FORMAT_R32_UINT);

			mRenderer.InitializeUAV(UAVPrefilterPassOutputs[i], 0, TexRadiance[1-i]);
			mRenderer.InitializeUAV(UAVPrefilterPassOutputs[i], 1, TexVariance[1-i]);
			mRenderer.InitializeUAV(UAVPrefilterPassOutputs[i], 2, TexSampleCount[1-i]);
		}
		//==============================ResolveTemporal==========================================
		{
			mRenderer.InitializeSRV(SRVTemporalResolveInputs[i], 0, TexExtractedRoughness , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVTemporalResolveInputs[i], 1, TexAvgRadiance[i]     , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVTemporalResolveInputs[i], 2, TexRadiance[1-i]      , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVTemporalResolveInputs[i], 3, TexReprojectedRadiance, DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVTemporalResolveInputs[i], 4, TexVariance[1-i]      , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRV(SRVTemporalResolveInputs[i], 5, TexSampleCount[1-i]   , DONT_USE_ARRAY_VIEW, DONT_USE_CUBEMAP_VIEW);
			mRenderer.InitializeSRVForBuffer(SRVTemporalResolveInputs[i], 6, TexDenoiserTileList, DXGI_FORMAT_R32_UINT);

			mRenderer.InitializeUAV(UAVTemporalResolveOutputs[i], 0, TexRadiance[i]);
			mRenderer.InitializeUAV(UAVTemporalResolveOutputs[i], 1, TexVariance[i]);
			mRenderer.InitializeUAV(UAVTemporalResolveOutputs[i], 2, TexSampleCount[i]);
		}
		//==============================Output ResourceViews==========================================
		{
			mRenderer.InitializeSRV(SRVScreenSpaceReflectionOutput[i], 0, TexRadiance[i]);
		}
	}
}
