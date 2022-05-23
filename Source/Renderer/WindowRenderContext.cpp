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

void FWindowRenderContext::InitializeContext(const Window* pWin, Device* pVQDevice, int NumSwapchainBuffers, bool bVSync, bool bHDRSwapchain)
{
	HWND hwnd = pWin->GetHWND();

	this->pDevice = pVQDevice;
	ID3D12Device4* pDevice = this->pDevice->GetDevice4Ptr();

	// create the present queue
	char c[127] = {}; sprintf_s(c, "PresentQueue<0x%p>", hwnd);
	this->PresentQueue.Create(pVQDevice, CommandQueue::EType::GFX, c); // Create the GFX queue for presenting the SwapChain
	
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
	mCommandAllocatorsGFX.resize(NumSwapchainBuffers);
	mCommandAllocatorsCompute.resize(NumSwapchainBuffers);
	mCommandAllocatorsCopy.resize(NumSwapchainBuffers);
	for (int b = 0; b < NumSwapchainBuffers; ++b)
	{
		// make at least one command allocator and command ready for each kind of queue, per back buffer
		this->mCommandAllocatorsGFX[b].resize(1);
		this->mCommandAllocatorsCompute[b].resize(1);
		this->mCommandAllocatorsCopy[b].resize(1);

		pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT , IID_PPV_ARGS(&this->mCommandAllocatorsGFX[b][0]));
		pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&this->mCommandAllocatorsCompute[b][0]));
		pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY   , IID_PPV_ARGS(&this->mCommandAllocatorsCopy[b][0]));
		SetName(this->mCommandAllocatorsGFX[b][0]    , "RenderContext<0x%p>::CmdAllocGFX[%d][0]    ", hwnd, b);
		SetName(this->mCommandAllocatorsCompute[b][0], "RenderContext<0x%p>::CmdAllocCompute[%d][0]", hwnd, b);
		SetName(this->mCommandAllocatorsCopy[b][0]   , "RenderContext<0x%p>::CmdAllocCopy[%d][0]   ", hwnd, b);
	}


	// create 1 command list for each type
#if 0
	// TODO: device4 create command list1 doesn't require command allocator, figure out if a further refactor is needed.
	// https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nn-d3d12-id3d12device4
	auto pDevice4 = pVQDevice->GetDevice4Ptr();
	pDevice4->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, this->mCommandAllocatorsGFX[b][0]);
#else	
	this->mpCmdGFX.resize(1);
	this->mpCmdCompute.resize(1);
	this->mpCmdCopy.resize(1);
	pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT , this->mCommandAllocatorsGFX[0][0], nullptr, IID_PPV_ARGS(&this->mpCmdGFX[0]));
	//pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, this->mCommandAllocatorsGFX[0][0], nullptr, IID_PPV_ARGS(&this->mpCmdCompute[0]));
	//pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY   , this->mCommandAllocatorsGFX[0][0], nullptr, IID_PPV_ARGS(&this->mpCmdCopy[0]));
	this->mpCmdGFX[0]->Close();
	//this->mpCmdCompute[0]->Close();
	//this->mpCmdCopy[0]->Close();
#endif

	// create 1 constant buffer
	this->mDynamicHeap_ConstantBuffer.resize(1);
	this->mDynamicHeap_ConstantBuffer[0].Create(pDevice, NumSwapchainBuffers, 32 * MEGABYTE);
}

