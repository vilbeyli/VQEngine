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


#include "Device.h"
#include "Renderer.h"

#include "../Engine/Core/Platform.h" // CHECK_HR
#include "../../Libs/VQUtils/Source/Log.h"
#include "../../Libs/VQUtils/Source/utils.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#ifdef _DEBUG
#pragma comment(lib, "dxguid.lib")
#include <DXGIDebug.h>
#endif 

#include <cassert>
#include <vector>

using namespace VQSystemInfo;

static void CheckDeviceFeatureSupport(ID3D12Device4* pDevice, FDeviceCapabilities& dc)
{
    HRESULT hr;
    {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS ftQualityLevels = {};
        hr = pDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &ftQualityLevels, sizeof(ftQualityLevels));
        if (!SUCCEEDED(hr))
        {
            Log::Warning("Device::CheckFeatureSupport(): MSAA Quality Levels failed.");
        }
        else
        {
            dc.SupportedMaxMultiSampleQualityLevel = ftQualityLevels.NumQualityLevels;
        }
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 ftOpt5 = {};
        hr = pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &ftOpt5, sizeof(ftOpt5));
        if (!SUCCEEDED(hr))
        {
            Log::Warning("Device::CheckFeatureSupport(): HW Ray Tracing failed.");
        }
        else
        {
            dc.bSupportsHardwareRayTracing = ftOpt5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
        }
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS1 ftOpt1 = {};
        hr = pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &ftOpt1, sizeof(ftOpt1));
        if (!SUCCEEDED(hr))
        {
            Log::Warning("Device::CheckFeatureSupport(): Wave ops failed.");
        }
        else
        {
            dc.bSupportsWaveOps = ftOpt1.WaveOps;
        }
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS4 ftOpt4 = {};
        hr = pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &ftOpt4, sizeof(ftOpt4));
        if (!SUCCEEDED(hr))
        {
            Log::Warning("Device::CheckFeatureSupport(): Half-precision floating point shader ops failed.");
        }
        else
        {
            dc.bSupportsFP16 = ftOpt4.Native16BitShaderOpsSupported;
        }
    }
    {
        // https://docs.microsoft.com/en-us/windows/win32/direct3d12/typed-unordered-access-view-loads#supported-formats-and-api-calls
        D3D12_FEATURE_DATA_D3D12_OPTIONS ftOpt0;
        ZeroMemory(&ftOpt0, sizeof(ftOpt0));
        HRESULT hr = pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &ftOpt0, sizeof(ftOpt0));
        if (SUCCEEDED(hr))
        {
            // TypedUAVLoadAdditionalFormats contains a Boolean that tells you whether the feature is supported or not
            if (ftOpt0.TypedUAVLoadAdditionalFormats)
            {
                // Can assume "all-or-nothing" subset is supported (e.g. R32G32B32A32_FLOAT)
                dc.bSupportsTypedUAVLoads = true;

                // Cannot assume other formats are supported, so we check:
                std::vector<DXGI_FORMAT> formatQueries =
                {
                      DXGI_FORMAT_R16G16B16A16_UNORM
                    , DXGI_FORMAT_R16G16B16A16_SNORM
                    , DXGI_FORMAT_R32G32_FLOAT
                    , DXGI_FORMAT_R32G32_UINT
                    , DXGI_FORMAT_R32G32_SINT
                    , DXGI_FORMAT_R10G10B10A2_UNORM
                    , DXGI_FORMAT_R10G10B10A2_UINT
                    , DXGI_FORMAT_R11G11B10_FLOAT
                    , DXGI_FORMAT_R8G8B8A8_SNORM
                    , DXGI_FORMAT_R16G16_FLOAT
                    , DXGI_FORMAT_R16G16_UNORM
                    , DXGI_FORMAT_R16G16_UINT
                    , DXGI_FORMAT_R16G16_SNORM
                    , DXGI_FORMAT_R16G16_SINT
                    , DXGI_FORMAT_R8G8_UNORM
                    , DXGI_FORMAT_R8G8_UINT
                    , DXGI_FORMAT_R8G8_SNORM
                    , DXGI_FORMAT_R8G8_SINT
                    , DXGI_FORMAT_R16_UNORM
                    , DXGI_FORMAT_R16_SNORM
                    , DXGI_FORMAT_R8_SNORM
                    , DXGI_FORMAT_A8_UNORM
                    , DXGI_FORMAT_B5G6R5_UNORM
                    , DXGI_FORMAT_B5G5R5A1_UNORM
                    , DXGI_FORMAT_B4G4R4A4_UNORM
                };

                for (DXGI_FORMAT fmt : formatQueries)
                {

                    D3D12_FEATURE_DATA_FORMAT_SUPPORT ftDataSupport0 =
                    {
                          fmt
                        , D3D12_FORMAT_SUPPORT1_NONE
                        , D3D12_FORMAT_SUPPORT2_NONE
                    };
                    hr = pDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &ftDataSupport0, sizeof(ftDataSupport0));

                    if (SUCCEEDED(hr) && (ftDataSupport0.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0)
                    {
                        dc.TypedUAVLoadFormatSupportMap[fmt] = true;
                    }
                    else
                    {
                        Log::Warning("Device::CheckFeatureSupport(): Typed UAV load for DXGI_FORMAT:%d failed.", fmt);
                    }
                }
            }
        }
    }
