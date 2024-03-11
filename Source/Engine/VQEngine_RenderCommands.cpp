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

#include "VQEngine.h"
#include "GPUMarker.h"
#include "../VQUtils/Source/utils.h"

#include <d3d12.h>
#include <dxgi.h>
#include <DirectXMath.h>

#include "RenderPass/AmbientOcclusion.h"
#include "RenderPass/DepthPrePass.h"

#include "VQEngine_RenderCommon.h"

#include "imgui.h"
//
// DRAW COMMANDS
//
void VQEngine::DrawShadowViewMeshList(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowView::FShadowView& shadowView)
{
	using namespace DirectX;

#if RENDER_INSTANCED_SHADOW_MESHES
	struct FCBufferLightVS
	{
		XMMATRIX matWorldViewProj[MAX_INSTANCE_COUNT__SHADOW_MESHES];
		XMMATRIX matWorld        [MAX_INSTANCE_COUNT__SHADOW_MESHES];
	};

	for (const FInstancedShadowMeshRenderCommand& renderCmd : shadowView.meshRenderCommands)
	{
		const uint32 NumInstances = (uint32)renderCmd.matWorldViewProj.size();
		assert(NumInstances == renderCmd.matWorldViewProj.size());
		assert(NumInstances == renderCmd.matWorldViewProjTransformations.size());

		// set constant buffer data
		FCBufferLightVS* pCBuffer = {};
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		pCBufferHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer)), (void**)(&pCBuffer), &cbAddr);
		if (renderCmd.matWorldViewProj.empty())
		{
			Log::Error("EMPTY COMMAND LIST WHYT??");
		}
		const size_t memcpySrcSize = renderCmd.matWorldViewProj.size() * sizeof(XMMATRIX);
		const size_t memcpyDstSize = _countof(pCBuffer->matWorld) * sizeof(XMMATRIX);
		if (memcpyDstSize < memcpySrcSize)
		{
			Log::Error("Batch data too big (%d: %s) for destination cbuffer (%d: %s): "
				, renderCmd.matWorldViewProj.size()
				, StrUtil::FormatByte(memcpySrcSize)
				, _countof(pCBuffer->matWorld)
				, StrUtil::FormatByte(memcpyDstSize)
			);
		}
		memcpy(pCBuffer->matWorld        , renderCmd.matWorldViewProj.data()               , NumInstances * sizeof(XMMATRIX));
		memcpy(pCBuffer->matWorldViewProj, renderCmd.matWorldViewProjTransformations.data(), NumInstances * sizeof(XMMATRIX));
		
		auto vb = mRenderer.GetVertexBufferView(renderCmd.vertexIndexBuffer.first);
		auto ib = mRenderer.GetIndexBufferView(renderCmd.vertexIndexBuffer.second);
		
		pCmd->SetGraphicsRootConstantBufferView(0, cbAddr);
		
		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmd->IASetVertexBuffers(0, 1, &vb);
		pCmd->IASetIndexBuffer(&ib);

		pCmd->DrawIndexedInstanced(renderCmd.numIndices, NumInstances, 0, 0, 0);
	}

#else 
	struct FCBufferLightVS
	{
		XMMATRIX matWorldViewProj;
		XMMATRIX matWorld;
	};

	for (const FShadowMeshRenderCommand& renderCmd : shadowView.meshRenderCommands)
	{
		SCOPED_CPU_MARKER("Process_ShadowMeshRenderCommand");
		// set constant buffer data
		FCBufferLightVS* pCBuffer = {};
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		pCBufferHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer)), (void**)(&pCBuffer), &cbAddr);
		pCBuffer->matWorldViewProj = renderCmd.matWorldViewProj;
		pCBuffer->matWorld = renderCmd.matWorldTransformation;
		pCmd->SetGraphicsRootConstantBufferView(0, cbAddr);

		const Mesh& mesh = mpScene->mMeshes.at(renderCmd.meshID);
		DrawMesh(pCmd, mesh);
	}
#endif
}


//
// RENDER PASSES
//
void VQEngine::RenderDirectionalShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowView& SceneShadowView)
{
	SCOPED_GPU_MARKER(pCmd, "RenderDirectionalShadowMaps");

	if (!SceneShadowView.ShadowView_Directional.meshRenderCommands.empty())
	{
		const std::string marker = "Directional";
		SCOPED_GPU_MARKER(pCmd, marker.c_str());

		pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::DEPTH_PASS_PSO));
		pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ShadowPassDepthOnlyVS));

		const float RenderResolutionX = 2048.0f; // TODO
		const float RenderResolutionY = 2048.0f; // TODO
		D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
		D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
		pCmd->RSSetViewports(1, &viewport);
		pCmd->RSSetScissorRects(1, &scissorsRect);

		// Bind Depth / clear
		const DSV& dsv = mRenderer.GetDSV(mResources_MainWnd.DSV_ShadowMaps_Directional);
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsv.GetCPUDescHandle();
		pCmd->OMSetRenderTargets(0, NULL, FALSE, &dsvHandle);

		//if constexpr (!B_CLEAR_DEPTH_BUFFERS_BEFORE_DRAW)
		{
			D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
			pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, NULL);
		}

		DrawShadowViewMeshList(pCmd, pCBufferHeap, SceneShadowView.ShadowView_Directional);
	}
}
void VQEngine::RenderSpotShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowView& SceneShadowView)
{
	SCOPED_GPU_MARKER(pCmd, "RenderSpotShadowMaps");
	const bool bRenderAtLeastOneSpotShadowMap = SceneShadowView.NumSpotShadowViews > 0;

	// Set Viewport & Scissors
	const float RenderResolutionX = 1024.0f; // TODO
	const float RenderResolutionY = 1024.0f; // TODO
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);
	
	if (!bRenderAtLeastOneSpotShadowMap)
		return;


	//
	// SPOT LIGHTS
	//
	pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::DEPTH_PASS_PSO));
	pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ShadowPassDepthOnlyVS));
	
	for (uint i = 0; i < SceneShadowView.NumSpotShadowViews; ++i)
	{
		const FSceneShadowView::FShadowView& ShadowView = SceneShadowView.ShadowViews_Spot[i];

		if (ShadowView.meshRenderCommands.empty())
			continue;

		const std::string marker = "Spot[" + std::to_string(i) + "]";
		SCOPED_GPU_MARKER(pCmd, marker.c_str());

		// Bind Depth / clear
		const DSV& dsv = mRenderer.GetDSV(mResources_MainWnd.DSV_ShadowMaps_Spot);
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsv.GetCPUDescHandle(i);
		pCmd->OMSetRenderTargets(0, NULL, FALSE, &dsvHandle);

		D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
		pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, NULL);

		DrawShadowViewMeshList(pCmd, pCBufferHeap, ShadowView);
	}
}
void VQEngine::RenderPointShadowMaps(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneShadowView& SceneShadowView, size_t iBegin, size_t NumPointLights)
{
	SCOPED_GPU_MARKER(pCmd, "RenderPointShadowMaps");
	if (SceneShadowView.NumPointShadowViews <= 0)
		return;

	using namespace DirectX;
	struct FCBufferLightPS
	{
		XMFLOAT3 vLightPos;
		float fFarPlane;
	};
	
#if RENDER_THREAD__MULTI_THREADED_COMMAND_RECORDING
	// Set Viewport & Scissors
	const float RenderResolutionX = 1024.0f; // TODO
	const float RenderResolutionY = 1024.0f; // TODO
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);
#endif

	pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::DEPTH_PASS_LINEAR_PSO));
	pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ShadowPassLinearDepthVSPS));
	for (size_t i = iBegin; i < iBegin + NumPointLights; ++i)
	{
		const std::string marker = "Point[" + std::to_string(i) + "]";
		SCOPED_GPU_MARKER(pCmd, marker.c_str());

		FCBufferLightPS* pCBuffer = {};
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		pCBufferHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer)), (void**)(&pCBuffer), &cbAddr);

		pCBuffer->vLightPos = SceneShadowView.PointLightLinearDepthParams[i].vWorldPos;
		pCBuffer->fFarPlane = SceneShadowView.PointLightLinearDepthParams[i].fFarPlane;
		pCmd->SetGraphicsRootConstantBufferView(1, cbAddr);

		for (size_t face = 0; face < 6; ++face)
		{
			const size_t iShadowView = i * 6 + face;
			const FSceneShadowView::FShadowView& ShadowView = SceneShadowView.ShadowViews_Point[iShadowView];

			if (ShadowView.meshRenderCommands.empty())
				continue;

			const std::string marker_face = "[Cubemap Face=" + std::to_string(face) + "]";
			SCOPED_GPU_MARKER(pCmd, marker_face.c_str());

			// Bind Depth / clear
			const DSV& dsv = mRenderer.GetDSV(mResources_MainWnd.DSV_ShadowMaps_Point);
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsv.GetCPUDescHandle((uint32_t)iShadowView);
			D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
			pCmd->OMSetRenderTargets(0, NULL, FALSE, &dsvHandle);
			pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, NULL);

			// draw render list
			DrawShadowViewMeshList(pCmd, pCBufferHeap, ShadowView);
		}
	}
}

void VQEngine::RenderDepthPrePass(
	ID3D12GraphicsCommandList* pCmd, 
	const std::vector< D3D12_GPU_VIRTUAL_ADDRESS>& cbAddresses,
	const FSceneView& SceneView
)
{
	using namespace DirectX;
	using namespace VQ_SHADER_DATA;
	const bool& bMSAA = mSettings.gfx.bAntiAliasing;
	const bool bAsyncCompute = ShouldEnableAsyncCompute();
	const auto& rsc = mResources_MainWnd;
	auto pRscNormals     = mRenderer.GetTextureResource(rsc.Tex_SceneNormals);
	auto pRscNormalsMSAA = mRenderer.GetTextureResource(rsc.Tex_SceneNormalsMSAA);
	auto pRscDepthResolve= mRenderer.GetTextureResource(rsc.Tex_SceneDepthResolve);
	auto pRscDepthMSAA   = mRenderer.GetTextureResource(rsc.Tex_SceneDepthMSAA);
	auto pRscDepth       = mRenderer.GetTextureResource(rsc.Tex_SceneDepth);

	if (!bMSAA)
	{
		std::vector<CD3DX12_RESOURCE_BARRIER> Barriers;
		Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscNormals, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
		pCmd->ResourceBarrier((UINT32)Barriers.size(), Barriers.data());
	}

	const DSV& dsvColor   = mRenderer.GetDSV(bMSAA ? mResources_MainWnd.DSV_SceneDepthMSAA : mResources_MainWnd.DSV_SceneDepth);
	const RTV& rtvNormals = mRenderer.GetRTV(bMSAA ? mResources_MainWnd.RTV_SceneNormalsMSAA : mResources_MainWnd.RTV_SceneNormals);

	D3D12_CLEAR_FLAGS DSVClearFlags = D3D12_CLEAR_FLAGS::D3D12_CLEAR_FLAG_DEPTH;
	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvNormals.GetCPUDescHandle();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvColor.GetCPUDescHandle();


	const float RenderResolutionX = static_cast<float>(SceneView.SceneRTWidth);
	const float RenderResolutionY = static_cast<float>(SceneView.SceneRTHeight);
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };


	SCOPED_GPU_MARKER(pCmd, "RenderDepthPrePass");

	pCmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	pCmd->ClearDepthStencilView(dsvHandle, DSVClearFlags, 1.0f, 0, 0, nullptr);

	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);

	pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA ? EBuiltinPSOs::DEPTH_PREPASS_PSO_MSAA_4 : EBuiltinPSOs::DEPTH_PREPASS_PSO));
	pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ZPrePass));

	int iCB = 0;

	// draw meshes
	for (const MeshRenderCommand_t& meshRenderCmd : SceneView.meshRenderCommands)
	{
		const Material& mat = mpScene->GetMaterial(meshRenderCmd.matID);
		const auto VBIBIDs = meshRenderCmd.vertexIndexBuffer;
		const BufferID& VB_ID = VBIBIDs.first;
		const BufferID& IB_ID = VBIBIDs.second;
		const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
		const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);
		const uint32 NumIndices = meshRenderCmd.numIndices;

#if RENDER_INSTANCED_SCENE_MESHES
		const uint32 NumInstances = (uint32)meshRenderCmd.matNormal.size();
#else
		const uint32 NumInstances = 1;
#endif

		pCmd->SetGraphicsRootConstantBufferView(1, cbAddresses[iCB++]);

		if (mat.SRVMaterialMaps != INVALID_ID) // set textures
		{
			pCmd->SetGraphicsRootDescriptorTable(0, mRenderer.GetSRV(mat.SRVMaterialMaps).GetGPUDescHandle(0));
			//pCmd->SetGraphicsRootDescriptorTable(4, mRenderer.GetSRV(mat.SRVMaterialMaps).GetGPUDescHandle(0));
		}

		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmd->IASetVertexBuffers(0, 1, &vb);
		pCmd->IASetIndexBuffer(&ib);
		pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
	}

}

