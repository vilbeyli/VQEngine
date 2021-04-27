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
#pragma once

#include "RenderPass.h"

// #define FFX_CACAO_ENABLE_PROFILING 1
// #define FFX_CACAO_ENABLE_NATIVE_RESOLUTION 1 
#include "FidelityFX/CACAO/ffx_cacao.h"

#include <DirectXMath.h>


struct FAmbientOcclusionPass : public IRenderPass
{
	enum EMethod
	{
		FFX_CACAO = 0,
		// TODO: do we want to bring back vq-dx11 ssao?

		NUM_AMBIENT_OCCLUSION_METHODS
	};

	struct FResourceParameters
	{
		ID3D12Resource* pRscNormalBuffer = nullptr;
		ID3D12Resource* pRscDepthBuffer  = nullptr;
		ID3D12Resource* pRscOutput       = nullptr;
		DXGI_FORMAT     FmtNormalBuffer = DXGI_FORMAT_UNKNOWN;
		DXGI_FORMAT     FmtDepthBuffer  = DXGI_FORMAT_UNKNOWN;
		DXGI_FORMAT     FmtOutput       = DXGI_FORMAT_UNKNOWN;
	};
	struct FDrawParameters
	{
		ID3D12GraphicsCommandList* pCmd = nullptr;
		DirectX::XMFLOAT4X4 matProj;
		DirectX::XMFLOAT4X4 matNormalToView;
	};

	//------------------------------------------------------

	FAmbientOcclusionPass(EMethod InMethod);

	bool Initialize(ID3D12Device* pDevice) override;
	void Exit() override;

	void OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const void* pRscParameters = nullptr) override;
	void OnDestroyWindowSizeDependentResources() override;

	void RecordCommands(const void* pDrawParameters = nullptr) override;

	//------------------------------------------------------

	EMethod Method;
	FfxCacaoSettings AOSettings;
};
