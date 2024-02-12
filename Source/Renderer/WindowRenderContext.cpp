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


#include "WindowRenderContext.h"
#include "../Engine/Core/Window.h"

static D3D12_COMMAND_LIST_TYPE GetDX12CmdListType(CommandQueue::EType type)
{
	switch (type)
	{
	case CommandQueue::GFX     : return D3D12_COMMAND_LIST_TYPE_DIRECT;
	case CommandQueue::COMPUTE : return D3D12_COMMAND_LIST_TYPE_COMPUTE;
	case CommandQueue::COPY    : return D3D12_COMMAND_LIST_TYPE_COPY;
	}
	assert(false);
	return D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE; // shouldnt happen
}

void FWindowRenderContext::InitializeContext(const Window* pWin, Device* pVQDevice, int NumSwapchainBuffers, bool bVSync, bool bHDRSwapchain)
{
	HWND hwnd = pWin->GetHWND();

	this->pDevice = pVQDevice;
	ID3D12Device4* pDevice = this->pDevice->GetDevice4Ptr();

	// create the present queue
	char c[127] = {}; sprintf_s(c, "PresentQueue<0x%p>", hwnd);
	this->PresentQueue = PresentQueue;
	
	// create teh swapchain
	FSwapChainCreateDesc swapChainDesc = {};
	swapChainDesc.numBackBuffers = NumSwapchainBuffers;
	swapChainDesc.pDevice        = pVQDevice->GetDevicePtr();
	swapChainDesc.pWindow        = pWin;
	swapChainDesc.pCmdQueue      = &this->PresentQueue;
	swapChainDesc.bVSync         = bVSync;
	swapChainDesc.bHDR           = bHDRSwapchain;
	swapChainDesc.bitDepth       = bHDRSwapchain ? _16 : _8; // currently no support for HDR10 / R10G10B10A2 signals
	swapChainDesc.bFullscreen    = false; // TODO: exclusive fullscreen to be deprecated. App shouldn't make dxgi mode changes.
	this->SwapChain.Create(swapChainDesc);
	if (bHDRSwapchain)
	{
		FSetHDRMetaDataParams p = {}; // default
		// Set default HDRMetaData - the engine is likely loading resources at this stage 
		// and all the HDRMetaData parts are not ready yet.
		// Engine dispatches an event to set HDR MetaData when mSystemInfo is initialized.
		this->SwapChain.SetHDRMetaData(p);
	}

	// allocate per-backbuffer containers
	for (int i = 0; i < CommandQueue::EType::NUM_COMMAND_QUEUE_TYPES; ++i)
	{
		mCommandAllocators[i].resize(NumSwapchainBuffers);
	}
	for (int b = 0; b < NumSwapchainBuffers; ++b)
	{
		for (int i = 0; i < CommandQueue::EType::NUM_COMMAND_QUEUE_TYPES; ++i)
		{
			mCommandAllocators[i][b].resize(1); // make at least one command allocator and command ready for each kind of queue, per back buffer
			D3D12_COMMAND_LIST_TYPE t = GetDX12CmdListType((CommandQueue::EType)i);
			pDevice->CreateCommandAllocator(t, IID_PPV_ARGS(&this->mCommandAllocators[i][b][0]));
		}

		SetName(this->mCommandAllocators[CommandQueue::EType::GFX][b][0], "RenderContext<0x%p>::CmdAllocGFX[%d][0]", hwnd, b);
		SetName(this->mCommandAllocators[CommandQueue::EType::COPY][b][0], "RenderContext<0x%p>::CmdAllocCopy[%d][0]", hwnd, b);
		SetName(this->mCommandAllocators[CommandQueue::EType::COMPUTE][b][0], "RenderContext<0x%p>::CmdAllocCompute[%d][0]", hwnd, b);
	}


	// create 1 command list for each type
#if 0
	// TODO: device4 create command list1 doesn't require command allocator, figure out if a further refactor is needed.
	// https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nn-d3d12-id3d12device4
	auto pDevice4 = pVQDevice->GetDevice4Ptr();
	pDevice4->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, this->mCommandAllocators[CommandQueue::EType::GFX][b][b][0]);
#else	
	for (int q = 0; q < CommandQueue::EType::NUM_COMMAND_QUEUE_TYPES; ++q)
	{
		mpCmds[q].resize(1);
		D3D12_COMMAND_LIST_TYPE t = GetDX12CmdListType((CommandQueue::EType)q);
		pDevice->CreateCommandList(0, t, this->mCommandAllocators[q][0][0], nullptr, IID_PPV_ARGS(&this->mpCmds[q][0]));
		static_cast<ID3D12GraphicsCommandList*>(this->mpCmds[q][0])->Close();
	}
#endif

	// create 1 constant buffer
	this->mDynamicHeap_ConstantBuffer.resize(1);
	this->mDynamicHeap_ConstantBuffer[0].Create(pDevice, NumSwapchainBuffers, 64 * MEGABYTE);
}