void VQEngine::RenderObjectIDPass(
	ID3D12GraphicsCommandList* pCmd, 
	ID3D12CommandList* pCmdCopy, 
	const std::vector< D3D12_GPU_VIRTUAL_ADDRESS>& CBAddresses, 
	const FSceneView& SceneView,
	const int BACK_BUFFER_INDEX
)
{
	{
		SCOPED_GPU_MARKER(pCmd, "RenderObjectIDPass");
		ObjectIDPass::FDrawParameters params;
		params.pCmd = pCmd;
		params.pCmdCopy = pCmdCopy;
		params.pCBAddresses = &CBAddresses;
		params.pSceneView = &SceneView;
		params.pMeshes = &mpScene->mMeshes;
		params.pMaterials = &mpScene->mMaterials;
		params.bEnableAsyncCopy = mSettings.gfx.bEnableAsyncCopy;
		mRenderPass_ObjectID.RecordCommands(&params);
	}

	SCOPED_CPU_MARKER("Copy");
	D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
	srcLoc.pResource = mRenderPass_ObjectID.GetGPUTextureResource();
	srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	srcLoc.SubresourceIndex = 0; // Assuming copying from the first mip level

	D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
	dstLoc.pResource = mRenderPass_ObjectID.GetCPUTextureResource();
	dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	mRenderer.GetDevicePtr()->GetCopyableFootprints(&srcLoc.pResource->GetDesc(), 0, 1, 0, &dstLoc.PlacedFootprint, nullptr, nullptr, nullptr);

	if (mSettings.gfx.bEnableAsyncCopy)
	{
		CommandQueue& GFXCmdQ = mRenderer.GetCommandQueue(CommandQueue::EType::GFX);
		CommandQueue& CPYCmdQ = mRenderer.GetCommandQueue(CommandQueue::EType::COPY);
		CommandQueue& CMPCmdQ = mRenderer.GetCommandQueue(CommandQueue::EType::COMPUTE);
		ID3D12GraphicsCommandList* pCmdCpy = static_cast<ID3D12GraphicsCommandList*>(pCmdCopy);
		Fence& CopyFence = mCopyObjIDDoneFence[BACK_BUFFER_INDEX];

		pCmd->Close();
		if (ShouldEnableAsyncCompute())
		{
			SCOPED_CPU_MARKER_C("WAIT_DEPTH_PREPASS_SUBMIT", 0xFFFF0000);
			while (!mAsyncComputeWorkSubmitted.load());
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
		dstLoc.pResource = mRenderPass_ObjectID.GetCPUTextureResource();
		dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		mRenderer.GetDevicePtr()->GetCopyableFootprints(&srcLoc.pResource->GetDesc(), 0, 1, 0, &dstLoc.PlacedFootprint, nullptr, nullptr, nullptr);

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

		pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(srcLoc.pResource,
			D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
	}

}

void VQEngine::TransitionDepthPrePassForRead(ID3D12GraphicsCommandList* pCmd, bool bMSAA, bool bAsyncCompute)
{
	SCOPED_GPU_MARKER(pCmd, "TransitionDepthPrePassForRead");

	const auto& rsc = mResources_MainWnd;
	auto pRscNormals     = mRenderer.GetTextureResource(rsc.Tex_SceneNormals);
	auto pRscNormalsMSAA = mRenderer.GetTextureResource(rsc.Tex_SceneNormalsMSAA);
	auto pRscDepthResolve= mRenderer.GetTextureResource(rsc.Tex_SceneDepthResolve);
	auto pRscDepthMSAA   = mRenderer.GetTextureResource(rsc.Tex_SceneDepthMSAA);
	auto pRscDepth       = mRenderer.GetTextureResource(rsc.Tex_SceneDepth);

	std::vector<CD3DX12_RESOURCE_BARRIER> Barriers;
	if (bMSAA)
	{
		if (!bAsyncCompute)
		{
			Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthMSAA, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));
			Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscNormalsMSAA, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
		}
		Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthResolve, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
		Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscNormals, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	}
	else
	{
		Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE));
		Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthResolve, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST));
		Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscNormals, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	}

	pCmd->ResourceBarrier((UINT32)Barriers.size(), Barriers.data());
}

void VQEngine::TransitionDepthPrePassMSAAResolve(ID3D12GraphicsCommandList* pCmd, bool bMSAA)
{
	const auto& rsc      = mResources_MainWnd;
	auto pRscNormals     = mRenderer.GetTextureResource(rsc.Tex_SceneNormals);
	auto pRscNormalsMSAA = mRenderer.GetTextureResource(rsc.Tex_SceneNormalsMSAA);
	auto pRscDepthResolve= mRenderer.GetTextureResource(rsc.Tex_SceneDepthResolve);
	auto pRscDepthMSAA   = mRenderer.GetTextureResource(rsc.Tex_SceneDepthMSAA);
	auto pRscDepth       = mRenderer.GetTextureResource(rsc.Tex_SceneDepth);

	const CD3DX12_RESOURCE_BARRIER pBarriers[] =
	{
		  CD3DX12_RESOURCE_BARRIER::Transition(pRscNormalsMSAA , D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthMSAA   , D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscNormals     , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthResolve, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
	};
	pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
}

void VQEngine::ResolveMSAA_DepthPrePass(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap)
{
	SCOPED_GPU_MARKER(pCmd, "MSAAResolve<Depth=%d, Normals=%d, Roughness=%d>"); // TODO: string formatting
	
	const auto& rsc = mResources_MainWnd;

	DepthMSAAResolvePass::FDrawParameters params;
	params.pCmd = pCmd;
	params.pCBufferHeap = pCBufferHeap;
	params.SRV_MSAADepth = rsc.SRV_SceneDepthMSAA;
	params.SRV_MSAANormals = rsc.SRV_SceneNormalsMSAA;
	params.SRV_MSAARoughness = rsc.SRV_SceneColorMSAA;
	params.UAV_ResolvedDepth = rsc.UAV_SceneDepth;
	params.UAV_ResolvedNormals = rsc.UAV_SceneNormals;
	params.UAV_ResolvedRoughness = INVALID_ID;

	mRenderPass_DepthResolve.RecordCommands(&params);
}

void VQEngine::CopyDepthForCompute(ID3D12GraphicsCommandList* pCmd)
{
	const auto& rsc = mResources_MainWnd;
	auto pRscDepthResolve= mRenderer.GetTextureResource(rsc.Tex_SceneDepthResolve);
	auto pRscDepth       = mRenderer.GetTextureResource(rsc.Tex_SceneDepth);

	pCmd->CopyResource(pRscDepthResolve, pRscDepth);
	
	std::vector<CD3DX12_RESOURCE_BARRIER> Barriers;
	Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthResolve, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepth, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));
	pCmd->ResourceBarrier((UINT32)Barriers.size(), Barriers.data());
}

void VQEngine::RenderAmbientOcclusion(ID3D12GraphicsCommandList* pCmd, const FSceneView& SceneView)
{
	bool bAsyncCompute = ShouldEnableAsyncCompute();
	const auto& rsc = mResources_MainWnd;
	const bool& bMSAA = mSettings.gfx.bAntiAliasing;

	ID3D12Resource* pRscAmbientOcclusion = mRenderer.GetTextureResource(rsc.Tex_AmbientOcclusion);
	const UAV& uav = mRenderer.GetUAV(rsc.UAV_FFXCACAO_Out);

	const char* pStrPass[AmbientOcclusionPass::EMethod::NUM_AMBIENT_OCCLUSION_METHODS] =
	{
		"Ambient Occlusion (FidelityFX CACAO)"
	};
	SCOPED_GPU_MARKER(pCmd, pStrPass[mRenderPass_AO.GetMethod()]);
	
	static bool sbScreenSpaceAO_Previous = false;
	const bool bSSAOToggledOff = sbScreenSpaceAO_Previous && !SceneView.sceneParameters.bScreenSpaceAO;

	if (SceneView.sceneParameters.bScreenSpaceAO)
	{
		AmbientOcclusionPass::FDrawParameters drawParams = {};
		DirectX::XMStoreFloat4x4(&drawParams.matNormalToView, SceneView.view);
		DirectX::XMStoreFloat4x4(&drawParams.matProj, SceneView.proj);
		drawParams.pCmd = pCmd;
		drawParams.bAsyncCompute = bAsyncCompute;

		if (!bAsyncCompute)
		{
			// TODO: move into RecordCommands?
			pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pRscAmbientOcclusion, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
		}
		mRenderPass_AO.RecordCommands(&drawParams);

		// set back the descriptor heap for the VQEngine
		ID3D12DescriptorHeap* ppHeaps[] = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };
		pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	}
	
	// clear UAV only once
	if(bSSAOToggledOff)
	{
		pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pRscAmbientOcclusion, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
		const FLOAT clearValue[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		pCmd->ClearUnorderedAccessViewFloat(uav.GetGPUDescHandle(), uav.GetCPUDescHandle(), pRscAmbientOcclusion, clearValue, 0, NULL);
		pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pRscAmbientOcclusion, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ));
	}

	sbScreenSpaceAO_Previous = SceneView.sceneParameters.bScreenSpaceAO;
}

