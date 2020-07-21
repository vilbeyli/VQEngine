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

#define VERBOSE_LOGGING 0

// ------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// MAIN THREAD
//
// ------------------------------------------------------------------------------------------------------------------------------------------------------------
void VQEngine::MainThread_HandleEvents()
{
	// Swap event recording buffers so we can read & process a limited number of events safely.
	mEventQueue_VQEToWin_Main.SwapBuffers();
	std::queue<EventPtr_t>& q = mEventQueue_VQEToWin_Main.GetBackContainer();

	if (q.empty())
		return;

	// process the events
	std::shared_ptr<IEvent> pEvent = nullptr;
	while (!q.empty())
	{
		pEvent = std::move(q.front());
		q.pop();

		switch (pEvent->mType)
		{
		case MOUSE_CAPTURE_EVENT:
		{
			std::shared_ptr<SetMouseCaptureEvent> p = std::static_pointer_cast<SetMouseCaptureEvent>(pEvent);
			this->SetMouseCaptureForWindow(p->hwnd, p->bCapture);
		} break;
		case HANDLE_WINDOW_TRANSITIONS_EVENT:
		{
			auto& pWnd = this->GetWindow(pEvent->hwnd);
			HandleWindowTransitions(pWnd, this->GetWindowSettings(pEvent->hwnd));
		} break;
		case SHOW_WINDOW_EVENT:
		{
			this->GetWindow(pEvent->hwnd)->Show();
		} break;
		}
	}
}

void VQEngine::HandleWindowTransitions(std::unique_ptr<Window>& pWin, const FWindowSettings& settings)
{
	if (!pWin) return;

	const bool bHandlingMainWindowTransition = pWin == mpWinMain;

	// TODO: generic solution to multi window/display settings. 
	//       For now, simply prevent debug wnd occupying main wnd's display.
	if (mpWinMain->IsFullscreen()
		&& (mSettings.WndMain.PreferredDisplay == mSettings.WndDebug.PreferredDisplay)
		&& settings.IsDisplayModeFullscreen()
		&& !bHandlingMainWindowTransition)
	{
		Log::Warning("Debug window and Main window cannot be Fullscreen on the same display!");
		pWin->SetFullscreen(false);
		// TODO: as a more graceful fallback, move it to the next monitor and keep fullscreen
		return;
	}


	// Borderless fullscreen transitions are handled through Window object
	// Exclusive  fullscreen transitions are handled through the Swapchain
	if (settings.DisplayMode == EDisplayMode::BORDERLESS_FULLSCREEN)
	{
		pWin->ToggleWindowedFullscreen(&mRenderer.GetWindowSwapChain(pWin->GetHWND()));
	}
}

void VQEngine::SetMouseCaptureForWindow(HWND hwnd, bool bCaptureMouse)
{
	auto& pWin = this->GetWindow(hwnd);

	if(mInputStates.find(hwnd) != mInputStates.end())
		mInputStates.at(hwnd).SetInputBypassing(!bCaptureMouse);
	
	pWin->SetMouseCapture(bCaptureMouse);

	if (bCaptureMouse)
	{
		GetCursorPos(&this->mMouseCapturePosition);
#if VERBOSE_LOGGING
		Log::Info("Capturing Mouse: Last position=(%d, %d)", this->mMouseCapturePosition.x, this->mMouseCapturePosition.y);
#endif
	}
	else
	{ 
		SetCursorPos(this->mMouseCapturePosition.x, this->mMouseCapturePosition.y);
#if VERBOSE_LOGGING
		Log::Info("Releasing Mouse: Settingh Position=(%d, %d)", this->mMouseCapturePosition.x, this->mMouseCapturePosition.y);
#endif
	}
}



