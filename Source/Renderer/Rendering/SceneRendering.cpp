//	VQE
//	Copyright(C) 2025  - Volkan Ilbeyli
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

#include "Renderer.h"
#include "RenderPass/ObjectIDPass.h"

#include "Engine/GPUMarker.h"
#include "Engine/Scene/SceneViews.h"

bool VQRenderer::ShouldEnableAsyncCompute(const FGraphicsSettings& GFXSettings, const FSceneView& SceneView, const FSceneShadowViews& ShadowView) const
{
	if (!GFXSettings.bEnableAsyncCompute)
		return false;

	// TODO: test and figure out a better way to handle these cases
	//if (mbLoadingLevel || mbLoadingEnvironmentMap)
	//	return false;

	if (ShadowView.NumPointShadowViews > 0)
		return true;
	if (ShadowView.NumPointShadowViews > 3)
		return true;

	return false;
}

void VQRenderer::RenderObjectIDPass(
	ID3D12GraphicsCommandList* pCmd,
	ID3D12CommandList* pCmdCopy,
	DynamicBufferHeap* pCBufferHeap,
	const std::vector< D3D12_GPU_VIRTUAL_ADDRESS>& CBAddresses,
	D3D12_GPU_VIRTUAL_ADDRESS perViewCBAddr,
	const FSceneView& SceneView,
	const FSceneShadowViews& ShadowView,
	const int BACK_BUFFER_INDEX,
	const FGraphicsSettings& GFXSettings,
	std::atomic<bool>& mAsyncComputeWorkSubmitted
)
{
	std::shared_ptr<ObjectIDPass> pObjectIDPass = std::static_pointer_cast<ObjectIDPass>(this->GetRenderPass(ERenderPass::ObjectID));

	{
		SCOPED_GPU_MARKER(pCmd, "RenderObjectIDPass");
		ObjectIDPass::FDrawParameters params;
		params.pCmd = pCmd;
		params.pCmdCopy = pCmdCopy;
		params.pCBAddresses = &CBAddresses;
		params.pSceneView = &SceneView;
		params.pCBufferHeap = pCBufferHeap;
		params.cbPerView = perViewCBAddr;
		params.bEnableAsyncCopy = GFXSettings.bEnableAsyncCopy;
		pObjectIDPass->RecordCommands(&params);
	}

	SCOPED_CPU_MARKER("Copy");
	D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
	srcLoc.pResource = pObjectIDPass->GetGPUTextureResource();
	srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	srcLoc.SubresourceIndex = 0; // Assuming copying from the first mip level

	D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
	dstLoc.pResource = pObjectIDPass->GetCPUTextureResource();
	dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	D3D12_RESOURCE_DESC srcDesc = srcLoc.pResource->GetDesc();
	this->GetDevicePtr()->GetCopyableFootprints(&srcDesc, 0, 1, 0, &dstLoc.PlacedFootprint, nullptr, nullptr, nullptr);

	if (GFXSettings.bEnableAsyncCopy)
	{
		CommandQueue& GFXCmdQ = this->GetCommandQueue(CommandQueue::EType::GFX);
		CommandQueue& CPYCmdQ = this->GetCommandQueue(CommandQueue::EType::COPY);
		CommandQueue& CMPCmdQ = this->GetCommandQueue(CommandQueue::EType::COMPUTE);
		ID3D12GraphicsCommandList* pCmdCpy = static_cast<ID3D12GraphicsCommandList*>(pCmdCopy);
		Fence& CopyFence = mCopyObjIDDoneFence[BACK_BUFFER_INDEX];

		pCmd->Close();
		if (ShouldEnableAsyncCompute(GFXSettings, SceneView, ShadowView))
		{
			SCOPED_CPU_MARKER_C("WAIT_DEPTH_PREPASS_SUBMIT", 0xFFFF0000);
			while (!mAsyncComputeWorkSubmitted.load()); // pls don't judge
			mAsyncComputeWorkSubmitted.store(false);
		}
		{
			SCOPED_CPU_MARKER("ExecuteList");
			GFXCmdQ.pQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&pCmd);
		}
		{
			SCOPED_CPU_MARKER("Fence");
			CopyFence.Signal(GFXCmdQ.pQueue);
			CopyFence.WaitOnGPU(CPYCmdQ.pQueue); // wait for render target done
		}

		D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
		dstLoc.pResource = pObjectIDPass->GetCPUTextureResource();
		dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		this->GetDevicePtr()->GetCopyableFootprints(&srcDesc, 0, 1, 0, &dstLoc.PlacedFootprint, nullptr, nullptr, nullptr);

		pCmdCpy->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

		{
			SCOPED_CPU_MARKER("ExecuteListCpy");
			pCmdCpy->Close();
			CPYCmdQ.pQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&pCmdCpy);
		}
		{
			SCOPED_CPU_MARKER("Fence Signal");
			CopyFence.Signal(CPYCmdQ.pQueue);
		}
	}
	else // execute copy on graphics
	{
		pCmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(srcLoc.pResource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		pCmd->ResourceBarrier(1, &barrier);
	}
}