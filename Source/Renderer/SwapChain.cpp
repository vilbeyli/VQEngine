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
#include "Common.h"
#include "Renderer.h"

#include "../Application/Platform.h" // CHECK_HR
#include "../Application/Window.h"
#include "../../Libs/VQUtils/Source/Log.h"
#include "../../Libs/VQUtils/Source/utils.h"
#include "../../Libs/VQUtils/Source/SystemInfo.h"


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

#if LOG_SWAPCHAIN_VERBOSE
    #define LOG_SWAPCHAIN_SYNCHRONIZATION_EVENTS  0
#endif

using namespace VQSystemInfo;


// assumes only 1 HDR capable display and selects the 
// first one that coems up with EnumDisplayMonitors() call.
static bool CheckHDRCapability(VQSystemInfo::FMonitorInfo* pOutMonitorInfo)
{
    std::vector<FMonitorInfo> ConnectedDisplays = VQSystemInfo::GetDisplayInfo(); // TODO: way too slow due to reading from registry.

    bool bHDRMonitorFound = false;
    for (const FMonitorInfo& i : ConnectedDisplays)
    {
        bHDRMonitorFound |= i.bSupportsHDR;
        if (bHDRMonitorFound)
        {
            *pOutMonitorInfo = i;
            break;
        }
    }
    return bHDRMonitorFound;
}