// ------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// UPDATE THREAD
//
// ------------------------------------------------------------------------------------------------------------------------------------------------------------
void VQEngine::UpdateThread_HandleEvents()
{
	// Swap event recording buffers so we can read & process a limited number of events safely.
	mEventQueue_WinToVQE_Update.SwapBuffers();
	std::queue<EventPtr_t>& q = mEventQueue_WinToVQE_Update.GetBackContainer();

	if (q.empty())
		return;

	// process the events
	std::shared_ptr<IEvent> pEvent = nullptr;
	while (!q.empty())
	{
		pEvent = std::move(q.front());
		q.pop();

		switch (pEvent->mType)
		{
		case KEY_DOWN_EVENT:
		{
			std::shared_ptr<KeyDownEvent> p = std::static_pointer_cast<KeyDownEvent>(pEvent);
			mInputStates.at(p->hwnd).UpdateKeyDown(p->data);
		} break;
		case KEY_UP_EVENT:
		{
			std::shared_ptr<KeyUpEvent> p = std::static_pointer_cast<KeyUpEvent>(pEvent);
			mInputStates.at(p->hwnd).UpdateKeyUp(p->wparam, p->bMouseEvent);
		} break;

		case MOUSE_MOVE_EVENT:
		{
			std::shared_ptr<MouseMoveEvent> p = std::static_pointer_cast<MouseMoveEvent>(pEvent);
			mInputStates.at(p->hwnd).UpdateMousePos(p->x, p->y, 0);
		} break;
		case MOUSE_SCROLL_EVENT:
		{
			std::shared_ptr<MouseScrollEvent> p = std::static_pointer_cast<MouseScrollEvent>(pEvent);
			mInputStates.at(p->hwnd).UpdateMousePos(0, 0, p->scroll);
		} break;
		case MOUSE_INPUT_EVENT:
		{
			std::shared_ptr<MouseInputEvent> p = std::static_pointer_cast<MouseInputEvent>(pEvent);
			mInputStates.at(p->hwnd).UpdateMousePos_Raw(
				  p->data.relativeX
				, p->data.relativeY
				, static_cast<short>(p->data.scrollDelta)
				, GetWindow(p->hwnd)->IsMouseCaptured()
			);
		} break;
		case WINDOW_RESIZE_EVENT: UpdateThread_HandleWindowResizeEvent(pEvent);  break;
		}
	}

}

void VQEngine::UpdateThread_HandleWindowResizeEvent(const std::shared_ptr<IEvent>& pEvent)
{
	std::shared_ptr<WindowResizeEvent> p = std::static_pointer_cast<WindowResizeEvent>(pEvent);

	// TODO: potentially use a lookup when camera count gets larger.
	//       also need to take into account the array of framedata that containts the camera.
	// there's only one camera for one window for now.
	if (p->hwnd == mpWinMain->GetHWND())
	{
		if (mWindowUpdateContextLookup.find(p->hwnd) != mWindowUpdateContextLookup.end())
		{
			Camera& cam = GetCurrentFrameData(p->hwnd).SceneCamera;
			
			ProjectionMatrixParameters UpdatedProjectionMatrixParams = cam.mProjParams;
			UpdatedProjectionMatrixParams.ViewporWidth  = static_cast<float>(p->width );
			UpdatedProjectionMatrixParams.ViewporHeight = static_cast<float>(p->height);

			cam.SetProjectionMatrix(UpdatedProjectionMatrixParams);
		}
	}
}