static bool IsFFX_SSSREnabled(const FEngineSettings& settings)
{
	return settings.gfx.Reflections == EReflections::SCREEN_SPACE_REFLECTIONS__FFX;
}
static bool ShouldUseMotionVectorsTarget(const FEngineSettings& settings)
{
	return IsFFX_SSSREnabled(settings);
}
static bool ShouldUseVisualizationTarget(EDrawMode DrawModeEnum)
{
	return DrawModeEnum == EDrawMode::ALBEDO
		|| DrawModeEnum == EDrawMode::ROUGHNESS
		|| DrawModeEnum == EDrawMode::METALLIC;
}
void VQEngine::TransitionForSceneRendering(ID3D12GraphicsCommandList* pCmd, FWindowRenderContext& ctx, const FPostProcessParameters& PPParams)
{
	const bool& bMSAA = mSettings.gfx.bAntiAliasing;

	const auto& rsc = mResources_MainWnd;
	auto pRscColor           = mRenderer.GetTextureResource(rsc.Tex_SceneColor);
	auto pRscColorMSAA       = mRenderer.GetTextureResource(rsc.Tex_SceneColorMSAA);
	auto pRscViz             = mRenderer.GetTextureResource(rsc.Tex_SceneVisualization);
	auto pRscVizMSAA         = mRenderer.GetTextureResource(rsc.Tex_SceneVisualizationMSAA);
	auto pRscMoVecMSAA       = mRenderer.GetTextureResource(rsc.Tex_SceneMotionVectorsMSAA);
	auto pRscMoVec           = mRenderer.GetTextureResource(rsc.Tex_SceneMotionVectors);
	auto pRscNormals         = mRenderer.GetTextureResource(rsc.Tex_SceneNormals);
	auto pRscNormalsMSAA     = mRenderer.GetTextureResource(rsc.Tex_SceneNormalsMSAA);
	auto pRscShadowMaps_Spot = mRenderer.GetTextureResource(rsc.Tex_ShadowMaps_Spot);
	auto pRscShadowMaps_Point= mRenderer.GetTextureResource(rsc.Tex_ShadowMaps_Point);
	auto pRscShadowMaps_Directional = mRenderer.GetTextureResource(rsc.Tex_ShadowMaps_Directional);

	SCOPED_GPU_MARKER(pCmd, "TransitionForSceneRendering");

	CD3DX12_RESOURCE_BARRIER ColorTransition = bMSAA
		? CD3DX12_RESOURCE_BARRIER::Transition(pRscColorMSAA, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
		: CD3DX12_RESOURCE_BARRIER::Transition(pRscColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	std::vector<CD3DX12_RESOURCE_BARRIER> vBarriers;
	vBarriers.push_back(ColorTransition);
	vBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Spot, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	vBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Point, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	vBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Directional, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

	if (ShouldUseVisualizationTarget(PPParams.DrawModeEnum))
	{
		vBarriers.push_back(bMSAA
			? CD3DX12_RESOURCE_BARRIER::Transition(pRscVizMSAA, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
			: CD3DX12_RESOURCE_BARRIER::Transition(pRscViz, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
		);
	}
	if (ShouldUseMotionVectorsTarget(mSettings))
	{
		vBarriers.push_back(bMSAA
			? CD3DX12_RESOURCE_BARRIER::Transition(pRscMoVecMSAA, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
			: CD3DX12_RESOURCE_BARRIER::Transition(pRscMoVec, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
		);
	}

	pCmd->ResourceBarrier((UINT)vBarriers.size(), vBarriers.data());
}

void VQEngine::RenderSceneColor(
	ID3D12GraphicsCommandList* pCmd, 
	DynamicBufferHeap* pCBufferHeap, 
	const FSceneView& SceneView, 
	const FPostProcessParameters& PPParams,
	const std::vector< D3D12_GPU_VIRTUAL_ADDRESS>& perObjCBAddr
)
{
	using namespace VQ_SHADER_DATA;

	const bool bUseHDRRenderPath = this->ShouldRenderHDR(mpWinMain->GetHWND());
	const bool bHasEnvironmentMapHDRTexture = mResources_MainWnd.EnvironmentMap.SRV_HDREnvironment != INVALID_ID;
	const bool bDrawEnvironmentMap = bHasEnvironmentMapHDRTexture && true;
	const bool bUseVisualizationRenderTarget = ShouldUseVisualizationTarget(PPParams.DrawModeEnum);
	const bool bRenderMotionVectors = ShouldUseMotionVectorsTarget(mSettings);
	const bool bRenderScreenSpaceReflections = IsFFX_SSSREnabled(mSettings);

	using namespace DirectX;

	const bool& bMSAA = mSettings.gfx.bAntiAliasing;

	const RTV& rtvColor = mRenderer.GetRTV(bMSAA ? mResources_MainWnd.RTV_SceneColorMSAA : mResources_MainWnd.RTV_SceneColor);
	const RTV& rtvColorViz = mRenderer.GetRTV(bMSAA ? mResources_MainWnd.RTV_SceneVisualizationMSAA : mResources_MainWnd.RTV_SceneVisualization);
	const RTV& rtvMoVec = mRenderer.GetRTV(bMSAA ? mResources_MainWnd.RTV_SceneMotionVectorsMSAA : mResources_MainWnd.RTV_SceneMotionVectors);

	const DSV& dsvColor = mRenderer.GetDSV(bMSAA ? mResources_MainWnd.DSV_SceneDepthMSAA : mResources_MainWnd.DSV_SceneDepth);
	auto pRscDepth = mRenderer.GetTextureResource(mResources_MainWnd.Tex_SceneDepth);

	const CBV_SRV_UAV& NullCubemapSRV = mRenderer.GetSRV(mResources_MainWnd.SRV_NullCubemap);
	const CBV_SRV_UAV& NullTex2DSRV = mRenderer.GetSRV(mResources_MainWnd.SRV_NullTexture2D);

	SCOPED_GPU_MARKER(pCmd, "RenderSceneColor");

	// Clear Depth & Render targets
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvColor.GetCPUDescHandle();
	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles(1, rtvColor.GetCPUDescHandle());
	if (bUseVisualizationRenderTarget) rtvHandles.push_back(rtvColorViz.GetCPUDescHandle());
	if (bRenderMotionVectors)          rtvHandles.push_back(rtvMoVec.GetCPUDescHandle());
	
	{
		SCOPED_GPU_MARKER(pCmd, "Clear");
		for (D3D12_CPU_DESCRIPTOR_HANDLE& rtv : rtvHandles)
			pCmd->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
	}
	
	pCmd->OMSetRenderTargets((UINT)rtvHandles.size(), rtvHandles.data(), FALSE, &dsvHandle);

	// Set Viewport & Scissors
	const float RenderResolutionX = static_cast<float>(SceneView.SceneRTWidth);
	const float RenderResolutionY = static_cast<float>(SceneView.SceneRTHeight);
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);

	pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA 
		? (bUseVisualizationRenderTarget 
			? (bRenderMotionVectors ? EBuiltinPSOs::FORWARD_LIGHTING_AND_VIZ_AND_MV_PSO_MSAA_4 : EBuiltinPSOs::FORWARD_LIGHTING_AND_VIZ_PSO_MSAA_4)
			: (bRenderMotionVectors ? EBuiltinPSOs::FORWARD_LIGHTING_AND_MV_PSO_MSAA_4 : EBuiltinPSOs::FORWARD_LIGHTING_PSO_MSAA_4) )
		: (bUseVisualizationRenderTarget 
			? (bRenderMotionVectors ? EBuiltinPSOs::FORWARD_LIGHTING_AND_VIZ_AND_MV_PSO : EBuiltinPSOs::FORWARD_LIGHTING_AND_VIZ_PSO)
			: (bRenderMotionVectors ? EBuiltinPSOs::FORWARD_LIGHTING_AND_MV_PSO : EBuiltinPSOs::FORWARD_LIGHTING_PSO))
	));
	pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ForwardLighting));

	// set PerFrame constants
	D3D12_GPU_VIRTUAL_ADDRESS cbAddr_PerFrame = {};
	D3D12_GPU_VIRTUAL_ADDRESS cbAddr_PerView = {};
	constexpr UINT PerFrameRSBindSlot = 3;
	constexpr UINT PerViewRSBindSlot = 2;
	
	{
		SCOPED_GPU_MARKER(pCmd, "CB");
		{
			SCOPED_GPU_MARKER(pCmd, "CBPerFrame");

			PerFrameData* pPerFrame = {};
			pCBufferHeap->AllocConstantBuffer(sizeof(PerFrameData), (void**)(&pPerFrame), &cbAddr_PerFrame);

			assert(pPerFrame);
			pPerFrame->Lights = SceneView.GPULightingData;
			pPerFrame->fAmbientLightingFactor = SceneView.sceneParameters.fAmbientLightingFactor;
			pPerFrame->f2PointLightShadowMapDimensions = { 1024.0f, 1024.f }; // TODO
			pPerFrame->f2SpotLightShadowMapDimensions  = { 1024.0f, 1024.f }; // TODO
			pPerFrame->f2DirectionalLightShadowMapDimensions  = { 2048.0f, 2048.0f }; // TODO
			pPerFrame->fHDRIOffsetInRadians = SceneView.HDRIYawOffset;
		
			if (bUseHDRRenderPath)
			{
				// adjust ambient factor as the tonemapper changes the output curve for HDR displays 
				// and makes the ambient lighting too strong.
				pPerFrame->fAmbientLightingFactor *= 0.005f;
			}

			//pCmd->SetGraphicsRootDescriptorTable(PerFrameRSBindSlot, );
			pCmd->SetGraphicsRootConstantBufferView(PerFrameRSBindSlot, cbAddr_PerFrame);
			pCmd->SetGraphicsRootDescriptorTable(4, mRenderer.GetSRV(mResources_MainWnd.SRV_ShadowMaps_Spot).GetGPUDescHandle(0));
			pCmd->SetGraphicsRootDescriptorTable(5, mRenderer.GetSRV(mResources_MainWnd.SRV_ShadowMaps_Point).GetGPUDescHandle(0));
			pCmd->SetGraphicsRootDescriptorTable(6, mRenderer.GetSRV(mResources_MainWnd.SRV_ShadowMaps_Directional).GetGPUDescHandle(0));
			pCmd->SetGraphicsRootDescriptorTable(7, bDrawEnvironmentMap
				? mRenderer.GetSRV(mResources_MainWnd.EnvironmentMap.SRV_IrradianceDiffBlurred).GetGPUDescHandle(0)
				: NullCubemapSRV.GetGPUDescHandle()
			);
			pCmd->SetGraphicsRootDescriptorTable(8, bDrawEnvironmentMap
				? mRenderer.GetSRV(mResources_MainWnd.EnvironmentMap.SRV_IrradianceSpec).GetGPUDescHandle(0)
				: NullCubemapSRV.GetGPUDescHandle()
			);
			pCmd->SetGraphicsRootDescriptorTable(9, bDrawEnvironmentMap
				? mRenderer.GetSRV(mResources_MainWnd.EnvironmentMap.SRV_BRDFIntegrationLUT).GetGPUDescHandle()
				: NullTex2DSRV.GetGPUDescHandle()
			);
			pCmd->SetGraphicsRootDescriptorTable(10, mRenderer.GetSRV(mResources_MainWnd.SRV_FFXCACAO_Out).GetGPUDescHandle());
			// pCmd->SetGraphicsRootDescriptorTable(11, mRenderer.GetSRV().GetGPUDescHandle()); // TODO: bind heightmap
		}

		// set PerView constants
		{
			SCOPED_GPU_MARKER(pCmd, "CBPerView");
			PerViewData* pPerView = {};
			pCBufferHeap->AllocConstantBuffer(sizeof(decltype(*pPerView)), (void**)(&pPerView), &cbAddr_PerView);

			assert(pPerView);
			XMStoreFloat3(&pPerView->CameraPosition, SceneView.cameraPosition);
			pPerView->ScreenDimensions.x = RenderResolutionX;
			pPerView->ScreenDimensions.y = RenderResolutionY;
			pPerView->MaxEnvMapLODLevels = static_cast<float>(mResources_MainWnd.EnvironmentMap.GetNumSpecularIrradianceCubemapLODLevels(mRenderer));
			pPerView->EnvironmentMapDiffuseOnlyIllumination = IsFFX_SSSREnabled(mSettings);

			// TODO: PreView data

			//pCmd->SetGraphicsRootDescriptorTable(PerViewRSBindSlot, D3D12_GPU_DESCRIPTOR_HANDLE{ cbAddr_PerView });
			pCmd->SetGraphicsRootConstantBufferView(PerViewRSBindSlot, cbAddr_PerView);
		}
	}


	// Draw Objects -----------------------------------------------
	if(!SceneView.meshRenderCommands.empty())
	{
		constexpr UINT PerObjRSBindSlot = 1;
		SCOPED_GPU_MARKER(pCmd, "Geometry");

		int iCB = 0;
		for (const MeshRenderCommand_t& meshRenderCmd : SceneView.meshRenderCommands)
		{
			const Material& mat = mpScene->GetMaterial(meshRenderCmd.matID);

			const auto VBIBIDs = meshRenderCmd.vertexIndexBuffer;
			const uint32 NumIndices = meshRenderCmd.numIndices;
			const VBV& vb = mRenderer.GetVertexBufferView(VBIBIDs.first);
			const IBV& ib = mRenderer.GetIndexBufferView(VBIBIDs.second);

#if RENDER_INSTANCED_SCENE_MESHES
			const uint32 NumInstances = (uint32)meshRenderCmd.matNormal.size();;
#else
			const uint32 NumInstances = 1;
#endif

			pCmd->SetGraphicsRootConstantBufferView(PerObjRSBindSlot, perObjCBAddr[iCB++]);

			if (mat.SRVMaterialMaps != INVALID_ID) // set textures
			{
				pCmd->SetGraphicsRootDescriptorTable(0, mRenderer.GetSRV(mat.SRVMaterialMaps).GetGPUDescHandle(0));
				//pCmd->SetGraphicsRootDescriptorTable(4, mRenderer.GetSRV(mat.SRVMaterialMaps).GetGPUDescHandle(0));
			}

			pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmd->IASetVertexBuffers(0, 1, &vb);
			pCmd->IASetIndexBuffer(&ib);

			pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
		}
	}

	// Draw Terrain ---
	if(!SceneView.terrainDrawParams.empty())
	{
		SCOPED_GPU_MARKER(pCmd, "Terrain");

		pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__Terrain));
		
		pCmd->SetGraphicsRootDescriptorTable(2, mRenderer.GetSRV(mResources_MainWnd.SRV_FFXCACAO_Out).GetGPUDescHandle());
		pCmd->SetGraphicsRootDescriptorTable(3, bDrawEnvironmentMap
			? mRenderer.GetSRV(mResources_MainWnd.EnvironmentMap.SRV_IrradianceDiffBlurred).GetGPUDescHandle(0)
			: NullCubemapSRV.GetGPUDescHandle()
		);
		pCmd->SetGraphicsRootDescriptorTable(4, bDrawEnvironmentMap
			? mRenderer.GetSRV(mResources_MainWnd.EnvironmentMap.SRV_IrradianceSpec).GetGPUDescHandle(0)
			: NullCubemapSRV.GetGPUDescHandle()
		);
		pCmd->SetGraphicsRootDescriptorTable(5, bDrawEnvironmentMap
			? mRenderer.GetSRV(mResources_MainWnd.EnvironmentMap.SRV_BRDFIntegrationLUT).GetGPUDescHandle()
			: NullTex2DSRV.GetGPUDescHandle()
		);

		pCmd->SetGraphicsRootDescriptorTable(6, mRenderer.GetSRV(mResources_MainWnd.SRV_ShadowMaps_Directional).GetGPUDescHandle(0));
		pCmd->SetGraphicsRootDescriptorTable(7, mRenderer.GetSRV(mResources_MainWnd.SRV_ShadowMaps_Spot).GetGPUDescHandle(0));
		pCmd->SetGraphicsRootDescriptorTable(8, mRenderer.GetSRV(mResources_MainWnd.SRV_ShadowMaps_Point).GetGPUDescHandle(0));

		pCmd->SetGraphicsRootConstantBufferView(9, cbAddr_PerFrame);
		pCmd->SetGraphicsRootConstantBufferView(10, cbAddr_PerView);
		for (const FTerrainDrawParams& param : SceneView.terrainDrawParams)
		{		
			const bool bWireframe  = param.bWireframe;
			const bool bTessellate = param.bTessellate;
			pCmd->SetPipelineState(mRenderer.GetPSO(
				mRenderer.mTerrainPSOs.GetPSOId(
					(bTessellate ? 1 : 0), 
					(bWireframe ? 1 : 0), 
					(bMSAA ? 1 : 0),
					param.iDomain,
					param.iPartition,
					param.iOutTopology))
			);

			// cb0
			VQ_SHADER_DATA::TerrainParams* pCBuffer_Terrain = nullptr;
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr_Trn;
			pCBufferHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer_Terrain)), (void**)(&pCBuffer_Terrain), &cbAddr_Trn);
			memcpy(pCBuffer_Terrain, &param.Terrain, sizeof(param.Terrain));
			pCmd->SetGraphicsRootConstantBufferView(11, cbAddr_Trn);

			// cb1
			VQ_SHADER_DATA::TerrainTessellationParams* pCBuffer_Tessellation = nullptr;
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr_Tsl;
			pCBufferHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer_Tessellation)), (void**)(&pCBuffer_Tessellation), &cbAddr_Tsl);
			memcpy(pCBuffer_Tessellation, &param.Tessellation, sizeof(param.Tessellation));
			pCmd->SetGraphicsRootConstantBufferView(12, cbAddr_Tsl);

			// per-terrain textures
			pCmd->SetGraphicsRootDescriptorTable(0, mRenderer.GetSRV(param.MaterialSRV).GetGPUDescHandle(0));
			pCmd->SetGraphicsRootDescriptorTable(1, param.HeightmapSRV == INVALID_ID 
				? NullTex2DSRV.GetGPUDescHandle()
				: mRenderer.GetSRV(param.HeightmapSRV).GetGPUDescHandle(0)
			);

			// IA
			const VBV& vb = mRenderer.GetVertexBufferView(param.vertexIndexBuffer.first);
			const IBV& ib = mRenderer.GetIndexBufferView(param.vertexIndexBuffer.second);
			pCmd->IASetVertexBuffers(0, 1, &vb);
			pCmd->IASetIndexBuffer(&ib);
			pCmd->IASetPrimitiveTopology(bTessellate 
				? (param.iDomain == 0 ? D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST : D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST)
				: D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
			);

			// draw
			const int NumInstances = 1;
			pCmd->DrawIndexedInstanced(param.numIndices, NumInstances, 0, 0, 0);
		}
	}

	// Draw Light Meshes ------------------------------------------
	if(!SceneView.lightRenderCommands.empty())
	{
		SCOPED_GPU_MARKER(pCmd, "Lights");
		pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA
			? EBuiltinPSOs::UNLIT_PSO_MSAA_4
			: EBuiltinPSOs::UNLIT_PSO)
		);
		
		pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__WireframeUnlit));

		for (const FLightRenderCommand& lightRenderCmd : SceneView.lightRenderCommands)
		{
			FFrameConstantBufferUnlit* pCBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			pCBufferHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer)), (void**)(&pCBuffer), &cbAddr);
			pCBuffer->color            = lightRenderCmd.color;
			pCBuffer->matModelViewProj = lightRenderCmd.matWorldTransformation * SceneView.viewProj;
			pCmd->SetGraphicsRootConstantBufferView(0, cbAddr);

			const Mesh& mesh = mpScene->mMeshes.at(lightRenderCmd.meshID);
			const int LastLOD = mesh.GetNumLODs() - 1;
			const auto VBIBIDs = mesh.GetIABufferIDs(LastLOD);
			const uint32 NumIndices = mesh.GetNumIndices(LastLOD);
			const uint32 NumInstances = 1;
			const BufferID& VB_ID = VBIBIDs.first;
			const BufferID& IB_ID = VBIBIDs.second;
			const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
			const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

			pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmd->IASetVertexBuffers(0, 1, &vb);
			pCmd->IASetIndexBuffer(&ib);
			pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
		}
	}


	if (!bRenderScreenSpaceReflections)
	{
		RenderLightBounds(pCmd, pCBufferHeap, SceneView, bMSAA);
		RenderBoundingBoxes(pCmd, pCBufferHeap, SceneView, bMSAA);
		RenderDebugVertexAxes(pCmd, pCBufferHeap, SceneView, bMSAA);
		RenderOutline(pCmd, pCBufferHeap, SceneView, bMSAA, rtvHandles);
	}
	
	// Draw Environment Map ---------------------------------------
	if (bDrawEnvironmentMap)
	{
		SCOPED_GPU_MARKER(pCmd, "EnvironmentMap");

		ID3D12DescriptorHeap* ppHeaps[] = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };

		Camera skyCam = mpScene->GetActiveCamera().Clone();
		FCameraParameters p = {};
		p.bInitializeCameraController = false;
		p.ProjectionParams = skyCam.GetProjectionParameters();
		p.ProjectionParams.bPerspectiveProjection = true;
		p.ProjectionParams.FieldOfView = p.ProjectionParams.FieldOfView * RAD2DEG; // TODO: remove the need for this conversion
		p.x = p.y = p.z = 0;
		p.Yaw   = (SceneView.MainViewCameraYaw + SceneView.HDRIYawOffset)   * RAD2DEG;
		p.Pitch = SceneView.MainViewCameraPitch * RAD2DEG;
		skyCam.InitializeCamera(p);

		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		FFrameConstantBuffer * pConstBuffer = {};
		pCBufferHeap->AllocConstantBuffer(sizeof(FFrameConstantBuffer ), (void**)(&pConstBuffer), &cbAddr);
		pConstBuffer->matModelViewProj = skyCam.GetViewMatrix() * skyCam.GetProjectionMatrix();

		pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA ? EBuiltinPSOs::SKYDOME_PSO_MSAA_4 : EBuiltinPSOs::SKYDOME_PSO));
		pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__HelloWorldCube));

		pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		pCmd->SetGraphicsRootDescriptorTable(0, mRenderer.GetSRV(mResources_MainWnd.EnvironmentMap.SRV_HDREnvironment).GetGPUDescHandle());
		pCmd->SetGraphicsRootConstantBufferView(1, cbAddr);

		const UINT NumIndices = mBuiltinMeshes[EBuiltInMeshes::CUBE].GetNumIndices();
		auto VBIB = mBuiltinMeshes[EBuiltInMeshes::CUBE].GetIABufferIDs();
		auto vb = mRenderer.GetVertexBufferView(VBIB.first);
		auto ib = mRenderer.GetIndexBufferView(VBIB.second);

		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmd->IASetVertexBuffers(0, 1, &vb);
		pCmd->IASetIndexBuffer(&ib);

		pCmd->DrawIndexedInstanced(NumIndices, 1, 0, 0, 0);
	}
}

