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


#include "SwapChain.h"
#include "CommandQueue.h"

#include "../Application/Platform.h" // CHECK_HR
#include "../Application/Window.h" // CHECK_HR
#include "../../Libs/VQUtils/Source/Log.h"
#include "../../Libs/VQUtils/Source/utils.h"


#include <dxgi1_6.h>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#ifdef _DEBUG
#pragma comment(lib, "dxguid.lib")
#include <DXGIDebug.h>
#endif 

#include <cassert>
#include <vector>

#define NUM_MAX_BACK_BUFFERS 3

FWindowRepresentation::FWindowRepresentation(const std::unique_ptr<Window>& pWnd)
    : hwnd(pWnd->GetHWND())
    , width(pWnd->GetWidth())
    , height(pWnd->GetHeight())
{}

// The programming model for swap chains in D3D12 is not identical to that in earlier versions of D3D. 
// The programming convenience, for example, of supporting automatic resource rotation that was present 
// in D3D10 and D3D11 is now NOT supported.
bool SwapChain::Create(const FSwapChainCreateDesc& desc)
{
    assert(desc.pDevice);
    assert(desc.pWindow && desc.pWindow->hwnd);
    assert(desc.pCmdQueue && desc.pCmdQueue->pQueue);
    assert(desc.numBackBuffers > 0 && desc.numBackBuffers <= NUM_MAX_BACK_BUFFERS);

    this->mpDevice = desc.pDevice;
    this->mHwnd = desc.pWindow->hwnd;
    this->mNumBackBuffer = desc.numBackBuffers;

    HRESULT hr = {};
    IDXGIFactory4* pDxgiFactory = nullptr;
    hr = CreateDXGIFactory1(IID_PPV_ARGS(&pDxgiFactory));
    if (FAILED(hr))
    {
        Log::Error("SwapChain::Create(): Couldn't create DXGI Factory.");
        return false;
    }

    // https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-flip-model
    // https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-1-4-improvements
    // DXGI_SWAP_EFFECT_FLIP_DISCARD    should be preferred when applications fully render over the backbuffer before 
    //                                  presenting it or are interested in supporting multi-adapter scenarios easily.
    // DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL should be used by applications that rely on partial presentation optimizations 
    //                                  or regularly read from previously presented backbuffers.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount        = desc.numBackBuffers;
    swapChainDesc.Height             = desc.pWindow->height;
    swapChainDesc.Width              = desc.pWindow->width;
    swapChainDesc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.SampleDesc.Count   = 1;
    swapChainDesc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.Flags              = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    // SwapChain creation methods for the newer DXGI_SWAP_CHAIN_DESC1
    // https://docs.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforhwnd
    // https://docs.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforcorewindow
    // https://docs.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforcomposition
    //
    // CoreWindow  -> Windows Store Apps
    // HWND        -> Windows Desktop Apps
    // Composition -> XAML & DirectComposition Apps
    //
    IDXGISwapChain1* pSwapChain = nullptr;
    hr = pDxgiFactory->CreateSwapChainForHwnd(
          desc.pCmdQueue->pQueue
        , desc.pWindow->hwnd
        , &swapChainDesc
        , NULL
        , NULL
        , &pSwapChain
    );

    const bool bSuccess = SUCCEEDED(hr);
    if (bSuccess)
    {
        pSwapChain->QueryInterface(IID_PPV_ARGS(&this->mpSwapChain));
        pSwapChain->Release();
    }
    else
    {
        std::string reason;
        switch (hr)
        {
        case E_OUTOFMEMORY           : reason = "Out of memory"; break;
        case DXGI_ERROR_INVALID_CALL : reason = "DXGI Invalid call"; break;
        // lookup: https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-error
        default                      : reason = "UNKNOWN"; break; 
        }
        Log::Error("Couldn't create Swapchain: %s", reason.c_str());
    }

    pDxgiFactory->Release();
    return bSuccess;
}

void SwapChain::Destroy()
{
    // https://docs.microsoft.com/en-us/windows/win32/direct3d12/swap-chains
    // Full-screen swap chains continue to have the restriction that 
    // SetFullscreenState(FALSE, NULL) must be called before the final 
    // release of the swap chain. 
    SetFullscreen(false);

    if(mpSwapChain) mpSwapChain->Release();
}

void SwapChain::Resize(int w, int h)
{
    assert(false);
}

void SwapChain::SetFullscreen(bool bState)
{
    mpSwapChain->SetFullscreenState(bState ? TRUE : FALSE, NULL);
}

bool SwapChain::IsFullscreen() const
{
    assert(false);
    return false;
}

void SwapChain::Present(bool bVSync)
{
    constexpr UINT VSYNC_INTERVAL = 1;

    // TODO: glitch detection and avoidance 
    // https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-flip-model#avoiding-detecting-and-recovering-from-glitches

    HRESULT hr = {};
    UINT FlagPresent = !bVSync 
        ? DXGI_PRESENT_ALLOW_TEARING // works only in Windowed mode
        : 0;

    if (bVSync) hr = mpSwapChain->Present(VSYNC_INTERVAL, FlagPresent);
    else        hr = mpSwapChain->Present(0, FlagPresent);

    if (hr != S_OK)
    {
        Log::Error("SwapChain::Present(): Error on Present(): HRESULT=%u", hr);

        // https://docs.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present
        switch (hr)
        {
        case DXGI_ERROR_DEVICE_RESET:
            // TODO: call HandleDeviceReset() from whoever will be responsible
            break;
        case DXGI_ERROR_DEVICE_REMOVED:
            // TODO: call HandleDeviceReset()
            break;
        case DXGI_STATUS_OCCLUDED:
            // TODO: call HandleStatusOcclueded() 
            break;
        default:
            assert(false); // unhandled Present() return code
            break;
        }
    }
}
