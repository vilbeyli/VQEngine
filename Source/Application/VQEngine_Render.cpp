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

#include <d3d12.h>
#include <dxgi.h>


void VQEngine::RenderThread_Main()
{
	Log::Info("RenderThread_Main()");
	this->RenderThread_Inititalize();

	bool bQuit = false;
	while (!this->mbStopAllThreads && !bQuit)
	{
		RenderThread_PreRender();
		RenderThread_Render();
		++mNumRenderLoopsExecuted;
	}

	this->RenderThread_Exit();
	Log::Info("RenderThread_Main() : Exit");
}


void VQEngine::RenderThread_Inititalize()
{
	FRendererInitializeParameters params = {};
	params.Windows.push_back(FWindowRepresentation(mpWinMain , mSettings.gfx.bVsync));
	params.Windows.push_back(FWindowRepresentation(mpWinDebug, false));
	params.Settings = mSettings.gfx;
	mRenderer.Initialize(params);

	//mFrameData_MainWnd.resize(mRenderer.GetSwapChainBackBufferCountOfWindow(mpWinMain.get()->GetHWND()));
	//mFrameData_DbgWnd .resize(mRenderer.GetSwapChainBackBufferCountOfWindow(mpWinDebug.get()->GetHWND()));

	mbRenderThreadInitialized.store(true);
	mNumRenderLoopsExecuted.store(0);
}

void VQEngine::RenderThread_Exit()
{
	mRenderer.Exit();
}

void VQEngine::RenderThread_PreRender()
{
}

void VQEngine::RenderThread_Render()
{
	// TODO: sync with CPU

	// TODO: render in parallel???
	mRenderer.RenderWindowContext(mpWinMain->GetHWND());
	mRenderer.RenderWindowContext(mpWinDebug->GetHWND());
}