void SwapChain::EnsureSwapChainColorSpace(SwapChainBitDepth swapChainBitDepth, bool enableST2084)
{
    DXGI_COLOR_SPACE_TYPE colorSpace = {};
    switch (swapChainBitDepth)
    {
    case _8:
        colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        //m_rootConstants[DisplayCurve] = sRGB;
        break;

    case _10:
        colorSpace = enableST2084 ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        //m_rootConstants[DisplayCurve] = enableST2084 ? ST2084 : sRGB;
        break;

    case _16:
        colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
        //m_rootConstants[DisplayCurve] = None;
        break;
    }

    if (mColorSpace != colorSpace)
    {
        UINT colorSpaceSupport = 0;
        if (SUCCEEDED(mpSwapChain->CheckColorSpaceSupport(colorSpace, &colorSpaceSupport)) &&
            ((colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
        {
            ThrowIfFailed(mpSwapChain->SetColorSpace1(colorSpace));
            mColorSpace = colorSpace;
        }
    }
}

void SwapChain::SetHDRMetaData(EColorSpace ColorSpace, float MaxOutputNits, float MinOutputNits, float MaxContentLightLevel, float MaxFrameAverageLightLevel)
{
    using namespace VQSystemInfo;
    
    if (!IsHDRFormat())
    {
        ThrowIfFailed(mpSwapChain->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr));
        Log::Info("Cleared HDR Metadata.");
        return;
    }

    // https://en.wikipedia.org/wiki/Chromaticity
    // E.g., the white point of an sRGB display  is an x,y chromaticity of 
    //       (0.3216, 0.3290) where x and y coords are used in the xyY space.
    static const FDisplayChromaticities DisplayChromaticityList[] =
    {
        //                      Red_x   , Red_y   , Green_x , Green_y,  Blue_x  , Blue_y  , White_x , White_y     // xyY space
        FDisplayChromaticities( 0.64000f, 0.33000f, 0.30000f, 0.60000f, 0.15000f, 0.06000f, 0.31270f, 0.32900f ), // Display Gamut Rec709 
        FDisplayChromaticities( 0.70800f, 0.29200f, 0.17000f, 0.79700f, 0.13100f, 0.04600f, 0.31270f, 0.32900f ), // Display Gamut Rec2020
        FDisplayChromaticities(), // Display Gamut DCI-P3 | TODO: handle p3
    };

    // Set HDR meta data : https://docs.microsoft.com/en-us/windows/win32/api/dxgi1_5/ns-dxgi1_5-dxgi_hdr_metadata_hdr10
    const FDisplayChromaticities& Chroma = DisplayChromaticityList[ColorSpace];
    mHDRMetaData.RedPrimary[0]   = static_cast<UINT16>(Chroma.RedPrimary_xy[0]   * 50000.0f);
    mHDRMetaData.RedPrimary[1]   = static_cast<UINT16>(Chroma.RedPrimary_xy[1]   * 50000.0f);
    mHDRMetaData.GreenPrimary[0] = static_cast<UINT16>(Chroma.GreenPrimary_xy[0] * 50000.0f);
    mHDRMetaData.GreenPrimary[1] = static_cast<UINT16>(Chroma.GreenPrimary_xy[1] * 50000.0f);
    mHDRMetaData.BluePrimary[0]  = static_cast<UINT16>(Chroma.BluePrimary_xy[0]  * 50000.0f);
    mHDRMetaData.BluePrimary[1]  = static_cast<UINT16>(Chroma.BluePrimary_xy[1]  * 50000.0f);
    mHDRMetaData.WhitePoint[0]   = static_cast<UINT16>(Chroma.WhitePoint_xy[0]   * 50000.0f);
    mHDRMetaData.WhitePoint[1]   = static_cast<UINT16>(Chroma.WhitePoint_xy[1]   * 50000.0f);
    mHDRMetaData.MaxMasteringLuminance = static_cast<UINT>(MaxOutputNits * 10000.0f);
    mHDRMetaData.MinMasteringLuminance = static_cast<UINT>(MinOutputNits * 10000.0f);
    mHDRMetaData.MaxContentLightLevel = static_cast<UINT16>(MaxContentLightLevel);
    mHDRMetaData.MaxFrameAverageLightLevel = static_cast<UINT16>(MaxFrameAverageLightLevel);
    ThrowIfFailed(mpSwapChain->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(DXGI_HDR_METADATA_HDR10), &mHDRMetaData));
}



// The programming model for swap chains in D3D12 is not identical to that in earlier versions of D3D. 
// The programming convenience, for example, of supporting automatic resource rotation that was present 
// in D3D10 and D3D11 is now NOT supported.
bool SwapChain::Create(const FSwapChainCreateDesc& desc)
{
    assert(desc.pDevice);
    assert(desc.pWindow && desc.pWindow->GetHWND());
    assert(desc.pCmdQueue && desc.pCmdQueue->pQueue);
    assert(desc.numBackBuffers > 0 && desc.numBackBuffers <= NUM_MAX_BACK_BUFFERS);

    const int WIDTH  = desc.pWindow->GetWidth();
    const int HEIGHT = desc.pWindow->GetHeight();

    this->mpDevice = desc.pDevice;
    this->mHwnd = desc.pWindow->GetHWND();
    this->mNumBackBuffers = desc.numBackBuffers;
    this->mpPresentQueue = desc.pCmdQueue->pQueue;
    this->mbVSync = desc.bVSync;

    HRESULT hr = {};
    IDXGIFactory4* pDxgiFactory = nullptr;
    hr = CreateDXGIFactory1(IID_PPV_ARGS(&pDxgiFactory));
    if (FAILED(hr))
    {
        Log::Error("SwapChain::Create(): Couldn't create DXGI Factory.");
        return false;
    }

    // Determine Swapchain Format based on whether HDR is supported & enabled or not
    DXGI_FORMAT SwapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    // Check HDR support : https://docs.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range
    const bool bIsHDRCapableDisplayAvailable = VQSystemInfo::FMonitorInfo::CheckHDRSupport(this->mHwnd);
    if (desc.bHDR)
    {
        if (bIsHDRCapableDisplayAvailable)
        {
            switch (desc.bitDepth)
            {
            case _10:
                assert(false); // HDR10 isn't supported for now
                break;
            case _16:
                // By default, a swap chain created with a floating point pixel format is treated as if it 
                // uses the DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 color space, which is also known as scRGB.
                SwapChainFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
                mColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709; // set mColorSpace value to ensure consistant state
                break;
            }
        }
        else
        {
            Log::Warning("No HDR capable display found! Falling back to SDR swapchain.");
            SwapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
            mColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        }
    }

    // https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-flip-model
    // https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-1-4-improvements
    // DXGI_SWAP_EFFECT_FLIP_DISCARD    should be preferred when applications fully render over the backbuffer before 
    //                                  presenting it or are interested in supporting multi-adapter scenarios easily.
    // DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL should be used by applications that rely on partial presentation optimizations 
    //                                  or regularly read from previously presented backbuffers.
    constexpr DXGI_SWAP_EFFECT SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;


    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = desc.numBackBuffers;
    swapChainDesc.Height = HEIGHT;
    swapChainDesc.Width  = WIDTH;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SwapEffect = SwapEffect;
    swapChainDesc.Format = SwapChainFormat;
    swapChainDesc.Flags = desc.bVSync ? 0 : DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    if (desc.bFullscreen)
    {
        // https://docs.microsoft.com/en-us/windows/win32/direct3darticles/dxgi-best-practices#full-screen-issues
        // Also, developers may create a full-screen swap chain and give a specific resolution, only to find that 
        // DXGI defaults to the desktop resolution regardless of the numbers passed in. Unless otherwise instructed, 
        // DXGI defaults to the desktop resolution for full-screen swap chains.
        // When creating a full - screen swap chain, the Flags member of the DXGI_SWAP_CHAIN_DESC structure must 
        // be set to DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH to override DXGI's default behavior.
        #if 0
        swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        #endif
    }

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
        , this->mHwnd
        , &swapChainDesc
        , NULL
        , NULL
        , &pSwapChain
    );
    this->mFormat = SwapChainFormat;

    // https://docs.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgifactory-makewindowassociation
    UINT WndAssocFlags = DXGI_MWA_NO_ALT_ENTER; // We're gonna handle the Alt+Enter ourselves instead of DXGI
    pDxgiFactory->MakeWindowAssociation(this->mHwnd, WndAssocFlags);

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
        case E_OUTOFMEMORY: reason = "Out of memory"; break;
        case DXGI_ERROR_INVALID_CALL: reason = "DXGI Invalid call"; break;
            // lookup: https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-error
        default: reason = "UNKNOWN"; break;
        }
        Log::Error("Couldn't create Swapchain: %s", reason.c_str());
    }

    // Set color space for HDR if specified
    if (desc.bHDR && bIsHDRCapableDisplayAvailable)
    {
        constexpr bool bIsOutputSignalST2084 = false; // output signal type for the HDR10 standard
        const bool bIsHDR10Signal = desc.bitDepth == _10 && bIsOutputSignalST2084;
        EnsureSwapChainColorSpace(desc.bitDepth, bIsHDR10Signal);
    }

    pDxgiFactory->Release();

    // Create Fence & Fence Event
    this->mICurrentBackBuffer = mpSwapChain->GetCurrentBackBufferIndex();
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
        Log::Error("SwapChain::Create() : Couldn't create DescriptorHeap: %0x%x", hr);
        return false;
    }
