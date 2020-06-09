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

#include "../Application/Platform.h" // CHECK_HR
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

/*
@bEnumerateSoftwareAdapters : Basic Render Driver adapter.
*/
static std::vector< FGPUInfo > EnumerateDX12Adapters(bool bEnableDebugLayer, bool bEnumerateSoftwareAdapters = false)
{
    std::vector< FGPUInfo > GPUs;
    HRESULT hr = {};

    IDXGIAdapter1* pAdapter = nullptr; // adapters are the graphics card (this includes the embedded graphics on the motherboard)
    int iAdapter = 0;             // we'll start looking for DX12  compatible graphics devices starting at index 0
    bool bAdapterFound = false;        // set this to true when a good one was found

    // https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/d3d10-graphics-programming-guide-dxgi
    // https://stackoverflow.com/questions/42354369/idxgifactory-versions
    // Chuck Walbourn: For DIrect3D 12, you can assume CreateDXGIFactory2 and IDXGIFactory4 or later is supported. 
    IDXGIFactory6* pDxgiFactory; // DXGIFactory6 supports preferences when querying devices: DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE
    UINT DXGIFlags = 0;
    if (bEnableDebugLayer)
    {
        DXGIFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
    hr = CreateDXGIFactory2(DXGIFlags, IID_PPV_ARGS(&pDxgiFactory));

    auto fnAddAdapter = [&bAdapterFound, &GPUs](IDXGIAdapter1*& pAdapter, const DXGI_ADAPTER_DESC1& desc, D3D_FEATURE_LEVEL FEATURE_LEVEL)
    {
        bAdapterFound = true;

        FGPUInfo GPUInfo = {};
        GPUInfo.DedicatedGPUMemory = desc.DedicatedVideoMemory;
        GPUInfo.DeviceID = desc.DeviceId;
        GPUInfo.DeviceName = StrUtil::UnicodeToASCII<_countof(desc.Description)>(desc.Description);
        GPUInfo.VendorID = desc.VendorId;
        GPUInfo.MaxSupportedFeatureLevel = FEATURE_LEVEL;
        pAdapter->QueryInterface(IID_PPV_ARGS(&GPUInfo.pAdapter));
        GPUs.push_back(GPUInfo);
    };

    // Find GPU with highest perf: https://stackoverflow.com/questions/49702059/dxgi-integred-pAdapter
    // https://docs.microsoft.com/en-us/windows/win32/api/dxgi1_6/nf-dxgi1_6-idxgifactory6-enumadapterbygpupreference
    while ( pDxgiFactory->EnumAdapterByGpuPreference(iAdapter, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&pAdapter)) != DXGI_ERROR_NOT_FOUND)
    {
        DXGI_ADAPTER_DESC1 desc;
        pAdapter->GetDesc1(&desc);

        const bool bSoftwareAdapter = desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE;
        if (   (bEnumerateSoftwareAdapters && !bSoftwareAdapter) // We want software adapters, but we got a hardware adapter
            || (!bEnumerateSoftwareAdapters && bSoftwareAdapter) // We want hardware adapters, but we got a software adapter
        )
        {
            ++iAdapter;
            pAdapter->Release();
            continue;
        }
        
        hr = D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_1, _uuidof(ID3D12Device), nullptr);
        if (SUCCEEDED(hr))
        {
            fnAddAdapter(pAdapter, desc, D3D_FEATURE_LEVEL_12_1);
        }
        else
        {
            const std::string AdapterDesc = StrUtil::UnicodeToASCII(desc.Description);
            Log::Warning("Device::Create(): D3D12CreateDevice() with Feature Level 12_1 failed with adapter=%s, retrying with Feature Level 12_0", AdapterDesc.c_str());
            hr = D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr);
            if (SUCCEEDED(hr))
            {
                fnAddAdapter(pAdapter, desc, D3D_FEATURE_LEVEL_12_0);
            }
            else
            {
                Log::Error("Device::Create(): D3D12CreateDevice() with Feature Level 12_0 failed ith adapter=%s", AdapterDesc.c_str());
            }
        }

        pAdapter->Release();
        ++iAdapter;
    }
    pDxgiFactory->Release();
    assert(bAdapterFound);

    return GPUs;
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
            if (desc.bEnableValidationLayer)
            {
                pDebugController->SetEnableGPUBasedValidation(TRUE);
                pDebugController->SetEnableSynchronizedCommandQueueValidation(TRUE);
            }
            pDebugController->Release();
            Log::Info("Device::Create(): Enabled Debug %s", (desc.bEnableValidationLayer ? "and Validation layers" : "layer"));
        }
        else
        {
            Log::Warning("Device::Create(): D3D12GetDebugInterface() returned != S_OK : %l", hr);
        }
    }

    std::vector<FGPUInfo> vAdapters = EnumerateDX12Adapters(desc.bEnableDebugLayer);

    // TODO: implement software device as fallback ---------------------------------
    // https://walbourn.github.io/anatomy-of-direct3d-12-create-device/
    assert(vAdapters.size() > 0);
    //       implement software device as fallback ---------------------------------

    FGPUInfo& adapter = vAdapters[0];

    // throws COM error but returns S_OK : Microsoft C++ exception: _com_error at memory location 0x
    this->mpAdapter = std::move(adapter.pAdapter);
    hr = D3D12CreateDevice(this->mpAdapter, adapter.MaxSupportedFeatureLevel, IID_PPV_ARGS(&mpDevice));

    if (!SUCCEEDED(hr))
    {
        Log::Error("Device::Create(): D3D12CreateDevice() failed.");
	    return false;
    }

    return true;
}

void Device::Destroy()
{
    //m_pDirectQueue->Release();
    //m_pComputeQueue->Release();
    mpAdapter->Release();
    mpDevice->Release();
    

#ifdef _DEBUG
    // Report live objects
    {
        IDXGIDebug1* pDxgiDebug;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDxgiDebug))))
        {
            pDxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
            pDxgiDebug->Release();
        }
    }
#endif
}