void FWindowRenderContext::CleanupContext()
{
	this->SwapChain.Destroy(); // syncs GPU before releasing resources
	this->pDevice = nullptr;

	// clean up command lists & memory
	assert(mCommandAllocators[CommandQueue::EType::GFX].size() == mCommandAllocators[CommandQueue::EType::COMPUTE].size());
	assert(mCommandAllocators[CommandQueue::EType::COMPUTE].size() == mCommandAllocators[CommandQueue::EType::COPY].size());
	assert(mCommandAllocators[CommandQueue::EType::COPY].size() == mCommandAllocators[CommandQueue::EType::GFX].size());
	for (size_t BackBuffer = 0; BackBuffer < mCommandAllocators[CommandQueue::EType::GFX].size(); ++BackBuffer)
	{
		for (int q = 0; q < CommandQueue::EType::NUM_COMMAND_QUEUE_TYPES; ++q)
		for (ID3D12CommandAllocator* pCmdAlloc : mCommandAllocators[q][BackBuffer]) 
			if (pCmdAlloc) 
				pCmdAlloc->Release();
	}
	
	for (int i = 0; i < CommandQueue::EType::NUM_COMMAND_QUEUE_TYPES; ++i)
	for (ID3D12CommandList* pCmd : mpCmds[i]) 
		if (pCmd) 
			pCmd->Release();

	for (DynamicBufferHeap& Heap : mDynamicHeap_ConstantBuffer) // per cmd recording thread?
	{
		Heap.Destroy();
	}
}

void FWindowRenderContext::AllocateCommandLists(CommandQueue::EType eQueueType, size_t NumRecordingThreads)
{
	ID3D12Device* pD3DDevice = pDevice->GetDevicePtr();

	D3D12_COMMAND_LIST_TYPE CMD_LIST_TYPE = GetDX12CmdListType(eQueueType);
	std::vector<ID3D12CommandAllocator*>& vCmdAllocators = GetCommandAllocators(eQueueType);
	std::vector<ID3D12CommandList*>&      vCmdListPtrs   = GetCommandListPtrs(eQueueType);

	mNumCurrentlyRecordingThreads[eQueueType] = static_cast<UINT>(NumRecordingThreads);

	// we need to create new command list allocators
	const size_t NumAlreadyAllocatedCommandListAllocators = vCmdAllocators.size();
	if (NumAlreadyAllocatedCommandListAllocators < NumRecordingThreads)
	{
		// create command allocators and command lists for the new threads
		vCmdAllocators.resize(NumRecordingThreads);

		assert(NumAlreadyAllocatedCommandListAllocators >= 1);
		for (size_t iNewCmdListAlloc = NumAlreadyAllocatedCommandListAllocators; iNewCmdListAlloc < NumRecordingThreads; ++iNewCmdListAlloc)
		{
			// create the command allocator
			ID3D12CommandAllocator*& pAlloc = vCmdAllocators[iNewCmdListAlloc];
			pD3DDevice->CreateCommandAllocator(CMD_LIST_TYPE, IID_PPV_ARGS(&pAlloc));
			SetName(pAlloc, "CmdListAlloc[%d]", (int)iNewCmdListAlloc);
		}
	}

	// we need to create new command lists
	const size_t NumAlreadyAllocatedCommandLists = vCmdListPtrs.size();
	if (NumAlreadyAllocatedCommandLists < NumRecordingThreads)
	{
		// create command allocators and command lists for the new threads
		vCmdListPtrs.resize(NumRecordingThreads);

		assert(NumAlreadyAllocatedCommandLists >= 1);
		for (size_t iNewCmdListAlloc = NumAlreadyAllocatedCommandLists; iNewCmdListAlloc < NumRecordingThreads; ++iNewCmdListAlloc)
		{
			// create the command list
			ID3D12CommandList* pCmd = nullptr;
			pD3DDevice->CreateCommandList(0, CMD_LIST_TYPE, vCmdAllocators[iNewCmdListAlloc], nullptr, IID_PPV_ARGS(&vCmdListPtrs[iNewCmdListAlloc]));
			pCmd = vCmdListPtrs[iNewCmdListAlloc];
			SetName(pCmd, "pCmd[%d]", (int)iNewCmdListAlloc);

			// close if gfx command list
			//if (eQueueType == CommandQueue::EType::GFX)
			{
				ID3D12GraphicsCommandList* pGfxCmdList = (ID3D12GraphicsCommandList*)pCmd;
				pGfxCmdList->Close();
			}
		}
	}
}
void FWindowRenderContext::AllocateConstantBufferMemory(uint32_t NumHeaps, uint32_t MemoryPerHeap)
{
	const size_t NumAlreadyAllocatedHeaps = mDynamicHeap_ConstantBuffer.size();

	// we need to create new dynamic memory heaps
	if (NumAlreadyAllocatedHeaps < NumHeaps)
	{
		assert(NumAlreadyAllocatedHeaps >= 1);
		const uint32_t NumBackBuffers = SwapChain.GetNumBackBuffers();

		mDynamicHeap_ConstantBuffer.resize(NumHeaps);

		for (uint32_t iHeap = (uint32_t)NumAlreadyAllocatedHeaps; iHeap < NumHeaps; ++iHeap)
		{
			mDynamicHeap_ConstantBuffer[iHeap].Create(pDevice->GetDevicePtr(), NumBackBuffers, MemoryPerHeap);
		}
	}
}

void FWindowRenderContext::ResetCommandLists(CommandQueue::EType eQueueType, size_t NumRecordingThreads)
{
	std::vector<ID3D12CommandAllocator*>& vCmdAllocators = GetCommandAllocators(eQueueType);
	assert(NumRecordingThreads <= vCmdAllocators.size());

	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	for (size_t iThread = 0; iThread < NumRecordingThreads; ++iThread)
	{
		ID3D12CommandAllocator*& pAlloc = vCmdAllocators[iThread];
		ThrowIfFailed(pAlloc->Reset());

		// However, when ExecuteCommandList() is called on a particular command 
		// list, that command list can then be reset at any time and must be before 
		// re-recording.
		assert(mpCmds[eQueueType][iThread]);
		static_cast<ID3D12GraphicsCommandList*>(mpCmds[eQueueType][iThread])->Reset(pAlloc, nullptr);
	}
}