#if _DEBUG
    mpDescHeapRTV->SetName(L"SwapChainRTVDescHeap");
#endif

    // Create a RTV for each SwapChain buffer
    this->mRenderTargets.resize(this->mNumBackBuffers, nullptr);

    // Resize will handle RTV creation logic if its a Fullscreen SwapChain
    if (desc.bFullscreen)
    {
        // TODO: the SetFullscreen here doesn't trigger WM_SIZE event, hence
        //       we have to call here. For now, we use the specified w and h,
        //       however, that results in non native screen resolution (=low res fullscreen).
        //       Need to figure out how to properly start a swapchain in fullscreen mode.
        this->SetFullscreen(true, WIDTH, HEIGHT);
        this->Resize(WIDTH, HEIGHT, this->mFormat);
    }
    // create RTVs if non-fullscreen swapchain
    else
    {
        CreateRenderTargetViews();
    }


    Log::Info("SwapChain: Created <hwnd=0x%x, bVSync=%d> w/ %d back buffers of resolution %dx%d %s."
        , mHwnd
        , this->mbVSync
        , desc.numBackBuffers
        , WIDTH
        , HEIGHT
        , VQRenderer::DXGIFormatAsString(this->mFormat).data()
    );
    return bSuccess;
}

void SwapChain::Destroy()
{
    // https://docs.microsoft.com/en-us/windows/win32/direct3d12/swap-chains
    // Full-screen swap chains continue to have the restriction that 
    // SetFullscreenState(FALSE, NULL) must be called before the final 
    // release of the swap chain. 
    mpSwapChain->SetFullscreenState(FALSE, NULL);

    WaitForGPU();

    this->mpFence->Release();
    CloseHandle(this->mHEvent);

    DestroyRenderTargetViews();
    if (this->mpDescHeapRTV) this->mpDescHeapRTV->Release();
    if (this->mpSwapChain)   this->mpSwapChain->Release();
}

