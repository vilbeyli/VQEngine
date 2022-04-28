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
#include "AMDFidelityFX/CACAO/ffx_cacao_impl.h"

#include "Libs/VQUtils/Source/Log.h"

#include "../../Renderer/Libs/D3DX12/d3dx12.h"
#include "../../Renderer/Renderer.h"

#include <cassert>

static FFX_CACAO_D3D12Context* pFFX_CACAO_Context = nullptr; // assumes only 1 CACAO context;

static void LogFFX_CACAO_Status(FFX_CACAO_Status hr, const std::string& functionName)
{
}

AmbientOcclusionPass::AmbientOcclusionPass(VQRenderer& Renderer, EMethod InMethod)
	: RenderPassBase(Renderer)
	, Method(InMethod)
	, AOSettings(FFX_CACAO_DEFAULT_SETTINGS)
{
}

AmbientOcclusionPass::~AmbientOcclusionPass()
{
}

bool AmbientOcclusionPass::Initialize()
{
	pFFX_CACAO_Context = nullptr;
	const size_t SIZE_CONTEXT = FFX_CACAO_D3D12GetContextSize();

	pFFX_CACAO_Context = (FFX_CACAO_D3D12Context*)malloc(SIZE_CONTEXT);
	FFX_CACAO_Status hr = FFX_CACAO_D3D12InitContext(pFFX_CACAO_Context, mRenderer.GetDevicePtr());
	
	bool bResult = false;
	switch (hr)
	{
	case FFX_CACAO_STATUS_OK:
		Log::Info("FFX-CACAO Context initialized.");
		FFX_CACAO_D3D12UpdateSettings(pFFX_CACAO_Context, &AOSettings);
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

void AmbientOcclusionPass::Destroy()
{
	FFX_CACAO_Status hr = FFX_CACAO_D3D12DestroyContext(pFFX_CACAO_Context);
	free(pFFX_CACAO_Context);
}

void AmbientOcclusionPass::OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const IRenderPassResourceCollection* pRscParameters /*= nullptr*/)
{
	const FResourceParameters* pParams = static_cast<const FResourceParameters*>(pRscParameters);
	
	assert(pFFX_CACAO_Context);
	assert(pParams);
	assert(pParams->pRscNormalBuffer);
	assert(pParams->pRscOutput);
	assert(pParams->pRscDepthBuffer);

	FFX_CACAO_D3D12ScreenSizeInfo iScreenSize = {};
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

	FFX_CACAO_Status hr = FFX_CACAO_D3D12InitScreenSizeDependentResources(pFFX_CACAO_Context, &iScreenSize);

	if (hr != FFX_CACAO_STATUS_OK)
	{
		Log::Error("AmbientOcclusionPass::OnCreateWindowSizeDependentResources(): error FFX_CACAO_D3D12InitScreenSizeDependentResources()");
	}
}

void AmbientOcclusionPass::OnDestroyWindowSizeDependentResources()
{
	FFX_CACAO_Status hr = FFX_CACAO_D3D12DestroyScreenSizeDependentResources(pFFX_CACAO_Context);

	if (hr != FFX_CACAO_STATUS_OK)
	{
		Log::Error("AmbientOcclusionPass::OnDestroyWindowSizeDependentResources(): error FFX_CACAO_D3D12DestroyScreenSizeDependentResources()");
	}
}

void AmbientOcclusionPass::RecordCommands(const IRenderPassDrawParameters* pDrawParameters /*= nullptr*/)
{
	const FDrawParameters* pParameters = static_cast<const FDrawParameters*>(pDrawParameters);
	using namespace DirectX;


	FFX_CACAO_Matrix4x4 proj;
	FFX_CACAO_Matrix4x4 normalsWorldToView;
#if 0
	// TODO: reinterpret?
	memcpy(&proj, &pParameters->matProj, sizeof(float) * 16);
	memcpy(&normalToView, &pParameters->matProj, sizeof(float) * 16); // TODO
#else
	{
		const XMFLOAT4X4& p = pParameters->matProj;
		proj.elements[0][0] = p._11; proj.elements[0][1] = p._12; proj.elements[0][2] = p._13; proj.elements[0][3] = p._14;
		proj.elements[1][0] = p._21; proj.elements[1][1] = p._22; proj.elements[1][2] = p._23; proj.elements[1][3] = p._24;
		proj.elements[2][0] = p._31; proj.elements[2][1] = p._32; proj.elements[2][2] = p._33; proj.elements[2][3] = p._34;
		proj.elements[3][0] = p._41; proj.elements[3][1] = p._42; proj.elements[3][2] = p._43; proj.elements[3][3] = p._44;
	}
	{
		XMFLOAT4X4 p;
		XMMATRIX xView = XMLoadFloat4x4(&pParameters->matNormalToView);
		XMVECTOR Det = XMMatrixDeterminant(xView);
		XMMATRIX xNormalsWorldToView = XMMATRIX(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1) * XMMatrixInverse(&Det, xView); // should be transpose(inverse(view)), but XMM is row-major and HLSL is column-major
		//xNormalsWorldToView = XMMatrixTranspose(xNormalsWorldToView);
		XMStoreFloat4x4(&p, xNormalsWorldToView);

		normalsWorldToView.elements[0][0] = p._11; normalsWorldToView.elements[0][1] = p._12; normalsWorldToView.elements[0][2] = p._13; normalsWorldToView.elements[0][3] = p._14;
		normalsWorldToView.elements[1][0] = p._21; normalsWorldToView.elements[1][1] = p._22; normalsWorldToView.elements[1][2] = p._23; normalsWorldToView.elements[1][3] = p._24;
		normalsWorldToView.elements[2][0] = p._31; normalsWorldToView.elements[2][1] = p._32; normalsWorldToView.elements[2][2] = p._33; normalsWorldToView.elements[2][3] = p._34;
		normalsWorldToView.elements[3][0] = p._41; normalsWorldToView.elements[3][1] = p._42; normalsWorldToView.elements[3][2] = p._43; normalsWorldToView.elements[3][3] = p._44;
	}
#endif

	FFX_CACAO_Status hr = FFX_CACAO_D3D12Draw(pFFX_CACAO_Context, pParameters->pCmd, &proj, &normalsWorldToView);

	if (hr != FFX_CACAO_STATUS_OK)
	{
		Log::Error("AmbientOcclusionPass::RecordCommands(): error FFX_CACAO_D3D12Draw()");
	}
}

std::vector<FPSOCreationTaskParameters> AmbientOcclusionPass::CollectPSOCreationParameters()
{
	return std::vector<FPSOCreationTaskParameters>();
}