void FWindowRenderContext::CleanupContext()
{
	this->SwapChain.Destroy(); // syncs GPU before releasing resources
	this->PresentQueue.Destroy();
	this->pDevice = nullptr;

	// clean up command lists & memory
	assert(mCommandAllocatorsGFX.size() == mCommandAllocatorsCompute.size());
	assert(mCommandAllocatorsCompute.size() == mCommandAllocatorsCopy.size());
	assert(mCommandAllocatorsCopy.size() == mCommandAllocatorsGFX.size());
	for (size_t BackBuffer = 0; BackBuffer < mCommandAllocatorsGFX.size(); ++BackBuffer)
	{
		for (ID3D12CommandAllocator* pCmdAlloc : mCommandAllocatorsGFX[BackBuffer]    ) if (pCmdAlloc) pCmdAlloc->Release();
		for (ID3D12CommandAllocator* pCmdAlloc : mCommandAllocatorsCompute[BackBuffer]) if (pCmdAlloc) pCmdAlloc->Release();
		for (ID3D12CommandAllocator* pCmdAlloc : mCommandAllocatorsCopy[BackBuffer]   ) if (pCmdAlloc) pCmdAlloc->Release();
	}
	for (ID3D12GraphicsCommandList* pCmd : mpCmdGFX    ) if (pCmd) pCmd->Release();
	for (ID3D12CommandList*         pCmd : mpCmdCompute) if (pCmd) pCmd->Release();
	for (ID3D12CommandList*         pCmd : mpCmdCopy   ) if (pCmd) pCmd->Release();
	for (DynamicBufferHeap& Heap : mDynamicHeap_ConstantBuffer) // per cmd recording thread?
	{
		Heap.Destroy();
	}
}

void FWindowRenderContext::AllocateCommandLists(CommandQueue::EType eQueueType, size_t NumRecordingThreads)
{
	ID3D12Device* pD3DDevice = pDevice->GetDevicePtr();

	D3D12_COMMAND_LIST_TYPE CMD_LIST_TYPE;
	switch (eQueueType)
	{
	case CommandQueue::EType::GFX    : CMD_LIST_TYPE = D3D12_COMMAND_LIST_TYPE_DIRECT ; break;
	case CommandQueue::EType::COMPUTE: CMD_LIST_TYPE = D3D12_COMMAND_LIST_TYPE_COMPUTE; break;
	case CommandQueue::EType::COPY   : CMD_LIST_TYPE = D3D12_COMMAND_LIST_TYPE_COPY   ; break;
	default: assert(false); break;
	}
	
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
			if (eQueueType == CommandQueue::EType::GFX)
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
	assert(eQueueType == CommandQueue::EType::GFX); // Reset() is ID3D12GraphicsCommandList function

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
		assert(mpCmdGFX[iThread]);
		mpCmdGFX[iThread]->Reset(pAlloc, nullptr);
	}
}

ID3D12CommandList* FWindowRenderContext::GetCommandListPtr(CommandQueue::EType eQueueType, size_t THREAD_INDEX)
{
	int BACK_BUFFER_INDEX = SwapChain.GetCurrentBackBufferIndex();
	switch (eQueueType)
	{
	case CommandQueue::EType::GFX    : assert(THREAD_INDEX < mpCmdGFX.size())    ; return mpCmdGFX[THREAD_INDEX];
	case CommandQueue::EType::COMPUTE: assert(THREAD_INDEX < mpCmdCompute.size()); return mpCmdCompute[THREAD_INDEX];
	case CommandQueue::EType::COPY   : assert(THREAD_INDEX < mpCmdCopy.size())   ; return mpCmdCopy[THREAD_INDEX];
	default: assert(false);
	}
	return nullptr;
}

std::vector<ID3D12CommandAllocator*>& FWindowRenderContext::GetCommandAllocators(CommandQueue::EType eQueueType)
{
	int BACK_BUFFER_INDEX = SwapChain.GetCurrentBackBufferIndex();
	switch (eQueueType)
	{
	case CommandQueue::EType::GFX    : return mCommandAllocatorsGFX[BACK_BUFFER_INDEX];
	case CommandQueue::EType::COMPUTE: return mCommandAllocatorsCompute[BACK_BUFFER_INDEX];
	case CommandQueue::EType::COPY   : return mCommandAllocatorsCopy[BACK_BUFFER_INDEX];
	}
	assert(false); // shouldn't happen
	return mCommandAllocatorsGFX[BACK_BUFFER_INDEX];
}