void SwapChain::Resize(int w, int h, DXGI_FORMAT format)
{
#if LOG_SWAPCHAIN_VERBOSE
    Log::Info("SwapChain<hwnd=0x%x> Resize: %dx%d", mHwnd, w, h);
#endif

    DestroyRenderTargetViews();
    for (int i = 0; i < this->mNumBackBuffers; i++)
    {
        mFenceValues[i] = mFenceValues[mpSwapChain->GetCurrentBackBufferIndex()];
    }

    mpSwapChain->ResizeBuffers(
        (UINT)this->mFenceValues.size(),
        w, h,
        format,
        this->mbVSync ? 0 : (DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING /*| DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH*/)
    );
    this->mFormat = format;

    CreateRenderTargetViews();
    this->mICurrentBackBuffer = mpSwapChain->GetCurrentBackBufferIndex();
}

// https://docs.microsoft.com/de-de/windows/win32/direct3darticles/dxgi-best-practices#full-screen-issues
void SwapChain::SetFullscreen(bool bState, int FSRecoveryWindowWidth, int FSRecoveryWindowHeight)
{
    HRESULT hr = {};

    // Set Fullscreen
    // https://docs.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-setfullscreenstate
    hr = mpSwapChain->SetFullscreenState(bState ? TRUE : FALSE, NULL);
    if (hr != S_OK)
    {
        switch (hr)
        {
        case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE : Log::Error("SwapChain::SetFullScreen() : DXGI_ERROR_NOT_CURRENTLY_AVAILABLE"); break;
        case DXGI_STATUS_MODE_CHANGE_IN_PROGRESS: Log::Error("SwapChain::SetFullScreen() : DXGI_STATUS_MODE_CHANGE_IN_PROGRESS "); break;
            
        default: Log::Error("SwapChain::SetFullScreen() : unhandled error code"); break;
        }
    }

    // Figure out which monitor swapchain is in
    // and set the mode we want to use for ResizeTarget().
    IDXGIOutput6* pOut = nullptr;
    {
        IDXGIOutput* pOutput = nullptr;
        mpSwapChain->GetContainingOutput(&pOutput);
        hr = pOutput->QueryInterface(IID_PPV_ARGS(&pOut));
        assert(hr == S_OK);
        pOutput->Release();
    }
    
    DXGI_OUTPUT_DESC1 desc;
    pOut->GetDesc1(&desc);
    
    // Get supported mode count and then all the supported modes
    UINT NumModes = 0;
    hr = pOut->GetDisplayModeList1(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &NumModes, NULL);
    std::vector<DXGI_MODE_DESC1> currMode(NumModes);
    hr = pOut->GetDisplayModeList1(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &NumModes, &currMode[0]);

    DXGI_MODE_DESC1 matchDesc = {};
    matchDesc = currMode.back(); // back() usually has the highest resolution + refresh rate (not always the refresh rate)
    matchDesc.RefreshRate.Numerator = matchDesc.RefreshRate.Denominator = 0;  // let DXGI figure out the refresh rate
    if (!bState)
    {
        matchDesc.Width = FSRecoveryWindowWidth;
        matchDesc.Height = FSRecoveryWindowHeight;
    }
    DXGI_MODE_DESC1 matchedDesc = {};
    pOut->FindClosestMatchingMode1(&matchDesc, &matchedDesc, NULL);
    pOut->Release();

    DXGI_MODE_DESC mode = {};
    mode.Width            = matchedDesc.Width;
    mode.Height           = matchedDesc.Height;
    mode.RefreshRate      = matchedDesc.RefreshRate;
    mode.Format           = matchedDesc.Format;
    mode.Scaling          = matchedDesc.Scaling;
    mode.ScanlineOrdering = matchedDesc.ScanlineOrdering;
    // https://docs.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizetarget
    // calling ResizeTarget() will produce WM_SIZE event right away.
    // RenderThread handles events twice before present to be able to catch
    // the Resize() event before calling Present() as the events
    // are produced and consumed on different threads (p:main, c:render).
    hr = mpSwapChain->ResizeTarget(&mode);
    if (hr != S_OK)
    {
        Log::Error("SwapChain::ResizeTarget() : unhandled error code");
    }

    const bool bRefreshRateIsInteger = mode.RefreshRate.Denominator == 1;

    if(bRefreshRateIsInteger)
        Log::Info("SwapChain::SetFullscreen(%s) Mode: %dx%d@%dHz" , (bState ? "true" : "false"), mode.Width, mode.Height, mode.RefreshRate.Numerator);
    else
        Log::Info("SwapChain::SetFullscreen(%s) Mode: %dx%d@%.2fHz", (bState ? "true" : "false"), mode.Width, mode.Height, (float)mode.RefreshRate.Numerator / mode.RefreshRate.Denominator);
}

