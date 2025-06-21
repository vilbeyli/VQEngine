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
#pragma once

#include "RenderPass.h"
#include <unordered_map>

struct FSceneView;
struct FSceneDrawData;
class DynamicBufferHeap;
struct ID3D12GraphicsCommandList;

class OutlinePass : public RenderPassBase
{
public:
	struct FResourceCollection : public IRenderPassResourceCollection {};
	struct FDrawParameters : public IRenderPassDrawParameters 
	{
		ID3D12GraphicsCommandList* pCmd = nullptr;
		DynamicBufferHeap* pCBufferHeap = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS cbPerView = 0;

		const FSceneView* pSceneView = nullptr;
		const FSceneDrawData* pSceneDrawData = nullptr;
		const std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>* pRTVHandles = nullptr;
		bool bMSAA = false;
	};

	OutlinePass(VQRenderer& Renderer);
	OutlinePass() = delete;
	virtual ~OutlinePass() override {}

	virtual bool Initialize() override;
	virtual void Destroy() override;
	virtual void OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const IRenderPassResourceCollection* pRscParameters = nullptr) override;
	virtual void OnDestroyWindowSizeDependentResources() override;
	virtual void RecordCommands(const IRenderPassDrawParameters* pDrawParameters = nullptr) override;

	virtual std::vector<FPSOCreationTaskParameters> CollectPSOCreationParameters() override;

private:
	int mOutputResolutionX = 0;
	int mOutputResolutionY = 0;

	TextureID TEXPassOutputDepth = INVALID_ID;
	TextureID TEXPassOutputDepthMSAA4 = INVALID_ID;
	DSV_ID DSV = INVALID_ID;
	DSV_ID DSVMSAA = INVALID_ID;

	// PSOs
	static constexpr size_t NUM_RENDERING_OPTIONS = NUM_MSAA_OPTIONS;
	static constexpr size_t NUM_ALPHA_OPTIONS = 2; // opaque/alpha masked
	static constexpr size_t NUM_MAT_OPTIONS = NUM_ALPHA_OPTIONS;
	static constexpr size_t NUM_PASS_OPTIONS = 2; // 0:stencil/1:mask
	static constexpr size_t NUM_OPTIONS_PERMUTATIONS = NUM_PASS_OPTIONS * NUM_MAT_OPTIONS * NUM_RENDERING_OPTIONS * Tessellation::NUM_TESS_OPTIONS;
	static size_t Hash(size_t iPass, size_t iMSAA, size_t iTess, size_t iDomain, size_t iPart, size_t iOutTopo, size_t iTessCullMode, size_t iAlpha);

	std::unordered_map<size_t, PSO_ID>   mapPSO;
};
