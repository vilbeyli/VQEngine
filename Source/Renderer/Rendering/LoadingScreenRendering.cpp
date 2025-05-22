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

#include "Shaders/LightingConstantBufferData.h"

#include "Engine/GPUMarker.h"
#include "Engine/Scene/SceneViews.h"
#include "Engine/UI/VQUI.h"
#include "Engine/Core/Window.h"
#include "Engine/LoadingScreen.h"

#include "Libs/VQUtils/Source/utils.h"
#include "Libs/imgui/imgui.h"


HRESULT VQRenderer::PreRenderLoadingScreen(ThreadPool& WorkerThreads, const Window* pWindow, const FGraphicsSettings& GFXSettings, const FUIState& UIState)
{
	SCOPED_CPU_MARKER("Renderer.PreRenderScene");
	WaitMainSwapchainReady();
	WaitHeapsInitialized();

	FWindowRenderContext& ctx = this->GetWindowRenderContext(pWindow->GetHWND());
	const int SWAPCHAIN_INDEX = ctx.SwapChain.GetCurrentBackBufferIndex();
	const int NUM_SWAPCHAIN_BACKBUFFERS = ctx.SwapChain.GetNumBackBuffers();
	constexpr int NUM_COMMAND_RECORDING_THREADS = 1;
	{
		SCOPED_CPU_MARKER("AllocCmdLists");
		AllocateCommandLists(ECommandQueueType::GFX, SWAPCHAIN_INDEX, NUM_COMMAND_RECORDING_THREADS);
	}
	{
		SCOPED_CPU_MARKER("ResetCmdLists");
		ResetCommandLists(ECommandQueueType::GFX, SWAPCHAIN_INDEX, NUM_COMMAND_RECORDING_THREADS);
	}
	
	ID3D12DescriptorHeap* ppHeaps[] = { this->GetDescHeap(EResourceHeapType::CBV_SRV_UAV_HEAP) };
	{
		SCOPED_CPU_MARKER("Cmd.SetDescHeap");
		auto& vpGFXCmds = mpRenderingCmds[ECommandQueueType::GFX];
		for (uint32_t iGFX = 0; iGFX < NUM_COMMAND_RECORDING_THREADS; ++iGFX)
		{
			static_cast<ID3D12GraphicsCommandList*>(vpGFXCmds[iGFX])->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		}
	}

	return S_OK;
}

HRESULT VQRenderer::RenderLoadingScreen(const Window* pWindow, const FLoadingScreenData& LoadingScreenData, bool bUseHDRRenderPath)
{
	SCOPED_CPU_MARKER("RenderLoadingScreen");
	HRESULT hr = S_OK;
	
	HWND hwnd = pWindow->GetHWND();
	FWindowRenderContext& ctx = mRenderContextLookup.at(hwnd);

	ID3D12GraphicsCommandList* pCmd = (ID3D12GraphicsCommandList*)mpRenderingCmds[ECommandQueueType::GFX][0];

	// Transition SwapChain RT
	ID3D12Resource* pSwapChainRT = ctx.SwapChain.GetCurrentBackBufferRenderTarget();
	CD3DX12_RESOURCE_BARRIER barrierPW = CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	CD3DX12_RESOURCE_BARRIER barrierWP = CD3DX12_RESOURCE_BARRIER::Transition(pSwapChainRT, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	pCmd->ResourceBarrier(1, &barrierPW);

	// Clear RT
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle = ctx.SwapChain.GetCurrentBackBufferRTVHandle();
	const float clearColor[] =
	{
		LoadingScreenData.SwapChainClearColor[0],
		LoadingScreenData.SwapChainClearColor[1],
		LoadingScreenData.SwapChainClearColor[2],
		LoadingScreenData.SwapChainClearColor[3]
	};
	pCmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

	// Draw Triangle
	const float           RenderResolutionX = static_cast<float>(ctx.WindowDisplayResolutionX);
	const float           RenderResolutionY = static_cast<float>(ctx.WindowDisplayResolutionY);
	D3D12_VIEWPORT        viewport          { 0.0f, 0.0f, RenderResolutionX, RenderResolutionY, 0.0f, 1.0f };
	D3D12_RECT            scissorsRect      { 0, 0, (LONG)RenderResolutionX, (LONG)RenderResolutionY };
	
	D3D12_INDEX_BUFFER_VIEW nullIBV = {};
	nullIBV.Format = DXGI_FORMAT_R32_UINT;
	nullIBV.SizeInBytes = 0;
	nullIBV.BufferLocation = 0;

	pCmd->OMSetRenderTargets(1, &rtvHandle, FALSE, NULL);

	{
		SCOPED_CPU_MARKER_C("WAIT_PSO_WORKER_DISPATCH", 0xFFFF0000);
		mLatchPSOLoaderDispatched.wait();
	}
	
	if (!mPSOCompileResults.empty())
	{
		const FPSOCompileResult& result = this->WaitPSOReady(bUseHDRRenderPath ? EBuiltinPSOs::HDR_FP16_SWAPCHAIN_PSO : EBuiltinPSOs::FULLSCREEN_TRIANGLE_PSO);
		mPSOs[result.id] = result.pPSO;
	}

	this->WaitLoadingScreenReady();
	pCmd->SetPipelineState(this->GetPSO(bUseHDRRenderPath ? EBuiltinPSOs::HDR_FP16_SWAPCHAIN_PSO : EBuiltinPSOs::FULLSCREEN_TRIANGLE_PSO));
	pCmd->SetGraphicsRootSignature(this->GetBuiltinRootSignature(EBuiltinRootSignatures::LEGACY__FullScreenTriangle));

	pCmd->SetGraphicsRootDescriptorTable(0, this->GetShaderResourceView(LoadingScreenData.GetSelectedLoadingScreenSRV_ID()).GetGPUDescHandle());

	pCmd->RSSetViewports(1, &viewport);
	pCmd->RSSetScissorRects(1, &scissorsRect);

	pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmd->IASetVertexBuffers(0, 1, NULL);
	pCmd->IASetIndexBuffer(&nullIBV);

	pCmd->DrawInstanced(3, 1, 0, 0);

	pCmd->ResourceBarrier(1, &barrierWP); // Transition SwapChain for Present

	pCmd->Close();

	{
		SCOPED_CPU_MARKER("ExecuteCommandLists()");
		ctx.PresentQueue.pQueue->ExecuteCommandLists(1, (ID3D12CommandList**)&pCmd);
	}

	hr = PresentFrame(ctx);
	if(hr == S_OK)
		ctx.SwapChain.MoveToNextFrame();

	return hr;
}