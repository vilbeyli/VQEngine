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

#include "HDR.h"

#include <d3d12.h>
#include <dxgi1_6.h>

#include <vector>
#include <memory>

#include "Libs/D3DX12/d3dx12.h"

class CommandQueue;
class Window;


// lifted (shamelessly) from D3D12HDR.h of Microsoft's DX12 samples
enum SwapChainBitDepth
{
	_8 = 0,
	_10,
	_16,
	SwapChainBitDepthCount
};

struct FSwapChainCreateDesc
{
	ID3D12Device* pDevice   = nullptr;
	const Window* pWindow   = nullptr;
	CommandQueue* pCmdQueue = nullptr;

	int numBackBuffers = 2;
	bool bVSync        = false;
	bool bFullscreen   = false;
	bool bHDR          = false;
	SwapChainBitDepth bitDepth = _8;
};

// https://docs.microsoft.com/en-us/windows/win32/direct3d12/swap-chains
class SwapChain
{
public:
	bool Create(const FSwapChainCreateDesc& desc);
	void Destroy();
	HRESULT Resize(int w, int h, DXGI_FORMAT format);

	HRESULT Present();
	void MoveToNextFrame();
	void WaitForGPU();

	/* Setters */
	void SetFullscreen(bool bState, int FSRecoveryWindowWidth, int FSRecoveryWindowHeight);
	void SetHDRMetaData(EColorSpace ColorSpace, float MaxOutputNits, float MinOutputNits, float MaxContentLightLevel, float MaxFrameAverageLightLevel);
	inline void SetHDRMetaData(const FSetHDRMetaDataParams& p) { SetHDRMetaData(p.ColorSpace, p.MaxOutputNits, p.MinOutputNits, p.MaxContentLightLevel, p.MaxFrameAverageLightLevel); }
	void ClearHDRMetaData();
	void EnsureSwapChainColorSpace(SwapChainBitDepth swapChainBitDepth, bool bHDR10Signal); 
	bool IsInitialized() const { return this->mpSwapChain != nullptr; }

	/* Getters */ 
	inline unsigned short                GetNumBackBuffers()                const { return mNumBackBuffers; }
	inline int                           GetCurrentBackBufferIndex()        const { return mICurrentBackBuffer; }
	inline CD3DX12_CPU_DESCRIPTOR_HANDLE GetCurrentBackBufferRTVHandle()    const { return CD3DX12_CPU_DESCRIPTOR_HANDLE(mpDescHeapRTV->GetCPUDescriptorHandleForHeapStart(), GetCurrentBackBufferIndex(), mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV)); }
	inline ID3D12Resource*               GetCurrentBackBufferRenderTarget() const { return mRenderTargets[GetCurrentBackBufferIndex()]; }
	inline unsigned long long            GetNumPresentedFrames()            const { return mNumTotalFrames; }
	       DXGI_OUTPUT_DESC1             GetContainingMonitorDesc()         const;
	inline bool                          IsVSyncOn()                        const { return mbVSync;}
	inline DXGI_FORMAT                   GetFormat()                        const { return mFormat; }
	       bool                          IsFullscreen() const;

	inline bool                          IsHDRFormat()                      const { return mFormat == DXGI_FORMAT_R16G16B16A16_FLOAT || mFormat == DXGI_FORMAT_R10G10B10A2_UNORM; }
	inline DXGI_COLOR_SPACE_TYPE         GetColorSpace()                    const { return mColorSpace; }
	inline float                         GetHDRMetaData_MaxOutputNits()     const { return static_cast<float>(mHDRMetaData.MaxMasteringLuminance) / 10000.0f; }
	inline float                         GetHDRMetaData_MinOutputNits()     const { return static_cast<float>(mHDRMetaData.MinMasteringLuminance) / 10000.0f; }

private:
	void CreateRenderTargetViews();
	void DestroyRenderTargetViews();

private:
	friend class Window; // Window can access pSwapChain.

	HWND                         mHwnd               = NULL;
	unsigned short               mNumBackBuffers     = 0;
	unsigned short               mICurrentBackBuffer = 0;
	unsigned long long           mNumTotalFrames     = 0;
	bool                         mbVSync = false;

	HANDLE                       mHEvent = 0;
	ID3D12Fence*                 mpFence = nullptr;
	std::vector<UINT64>          mFenceValues;

	std::vector<ID3D12Resource*> mRenderTargets;
	ID3D12DescriptorHeap*        mpDescHeapRTV = nullptr;

	ID3D12Device*                mpDevice         = nullptr;
	IDXGIAdapter*                mpAdapter        = nullptr;

	IDXGISwapChain4*             mpSwapChain      = nullptr;
	ID3D12CommandQueue*          mpPresentQueue   = nullptr;
	DXGI_FORMAT                  mFormat = DXGI_FORMAT_UNKNOWN;
	DXGI_COLOR_SPACE_TYPE        mColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709; // Rec709 w/ Gamma2.2
	DXGI_HDR_METADATA_HDR10      mHDRMetaData;
};