bool SwapChain::IsFullscreen(/*IDXGIOUtput* ?*/) const
{
    BOOL fullscreenState;
    ThrowIfFailed(mpSwapChain->GetFullscreenState(&fullscreenState, nullptr));
    return fullscreenState;
}

HRESULT SwapChain::Present()
{
    constexpr UINT VSYNC_INTERVAL = 1;
    const bool& bVSync = this->mbVSync;

    // TODO: glitch detection and avoidance
    // https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-flip-model#avoiding-detecting-and-recovering-from-glitches

    HRESULT hr = {};
    UINT FlagPresent = (!bVSync && !IsFullscreen())
        ? DXGI_PRESENT_ALLOW_TEARING // works only in Windowed mode
        : 0;

    if (bVSync) hr = mpSwapChain->Present(VSYNC_INTERVAL, FlagPresent);
    else        hr = mpSwapChain->Present(0, FlagPresent);

    if (hr != S_OK)
    {
        // https://docs.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present
        switch (hr)
        {
        case DXGI_ERROR_DEVICE_RESET:
            Log::Error("SwapChain::Present(): DXGI_ERROR_DEVICE_RESET");
            // TODO: call HandleDeviceReset() from whoever will be responsible
            break;
        case DXGI_ERROR_DEVICE_REMOVED:
            Log::Error("SwapChain::Present(): DXGI_ERROR_DEVICE_REMOVED");
            // TODO: call HandleDeviceReset()
            break;
        case DXGI_ERROR_INVALID_CALL:
            Log::Error("SwapChain::Present(): DXGI_ERROR_INVALID_CALL");
            // TODO:
            break;
        case DXGI_STATUS_OCCLUDED:
            Log::Warning("SwapChain::Present(): DXGI_STATUS_OCCLUDED");
            break;
        default:
            assert(false); // unhandled Present() return code
            break;
        }
    }

    return hr;
}

