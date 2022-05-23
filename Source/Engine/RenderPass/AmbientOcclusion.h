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
#include "AMDFidelityFX/CACAO/ffx_cacao.h"

#include <DirectXMath.h>
#include <dxgiformat.h>

struct ID3D12Resource;
struct ID3D12GraphicsCommandList;
class AmbientOcclusionPass : public RenderPassBase
{
public:
	enum EMethod
	{
		FFX_CACAO = 0,
		// RayTracedAO,
		// TODO: do we want to bring back vq-dx11 ssao?

		NUM_AMBIENT_OCCLUSION_METHODS
	};

	struct FResourceParameters : public IRenderPassResourceCollection
	{
		ID3D12Resource* pRscNormalBuffer = nullptr;
		ID3D12Resource* pRscDepthBuffer  = nullptr;
		ID3D12Resource* pRscOutput       = nullptr;
		DXGI_FORMAT     FmtNormalBuffer = DXGI_FORMAT_UNKNOWN;
		DXGI_FORMAT     FmtDepthBuffer  = DXGI_FORMAT_UNKNOWN;
		DXGI_FORMAT     FmtOutput       = DXGI_FORMAT_UNKNOWN;
	};
	struct FDrawParameters : public IRenderPassDrawParameters
	{
		ID3D12GraphicsCommandList* pCmd = nullptr;
		DirectX::XMFLOAT4X4 matProj;
		DirectX::XMFLOAT4X4 matNormalToView;
	};

	//------------------------------------------------------

	AmbientOcclusionPass(VQRenderer& Renderer, EMethod InMethod);
	AmbientOcclusionPass() = delete;
	AmbientOcclusionPass(const AmbientOcclusionPass&) = delete;
	virtual ~AmbientOcclusionPass() override;

	virtual bool Initialize() override;
	virtual void Destroy() override;
	virtual void OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const IRenderPassResourceCollection* pRscParameters = nullptr) override;
	virtual void OnDestroyWindowSizeDependentResources() override;
	virtual void RecordCommands(const IRenderPassDrawParameters* pDrawParameters = nullptr) override;


	virtual std::vector<FPSOCreationTaskParameters> CollectPSOCreationParameters() override;

	inline EMethod GetMethod() const { return this->Method; }

private:
	EMethod Method;
	FFX_CACAO_Settings AOSettings;
};