void VQEngine::RenderBoundingBoxes(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, bool bMSAA)
{
	if (!SceneView.boundingBoxRenderCommands.empty())
	{
		SCOPED_GPU_MARKER(pCmd, "BoundingBoxes");


		pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__WireframeUnlit));

		// set IA
		const FInstancedBoundingBoxRenderCommand& params = *SceneView.boundingBoxRenderCommands.begin();
		const auto VBIBIDs = params.vertexIndexBuffer;
		const uint32 NumIndices = params.numIndices;
		const BufferID& VB_ID = VBIBIDs.first;
		const BufferID& IB_ID = VBIBIDs.second;
		const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
		const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmd->IASetVertexBuffers(0, 1, &vb);
		pCmd->IASetIndexBuffer(&ib);

#if RENDER_INSTANCED_BOUNDING_BOXES
		pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA
			? EBuiltinPSOs::WIREFRAME_INSTANCED_MSAA4_PSO
			: EBuiltinPSOs::WIREFRAME_INSTANCED_PSO)
		);

		for (const FInstancedBoundingBoxRenderCommand& BBRenderCmd : SceneView.boundingBoxRenderCommands)
		{
			const int NumInstances = (int)BBRenderCmd.matWorldViewProj.size();
			if (NumInstances == 0)
				continue; // shouldnt happen

			assert(NumInstances <= MAX_INSTANCE_COUNT__UNLIT_SHADER);

			FFrameConstantBufferUnlitInstanced* pCBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			pCBufferHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer)), (void**)(&pCBuffer), &cbAddr);
			pCBuffer->color = BBRenderCmd.color;
			memcpy(pCBuffer->matModelViewProj, BBRenderCmd.matWorldViewProj.data(), sizeof(DirectX::XMMATRIX)* NumInstances);

			pCmd->SetGraphicsRootConstantBufferView(0, cbAddr);
			pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
		}

#else	
		pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA
			? EBuiltinPSOs::WIREFRAME_PSO_MSAA_4
			: EBuiltinPSOs::WIREFRAME_PSO)
		);

		const uint32 NumInstances = 1;
		for (const FBoundingBoxRenderCommand& BBRenderCmd : SceneView.boundingBoxRenderCommands)
		{
			// set constant buffer data
			FFrameConstantBufferUnlit* pCBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			pCBufferHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer)), (void**)(&pCBuffer), &cbAddr);
			pCBuffer->color = BBRenderCmd.color;
			pCBuffer->matModelViewProj = BBRenderCmd.matWorldTransformation * SceneView.viewProj;
			pCmd->SetGraphicsRootConstantBufferView(0, cbAddr);
			pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
		}
#endif
	}
}

void VQEngine::RenderOutline(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, bool bMSAA, const std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>& rtvHandles)
{
	SCOPED_GPU_MARKER(pCmd, "RenderOutlinePass");
	OutlinePass::FDrawParameters params;
	params.pCmd = pCmd;
	params.pCBufferHeap = pCBufferHeap;
	params.pSceneView = &SceneView;
	params.pMeshes = &mpScene->mMeshes;
	params.pMaterials = &mpScene->mMaterials;
	params.bMSAA = bMSAA;
	params.pRTVHandles = &rtvHandles;
	mRenderPass_Outline.RecordCommands(&params);
}

void VQEngine::RenderLightBounds(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, bool bMSAA)
{
	if (!SceneView.lightBoundsRenderCommands.empty())
	{
		SCOPED_GPU_MARKER(pCmd, "LightBounds");
		const uint32 NumInstances = 1;

		pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA ? EBuiltinPSOs::UNLIT_PSO_MSAA_4: EBuiltinPSOs::UNLIT_PSO));
		pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__WireframeUnlit));

		std::vector< D3D12_GPU_VIRTUAL_ADDRESS> cbAddresses(SceneView.lightBoundsRenderCommands.size());
		int iCbAddress = 0;
		for (const FLightRenderCommand& lightBoundRenderCmd : SceneView.lightBoundsRenderCommands)
		{
			// set constant buffer data
			FFrameConstantBufferUnlit* pCBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS& cbAddr = cbAddresses[iCbAddress++];
			pCBufferHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer)), (void**)(&pCBuffer), &cbAddr);
			pCBuffer->color = lightBoundRenderCmd.color;
			pCBuffer->color.w *= 0.01f;
			pCBuffer->matModelViewProj = lightBoundRenderCmd.matWorldTransformation * SceneView.viewProj;

			// set IA
			const Mesh& mesh = mpScene->mMeshes.at(lightBoundRenderCmd.meshID);

			const auto VBIBIDs = mesh.GetIABufferIDs();
			const uint32 NumIndices = mesh.GetNumIndices();
			const BufferID& VB_ID = VBIBIDs.first;
			const BufferID& IB_ID = VBIBIDs.second;
			const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
			const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

			pCmd->SetGraphicsRootConstantBufferView(0, cbAddr);
			pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmd->IASetVertexBuffers(0, 1, &vb);
			pCmd->IASetIndexBuffer(&ib);
			pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
		}

		pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA ? EBuiltinPSOs::WIREFRAME_PSO_MSAA_4 : EBuiltinPSOs::WIREFRAME_PSO));
		for (const FLightRenderCommand& lightBoundRenderCmd : SceneView.lightBoundsRenderCommands)
		{
			FFrameConstantBufferUnlit* pCBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			pCBufferHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer)), (void**)(&pCBuffer), &cbAddr);
			pCBuffer->color = lightBoundRenderCmd.color;
			pCBuffer->matModelViewProj = lightBoundRenderCmd.matWorldTransformation * SceneView.viewProj;

			const Mesh& mesh = mpScene->mMeshes.at(lightBoundRenderCmd.meshID);

			const auto VBIBIDs = mesh.GetIABufferIDs();
			const uint32 NumIndices = mesh.GetNumIndices();
			const BufferID& VB_ID = VBIBIDs.first;
			const BufferID& IB_ID = VBIBIDs.second;
			const VBV& vb = mRenderer.GetVertexBufferView(VB_ID);
			const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);

			pCmd->SetGraphicsRootConstantBufferView(0, cbAddr);
			pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmd->IASetVertexBuffers(0, 1, &vb);
			pCmd->IASetIndexBuffer(&ib);
			pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
		}
	}
}