#if 0
    // https://www.appveyor.com/docs/windows-images-software/#visual-studio-2019
    // AppVeyor (CI) currently doesn't support WinSDK Windows 10 SDK 10.0.19041, which means
    // it cannot compile and package the code below. As we're not currently using this info,
    // leave out this code snippet for the v0.8.0 release.
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 ftOpt7 = {};
        hr = pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &ftOpt7, sizeof(ftOpt7));
        if (!SUCCEEDED(hr))
        {
            Log::Warning("Device::CheckFeatureSupport(): Mesh Shaders & Sampler Feedback failed.");
        }
        else
        {
            dc.bSupportsMeshShaders = ftOpt7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
            dc.bSupportsSamplerFeedback = ftOpt7.SamplerFeedbackTier != D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED;
        }
    }
#endif
}

bool Device::Create(const FDeviceCreateDesc& desc)
{
    HRESULT hr = {};

    // Debug & Validation Layer
    if (desc.bEnableDebugLayer)
    {
        ID3D12Debug1* pDebugController;
        hr = D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController));
        if (hr == S_OK)
        {
            pDebugController->EnableDebugLayer();
            if (desc.bEnableGPUValidationLayer)
            {
                pDebugController->SetEnableGPUBasedValidation(TRUE);
                pDebugController->SetEnableSynchronizedCommandQueueValidation(TRUE);
            }
            pDebugController->Release();
            Log::Info("Device::Create(): Enabled Debug %s", (desc.bEnableGPUValidationLayer ? "and GPU Validation layers" : "layer"));
        }
        else
        {
            Log::Warning("Device::Create(): D3D12GetDebugInterface() returned != S_OK : %l", hr);
        }
    }
     
    std::vector<FGPUInfo> vAdapters = VQRenderer::EnumerateDX12Adapters(desc.bEnableDebugLayer, desc.pFactory);
    assert(vAdapters.size() > 0);

    // TODO: implement software device as fallback
    // https://walbourn.github.io/anatomy-of-direct3d-12-create-device/

    FGPUInfo& adapter = vAdapters[0];

    // throws COM error but returns S_OK : Microsoft C++ exception: _com_error at memory location 0x
    this->mpAdapter = std::move(adapter.pAdapter);
    hr = D3D12CreateDevice(this->mpAdapter, adapter.MaxSupportedFeatureLevel, IID_PPV_ARGS(&mpDevice));
    if (!SUCCEEDED(hr))
    {
        Log::Error("Device::Create(): D3D12CreateDevice() failed.");
	    return false;
    }
    hr = D3D12CreateDevice(this->mpAdapter, adapter.MaxSupportedFeatureLevel, IID_PPV_ARGS(&mpDevice4));
    if (!SUCCEEDED(hr))
    {
        Log::Error("Device::Create(): D3D12CreateDevice4() failed.");
    }
    const bool bDeviceCreated = true;

    CheckDeviceFeatureSupport(this->mpDevice4, this->mDeviceCapabilities);
    return bDeviceCreated;
}

void Device::Destroy()
{
    mpAdapter->Release();
    mpDevice->Release();
    if (mpDevice4)
        mpDevice4->Release();
    

#ifdef _DEBUG
    // Report live objects
    IDXGIDebug1* pDxgiDebug;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDxgiDebug))))
    {
        pDxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
        pDxgiDebug->Release();
    }
#endif
}

UINT Device::GetDeviceMemoryMax() const
{
    assert(false);
    return 0;
}

UINT Device::GetDeviceMemoryAvailable() const
{
    assert(false);
    return 0;
}
