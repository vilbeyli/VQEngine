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
#include "../Application/Window.h"
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

#define NUM_MAX_BACK_BUFFERS  3
#define LOG_SWAPCHAIN_VERBOSE 0

FWindowRepresentation::FWindowRepresentation(const std::unique_ptr<Window>& pWnd, bool bVSyncIn)
    : hwnd(pWnd->GetHWND())
    , width(pWnd->GetWidth())
    , height(pWnd->GetHeight())
    , bVSync(bVSyncIn)
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
    this->mNumBackBuffers = desc.numBackBuffers;
    this->mpPresentQueue = desc.pCmdQueue->pQueue;

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
    swapChainDesc.Flags              = desc.bVSync 
        ? 0
        : DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

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

    // Create Fence & Fence Event
    this->mFenceValues.resize(this->mNumBackBuffers, 0);
    D3D12_FENCE_FLAGS FenceFlags = D3D12_FENCE_FLAG_NONE;
    mpDevice->CreateFence(this->mFenceValues[this->mICurrentBackBuffer], FenceFlags, IID_PPV_ARGS(&this->mpFence));
    ++mFenceValues[mICurrentBackBuffer];
    mHEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (mHEvent == nullptr)
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }

    // -- Create the Back Buffers (render target views) Descriptor Heap -- //
    // describe an rtv descriptor heap and create
    D3D12_DESCRIPTOR_HEAP_DESC RTVHeapDesc = {};
    RTVHeapDesc.NumDescriptors = this->mNumBackBuffers; // number of descriptors for this heap.
    RTVHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; // this heap is a render target view heap

    // This heap will not be directly referenced by the shaders (not shader visible), as this will store the output from the pipeline
    // otherwise we would set the heap's flag to D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
    RTVHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ID3D12DescriptorHeap* rtvDescriptorHeap = nullptr;
    hr = this->mpDevice->CreateDescriptorHeap(&RTVHeapDesc, IID_PPV_ARGS(&this->mpDescHeapRTV));
    if (FAILED(hr))
    {
        assert(false); // TODO: err msg
    }

    // From Adam Sawicki's D3D12MA sample code:
    // get the size of a descriptor in this heap (this is a rtv heap, so only rtv descriptors should be stored in it.
    // descriptor sizes may vary from g_Device to g_Device, which is why there is no set size and we must ask the 
    // g_Device to give us the size. we will use this size to increment a descriptor handle offset
    const UINT RTVDescSize = this->mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE hRTV{ this->mpDescHeapRTV->GetCPUDescriptorHandleForHeapStart() };

    // Create a RTV for each SwapChain buffer
    this->mRenderTargets.resize(this->mNumBackBuffers);
    for (int i = 0; i < this->mNumBackBuffers; i++)
    {
        hr = this->mpSwapChain->GetBuffer(i, IID_PPV_ARGS(&this->mRenderTargets[i]));
        if (FAILED(hr))
        {
            assert(false);
        }

        this->mpDevice->CreateRenderTargetView(this->mRenderTargets[i], nullptr, hRTV);
        hRTV.ptr += RTVDescSize; 
    }

    return bSuccess;
}

void SwapChain::Destroy()
{
    // https://docs.microsoft.com/en-us/windows/win32/direct3d12/swap-chains
    // Full-screen swap chains continue to have the restriction that 
    // SetFullscreenState(FALSE, NULL) must be called before the final 
    // release of the swap chain. 
    SetFullscreen(false);

    WaitForGPU();

    this->mpFence->Release();
    CloseHandle(this->mHEvent);

    for (unsigned i = 0; i < this->mNumBackBuffers; ++i)
    {
        this->mRenderTargets[i]->Release();
    }
    if (this->mpDescHeapRTV) this->mpDescHeapRTV->Release();
    if (this->mpSwapChain)   this->mpSwapChain->Release();
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

void SwapChain::MoveToNextFrame()
{
#if LOG_SWAPCHAIN_VERBOSE
    Log::Info("MoveToNextFrame() Begin : hwnd=0x%x iBackBuff=%d / Frame=%d / FenceVal=%d"
        , mHwnd, mICurrentBackBuffer, mNumTotalFrames, mFenceValues[mICurrentBackBuffer]
    );
#endif


    // Schedule a Signal command in the queue.
    const UINT64 currentFenceValue = mFenceValues[mICurrentBackBuffer];
    ThrowIfFailed(mpPresentQueue->Signal(mpFence, currentFenceValue));

    // Update the frame index.
    mICurrentBackBuffer = mpSwapChain->GetCurrentBackBufferIndex();
    ++mNumTotalFrames;

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    if (mpFence->GetCompletedValue() < mFenceValues[mICurrentBackBuffer])
    {
        ThrowIfFailed(mpFence->SetEventOnCompletion(mFenceValues[mICurrentBackBuffer], mHEvent));
        WaitForSingleObjectEx(mHEvent, INFINITE, FALSE);
    }

    // Set the fence value for the next frame.
    mFenceValues[mICurrentBackBuffer] = currentFenceValue + 1;


#if LOG_SWAPCHAIN_VERBOSE
    Log::Info("MoveToNextFrame() End   : hwnd=0x%x iBackBuff=%d / Frame=%d / FenceVal=%d"
        , mHwnd, mICurrentBackBuffer, mNumTotalFrames, mFenceValues[mICurrentBackBuffer]
    );
#endif
}

void SwapChain::WaitForGPU()
{
    HRESULT hr = {};

    // Schedule a Signal command in the queue.
    ThrowIfFailed(mpPresentQueue->Signal(mpFence, mFenceValues[mICurrentBackBuffer]));

    // Wait until the fence has been processed.
    ThrowIfFailed(mpFence->SetEventOnCompletion(mFenceValues[mICurrentBackBuffer], mHEvent));
    hr = WaitForSingleObjectEx(mHEvent, 3000, FALSE); // wait for 3000 milliseconds tops
    
    switch (hr)
    {
    case WAIT_TIMEOUT:
        Log::Warning("SwapChain<hwnd=0x%x> timed out on WaitForGPU(): Signal=%d, ICurrBackBuffer=%d, NumFramesPresented=%d"
            , mHwnd, mFenceValues[mICurrentBackBuffer], mICurrentBackBuffer, this->GetNumPresentedFrames());
        break;
    default: break;
    }

    // Increment the fence value for the current frame.
    ++mFenceValues[mICurrentBackBuffer];
}