void VQEngine::RenderDebugVertexAxes(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, bool bMSAA)
{
	if (!SceneView.sceneParameters.bDrawVertexLocalAxes || SceneView.debugVertexAxesRenderCommands.empty())
	{
		return;
	}

	pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__ZPrePass));
	pCmd->SetPipelineState(mRenderer.GetPSO(bMSAA ? EBuiltinPSOs::DEBUGVERTEX_LOCALSPACEVECTORS_PSO_MSAA_4 : EBuiltinPSOs::DEBUGVERTEX_LOCALSPACEVECTORS_PSO));
	for (const MeshRenderCommand_t& cmd : SceneView.debugVertexAxesRenderCommands)
	{
		FObjectConstantBufferDebugVertexVectors* pCBuffer = {};
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		pCBufferHeap->AllocConstantBuffer(sizeof(decltype(*pCBuffer)), (void**)(&pCBuffer), &cbAddr);
		pCBuffer->matWorld    = cmd.matWorld [0];
		pCBuffer->matNormal   = cmd.matNormal[0];
		pCBuffer->matViewProj = SceneView.viewProj;
		pCBuffer->LocalAxisSize = SceneView.sceneParameters.fVertexLocalAxixSize;
		
		const uint32 NumInstances = 1;
		const uint32 NumIndices = cmd.numIndices;
		const VBV& vb = mRenderer.GetVertexBufferView(cmd.vertexIndexBuffer.first);
		const IBV& ib = mRenderer.GetIndexBufferView(cmd.vertexIndexBuffer.second);

		pCmd->SetGraphicsRootConstantBufferView(1, cbAddr);
		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
		pCmd->IASetVertexBuffers(0, 1, &vb);
		pCmd->IASetIndexBuffer(&ib);
		pCmd->DrawIndexedInstanced(NumIndices, NumInstances, 0, 0, 0);
	}
}

void VQEngine::ResolveMSAA(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FPostProcessParameters& PPParams)
{
	const bool& bMSAA = mSettings.gfx.bAntiAliasing;

	if (!bMSAA)
		return;

	const FRenderingResources_MainWindow& rsc = mResources_MainWnd; // shorthand

	SCOPED_GPU_MARKER(pCmd, "ResolveMSAA");
	auto pRscColor     = mRenderer.GetTextureResource(rsc.Tex_SceneColor);
	auto pRscColorMSAA = mRenderer.GetTextureResource(rsc.Tex_SceneColorMSAA);
	auto pRscViz       = mRenderer.GetTextureResource(rsc.Tex_SceneVisualization);
	auto pRscVizMSAA   = mRenderer.GetTextureResource(rsc.Tex_SceneVisualizationMSAA);
	auto pRscMoVec     = mRenderer.GetTextureResource(rsc.Tex_SceneMotionVectors);
	auto pRscMoVecMSAA = mRenderer.GetTextureResource(rsc.Tex_SceneMotionVectorsMSAA);
	auto pRscDepthMSAA = mRenderer.GetTextureResource(rsc.Tex_SceneDepthMSAA);

	const bool bUseVizualization = ShouldUseVisualizationTarget(PPParams.DrawModeEnum);
	const bool bRenderMotionVectors = ShouldUseMotionVectorsTarget(mSettings);
	const bool bResolveRoughness = IsFFX_SSSREnabled(mSettings) && false;

	// transition barriers
	{
		SCOPED_GPU_MARKER(pCmd, "TransitionBarriers");
		std::vector< CD3DX12_RESOURCE_BARRIER> barriers;
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColorMSAA, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE));
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST));
		if (bUseVizualization)
		{
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscVizMSAA, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE));
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscViz, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST));
		}
		if (bRenderMotionVectors)
		{
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscMoVecMSAA, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE));
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscMoVec, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST));
		}
		pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
	}

	// resolve MSAA
	{
		SCOPED_GPU_MARKER(pCmd, "Resolve");
		pCmd->ResolveSubresource(pRscColor, 0, pRscColorMSAA, 0, mRenderer.GetTextureFormat(rsc.Tex_SceneColor));
		if (bUseVizualization)
		{
			pCmd->ResolveSubresource(pRscViz, 0, pRscVizMSAA, 0, mRenderer.GetTextureFormat(rsc.Tex_SceneVisualization));
		}
		if (bRenderMotionVectors)
		{
			pCmd->ResolveSubresource(pRscMoVec, 0, pRscMoVecMSAA, 0, mRenderer.GetTextureFormat(rsc.Tex_SceneMotionVectors));
		}


		if (bResolveRoughness)
		{
			// TODO: remove redundant resource transitions
			{
				std::vector< CD3DX12_RESOURCE_BARRIER> barriers;
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthMSAA, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColorMSAA, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColor, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
				pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
			}

			// since roughness is stored in A channel of the SceneColor texture, and is wrongly resolved by pCmd->ResolveSubresource(),
			// we will run a custom resolve pass on SceneColorMSAA and overwrite the incorrect resolved roughness.
			DepthMSAAResolvePass::FDrawParameters params;
			params.pCmd = pCmd;
			params.pCBufferHeap          = pCBufferHeap;
			params.SRV_MSAADepth         = rsc.SRV_SceneDepthMSAA;
			params.SRV_MSAANormals       = INVALID_ID;
			params.SRV_MSAARoughness     = rsc.SRV_SceneColorMSAA;
			params.UAV_ResolvedDepth     = INVALID_ID;
			params.UAV_ResolvedNormals   = INVALID_ID;
			params.UAV_ResolvedRoughness = rsc.UAV_SceneColor;

			mRenderPass_DepthResolve.RecordCommands(&params);

			// TODO: remove redundant resource transitions
			{
				std::vector< CD3DX12_RESOURCE_BARRIER> barriers;
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColorMSAA, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_SOURCE));
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RESOLVE_DEST));
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscDepthMSAA, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));
				pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
			}
		}
	}
}

void VQEngine::DownsampleDepth(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, TextureID DepthTextureID, SRV_ID SRVDepth)
{
	int W, H;
	mRenderer.GetTextureDimensions(DepthTextureID, W, H);

	struct FParams { uint params[4]; } CBuffer;
	CBuffer.params[0] = W;
	CBuffer.params[1] = H;
	CBuffer.params[2] = 0xDEADBEEF;
	CBuffer.params[3] = 0xDEADC0DE;

	FParams* pConstBuffer = {};
	D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
	pCBufferHeap->AllocConstantBuffer(sizeof(FParams), (void**)&pConstBuffer, &cbAddr);
	*pConstBuffer = CBuffer;

	constexpr int DispatchGroupDimensionX = 64; // even though the threadgroup dims are 32x8, they work on 64x64 region
	constexpr int DispatchGroupDimensionY = 64; // even though the threadgroup dims are 32x8, they work on 64x64 region
	const     int DispatchX = (W + (DispatchGroupDimensionX - 1)) / DispatchGroupDimensionX; // DIV_AND_ROUND_UP()
	const     int DispatchY = (H + (DispatchGroupDimensionY - 1)) / DispatchGroupDimensionY; // DIV_AND_ROUND_UP()
	constexpr int DispatchZ = 1;

	SCOPED_GPU_MARKER(pCmd, "DownsampleDepth");
	pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::DOWNSAMPLE_DEPTH_CS_PSO));
	pCmd->SetComputeRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__DownsampleDepthCS));
	pCmd->SetComputeRootDescriptorTable(0, mRenderer.GetSRV(SRVDepth).GetGPUDescHandle());
	pCmd->SetComputeRootDescriptorTable(1, mRenderer.GetUAV(mResources_MainWnd.UAV_DownsampledSceneDepth).GetGPUDescHandle());
	pCmd->SetComputeRootDescriptorTable(2, mRenderer.GetUAV(mResources_MainWnd.UAV_DownsampledSceneDepthAtomicCounter).GetGPUDescHandle());
	pCmd->SetComputeRootConstantBufferView(3, cbAddr);
	pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);
}

static DirectX::XMMATRIX GetHDRIRotationMatrix(float fHDIROffsetInRadians)
{
	const float cosB = cos(-fHDIROffsetInRadians);
	const float sinB = sin(-fHDIROffsetInRadians);
	DirectX::XMMATRIX m;
	m.r[0] = { cosB, 0, sinB, 0 };
	m.r[1] = { 0, 1, 0, 0 };
	m.r[2] = { -sinB, 0, cosB, 0 };
	m.r[3] = { 0, 0, 0, 0 };
	return m;
}
void VQEngine::RenderReflections(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView)
{
	const FSceneRenderParameters::FFFX_SSSR_UIParameters& UIParams = SceneView.sceneParameters.FFX_SSSRParameters;
	
	const bool bHasEnvironmentMap = mResources_MainWnd.EnvironmentMap.SRV_IrradianceSpec != INVALID_ID;
	int EnvMapSpecIrrCubemapDimX = 0;
	int EnvMapSpecIrrCubemapDimY = 0;
	if (bHasEnvironmentMap)
	{
		mRenderer.GetTextureDimensions(mResources_MainWnd.EnvironmentMap.Tex_IrradianceSpec, EnvMapSpecIrrCubemapDimX, EnvMapSpecIrrCubemapDimY);
	}
	
	switch (mSettings.gfx.Reflections)
	{
	case EReflections::SCREEN_SPACE_REFLECTIONS__FFX:
	{
		ScreenSpaceReflectionsPass::FDrawParameters params = {};
		// ---- cmd recording ----
		params.pCmd = pCmd;
		params.pCBufferHeap = pCBufferHeap;
		// ---- cbuffer ----
		params.ffxCBuffer.invViewProjection          = DirectX::XMMatrixInverse(nullptr, SceneView.view * SceneView.proj);
		params.ffxCBuffer.projection                 = SceneView.proj;
		params.ffxCBuffer.invProjection              = SceneView.projInverse;
		params.ffxCBuffer.view                       = SceneView.view;
		params.ffxCBuffer.invView                    = SceneView.viewInverse;
		params.ffxCBuffer.bufferDimensions[0]        = SceneView.SceneRTWidth;
		params.ffxCBuffer.bufferDimensions[1]        = SceneView.SceneRTHeight;
		params.ffxCBuffer.envMapRotation             = GetHDRIRotationMatrix(SceneView.HDRIYawOffset);
		params.ffxCBuffer.inverseBufferDimensions[0] = 1.0f / params.ffxCBuffer.bufferDimensions[0];
		params.ffxCBuffer.inverseBufferDimensions[1] = 1.0f / params.ffxCBuffer.bufferDimensions[1];
		params.ffxCBuffer.frameIndex                 = static_cast<uint32>(mNumSimulationTicks);
		params.ffxCBuffer.temporalStabilityFactor    = UIParams.temporalStability;
		params.ffxCBuffer.depthBufferThickness       = UIParams.depthBufferThickness;
		params.ffxCBuffer.roughnessThreshold         = UIParams.roughnessThreshold;
		params.ffxCBuffer.varianceThreshold          = UIParams.temporalVarianceThreshold;
		params.ffxCBuffer.maxTraversalIntersections  = UIParams.maxTraversalIterations;
		params.ffxCBuffer.minTraversalOccupancy      = UIParams.minTraversalOccupancy;
		params.ffxCBuffer.mostDetailedMip            = UIParams.mostDetailedDepthHierarchyMipLevel;
		params.ffxCBuffer.samplesPerQuad             = UIParams.samplesPerQuad;
		params.ffxCBuffer.temporalVarianceGuidedTracingEnabled = UIParams.bEnableTemporalVarianceGuidedTracing;
		params.ffxCBuffer.envMapSpecularIrradianceCubemapMipLevelCount = mResources_MainWnd.EnvironmentMap.GetNumSpecularIrradianceCubemapLODLevels(mRenderer);
		params.TexDepthHierarchy = mResources_MainWnd.Tex_DownsampledSceneDepth;
		params.TexNormals = mResources_MainWnd.Tex_SceneNormals;
		params.SRVEnvironmentSpecularIrradianceCubemap = bHasEnvironmentMap ? mResources_MainWnd.EnvironmentMap.SRV_IrradianceSpec : mResources_MainWnd.SRV_NullCubemap;
		params.SRVBRDFIntegrationLUT = bHasEnvironmentMap ? mResources_MainWnd.EnvironmentMap.SRV_BRDFIntegrationLUT : mResources_MainWnd.SRV_NullTexture2D;

		if (bHasEnvironmentMap)
		{
			D3D12_RESOURCE_BARRIER barriers[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(mResources_MainWnd.EnvironmentMap.Tex_IrradianceSpec), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(mResources_MainWnd.EnvironmentMap.SRV_BRDFIntegrationLUT), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			};
			pCmd->ResourceBarrier(_countof(barriers), barriers);
		}

		SCOPED_GPU_MARKER(pCmd, "RenderReflections FidelityFX-SSSR");
		mRenderPass_SSR.RecordCommands(&params);

		if (bHasEnvironmentMap)
		{
			D3D12_RESOURCE_BARRIER barriers[] = {
	CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(mResources_MainWnd.EnvironmentMap.Tex_IrradianceSpec),	D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
	CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(mResources_MainWnd.EnvironmentMap.SRV_BRDFIntegrationLUT),	D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
			};
			pCmd->ResourceBarrier(_countof(barriers), barriers);
		}
	} break;

	case EReflections::RAY_TRACED_REFLECTIONS:
	default:
		Log::Warning("RenderReflections(): unrecognized setting or missing implementation for reflection enum: %d", mSettings.gfx.Reflections);
		return;

	case EReflections::REFLECTIONS_OFF:
		Log::Error("RenderReflections(): called with REFLECTIONS_OFF, why?");
		return;
	}
}