// ------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// RENDER THREAD
//
// ------------------------------------------------------------------------------------------------------------------------------------------------------------
void VQEngine::RenderThread_HandleEvents()
{
	// do not process events anymore if we're exiting
	if (mbStopAllThreads)
		return;

	// Swap event recording buffers so we can read & process a limited number of events safely.
	//   Otherwise, theoretically the producer (Main) thread could keep adding new events 
	//   while we're spinning on the queue items below, and cause render thread to stall while, say, resizing.
	mEventQueue_WinToVQE_Renderer.SwapBuffers();
	std::queue<EventPtr_t>& q = mEventQueue_WinToVQE_Renderer.GetBackContainer();
	if (q.empty())
		return;

	// keep track of the resize events per HWND and only handle the last one 
	std::unordered_map<HWND, std::shared_ptr<WindowResizeEvent>> pLastResizeEventLookup;

	// process the events
	std::shared_ptr<IEvent> pEvent = nullptr;
	while (!q.empty())
	{
		pEvent = std::move(q.front());
		q.pop();

		switch (pEvent->mType)
		{
		case EEventType::WINDOW_RESIZE_EVENT     : pLastResizeEventLookup[pEvent->hwnd] = std::static_pointer_cast<WindowResizeEvent>(pEvent); break;
		case EEventType::TOGGLE_FULLSCREEN_EVENT : RenderThread_HandleToggleFullscreenEvent(pEvent.get()); break;
		case EEventType::WINDOW_CLOSE_EVENT      : RenderThread_HandleWindowCloseEvent(pEvent.get()); break;
		}
	}

	// Handle the last resize event per hwnd and ignore the rest of it so the app can stay responsive while resizing.
	for (auto it = pLastResizeEventLookup.begin(); it != pLastResizeEventLookup.end(); ++it)
	{
		RenderThread_HandleWindowResizeEvent(it->second);
	}

}

void VQEngine::RenderThread_HandleWindowResizeEvent(const std::shared_ptr<IEvent>& pEvent)
{
	const std::shared_ptr<WindowResizeEvent> pResizeEvent = std::static_pointer_cast<WindowResizeEvent>(pEvent);
	const HWND&                      hwnd = pResizeEvent->hwnd;
	const int                       WIDTH = pResizeEvent->width;
	const int                      HEIGHT = pResizeEvent->height;
	SwapChain&                  Swapchain = mRenderer.GetWindowSwapChain(hwnd);
	std::unique_ptr<Window>&         pWnd = GetWindow(hwnd);
	const bool         bIsWindowMinimized = WIDTH == 0 && HEIGHT == 0;
	const bool                  bIsClosed = pWnd->IsClosed();


	if (bIsClosed || bIsWindowMinimized)
	{
		const std::string reason = bIsClosed ? "closed" : "minimized";
		Log::Warning("RenderThread: Ignoring WindowResizeEvent as Window<%x> is %s.", hwnd, reason.c_str());
		return;
	}

#if VERBOSE_LOGGING
	Log::Info("RenderThread: Handle Window<%x> Resize event, set resolution to %dx%d", hwnd, WIDTH, HEIGHT);
#endif


	mEventQueue_WinToVQE_Update.AddItem(pResizeEvent);

	Swapchain.WaitForGPU();
	Swapchain.Resize(WIDTH, HEIGHT);
	pWnd->OnResize(WIDTH, HEIGHT);
	mRenderer.OnWindowSizeChanged(hwnd, WIDTH, HEIGHT);

	RenderThread_UnloadWindowSizeDependentResources(hwnd);
	RenderThread_LoadWindowSizeDependentResources(hwnd, WIDTH, HEIGHT);

}

void VQEngine::RenderThread_HandleWindowCloseEvent(const IEvent* pEvent)
{
	const WindowCloseEvent* pWindowCloseEvent = static_cast<const WindowCloseEvent*>(pEvent);
	const HWND& hwnd = pWindowCloseEvent->hwnd;
	SwapChain& Swapchain = mRenderer.GetWindowSwapChain(hwnd);
	std::unique_ptr<Window>& pWnd = GetWindow(hwnd);

	Log::Info("RenderThread: Handle Window Close event <%x>", hwnd);

	RenderThread_UnloadWindowSizeDependentResources(hwnd);
	pWindowCloseEvent->Signal_WindowDependentResourcesDestroyed.NotifyAll();

	if (hwnd == mpWinMain->GetHWND())
	{
		mbStopAllThreads.store(true);
		RenderThread_SignalUpdateThread();
	}
}

