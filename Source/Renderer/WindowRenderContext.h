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

#include "Device.h"
#include "SwapChain.h"
#include "CommandQueue.h"
#include "ResourceHeaps.h"
#include "ResourceViews.h"
#include "Buffer.h"
#include "Texture.h"
#include "Shader.h"

#include "../Engine/Core/Types.h"
#include "../Engine/Core/Platform.h"
#include "../Engine/Settings.h"

#define VQUTILS_SYSTEMINFO_INCLUDE_D3D12 1
#include "../../Libs/VQUtils/Source/SystemInfo.h" // FGPUInfo
#include "../../Libs/VQUtils/Source/Image.h"
#include "../../Libs/VQUtils/Source/Multithreading.h"

#include <vector>
#include <unordered_map>
#include <array>
#include <queue>
#include <set>

namespace D3D12MA { class Allocator; }
class Window;
struct ID3D12RootSignature;
struct ID3D12PipelineState;



class FWindowRenderContext
{
public:
	void InitializeContext(const Window* pWin, Device* pDevice, int NumSwapchainBuffers, bool bVSync, bool bHDRSwapchain);
	void CleanupContext();

	void AllocateCommandLists(CommandQueue::EType eQueueType, size_t NumRecordingThreads);
	void ResetCommandLists(CommandQueue::EType eQueueType, size_t NumRecordingThreads);

	void AllocateConstantBufferMemory(uint32_t NumHeaps, uint32_t MemoryPerHeap);
	
	inline DynamicBufferHeap& GetConstantBufferHeap(size_t iThread) { return mDynamicHeap_ConstantBuffer[iThread]; }
	inline unsigned short     GetNumSwapchainBuffers() const { return SwapChain.GetNumBackBuffers(); }
	inline unsigned short     GetCurrentSwapchainBufferIndex() const { return SwapChain.GetCurrentBackBufferIndex(); }

	ID3D12CommandList* GetCommandListPtr(CommandQueue::EType eQueueType, int THREAD_INDEX);
	inline size_t GetNumCurrentlyRecordingThreads(CommandQueue::EType eQueueType) const { return mNumCurrentlyRecordingThreads[eQueueType]; }

	// returns the current back buffer's command allocators
	std::vector<ID3D12CommandAllocator*>& GetCommandAllocators(CommandQueue::EType eQueueType);

	// :(
	inline std::vector<ID3D12GraphicsCommandList*>& GetGFXCommandListPtrs() { return mpCmdGFX; }
	inline std::vector<ID3D12CommandList*>& GetComputeCommandListPtrs() { return mpCmdCompute; }
	inline std::vector<ID3D12CommandList*>& GetCopyCommandListPtrs() { return mpCmdCopy; }
	inline std::vector<ID3D12CommandList*>& GetCommandListPtrs(CommandQueue::EType eQueueType)
	{
		switch (eQueueType)
		{
		case CommandQueue::EType::GFX    : return (std::vector<ID3D12CommandList*>&)mpCmdGFX;
		case CommandQueue::EType::COMPUTE: return mpCmdCompute;
		case CommandQueue::EType::COPY   : return mpCmdCopy;
		}
		return mpCmdCompute; // shouldn't happen
	}

public:
	int WindowDisplayResolutionX = -1;
	int WindowDisplayResolutionY = -1;

	Device*      pDevice = nullptr;
	SwapChain    SwapChain;
	CommandQueue PresentQueue;

private:
	// command list allocators per back buffer, per recording thread
	std::vector<std::vector<ID3D12CommandAllocator*>>   mCommandAllocatorsGFX;     
	std::vector<std::vector<ID3D12CommandAllocator*>>   mCommandAllocatorsCompute;
	std::vector<std::vector<ID3D12CommandAllocator*>>   mCommandAllocatorsCopy;

	// command lists per recording thread
	std::vector<ID3D12GraphicsCommandList*> mpCmdGFX;
	std::vector<ID3D12CommandList*>         mpCmdCompute;
	std::vector<ID3D12CommandList*>         mpCmdCopy;
	
	// constant buffers per recording thread
	std::vector<DynamicBufferHeap> mDynamicHeap_ConstantBuffer;

	size_t mNumCurrentlyRecordingThreads[CommandQueue::EType::NUM_COMMAND_QUEUE_TYPES];
};

