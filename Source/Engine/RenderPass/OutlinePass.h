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

#include "../Scene/Mesh.h"
#include "../Scene/Material.h"

#include <unordered_map>


struct FSceneView;
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

		const FSceneView* pSceneView = nullptr;
		const std::unordered_map<MeshID, Mesh>* pMeshes = nullptr;
		const std::unordered_map<MaterialID, Material>* pMaterials = nullptr;
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
	PSO_ID PSOOutlineStencilWrite = INVALID_ID;
	PSO_ID PSOOutlineStencilMSAA4Write = INVALID_ID;
	PSO_ID PSOOutlineStencilMask = INVALID_ID;
	PSO_ID PSOOutlineStencilMSAA4Mask = INVALID_ID;

	int mOutputResolutionX = 0;
	int mOutputResolutionY = 0;

	TextureID TEXPassOutputDepth = INVALID_ID;
	TextureID TEXPassOutputDepthMSAA4 = INVALID_ID;
	DSV_ID DSV = INVALID_ID;
	DSV_ID DSVMSAA = INVALID_ID;
};