void VQEngine::RenderThread_HandleToggleFullscreenEvent(const IEvent* pEvent)
{
	const ToggleFullscreenEvent* pToggleFSEvent = static_cast<const ToggleFullscreenEvent*>(pEvent);
	HWND                                   hwnd = pToggleFSEvent->hwnd;
	SwapChain&                        Swapchain = mRenderer.GetWindowSwapChain(pToggleFSEvent->hwnd);
	const FWindowSettings&          WndSettings = GetWindowSettings(hwnd);
	const bool   bExclusiveFullscreenTransition = WndSettings.DisplayMode == EDisplayMode::EXCLUSIVE_FULLSCREEN;
	std::unique_ptr<Window>&               pWnd = GetWindow(hwnd);
	const bool            bFullscreenStateToSet = bExclusiveFullscreenTransition ? !Swapchain.IsFullscreen() : !pWnd->IsFullscreen();
	const int                             WIDTH = bFullscreenStateToSet ? pWnd->GetFullscreenWidth() : pWnd->GetWidth();
	const int                            HEIGHT = bFullscreenStateToSet ? pWnd->GetFullscreenHeight() : pWnd->GetHeight();

#if VERBOSE_LOGGING
	Log::Info("RenderThread: Handle Fullscreen(exclusiveFS=%s), %s %dx%d"
		, (bExclusiveFullscreenTransition ? "true" : "false")
		, (bFullscreenStateToSet ? "RestoreSize: " : "Transition to: ")
		, WndSettings.Width
		, WndSettings.Height
	);
#endif

	// if we're transitioning into Fullscreen, save the current window dimensions
	if (bFullscreenStateToSet)
	{
		FWindowSettings& WndSettings_ = GetWindowSettings(hwnd);
		WndSettings_.Width = pWnd->GetWidth();
		WndSettings_.Height = pWnd->GetHeight();
	}

	Swapchain.WaitForGPU(); // make sure GPU is finished


	RenderThread_UnloadWindowSizeDependentResources(hwnd);
	RenderThread_LoadWindowSizeDependentResources(hwnd, WIDTH, HEIGHT);

	//
	// EXCLUSIVE FULLSCREEN
	//
	if (bExclusiveFullscreenTransition)
	{
		// Swapchain handles resizing the window through SetFullscreenState() call
		Swapchain.SetFullscreen(bFullscreenStateToSet, WndSettings.Width, WndSettings.Height);

		if (!bFullscreenStateToSet)
		{
			// If the Swapchain is created in fullscreen mode, the WM_PAINT message will not be 
			// received upon switching to windowed mode (ALT+TAB or ALT+ENTER) and the window
			// will be visible, but not interactable and also not visible in taskbar.
			// Explicitly calling Show() here fixes the situation.
			pWnd->Show();

			// Handle one-time transition: Swapchains starting in exclusive fullscreen will not trigger
			// a Resize() event on the first Alt+Tab (Fullscreen -> Windowed). Doing it always in Swapchain.Resize()
			// will result flickering from double resizing.
			auto it = mInitialSwapchainResizeRequiredWindowLookup.find(hwnd);
			const bool bWndNeedsResize = it != mInitialSwapchainResizeRequiredWindowLookup.end() && it->second;
			if (bWndNeedsResize)
			{
				mInitialSwapchainResizeRequiredWindowLookup.erase(it);
				Swapchain.Resize(WndSettings.Width, WndSettings.Height);
			}
		}

		const bool bCapture = true;
		const bool bVisible = true;
		mEventQueue_VQEToWin_Main.AddItem(std::make_shared< SetMouseCaptureEvent>(hwnd, bCapture, bVisible));
	}

	//
	// BORDERLESS FULLSCREEN
	//
	else
	{
		pWnd->ToggleWindowedFullscreen(&Swapchain);

		const bool bCapture = true;
		const bool bVisible = true;
		
		// only capture/release mouse for the main window
		if(hwnd == mpWinMain->GetHWND())
			mEventQueue_VQEToWin_Main.AddItem(std::make_shared< SetMouseCaptureEvent>(hwnd, bFullscreenStateToSet, bVisible));
	}

}

