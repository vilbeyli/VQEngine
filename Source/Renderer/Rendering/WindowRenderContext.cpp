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


#include "Rendering/WindowRenderContext.h"
#include "Engine/Core/Window.h"
#include "Core/Device.h"
#include "Core/Common.h"

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
	swapChainDesc.bFullscreen    = false; // Exclusive fullscreen is deprecated. App shouldn't make dxgi mode changes.
	this->SwapChain.Create(swapChainDesc);
	if (bHDRSwapchain)
	{
		FSetHDRMetaDataParams p = {}; // default
		// Set default HDRMetaData - the engine is likely loading resources at this stage 
		// and all the HDRMetaData parts are not ready yet.
		// Engine dispatches an event to set HDR MetaData when mSystemInfo is initialized.
		this->SwapChain.SetHDRMetaData(p);
	}
}

void FWindowRenderContext::CleanupContext()
{
	this->SwapChain.Destroy(); // syncs GPU before releasing resources
	this->pDevice = nullptr;
}

