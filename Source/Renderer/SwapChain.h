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

#include "Fence.h"

#include <d3d12.h>
#include <dxgi1_6.h>

#include <vector>
#include <memory>

#include "Libs/D3DX12/d3dx12.h"

class CommandQueue;
class Window;

struct FWindowRepresentation
{
	HWND hwnd; int width, height;
	bool bVSync;
	bool bFullscreen;
	FWindowRepresentation(const std::unique_ptr<Window>& pWnd, bool bVSync, bool bFullscreen);
};
struct FSwapChainCreateDesc
{
	ID3D12Device*                pDevice   = nullptr;
	const FWindowRepresentation* pWindow   = nullptr;
	CommandQueue*                pCmdQueue = nullptr;

	int numBackBuffers;
	bool bVSync;
	bool bFullscreen;
};


// https://docs.microsoft.com/en-us/windows/win32/direct3d12/swap-chains
class SwapChain
{
public:
	bool Create(const FSwapChainCreateDesc& desc);
	void Destroy();
	void Resize(int w, int h);

	void SetFullscreen(bool bState, int FSRecoveryWindowWidth, int FSRecoveryWindowHeight);
	bool IsFullscreen() const;

	void Present(bool bVSync = false);
	void MoveToNextFrame();
	void WaitForGPU();

	/* Getters */ 
	inline int GetNumBackBuffers() const { return mNumBackBuffers; }
	inline int GetCurrentBackBufferIndex() const { return mICurrentBackBuffer; }
	inline CD3DX12_CPU_DESCRIPTOR_HANDLE GetCurrentBackBufferRTVHandle() const { return CD3DX12_CPU_DESCRIPTOR_HANDLE(mpDescHeapRTV->GetCPUDescriptorHandleForHeapStart(), GetCurrentBackBufferIndex(), mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV)); }
	inline ID3D12Resource* GetCurrentBackBufferRenderTarget() const { return mRenderTargets[GetCurrentBackBufferIndex()]; }
	inline unsigned long long GetNumPresentedFrames() const { return mNumTotalFrames; }

private:
	void CreateRenderTargetViews();
	void DestroyRenderTargetViews();

private:
	HWND                         mHwnd;
	unsigned short               mNumBackBuffers;
	unsigned short               mICurrentBackBuffer;
	unsigned long long           mNumTotalFrames = 0;

	HANDLE                       mHEvent = 0;
	ID3D12Fence*                 mpFence = nullptr;
	std::vector<UINT64>          mFenceValues;

	std::vector<ID3D12Resource*> mRenderTargets;
	ID3D12DescriptorHeap*        mpDescHeapRTV = nullptr;

	ID3D12Device*                mpDevice         = nullptr;
	IDXGIAdapter*                mpAdapter        = nullptr;

	IDXGISwapChain4*             mpSwapChain      = nullptr;
	ID3D12CommandQueue*          mpPresentQueue   = nullptr;
	DXGI_FORMAT                  mSwapChainFormat = DXGI_FORMAT_UNKNOWN;
	// TODO: HDR: https://docs.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range
};
