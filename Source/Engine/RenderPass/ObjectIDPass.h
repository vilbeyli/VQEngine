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

class ObjectIDPass : public RenderPassBase
{
public:
	struct FResourceCollection : public IRenderPassResourceCollection {};
	struct FDrawParameters : public IRenderPassDrawParameters 
	{
		ID3D12GraphicsCommandList* pCmd = nullptr;
		std::vector< D3D12_GPU_VIRTUAL_ADDRESS> CBAddresses;
		const FSceneView* pSceneView = nullptr;
		const std::unordered_map<MeshID, Mesh>* pMeshes = nullptr;
		const std::unordered_map<MaterialID, Material>* pMaterials = nullptr;
	};

	ObjectIDPass(VQRenderer& Renderer);
	ObjectIDPass() = delete;
	virtual ~ObjectIDPass() override {}

	virtual bool Initialize() override;
	virtual void Destroy() override;
	virtual void OnCreateWindowSizeDependentResources(unsigned Width, unsigned Height, const IRenderPassResourceCollection* pRscParameters = nullptr) override;
	virtual void OnDestroyWindowSizeDependentResources() override;
	virtual void RecordCommands(const IRenderPassDrawParameters* pDrawParameters = nullptr) override;

	virtual std::vector<FPSOCreationTaskParameters> CollectPSOCreationParameters() override;

	int4 ReadBackPixel(float2 uv) const;

private:
	PSO_ID PSOOpaque = INVALID_ID;
	PSO_ID PSOAlphaMasked = INVALID_ID;

	int mOutputResolutionX = 0;
	int mOutputResolutionY = 0;

	TextureID TEXPassOutput = INVALID_ID;
	RTV_ID RTVPassOutput = INVALID_ID;
	TextureID TEXPassOutputCPUReadback = INVALID_ID;

	TextureID TEXPassOutputDepth = INVALID_ID;
	DSV_ID DSVPassOutput = INVALID_ID;
};
