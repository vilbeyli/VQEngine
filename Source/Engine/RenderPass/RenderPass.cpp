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

#include "AmbientOcclusion.h"

#include "Libs/VQUtils/Source/Log.h"

#include <cassert>

static FfxCacaoD3D12Context* pFFXCacaoContext = nullptr; // assumes only 1 CACAO context;

static void LogFFXCACAOStatus(FfxCacaoStatus hr, const std::string& functionName)
{
}

FAmbientOcclusionPass::FAmbientOcclusionPass(EMethod InMethod)
	: Method(InMethod)
	, AOSettings(FFX_CACAO_DEFAULT_SETTINGS)
{
}

bool FAmbientOcclusionPass::Initialize(ID3D12Device* pDevice)
{
	pFFXCacaoContext = nullptr;
	const size_t SIZE_CONTEXT = ffxCacaoD3D12GetContextSize();

	pFFXCacaoContext = (FfxCacaoD3D12Context*)malloc(SIZE_CONTEXT);
	FfxCacaoStatus hr = ffxCacaoD3D12InitContext(pFFXCacaoContext, pDevice);
	
	bool bResult = false;
	switch (hr)
	{
	case FFX_CACAO_STATUS_OK:
		Log::Info("FFX-CACAO Context initialized.");
		bResult = true;
		break;
	case FFX_CACAO_STATUS_INVALID_ARGUMENT:
		Log::Error("FFX-CACAO Context initialization failed: Inavlied Argument");
		break;
	case FFX_CACAO_STATUS_INVALID_POINTER:
		Log::Error("FFX-CACAO Context initialization failed: Inavlied Pointer");
		break;
	case FFX_CACAO_STATUS_OUT_OF_MEMORY:
		Log::Error("FFX-CACAO Context initialization failed: Out of Memory!");
		break;
	case FFX_CACAO_STATUS_FAILED:
		Log::Error("FFX-CACAO Context initialization failed");
		break;
	}

	return bResult;
}

void FAmbientOcclusionPass::Exit()
{
	FfxCacaoStatus hr = ffxCacaoD3D12DestroyContext(pFFXCacaoContext);
	free(pFFXCacaoContext);
}

void FAmbientOcclusionPass::OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, void* pRscParameters /*= nullptr*/)
{
	FResourceParameters* pParams = static_cast<FResourceParameters*>(pRscParameters);
	
	assert(pFFXCacaoContext);
	assert(pParams);
	assert(pParams->pRscNormalBuffer);
	assert(pParams->pRscOutput);
	assert(pParams->pRscDepthBuffer);

	FfxCacaoD3D12ScreenSizeInfo iScreenSize = {};
	iScreenSize.height = Height;
	iScreenSize.width = Width;

	// Normal Maps
	iScreenSize.normalBufferResource = pParams->pRscNormalBuffer;
	iScreenSize.normalBufferSrvDesc.Format = pParams->FmtNormalBuffer;
	iScreenSize.normalBufferSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	iScreenSize.normalBufferSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	iScreenSize.normalBufferSrvDesc.Texture2D.MostDetailedMip = 0;
	iScreenSize.normalBufferSrvDesc.Texture2D.MipLevels = 1;
	iScreenSize.normalBufferSrvDesc.Texture2D.PlaneSlice = 0;
	iScreenSize.normalBufferSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	// Output UAV
	iScreenSize.outputResource = pParams->pRscOutput;
	iScreenSize.outputUavDesc.Format = pParams->FmtOutput;
	iScreenSize.outputUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	iScreenSize.outputUavDesc.Texture2D.MipSlice = 0;
	iScreenSize.outputUavDesc.Texture2D.PlaneSlice = 0;

	// Depth
	iScreenSize.depthBufferResource = pParams->pRscDepthBuffer;
	iScreenSize.depthBufferSrvDesc.Format = pParams->FmtDepthBuffer;
	iScreenSize.depthBufferSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	iScreenSize.depthBufferSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	iScreenSize.depthBufferSrvDesc.Texture2D.MipLevels = 1;
	iScreenSize.depthBufferSrvDesc.Texture2D.MostDetailedMip = 0;
	iScreenSize.depthBufferSrvDesc.Texture2D.PlaneSlice = 0;
	iScreenSize.depthBufferSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	FfxCacaoStatus hr = ffxCacaoD3D12InitScreenSizeDependentResources(pFFXCacaoContext, &iScreenSize);

	if (hr != FFX_CACAO_STATUS_OK)
	{
		Log::Error("FAmbientOcclusionPass::OnCreateWindowSizeDependentResources(): error ffxCacaoD3D12InitScreenSizeDependentResources()");
	}
}

void FAmbientOcclusionPass::OnDestroyWindowSizeDependentResources()
{
	FfxCacaoStatus hr = ffxCacaoD3D12DestroyScreenSizeDependentResources(pFFXCacaoContext);

	if (hr != FFX_CACAO_STATUS_OK)
	{
		Log::Error("FAmbientOcclusionPass::OnDestroyWindowSizeDependentResources(): error ffxCacaoD3D12DestroyScreenSizeDependentResources()");
	}
}

void FAmbientOcclusionPass::RecordCommands(const void* pDrawParameters /*= nullptr*/)
{
	const FDrawParameters* pParameters = static_cast<const FDrawParameters*>(pDrawParameters);

	FfxCacaoMatrix4x4 proj         ; // TODO: init
	FfxCacaoMatrix4x4 normalToView ; // TODO: init
	FfxCacaoStatus hr = ffxCacaoD3D12Draw(pFFXCacaoContext, pParameters->pCmd, &proj, &normalToView);

	if (hr != FFX_CACAO_STATUS_OK)
	{
		Log::Error("FAmbientOcclusionPass::RecordCommands(): error ffxCacaoD3D12Draw()");
	}
}