static bool ShouldSkipBoundsPass(const FSceneView& SceneView)
{
	return SceneView.boundingBoxRenderCommands.empty()
		&& SceneView.lightBoundsRenderCommands.empty()
		&& SceneView.outlineRenderCommands.empty()
		&& SceneView.debugVertexAxesRenderCommands.empty();
}
void VQEngine::RenderSceneBoundingVolumes(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView, bool bMSAA)
{
	SCOPED_GPU_MARKER(pCmd, "RenderSceneBoundingVolumes");
	const bool bNoBoundingVolumeToRender = ShouldSkipBoundsPass(SceneView);

	if (bNoBoundingVolumeToRender)
		return;

	const auto& rsc = mResources_MainWnd;
	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	ID3D12Resource* pRscColorMSAA = mRenderer.GetTextureResource(rsc.Tex_SceneColorMSAA);
	ID3D12Resource* pRscColor = mRenderer.GetTextureResource(rsc.Tex_SceneColor);
	ID3D12Resource* pRscColorBB = mRenderer.GetTextureResource(rsc.Tex_SceneColorBoundingVolumes);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = mRenderer.GetRTV(bMSAA ? rsc.RTV_SceneColorMSAA : rsc.RTV_SceneColor).GetCPUDescHandle();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = mRenderer.GetDSV(bMSAA ? rsc.DSV_SceneDepthMSAA : rsc.DSV_SceneDepth).GetCPUDescHandle();

	const float RenderResolutionX = static_cast<float>(SceneView.SceneRTWidth);
	const float RenderResolutionY = static_cast<float>(SceneView.SceneRTHeight);
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT scissorsRect{ 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };

	// -----------------------------------------------------------------------------------------------------------

	
	if(bMSAA)
	{
		// transition MSAA RT
		std::vector< CD3DX12_RESOURCE_BARRIER> barriers;
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColorMSAA, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColorBB  , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST));
		pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());

		// clear MSAA RT 
		pCmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	}
	else
	{
		std::vector< CD3DX12_RESOURCE_BARRIER> barriers;
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
		pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
	}
	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
	


	// Set Viewport & Scissors
	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);

	RenderBoundingBoxes(pCmd, pCBufferHeap, SceneView, bMSAA);
	RenderLightBounds(pCmd, pCBufferHeap, SceneView, bMSAA);
	RenderDebugVertexAxes(pCmd, pCBufferHeap, SceneView, bMSAA);
	RenderOutline(pCmd, pCBufferHeap, SceneView, bMSAA, { rtvHandle });

	// resolve MSAA RT 
	if (bMSAA)
	{
		{
			std::vector< CD3DX12_RESOURCE_BARRIER> barriers;
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColorMSAA, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE));
			pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
		}

		pCmd->ResolveSubresource(pRscColorBB, 0, pRscColorMSAA, 0, mRenderer.GetTextureFormat(rsc.Tex_SceneColor));
		
		{
			std::vector< CD3DX12_RESOURCE_BARRIER> barriers;
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColorBB, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
			pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
		}
	}
	else
	{
		std::vector< CD3DX12_RESOURCE_BARRIER> barriers;
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscColor, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
		pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
	}
}

void VQEngine::CompositeReflections(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FSceneView& SceneView)
{
	const bool bNoBoundingVolumeToRender = ShouldSkipBoundsPass(SceneView);

	const bool& bMSAA = mSettings.gfx.bAntiAliasing;

	SCOPED_GPU_MARKER(pCmd, "CompositeReflections");
	
	ApplyReflectionsPass::FDrawParameters params;
	params.pCmd = pCmd;
	params.pCBufferHeap = pCBufferHeap;
	params.SRVReflectionRadiance = mRenderPass_SSR.GetPassOutputSRV();
	params.SRVBoundingVolumes = (bNoBoundingVolumeToRender || !bMSAA) ? INVALID_ID : mResources_MainWnd.SRV_SceneColorBoundingVolumes;
	params.UAVSceneRadiance = mResources_MainWnd.UAV_SceneColor;
	params.iSceneRTWidth  = SceneView.SceneRTWidth;
	params.iSceneRTHeight = SceneView.SceneRTHeight;

	{			
		D3D12_RESOURCE_BARRIER barriers[] = {
	CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(mResources_MainWnd.Tex_SceneColor), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		};
		pCmd->ResourceBarrier(_countof(barriers), barriers);
	}
	mRenderPass_ApplyReflections.RecordCommands(&params);
	{
		D3D12_RESOURCE_BARRIER barriers[] = {
	CD3DX12_RESOURCE_BARRIER::Transition(mRenderer.GetTextureResource(mResources_MainWnd.Tex_SceneColor), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		};
		pCmd->ResourceBarrier(_countof(barriers), barriers);
	}
}

void VQEngine::TransitionForPostProcessing(ID3D12GraphicsCommandList* pCmd, const FPostProcessParameters& PPParams)
{
	const bool& bMSAA = mSettings.gfx.bAntiAliasing;
	const auto& rsc = mResources_MainWnd;

	const bool bCASEnabled = PPParams.IsFFXCASEnabled() && PPParams.Sharpness > 0.0f;
	const bool bFSREnabled = PPParams.IsFSREnabled();
	const bool bVizualizationEnabled = PPParams.DrawModeEnum != EDrawMode::LIT_AND_POSTPROCESSED;
	const bool bVizualizationSceneTargetUsed = ShouldUseVisualizationTarget(PPParams.DrawModeEnum);
	const bool bMotionVectorsEnabled = ShouldUseMotionVectorsTarget(mSettings);

	auto pRscPostProcessInput = mRenderer.GetTextureResource(rsc.Tex_SceneColor);
	auto pRscTonemapperOut    = mRenderer.GetTextureResource(rsc.Tex_PostProcess_TonemapperOut);
	auto pRscFFXCASOut        = mRenderer.GetTextureResource(rsc.Tex_PostProcess_FFXCASOut);
	auto pRscFSROut           = mRenderer.GetTextureResource(rsc.Tex_PostProcess_FSR_RCASOut); // TODO: handle RCAS=off
	auto pRscVizMSAA          = mRenderer.GetTextureResource(rsc.Tex_SceneVisualizationMSAA);
	auto pRscViz              = mRenderer.GetTextureResource(rsc.Tex_SceneVisualization);
	auto pRscMoVecMSAA        = mRenderer.GetTextureResource(rsc.Tex_SceneMotionVectorsMSAA);
	auto pRscMoVec            = mRenderer.GetTextureResource(rsc.Tex_SceneMotionVectors);
	auto pRscVizOut           = mRenderer.GetTextureResource(rsc.Tex_PostProcess_VisualizationOut);
	auto pRscPostProcessOut   = bVizualizationEnabled ? pRscVizOut
		: (bFSREnabled
			? pRscFSROut 
			: (bCASEnabled
				? pRscFFXCASOut 
				: pRscTonemapperOut)
	);

	auto pRscShadowMaps_Spot = mRenderer.GetTextureResource(rsc.Tex_ShadowMaps_Spot);
	auto pRscShadowMaps_Point = mRenderer.GetTextureResource(rsc.Tex_ShadowMaps_Point);
	auto pRscShadowMaps_Directional = mRenderer.GetTextureResource(rsc.Tex_ShadowMaps_Directional);

	SCOPED_GPU_MARKER(pCmd, "TransitionForPostProcessing");
	std::vector<CD3DX12_RESOURCE_BARRIER> barriers =
	{
		  CD3DX12_RESOURCE_BARRIER::Transition(pRscPostProcessInput , (bMSAA ? D3D12_RESOURCE_STATE_RESOLVE_DEST : D3D12_RESOURCE_STATE_RENDER_TARGET), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscPostProcessOut   , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)

		, CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Spot       , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Point      , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE)
		, CD3DX12_RESOURCE_BARRIER::Transition(pRscShadowMaps_Directional, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE)
	};
	if ((bFSREnabled || bCASEnabled) && !bVizualizationEnabled)
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
	
	if (bVizualizationSceneTargetUsed)
	{
		barriers.push_back(bMSAA
			? CD3DX12_RESOURCE_BARRIER::Transition(pRscViz, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
			: CD3DX12_RESOURCE_BARRIER::Transition(pRscViz, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		);
	}

	if (bMotionVectorsEnabled)
	{
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscMoVec, (bMSAA ? D3D12_RESOURCE_STATE_RESOLVE_DEST : D3D12_RESOURCE_STATE_RENDER_TARGET), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	}
	pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
}

ID3D12Resource* VQEngine::RenderPostProcess(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, const FPostProcessParameters& PPParams)
{
	ID3D12DescriptorHeap*       ppHeaps[] = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };

	const bool bHDR = this->ShouldRenderHDR(mpWinMain->GetHWND());
	const auto& rsc = mResources_MainWnd;

	// pass io
	const SRV& srv_ColorIn          = mRenderer.GetSRV(rsc.SRV_SceneColor);
	const UAV& uav_TonemapperOut    = mRenderer.GetUAV(rsc.UAV_PostProcess_TonemapperOut);
	const UAV& uav_VisualizationOut = mRenderer.GetUAV(rsc.UAV_PostProcess_VisualizationOut);
	const SRV& srv_TonemapperOut    = mRenderer.GetSRV(rsc.SRV_PostProcess_TonemapperOut);
	const UAV& uav_FFXCASOut        = mRenderer.GetUAV(rsc.UAV_PostProcess_FFXCASOut);
	const UAV& uav_FSR_EASUOut      = mRenderer.GetUAV(rsc.UAV_PostProcess_FSR_EASUOut);
	const SRV& srv_FSR_EASUOut      = mRenderer.GetSRV(rsc.SRV_PostProcess_FSR_EASUOut);
	const UAV& uav_FSR_RCASOut      = mRenderer.GetUAV(rsc.UAV_PostProcess_FSR_RCASOut);
	const SRV& srv_FSR_RCASOut      = mRenderer.GetSRV(rsc.SRV_PostProcess_FSR_RCASOut);
	ID3D12Resource* pRscTonemapperOut = mRenderer.GetTextureResource(rsc.Tex_PostProcess_TonemapperOut);


	constexpr bool PP_ENABLE_BLUR_PASS = false;

	// compute dispatch dimensions
	const int& InputImageWidth  = PPParams.SceneRTWidth;
	const int& InputImageHeight = PPParams.SceneRTHeight;
	assert(PPParams.SceneRTWidth != 0);
	assert(PPParams.SceneRTHeight != 0);
	constexpr int DispatchGroupDimensionX = 8;
	constexpr int DispatchGroupDimensionY = 8;
	const     int DispatchRenderX = (InputImageWidth  + (DispatchGroupDimensionX - 1)) / DispatchGroupDimensionX;
	const     int DispatchRenderY = (InputImageHeight + (DispatchGroupDimensionY - 1)) / DispatchGroupDimensionY;
	constexpr int DispatchZ = 1;

	// cmds
	ID3D12Resource* pRscOutput = nullptr;
	if (PPParams.DrawModeEnum != EDrawMode::LIT_AND_POSTPROCESSED)
	{
		SCOPED_GPU_MARKER(pCmd, "RenderPostProcess_DebugViz");

		// cbuffer
		using cbuffer_t = FPostProcessParameters::FVizualizationParams;
		cbuffer_t* pConstBuffer = {};
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		pCBufferHeap->AllocConstantBuffer(sizeof(cbuffer_t), (void**)&pConstBuffer, &cbAddr);
		memcpy(pConstBuffer, &PPParams.VizParams, sizeof(PPParams.VizParams));
		pConstBuffer->iDrawMode = static_cast<int>(PPParams.DrawModeEnum); // iDrawMode is not connected to the UI

		SRV SRVIn = srv_ColorIn;
		switch (PPParams.DrawModeEnum)
		{
		case EDrawMode::DEPTH         : SRVIn = mRenderer.GetSRV(rsc.SRV_SceneDepth); break;
		case EDrawMode::NORMALS       : SRVIn = mRenderer.GetSRV(rsc.SRV_SceneNormals); break;
		case EDrawMode::AO            : SRVIn = mRenderer.GetSRV(rsc.SRV_FFXCACAO_Out); break;
		case EDrawMode::ALBEDO        : // same as below
		case EDrawMode::METALLIC      : SRVIn = mRenderer.GetSRV(rsc.SRV_SceneVisualization); break;
		case EDrawMode::ROUGHNESS     : srv_ColorIn; break;
		case EDrawMode::REFLECTIONS   : SRVIn = mRenderer.GetSRV(mRenderPass_SSR.GetPassOutputSRV()); break;
		case EDrawMode::MOTION_VECTORS: SRVIn = mRenderer.GetSRV(rsc.SRV_SceneMotionVectors); break;
		}

		pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::VIZUALIZATION_CS_PSO));
		pCmd->SetComputeRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1));
		pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		pCmd->SetComputeRootDescriptorTable(0, SRVIn.GetGPUDescHandle());
		pCmd->SetComputeRootDescriptorTable(1, uav_VisualizationOut.GetGPUDescHandle());
		pCmd->SetComputeRootConstantBufferView(2, cbAddr);
		pCmd->Dispatch(DispatchRenderX, DispatchRenderY, DispatchZ);

		pRscOutput = mRenderer.GetTextureResource(rsc.Tex_PostProcess_VisualizationOut);
	}
	else
	{
		SCOPED_GPU_MARKER(pCmd, "RenderPostProcess");
		const SRV& srv_blurOutput         = mRenderer.GetSRV(rsc.SRV_PostProcess_BlurOutput);

		if constexpr (PP_ENABLE_BLUR_PASS && PPParams.bEnableGaussianBlur)
		{
			SCOPED_GPU_MARKER(pCmd, "BlurCS");
			const UAV& uav_BlurIntermediate = mRenderer.GetUAV(rsc.UAV_PostProcess_BlurIntermediate);
			const UAV& uav_BlurOutput       = mRenderer.GetUAV(rsc.UAV_PostProcess_BlurOutput);
			const SRV& srv_blurIntermediate = mRenderer.GetSRV(rsc.SRV_PostProcess_BlurIntermediate);
			auto pRscBlurIntermediate = mRenderer.GetTextureResource(rsc.Tex_PostProcess_BlurIntermediate);
			auto pRscBlurOutput       = mRenderer.GetTextureResource(rsc.Tex_PostProcess_BlurOutput);


			FPostProcessParameters::FBlurParams* pBlurParams = nullptr;

			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			pCBufferHeap->AllocConstantBuffer(sizeof(FPostProcessParameters::FBlurParams), (void**)&pBlurParams, &cbAddr);
			pBlurParams->iImageSizeX = PPParams.SceneRTWidth;
			pBlurParams->iImageSizeY = PPParams.SceneRTHeight;

			{
				SCOPED_GPU_MARKER(pCmd, "BlurX");
				pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_X_PSO));
				pCmd->SetComputeRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1));

				const int FFXDispatchGroupDimension = 16;
				const     int FFXDispatchX = (InputImageWidth  + (FFXDispatchGroupDimension - 1)) / FFXDispatchGroupDimension;
				const     int FFXDispatchY = (InputImageHeight + (FFXDispatchGroupDimension - 1)) / FFXDispatchGroupDimension;

				pCmd->SetComputeRootDescriptorTable(0, srv_ColorIn.GetGPUDescHandle());
				pCmd->SetComputeRootDescriptorTable(1, uav_BlurIntermediate.GetGPUDescHandle());
				pCmd->SetComputeRootConstantBufferView(2, cbAddr);
				pCmd->Dispatch(DispatchRenderX, DispatchRenderY, DispatchZ);

				const CD3DX12_RESOURCE_BARRIER pBarriers[] =
				{
					  CD3DX12_RESOURCE_BARRIER::Transition(pRscBlurIntermediate, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
					, CD3DX12_RESOURCE_BARRIER::Transition(pRscBlurOutput      , D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)

				};
				pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
			}


			{
				SCOPED_GPU_MARKER(pCmd, "BlurY");
				pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::GAUSSIAN_BLUR_CS_NAIVE_Y_PSO));
				pCmd->SetComputeRootDescriptorTable(0, srv_blurIntermediate.GetGPUDescHandle());
				pCmd->SetComputeRootDescriptorTable(1, uav_BlurOutput.GetGPUDescHandle());
				pCmd->SetComputeRootConstantBufferView(2, cbAddr);
				pCmd->Dispatch(DispatchRenderX, DispatchRenderY, DispatchZ);

				const CD3DX12_RESOURCE_BARRIER pBarriers[] =
				{
					  CD3DX12_RESOURCE_BARRIER::Transition(pRscBlurOutput      , D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
					, CD3DX12_RESOURCE_BARRIER::Transition(pRscBlurIntermediate, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
				};
				pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
			}
		}

		{
			SCOPED_GPU_MARKER(pCmd, "TonemapperCS");

			FPostProcessParameters::FTonemapper* pConstBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			pCBufferHeap->AllocConstantBuffer(sizeof(FPostProcessParameters::FTonemapper), (void**)&pConstBuffer, &cbAddr);
			*pConstBuffer = PPParams.TonemapperParams;

			pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::TONEMAPPER_PSO));
			pCmd->SetComputeRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1));
			pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
			pCmd->SetComputeRootDescriptorTable(0, PP_ENABLE_BLUR_PASS ? srv_blurOutput.GetGPUDescHandle() : srv_ColorIn.GetGPUDescHandle());
			pCmd->SetComputeRootDescriptorTable(1, uav_TonemapperOut.GetGPUDescHandle());
			pCmd->SetComputeRootConstantBufferView(2, cbAddr);
			pCmd->Dispatch(DispatchRenderX, DispatchRenderY, DispatchZ);
			pRscOutput = pRscTonemapperOut;
		}

