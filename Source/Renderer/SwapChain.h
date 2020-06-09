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

#include <d3d12.h>
#include <dxgi1_6.h>

#include <memory>

class CommandQueue;
class Window;

struct FWindowRepresentation
{
	HWND hwnd; int width, height;
	FWindowRepresentation(const std::unique_ptr<Window>& pWnd);
};
struct FSwapChainCreateDesc
{
	ID3D12Device*                pDevice   = nullptr;
	const FWindowRepresentation* pWindow   = nullptr;
	CommandQueue*                pCmdQueue = nullptr;

	int numBackBuffers;
};


// https://docs.microsoft.com/en-us/windows/win32/direct3d12/swap-chains
class SwapChain
{
public:
	bool Create(const FSwapChainCreateDesc& desc);
	void Destroy();

	void Resize(int w, int h);

	void SetFullscreen(bool bState);
	bool IsFullscreen() const;

	void Present(bool bVSync);

private:
	HWND mHwnd;
	unsigned short mNumBackBuffer;

	ID3D12Device*       mpDevice         = nullptr;
	IDXGIAdapter*       mpAdapter        = nullptr;
	IDXGISwapChain4*    mpSwapChain      = nullptr;
	ID3D12CommandQueue* mpDirectQueue    = nullptr;
	DXGI_FORMAT         mSwapChainFormat = DXGI_FORMAT_UNKNOWN;
	// TODO: HDR: https://docs.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range
};
