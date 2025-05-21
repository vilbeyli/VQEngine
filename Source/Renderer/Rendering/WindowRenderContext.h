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

#include "Core/SwapChain.h"
#include "Core/CommandQueue.h"
#include "Resources/ResourceHeaps.h"
#include "Resources/Buffer.h"

#if 1

namespace D3D12MA { class Allocator; }
class Window;
struct ID3D12RootSignature;
struct ID3D12PipelineState;
class Device;

class FWindowRenderContext
{
public:
	FWindowRenderContext(CommandQueue& PresentQueueIn) : PresentQueue(PresentQueueIn) {}
	void InitializeContext(const Window* pWin, Device* pDevice, int NumSwapchainBuffers, bool bVSync, bool bHDRSwapchain);
	void CleanupContext();

	inline unsigned short     GetNumSwapchainBuffers() const { return SwapChain.GetNumBackBuffers(); }
	inline unsigned short     GetCurrentSwapchainBufferIndex() const { return SwapChain.GetCurrentBackBufferIndex(); }

#if 0
	inline DynamicBufferHeap& GetConstantBufferHeap(size_t iThread) { return mDynamicHeap_RenderingConstantBuffer[iThread]; }
	inline ID3D12CommandList* GetCommandListPtr(ECommandQueueType eQueueType, size_t THREAD_INDEX) { return GetCommandListPtrs(eQueueType)[THREAD_INDEX]; };
	inline UINT GetNumCurrentlyRecordingThreads(ECommandQueueType eQueueType) const { return mNumCurrentlyRecordingRenderingThreads[eQueueType]; }

	// returns the current back buffer's command allocators
	inline std::vector<ID3D12CommandAllocator*>& GetCommandAllocators(ECommandQueueType eQueueType) { return mRenderingCommandAllocators[eQueueType][SwapChain.GetCurrentBackBufferIndex()]; };
	inline std::vector<ID3D12CommandList*>& GetCommandListPtrs(ECommandQueueType eQueueType){ return mpRenderingCmds[eQueueType]; }
	inline std::vector<ID3D12CommandList*>& GetGFXCommandListPtrs() { return GetCommandListPtrs(ECommandQueueType::GFX); }
	inline std::vector<ID3D12CommandList*>& GetComputeCommandListPtrs() { return GetCommandListPtrs(ECommandQueueType::COMPUTE); }
	inline std::vector<ID3D12CommandList*>& GetCopyCommandListPtrs() { return GetCommandListPtrs(ECommandQueueType::COPY); }
#endif

public:
	int WindowDisplayResolutionX = -1;
	int WindowDisplayResolutionY = -1;

	Device*       pDevice = nullptr;
	SwapChain     SwapChain;
	CommandQueue& PresentQueue;
};


#endif