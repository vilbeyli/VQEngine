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

#include "VQEngine.h"

void VQEngine::OnWindowCreate(	)
{
}

void VQEngine::OnWindowResize(HWND hWnd)
{
	// https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/d3d10-graphics-programming-guide-dxgi#handling-window-resizing
	RECT clientRect = {};
	GetClientRect(hWnd, &clientRect);
	int w = clientRect.right - clientRect.left;
	int h = clientRect.bottom - clientRect.top;
	
	// Due to multi-threading, this thread will record the events and 
	// Render Thread will process the queue at the of a render loop
	mWinEventQueue.AddItem(WindowResizeEvent(w, h, hWnd));
}

void VQEngine::OnWindowMinimize()
{
}

void VQEngine::OnWindowFocus()
{
}


void VQEngine::OnWindowKeyDown(WPARAM wParam)
{
}

void VQEngine::OnWindowClose(IWindow* pWindow)
{
	mRenderer.GetWindowSwapChain(static_cast<Window*>(pWindow)->GetHWND()).WaitForGPU();

	pWindow->Close();

	if (pWindow == mpWinMain.get())
		PostQuitMessage(0);

}
 