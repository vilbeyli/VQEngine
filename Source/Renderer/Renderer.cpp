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

#include "Renderer.h"

#include "Device.h"
#include "../Application/Window.h"

#ifdef _DEBUG
	#define ENABLE_DEBUG_LAYER      1
	#define ENABLE_VALIDATION_LAYER 1
#else
	#define ENABLE_DEBUG_LAYER      0
	#define ENABLE_VALIDATION_LAYER 0
#endif


void VQRenderer::Initialize(const FRendererInitializeParameters& RendererInitParams)
{
	// Create the device
	FDeviceCreateDesc deviceDesc = {};
	deviceDesc.bEnableDebugLayer = ENABLE_DEBUG_LAYER;
	deviceDesc.bEnableValidationLayer = ENABLE_VALIDATION_LAYER;
	mDevice.Create(deviceDesc);

	// Create the present queues & swapchains associated with each window passed into the VQRenderer
	const size_t NumWindows = RendererInitParams.Windows.size();
	mRenderContexts.resize(NumWindows);
	for(size_t i = 0; i< NumWindows; ++i)
	{
		const FWindowRepresentation& wnd = RendererInitParams.Windows[i];

		mRenderContexts[i].pDevice = &mDevice;

		// Create the GFX queue for presenting the SwapChain
		mRenderContexts[i].PresentQueue.CreateCommandQueue(mRenderContexts[i].pDevice, CommandQueue::ECommandQueueType::GFX);

		// Create the SwapChain
		FSwapChainCreateDesc swapChainDesc = {};
		swapChainDesc.numBackBuffers = 3;
		swapChainDesc.pDevice = mRenderContexts[i].pDevice->GetDevicePtr();
		swapChainDesc.pWindow = &wnd;
		swapChainDesc.pCmdQueue = &mRenderContexts[i].PresentQueue;
		mRenderContexts[i].SwapChain.Create(swapChainDesc);
	}
	
	
}

void VQRenderer::Exit()
{
	for (FRenderWindowContext ctx : mRenderContexts)
	{
		// ctx.pDevice is shared, its handled below
		ctx.PresentQueue.DestroyCommandQueue();
		ctx.SwapChain.Destroy();
	}

	mDevice.Destroy();
}

void VQRenderer::Render()
{
}