#if !DISABLE_FIDELITYFX_CAS
		if(PPParams.IsFFXCASEnabled() && PPParams.Sharpness > 0.0f)
		{
			ID3D12Resource* pRscFFXCASOut = mRenderer.GetTextureResource(rsc.Tex_PostProcess_FFXCASOut);
			const CD3DX12_RESOURCE_BARRIER pBarriers[] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
			};
			pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);

			SCOPED_GPU_MARKER(pCmd, "FFX-CAS CS");

			unsigned* pConstBuffer = {};
			D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
			const size_t cbSize = sizeof(unsigned) * 8;
			pCBufferHeap->AllocConstantBuffer(cbSize, (void**)&pConstBuffer, &cbAddr);
			memcpy(pConstBuffer, PPParams.FFXCASParams.CASConstantBlock, cbSize);
			

			ID3D12PipelineState* pPSO = mRenderer.GetPSO(EBuiltinPSOs::FFX_CAS_CS_PSO);
			assert(pPSO);
			pCmd->SetPipelineState(pPSO);
			pCmd->SetComputeRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::CS__SRV1_UAV1_ROOTCBV1));
			pCmd->SetComputeRootDescriptorTable(0, srv_TonemapperOut.GetGPUDescHandle());
			pCmd->SetComputeRootDescriptorTable(1, uav_FFXCASOut.GetGPUDescHandle());
			pCmd->SetComputeRootConstantBufferView(2, cbAddr);
			
			// each FFX-CAS CS thread processes 4 pixels.
			// workgroup is 64 threads, hence 256 (16x16) pixels are processed per thread group that is dispatched
 			constexpr int CAS_WORKGROUP_WORK_DIMENSION = 16;
			const int CASDispatchX = (InputImageWidth  + (CAS_WORKGROUP_WORK_DIMENSION - 1)) / CAS_WORKGROUP_WORK_DIMENSION;
			const int CASDispatchY = (InputImageHeight + (CAS_WORKGROUP_WORK_DIMENSION - 1)) / CAS_WORKGROUP_WORK_DIMENSION;
			pCmd->Dispatch(CASDispatchX, CASDispatchY, DispatchZ);
			pRscOutput = pRscFFXCASOut;
		}
#endif

		if (PPParams.IsFSREnabled()) // FSR & CAS are mutually exclusive
		{
			if (bHDR)
			{
				// TODO: color conversion pass, barriers etc.
			}

			std::vector<CD3DX12_RESOURCE_BARRIER> pBarriers =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut
				, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
					, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
				)
			};
			pCmd->ResourceBarrier((UINT)pBarriers.size(), pBarriers.data());

			{
				SCOPED_GPU_MARKER(pCmd, "FSR-EASU CS");

				unsigned* pConstBuffer = {};
				D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
				const size_t cbSize = sizeof(unsigned) * 16;
				pCBufferHeap->AllocConstantBuffer(cbSize, (void**)&pConstBuffer, &cbAddr);
				memcpy(pConstBuffer, PPParams.FSR_EASUParams.EASUConstantBlock, cbSize);

				ID3D12PipelineState* pPSO = mRenderer.GetPSO(EBuiltinPSOs::FFX_FSR1_EASU_CS_PSO);
				assert(pPSO);
				pCmd->SetPipelineState(pPSO);
				pCmd->SetComputeRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__FFX_FSR1));
				pCmd->SetComputeRootDescriptorTable(0, srv_TonemapperOut.GetGPUDescHandle());
				pCmd->SetComputeRootDescriptorTable(1, uav_FSR_EASUOut.GetGPUDescHandle());
				pCmd->SetComputeRootConstantBufferView(2, cbAddr);

				// each FSR-EASU CS thread processes 4 pixels.
				// workgroup is 64 threads, hence 256 (16x16) pixels are processed per thread group that is dispatched
				constexpr int WORKGROUP_WORK_DIMENSION = 16;
				const int DispatchX = (PPParams.DisplayResolutionWidth  + (WORKGROUP_WORK_DIMENSION - 1)) / WORKGROUP_WORK_DIMENSION;
				const int DispatchY = (PPParams.DisplayResolutionHeight + (WORKGROUP_WORK_DIMENSION - 1)) / WORKGROUP_WORK_DIMENSION;
				pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);
			}
			const bool bFFX_RCAS_Enabled = true; // TODO: drive with UI ?
			ID3D12Resource* pRscFSR1Out = mRenderer.GetTextureResource(rsc.Tex_PostProcess_FSR_RCASOut);
			if (bFFX_RCAS_Enabled)
			{
				ID3D12Resource* pRscEASUOut = mRenderer.GetTextureResource(rsc.Tex_PostProcess_FSR_EASUOut);
				ID3D12Resource* pRscRCASOut = mRenderer.GetTextureResource(rsc.Tex_PostProcess_FSR_RCASOut);

				SCOPED_GPU_MARKER(pCmd, "FSR-RCAS CS");
				{
					std::vector<CD3DX12_RESOURCE_BARRIER> barriers;
					barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscEASUOut
						, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
						, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
					));
					pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
				}

				ID3D12PipelineState* pPSO = mRenderer.GetPSO(EBuiltinPSOs::FFX_FSR1_RCAS_CS_PSO);

				FPostProcessParameters::FFSR1_RCAS* pConstBuffer = {};
				D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
				pCBufferHeap->AllocConstantBuffer(sizeof(FPostProcessParameters::FFSR1_RCAS), (void**)&pConstBuffer, &cbAddr);
				*pConstBuffer = PPParams.FSR_RCASParams;

				pCmd->SetPipelineState(pPSO);
				pCmd->SetComputeRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__FFX_FSR1));
				pCmd->SetComputeRootDescriptorTable(0, srv_FSR_EASUOut.GetGPUDescHandle());
				pCmd->SetComputeRootDescriptorTable(1, uav_FSR_RCASOut.GetGPUDescHandle());
				pCmd->SetComputeRootConstantBufferView(2, cbAddr);

				// each FSR-RCAS CS thread processes 4 pixels.
				// workgroup is 64 threads, hence 256 (16x16) pixels are processed per thread group that is dispatched
				constexpr int WORKGROUP_WORK_DIMENSION = 16;
				const int DispatchX = (PPParams.DisplayResolutionWidth + (WORKGROUP_WORK_DIMENSION - 1)) / WORKGROUP_WORK_DIMENSION;
				const int DispatchY = (PPParams.DisplayResolutionHeight + (WORKGROUP_WORK_DIMENSION - 1)) / WORKGROUP_WORK_DIMENSION;
				pCmd->Dispatch(DispatchX, DispatchY, DispatchZ);

				{
					const CD3DX12_RESOURCE_BARRIER pBarriers[] =
					{
						CD3DX12_RESOURCE_BARRIER::Transition(pRscEASUOut, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
						CD3DX12_RESOURCE_BARRIER::Transition(pRscRCASOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
					};
					pCmd->ResourceBarrier(_countof(pBarriers), pBarriers);
				}

			}

			pRscOutput = pRscFSR1Out;
		}
	}

	return pRscOutput;
}