void SwapChain::MoveToNextFrame()
{
#if LOG_SWAPCHAIN_SYNCHRONIZATION_EVENTS
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
    UINT64 fenceComplVal = mpFence->GetCompletedValue();
    HRESULT hr = {};
    if (fenceComplVal < mFenceValues[mICurrentBackBuffer])
    {
#if LOG_SWAPCHAIN_SYNCHRONIZATION_EVENTS
        Log::Warning("SwapChain : next frame not ready. FenceComplVal=%d < FenceVal[curr]=%d", fenceComplVal, mFenceValues[mICurrentBackBuffer]);
#endif
        ThrowIfFailed(mpFence->SetEventOnCompletion(mFenceValues[mICurrentBackBuffer], mHEvent));
        hr = WaitForSingleObjectEx(mHEvent, 200, FALSE);
    }
    switch (hr)
    {
    case S_OK: break;
    case WAIT_TIMEOUT:
        Log::Warning("SwapChain<hwnd=0x%x> timed out on WaitForGPU(): Signal=%d, ICurrBackBuffer=%d, NumFramesPresented=%d"
            , mHwnd, mFenceValues[mICurrentBackBuffer], mICurrentBackBuffer, this->GetNumPresentedFrames());
        break;
    default: break;
    }

    // Set the fence value for the next frame.
    mFenceValues[mICurrentBackBuffer] = currentFenceValue + 1;


#if LOG_SWAPCHAIN_SYNCHRONIZATION_EVENTS
    Log::Info("MoveToNextFrame() End   : hwnd=0x%x iBackBuff=%d / Frame=%d / FenceVal=%d"
        , mHwnd, mICurrentBackBuffer, mNumTotalFrames, mFenceValues[mICurrentBackBuffer]
    );
#endif
}

void SwapChain::WaitForGPU()
{
    ID3D12Fence* pFence;
    ThrowIfFailed(mpDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence)));

    ID3D12CommandQueue* queue = mpPresentQueue;

    ThrowIfFailed(queue->Signal(pFence, 1));

    HANDLE mHandleFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    pFence->SetEventOnCompletion(1, mHandleFenceEvent);
    WaitForSingleObject(mHandleFenceEvent, INFINITE);
    CloseHandle(mHandleFenceEvent);

    pFence->Release();
}

void SwapChain::CreateRenderTargetViews()
{
    // From Adam Sawicki's D3D12MA sample code:
    // get the size of a descriptor in this heap (this is a rtv heap, so only rtv descriptors should be stored in it.
    // descriptor sizes may vary from g_Device to g_Device, which is why there is no set size and we must ask the 
    // g_Device to give us the size. we will use this size to increment a descriptor handle offset
    const UINT RTVDescSize = this->mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE hRTV{ this->mpDescHeapRTV->GetCPUDescriptorHandleForHeapStart() };

    HRESULT hr = {};
    for (int i = 0; i < this->mNumBackBuffers; i++)
    {
        hr = this->mpSwapChain->GetBuffer(i, IID_PPV_ARGS(&this->mRenderTargets[i]));
        if (FAILED(hr))
        {
            assert(false);
        }

        this->mpDevice->CreateRenderTargetView(this->mRenderTargets[i], nullptr, hRTV);
        hRTV.ptr += RTVDescSize;
        SetName(this->mRenderTargets[i], "SwapChain<hwnd=0x%x>::RenderTarget[%d]", this->mHwnd, i);
    }
}

void SwapChain::DestroyRenderTargetViews()
{
    for (int i = 0; i < this->mNumBackBuffers; i++)
    {
        if (this->mRenderTargets[i])
        {
            this->mRenderTargets[i]->Release();
            this->mRenderTargets[i] = nullptr;
        }
    }
}