void VQEngine::RenderUI(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, FWindowRenderContext& ctx, const FPostProcessParameters& PPParams, ID3D12Resource* pRscInput)
{
	const float           RenderResolutionX = static_cast<float>(PPParams.DisplayResolutionWidth);
	const float           RenderResolutionY = static_cast<float>(PPParams.DisplayResolutionHeight);
	D3D12_VIEWPORT                  viewport{ 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	const auto                      VBIBIDs = mBuiltinMeshes[EBuiltInMeshes::TRIANGLE].GetIABufferIDs();
	const BufferID&                   IB_ID = VBIBIDs.second;
	const IBV&                        ib    = mRenderer.GetIndexBufferView(IB_ID);
	ID3D12DescriptorHeap*         ppHeaps[] = { mRenderer.GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };
	D3D12_RECT                 scissorsRect { 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
	SwapChain&                    swapchain = ctx.SwapChain;

	const bool bVizualizationEnabled = PPParams.DrawModeEnum != EDrawMode::LIT_AND_POSTPROCESSED;
	const bool bHDR = this->ShouldRenderHDR(mpWinMain->GetHWND());
	const bool bFFXCASEnabled = PPParams.IsFFXCASEnabled() && PPParams.Sharpness > 0.0f;
	const bool bFSREnabled = PPParams.IsFSREnabled();
	const SRV& srv_ColorIn = bVizualizationEnabled ?
		mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_VisualizationOut) : (
		bFFXCASEnabled 
		? mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_FFXCASOut)
		: (bFSREnabled 
			? mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_FSR_RCASOut) 
			: mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_TonemapperOut)));

	ID3D12Resource* pRscFSR1Out       = mRenderer.GetTextureResource(mResources_MainWnd.Tex_PostProcess_FSR_RCASOut);
	ID3D12Resource* pRscTonemapperOut = mRenderer.GetTextureResource(mResources_MainWnd.Tex_PostProcess_TonemapperOut);
	ID3D12Resource* pRscFFXCASOut     = mRenderer.GetTextureResource(mResources_MainWnd.Tex_PostProcess_FFXCASOut);
	ID3D12Resource* pRscUI            = mRenderer.GetTextureResource(mResources_MainWnd.Tex_UI_SDR);

	ID3D12Resource*          pSwapChainRT = swapchain.GetCurrentBackBufferRenderTarget();
	
	//Log::Info("RenderUI: Backbuffer[%d]: 0x%08x | pCmd = %p", swapchain.GetCurrentBackBufferIndex(), pSwapChainRT, pCmd);
	
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = bHDR 
		? mRenderer.GetRTV(mResources_MainWnd.RTV_UI_SDR).GetCPUDescHandle()
		: swapchain.GetCurrentBackBufferRTVHandle();

	// barriers
	{	
		// Transition Input & Output resources
		// ignore the tonemapper barrier if CAS is enabeld as it'll already be issued.
		CD3DX12_RESOURCE_BARRIER SwapChainTransition = CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		CD3DX12_RESOURCE_BARRIER UITransition = CD3DX12_RESOURCE_BARRIER::Transition(pRscUI, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		std::vector< CD3DX12_RESOURCE_BARRIER> barriers;
		barriers.push_back(bHDR ? UITransition : SwapChainTransition);
		if (bVizualizationEnabled)
		{
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscInput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
		}
		else
		{
			if (bFFXCASEnabled)
			{
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscFFXCASOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
			}
			else
			{
				if (bFSREnabled)
				{
					barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
				}
				else
				{
					barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pRscTonemapperOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
				}
			}
		}
		pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());
	}

	if (!bHDR)
	{
		if (mUIState.mpMagnifierState->bUseMagnifier)
		{
			SCOPED_GPU_MARKER(pCmd, "MagnifierPass");
			MagnifierPass::FDrawParameters MagnifierDrawParams;
			MagnifierDrawParams.pCmd = pCmd;
			MagnifierDrawParams.pCBufferHeap = pCBufferHeap;
			MagnifierDrawParams.IndexBufferView = ib;
			MagnifierDrawParams.RTV = rtvHandle;
			MagnifierDrawParams.SRVColorInput = srv_ColorIn;
			MagnifierDrawParams.pCBufferParams = mUIState.mpMagnifierState->pMagnifierParams;
			mRenderPass_Magnifier.RecordCommands(&MagnifierDrawParams);
		}
		else
		{
			SCOPED_GPU_MARKER(pCmd, "SwapchainPassthrough");
			pCmd->SetPipelineState(mRenderer.GetPSO(bHDR ? EBuiltinPSOs::HDR_FP16_SWAPCHAIN_PSO : EBuiltinPSOs::FULLSCREEN_TRIANGLE_PSO));
			pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__FullScreenTriangle));
			pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
			pCmd->SetGraphicsRootDescriptorTable(0, srv_ColorIn.GetGPUDescHandle());

			pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pCmd->IASetVertexBuffers(0, 1, NULL);
			pCmd->IASetIndexBuffer(&ib);

			pCmd->RSSetViewports(1, &viewport);
			pCmd->RSSetScissorRects(1, &scissorsRect);

			pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);

			pCmd->DrawIndexedInstanced(3, 1, 0, 0, 0);
		}
	}
	else
	{
		if (mUIState.mpMagnifierState->bUseMagnifier)
		{
			// TODO: make magnifier work w/ HDR when the new HDR monitor arrives
		}
		else
		{
			pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);
			const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
			pCmd->ClearRenderTargetView(rtvHandle, clearColor, 0, NULL);
		}
	}
#if !VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	{
		SCOPED_GPU_MARKER(pCmd, "UI");

		struct cb
		{
			float matTransformation[4][4];
		};

		ImGuiIO& io = ImGui::GetIO();

		ImGui::Render();

		ImDrawData* draw_data = ImGui::GetDrawData();

		// Create and grow vertex/index buffers if needed
		char* pVertices = NULL;
		D3D12_VERTEX_BUFFER_VIEW VerticesView;

		pCBufferHeap->AllocVertexBuffer(draw_data->TotalVtxCount, sizeof(ImDrawVert), (void**)&pVertices, &VerticesView);

		char* pIndices = NULL;
		D3D12_INDEX_BUFFER_VIEW IndicesView;
		pCBufferHeap->AllocIndexBuffer(draw_data->TotalIdxCount, sizeof(ImDrawIdx), (void**)&pIndices, &IndicesView);

		ImDrawVert* vtx_dst = (ImDrawVert*)pVertices;
		ImDrawIdx* idx_dst = (ImDrawIdx*)pIndices;
		for (int n = 0; n < draw_data->CmdListsCount; n++)
		{
			const ImDrawList* cmd_list = draw_data->CmdLists[n];
			memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
			memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
			vtx_dst += cmd_list->VtxBuffer.Size;
			idx_dst += cmd_list->IdxBuffer.Size;
		}

		// Setup orthographic projection matrix into our constant buffer
		D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
		{
			cb* constant_buffer;
			pCBufferHeap->AllocConstantBuffer(sizeof(cb), (void**)&constant_buffer, &cbAddr);

			float L = 0.0f;
			float R = io.DisplaySize.x;
			float B = io.DisplaySize.y;
			float T = 0.0f;
			float proj[4][4] =
			{
				{ 2.0f / (R - L)   , 0.0f             , 0.0f  ,  0.0f },
				{ 0.0f             , 2.0f / (T - B)   , 0.0f  ,  0.0f },
				{ 0.0f             , 0.0f             , 0.5f  ,  0.0f },
				{ (R + L) / (L - R), (T + B) / (B - T), 0.5f  ,  1.0f },
			};
			memcpy(constant_buffer->matTransformation, proj, sizeof(proj));
		}

		// Setup viewport
		D3D12_VIEWPORT vp;
		memset(&vp, 0, sizeof(D3D12_VIEWPORT));
		vp.Width = io.DisplaySize.x;
		vp.Height = io.DisplaySize.y;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		vp.TopLeftX = vp.TopLeftY = 0.0f;
		pCmd->RSSetViewports(1, &vp);

		// set pipeline & render state
		pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::UI_PSO));
		pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__HelloWorldCube));
		pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);

		pCmd->IASetIndexBuffer(&IndicesView);
		pCmd->IASetVertexBuffers(0, 1, &VerticesView);
		pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmd->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		pCmd->SetGraphicsRootConstantBufferView(1, cbAddr);

		// Render command lists
		int vtx_offset = 0;
		int idx_offset = 0;
		for (int n = 0; n < draw_data->CmdListsCount; n++)
		{
			const ImDrawList* drawList = draw_data->CmdLists[n];
			for (int cmd_i = 0; cmd_i < drawList->CmdBuffer.Size; cmd_i++)
			{
				const ImDrawCmd* pcmd = &drawList->CmdBuffer[cmd_i];
				if (pcmd->UserCallback)
				{
					pcmd->UserCallback(drawList, pcmd);
				}
				else
				{
					const D3D12_RECT r =
					{
						(LONG)pcmd->ClipRect.x,
						(LONG)pcmd->ClipRect.y,
						(LONG)pcmd->ClipRect.z,
						(LONG)pcmd->ClipRect.w
					};
					pCmd->RSSetScissorRects(1, &r);
					D3D12_GPU_DESCRIPTOR_HANDLE h = { (UINT64)(pcmd->TextureId) };
					pCmd->SetGraphicsRootDescriptorTable(0, h);

					pCmd->DrawIndexedInstanced(pcmd->ElemCount, 1, idx_offset, vtx_offset, 0);
				}
				idx_offset += pcmd->ElemCount;
			}
			vtx_offset += drawList->VtxBuffer.Size;
		}
	}
#endif

	if(!bHDR)
	{
		SCOPED_GPU_MARKER(pCmd, "SwapchainTransitionToPresent");
		// Transition SwapChain for Present
		pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT
			, D3D12_RESOURCE_STATE_RENDER_TARGET
			, D3D12_RESOURCE_STATE_PRESENT)
		);
	}
}

void VQEngine::CompositUIToHDRSwapchain(ID3D12GraphicsCommandList* pCmd, DynamicBufferHeap* pCBufferHeap, FWindowRenderContext& ctx, const FPostProcessParameters& PPParams)
{
	SCOPED_GPU_MARKER(pCmd, "CompositUIToHDRSwapchain");

	// handles
	const auto VBIBIDs = mBuiltinMeshes[EBuiltInMeshes::TRIANGLE].GetIABufferIDs();
	const BufferID& IB_ID = VBIBIDs.second;
	const IBV& ib = mRenderer.GetIndexBufferView(IB_ID);
	const bool bHDR = this->ShouldRenderHDR(mpWinMain->GetHWND());
	const bool bFFXCASEnabled = PPParams.IsFFXCASEnabled() && PPParams.Sharpness > 0.0f;
	const bool bFSREnabled = PPParams.IsFSREnabled();

	SwapChain& swapchain = ctx.SwapChain;
	ID3D12Resource* pSwapChainRT = swapchain.GetCurrentBackBufferRenderTarget();
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = swapchain.GetCurrentBackBufferRTVHandle();
	ID3D12Resource* pRscUI = mRenderer.GetTextureResource(mResources_MainWnd.Tex_UI_SDR);
	const SRV& srv_UI_SDR = mRenderer.GetSRV(mResources_MainWnd.SRV_UI_SDR);
	const SRV& srv_SceneColor = bFFXCASEnabled
		? mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_FFXCASOut)
		: (bFSREnabled
			? mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_FSR_RCASOut)
			: mRenderer.GetSRV(mResources_MainWnd.SRV_PostProcess_TonemapperOut));

	const int W = mpWinMain->GetWidth();
	const int H = mpWinMain->GetHeight();

	// transition barriers
	std::vector< CD3DX12_RESOURCE_BARRIER> barriers;
	CD3DX12_RESOURCE_BARRIER SwapChainTransition = CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	CD3DX12_RESOURCE_BARRIER UITransition = CD3DX12_RESOURCE_BARRIER::Transition(pRscUI, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	barriers.push_back(UITransition);
	barriers.push_back(SwapChainTransition);
	pCmd->ResourceBarrier((UINT)barriers.size(), barriers.data());

	// states
	D3D12_VIEWPORT vp = {};
	vp.Width  = static_cast<FLOAT>(W);
	vp.Height = static_cast<FLOAT>(H);
	vp.MaxDepth = 1.0f;

	D3D12_RECT rect = {0, 0, W, H};

	// cbuffer
	float* pConstBuffer = {};
	D3D12_GPU_VIRTUAL_ADDRESS cbAddr = {};
	const size_t cbSize = sizeof(float) * 1;
	pCBufferHeap->AllocConstantBuffer(cbSize, (void**)&pConstBuffer, &cbAddr);
	*pConstBuffer = PPParams.TonemapperParams.UIHDRBrightness;


	// set states
	pCmd->SetPipelineState(mRenderer.GetPSO(EBuiltinPSOs::UI_HDR_scRGB_PSO)); // TODO: HDR10/PQ PSO?
	pCmd->SetGraphicsRootSignature(mRenderer.GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__UI_HDR_Composite));
	pCmd->SetGraphicsRootDescriptorTable(0, srv_SceneColor.GetGPUDescHandle());
	pCmd->SetGraphicsRootDescriptorTable(1, srv_UI_SDR.GetGPUDescHandle());
	//pCmd->SetGraphicsRootConstantBufferView(1, cbAddr);
	pCmd->SetGraphicsRoot32BitConstant(2, *((UINT*)pConstBuffer), 0);
	pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmd->IASetVertexBuffers(0, 1, NULL);
	pCmd->IASetIndexBuffer(&ib);
	pCmd->RSSetScissorRects(1, &rect);
	pCmd->RSSetViewports(1, &vp);
	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);

	// draw fullscreen triangle
	pCmd->DrawIndexedInstanced(3, 1, 0, 0, 0);

	{
		//SCOPED_GPU_MARKER(pCmd, "SwapchainTransitionToPresent");
		// Transition SwapChain for Present
		pCmd->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT
			, D3D12_RESOURCE_STATE_RENDER_TARGET
			, D3D12_RESOURCE_STATE_PRESENT)
		);
	}
}

HRESULT VQEngine::PresentFrame(FWindowRenderContext& ctx)
{
	SCOPED_CPU_MARKER("Present");
	HRESULT hr = ctx.SwapChain.Present();
	return hr;
}

