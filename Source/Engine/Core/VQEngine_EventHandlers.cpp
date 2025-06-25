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

#include "Engine/VQEngine.h"
#include "Engine/GPUMarker.h"
#include "Engine/Core/Window.h"
#include "Engine/Scene/Scene.h"
#include "Renderer/Renderer.h"

#include "Windows.h"
#include "imgui.h"

#define VERBOSE_LOGGING 0

// ------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// MAIN THREAD
//
// ------------------------------------------------------------------------------------------------------------------------------------------------------------
void VQEngine::MainThread_HandleEvents()
{
	if (mEventQueue_VQEToWin_Main.IsEmpty())
		return;

	// Swap event recording buffers so we can read & process a limited number of events safely.
	mEventQueue_VQEToWin_Main.SwapBuffers();
	std::queue<EventPtr_t>& q = mEventQueue_VQEToWin_Main.GetBackContainer();

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
			this->SetMouseCaptureForWindow(p->hwnd, p->bCapture, p->bReleaseAtCapturedPosition);
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
		HWND hwnd = pWin->GetHWND();
		pWin->ToggleWindowedFullscreen(&mpRenderer->GetWindowSwapChain(hwnd));
		
		if (bHandlingMainWindowTransition)
			SetMouseCaptureForWindow(hwnd, true, true);
	}
}

void VQEngine::SetMouseCaptureForWindow(HWND hwnd, bool bCaptureMouse, bool bReleaseAtCapturedPosition)
{
	auto& pWin = this->GetWindow(hwnd);
	if (mInputStates.find(hwnd) == mInputStates.end())
	{
		Log::Error("Warning: couldn't find InputState for hwnd=0x%x", hwnd);
	}
	
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
		if (bReleaseAtCapturedPosition)
		{
			SetCursorPos(this->mMouseCapturePosition.x, this->mMouseCapturePosition.y);
		}
#if VERBOSE_LOGGING
		Log::Info("Releasing Mouse: Setting Position=(%d, %d), bReleaseAtCapturedPosition=%d", this->mMouseCapturePosition.x, this->mMouseCapturePosition.y, bReleaseAtCapturedPosition);
#endif
	}
}

void VQEngine::SetMouseCaptureForWindow(Window* pWin, bool bCaptureMouse, bool bReleaseAtCapturedPosition) { this->SetMouseCaptureForWindow(pWin->GetHWND(), bCaptureMouse, bReleaseAtCapturedPosition); }



// ------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// UPDATE THREAD
//
// ------------------------------------------------------------------------------------------------------------------------------------------------------------
static void UpdateImGui_KeyUp(KeyCode key, bool bIsMouseKey)
{
	ImGuiIO& io = ImGui::GetIO();
	if (bIsMouseKey)
	{
		const Input::EMouseButtons mouseBtn = static_cast<Input::EMouseButtons>(key);
		int btn = 0;
		if (mouseBtn & Input::EMouseButtons::MOUSE_BUTTON_LEFT  ) btn = 0;
		if (mouseBtn & Input::EMouseButtons::MOUSE_BUTTON_RIGHT ) btn = 1;
		if (mouseBtn & Input::EMouseButtons::MOUSE_BUTTON_MIDDLE) btn = 2;
		io.MouseDown[btn] = false;
	}
}
static void UpdateImGui_KeyDown(KeyDownEventData data)
{
	ImGuiIO& io = ImGui::GetIO();
	const auto& key = data.mouse.wparam;
	if (data.mouse.bMouse)
	{
		const Input::EMouseButtons mouseBtn = static_cast<Input::EMouseButtons>(key);
		int btn = 0;
		if (mouseBtn & Input::EMouseButtons::MOUSE_BUTTON_LEFT  ) btn = 0;
		if (mouseBtn & Input::EMouseButtons::MOUSE_BUTTON_RIGHT ) btn = 1;
		if (mouseBtn & Input::EMouseButtons::MOUSE_BUTTON_MIDDLE) btn = 2;
		io.MouseDown[btn] = true;
	}
}
static void UpdateImGui_MousePosition(HWND hwnd)
{
	ImGuiIO& io = ImGui::GetIO();
	POINT cursor_point;
	if (GetCursorPos(&cursor_point))
	{
		if (ScreenToClient(hwnd, &cursor_point))
		{
			io.MousePos.x = static_cast<float>(cursor_point.x);
			io.MousePos.y = static_cast<float>(cursor_point.y);
			//Log::Info("io.MousePos.xy = %.2f %.2f", io.MousePos.x, io.MousePos.y);
		}
	}
}
static void UpdateImGui_MousePosition1(long x, long y)
{
	ImGuiIO& io = ImGui::GetIO();
	io.MousePos.x = static_cast<float>(x);
	io.MousePos.y = static_cast<float>(y);
}

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
			UpdateImGui_KeyDown(p->data);

		} break;
		case KEY_UP_EVENT:
		{
			std::shared_ptr<KeyUpEvent> p = std::static_pointer_cast<KeyUpEvent>(pEvent);
			mInputStates.at(p->hwnd).UpdateKeyUp(p->wparam, p->bMouseEvent);
			UpdateImGui_KeyUp(p->wparam, p->bMouseEvent);
		} break;

		case MOUSE_MOVE_EVENT:
		{
			std::shared_ptr<MouseMoveEvent> p = std::static_pointer_cast<MouseMoveEvent>(pEvent);
			mInputStates.at(p->hwnd).UpdateMousePos(p->x, p->y, 0);
			UpdateImGui_MousePosition1(p->x, p->y);
		} break;
		case MOUSE_SCROLL_EVENT:
		{
			std::shared_ptr<MouseScrollEvent> p = std::static_pointer_cast<MouseScrollEvent>(pEvent);
			mInputStates.at(p->hwnd).UpdateMousePos(0, 0, p->scroll);
		} break;
		case MOUSE_INPUT_EVENT:
		{
			std::shared_ptr<MouseInputEvent> p = std::static_pointer_cast<MouseInputEvent>(pEvent);

			// discard the scroll event if its outside the application window
			if (p->data.scrollDelta)
			{
				POINT pt; GetCursorPos(&pt);
				ScreenToClient(p->hwnd, &pt);
				
				const bool bOutOfWindow = pt.x < 0 || pt.y < 0 
					|| pt.x > this->GetWindow(p->hwnd)->GetWidth() 
					|| pt.y > this->GetWindow(p->hwnd)->GetHeight();
				if (bOutOfWindow)
				{
					p->data.scrollDelta = 0;
				}
			}

			mInputStates.at(p->hwnd).UpdateMousePos_Raw(
				  p->data.relativeX
				, p->data.relativeY
				, static_cast<short>(p->data.scrollDelta)
			);

			ImGuiIO& io = ImGui::GetIO();
			UpdateImGui_MousePosition(pEvent->hwnd);
			io.MouseWheel += p->data.scrollDelta;
		} break;
		case WINDOW_RESIZE_EVENT: UpdateThread_HandleWindowResizeEvent(pEvent);  break;
		}
	}

}

void VQEngine::UpdateThread_HandleWindowResizeEvent(const std::shared_ptr<IEvent>& pEvent)
{
	SCOPED_CPU_MARKER("HandleWindowResizeEvent()");
	std::shared_ptr<WindowResizeEvent> p = std::static_pointer_cast<WindowResizeEvent>(pEvent);

	const uint uWidth  = p->width ;
	const uint uHeight = p->height;

	if (p->hwnd == mpWinMain->GetHWND())
	{
		SwapChain& Swapchain = mpRenderer->GetWindowSwapChain(p->hwnd);
		const int NUM_BACK_BUFFERS =  Swapchain.GetNumBackBuffers();

		if (mpScene)
		{
			// Update Camera Projection Matrices
			Camera& cam = mpScene->GetActiveCamera(); // TODO: all cameras?
			FProjectionMatrixParameters UpdatedProjectionMatrixParams = cam.GetProjectionParameters();
			UpdatedProjectionMatrixParams.ViewportWidth = static_cast<float>(uWidth);
			UpdatedProjectionMatrixParams.ViewportHeight = static_cast<float>(uHeight);
			cam.SetProjectionMatrix(UpdatedProjectionMatrixParams);

#if 0
			// Update PostProcess Data
			for (int i = 0; i < NUM_BACK_BUFFERS; ++i)
			{
				FPostProcessParameters& PPParams = mpScene->GetPostProcessParameters(i);

#if !DISABLE_FIDELITYFX_CAS
				// Update FidelityFX constant blocks
				if (PPParams.IsFFXCASEnabled())
				{
					PPParams.FFXCASParams.UpdateCASConstantBlock(uWidth, uHeight, uWidth, uHeight);
				}
#endif
				if (PPParams.IsFSR1Enabled())
				{
					const uint InputWidth  = static_cast<uint>(PPParams.ResolutionScale * uWidth);
					const uint InputHeight = static_cast<uint>(PPParams.ResolutionScale * uHeight);
					PPParams.FSR1ShaderParameters.easu.UpdateConstantBlock(InputWidth, InputHeight, InputWidth, InputHeight, uWidth, uHeight);
					PPParams.FSR1ShaderParameters.rcas.UpdateConstantBlock();
				}
			}
#endif
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
	SCOPED_CPU_MARKER("RenderThread_HandleEvents");

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
		case EEventType::WINDOW_RESIZE_EVENT              : pLastResizeEventLookup[pEvent->hwnd] = std::static_pointer_cast<WindowResizeEvent>(pEvent); break;
		case EEventType::TOGGLE_FULLSCREEN_EVENT          : RenderThread_HandleToggleFullscreenEvent(pEvent.get()); break;
		case EEventType::WINDOW_CLOSE_EVENT               : RenderThread_HandleWindowCloseEvent(pEvent.get()); break;
		case EEventType::SET_VSYNC_EVENT                  : RenderThread_HandleSetVSyncEvent(pEvent.get()); break;
		case EEventType::SET_SWAPCHAIN_FORMAT_EVENT       : RenderThread_HandleSetSwapchainFormatEvent(pEvent.get()); break;
		case EEventType::SET_SWAPCHAIN_PRESENTATION_QUEUE : RenderThread_HandleSetSwapchainQueueEvent(pEvent.get()); break;
		case EEventType::SET_HDR10_STATIC_METADATA_EVENT  : RenderThread_HandleSetHDRMetaDataEvent(pEvent.get()); break;
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
	SCOPED_CPU_MARKER("HandleWindowResizeEvent()");
	const std::shared_ptr<WindowResizeEvent> pResizeEvent = std::static_pointer_cast<WindowResizeEvent>(pEvent);
	const HWND&                      hwnd = pResizeEvent->hwnd;
	const int                       WIDTH = pResizeEvent->width;
	const int                      HEIGHT = pResizeEvent->height;
	SwapChain&                  Swapchain = mpRenderer->GetWindowSwapChain(hwnd);
	std::unique_ptr<Window>&         pWnd = GetWindow(hwnd);
	const bool         bIsWindowMinimized = WIDTH == 0 && HEIGHT == 0;
	const bool                  bIsClosed = pWnd->IsClosed();
	const bool      bFullscreenTransition = pWnd->GetFullscreenHeight() == HEIGHT && pWnd->GetFullscreenWidth() == WIDTH;
	const bool          bUseHDRRenderPath = this->ShouldRenderHDR(hwnd);

	if (bIsClosed || bIsWindowMinimized)
	{
		const std::string reason = bIsClosed ? "closed" : "minimized";
		Log::Warning("RenderThread: Ignoring WindowResizeEvent as Window<%x> is %s.", hwnd, reason.c_str());
		return;
	}

#if VERBOSE_LOGGING
	Log::Info("RenderThread: Handle Window<%x> Resize event, set resolution to %dx%d", hwnd, WIDTH, HEIGHT);
#endif

	const FSetHDRMetaDataParams HDRMetaData = this->GatherHDRMetaDataParameters(hwnd);

#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	mEventQueue_WinToVQE_Update.AddItem(pResizeEvent);
#endif

	Swapchain.WaitForGPU();
	HRESULT hr = Swapchain.Resize(WIDTH, HEIGHT, Swapchain.GetFormat());
	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
	{
		Log::Error("Device removed during Swpchain.Resize(%d, %d)", WIDTH, HEIGHT);
		// TODO: recreate the swapchain?
	}

	if (bFullscreenTransition)
	{
		const FSetHDRMetaDataParams HDRMetaData = this->GatherHDRMetaDataParameters(hwnd);
		if (pWnd->GetIsOnHDRCapableDisplay())
			Swapchain.SetHDRMetaData(HDRMetaData);
		else if (GetWindowSettings(hwnd).bEnableHDR)
			Swapchain.ClearHDRMetaData();
	}
	Swapchain.EnsureSwapChainColorSpace(Swapchain.GetFormat() == DXGI_FORMAT_R16G16B16A16_FLOAT ? _16 : _8, false);
	pWnd->OnResize(WIDTH, HEIGHT);
	mpRenderer->OnWindowSizeChanged(hwnd, WIDTH, HEIGHT); // updates render context

	const bool bFSREnabled = mSettings.gfx.IsFSR1Enabled() && !bUseHDRRenderPath; // TODO: remove this when FSR-HDR is implemented
	const bool bUpscaling = bFSREnabled || 0; // update here when other upscaling methods are added

	const float fResolutionScale = bUpscaling ? mSettings.gfx.Rendering.RenderResolutionScale : 1.0f;

	if (hwnd == mpWinMain->GetHWND())
	{
		mpRenderer->UnloadWindowSizeDependentResources(hwnd);
		mpRenderer->LoadWindowSizeDependentResources(hwnd, WIDTH, HEIGHT, fResolutionScale, this->ShouldRenderHDR(hwnd));
	}
}

void VQEngine::RenderThread_HandleWindowCloseEvent(const IEvent* pEvent)
{
	SCOPED_CPU_MARKER("HandleWindowCloseEvent()");
	const WindowCloseEvent* pWindowCloseEvent = static_cast<const WindowCloseEvent*>(pEvent);
	const HWND& hwnd = pWindowCloseEvent->hwnd;
	SwapChain& Swapchain = mpRenderer->GetWindowSwapChain(hwnd);
	std::unique_ptr<Window>& pWnd = GetWindow(hwnd);

	Log::Info("RenderThread: Handle Window Close event <%x>", hwnd);

	if (hwnd == mpWinMain->GetHWND())
	{
		mbStopAllThreads.store(true);
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
		RenderThread_SignalUpdateThread();
#endif
	}
	
	mpRenderer->GetWindowSwapChain(hwnd).WaitForGPU();
	mpRenderer->UnloadWindowSizeDependentResources(hwnd);
	pWindowCloseEvent->Signal_WindowDependentResourcesDestroyed.NotifyAll();
}

void VQEngine::RenderThread_HandleToggleFullscreenEvent(const IEvent* pEvent)
{
	const ToggleFullscreenEvent* pToggleFSEvent = static_cast<const ToggleFullscreenEvent*>(pEvent);
	HWND                                   hwnd = pToggleFSEvent->hwnd;
	SwapChain&                        Swapchain = mpRenderer->GetWindowSwapChain(pToggleFSEvent->hwnd);
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

	const bool bFSREnabled = mSettings.gfx.IsFSR1Enabled();
	const bool bUpscaling = bFSREnabled || 0; // update here when other upscaling methods are added

	const float fResolutionScale = bUpscaling ? mSettings.gfx.Rendering.RenderResolutionScale : 1.0f;

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
				Swapchain.Resize(WndSettings.Width, WndSettings.Height, Swapchain.GetFormat());
			}
		}
	}

	//
	// BORDERLESS FULLSCREEN
	//
	else
	{
		pWnd->ToggleWindowedFullscreen(&Swapchain);
	}

}

void VQEngine::RenderThread_HandleSetVSyncEvent(const IEvent* pEvent)
{
	const SetVSyncEvent*      pToggleVSyncEvent = static_cast<const SetVSyncEvent*>(pEvent);
	HWND                                   hwnd = pToggleVSyncEvent->hwnd;
	const bool                      bVsyncState = pToggleVSyncEvent->bToggleValue;
	SwapChain&                        Swapchain = mpRenderer->GetWindowSwapChain(pToggleVSyncEvent->hwnd);
	const FWindowSettings&          WndSettings = GetWindowSettings(hwnd);
	std::unique_ptr<Window>&               pWnd = GetWindow(hwnd);
	const bool   bExclusiveFullscreenTransition = WndSettings.DisplayMode == EDisplayMode::EXCLUSIVE_FULLSCREEN;
	const bool                 bFullscreenState = bExclusiveFullscreenTransition ? Swapchain.IsFullscreen() : pWnd->IsFullscreen();
	const int                             WIDTH = bFullscreenState ? pWnd->GetFullscreenWidth() : pWnd->GetWidth();
	const int                            HEIGHT = bFullscreenState ? pWnd->GetFullscreenHeight() : pWnd->GetHeight();

	Swapchain.WaitForGPU(); // make sure GPU is finished
	{
		auto& ctx = mpRenderer->GetWindowRenderContext(hwnd);

		FSwapChainCreateDesc desc;
		desc.bVSync         = bVsyncState;
		desc.bFullscreen    = Swapchain.IsFullscreen();
		desc.numBackBuffers = Swapchain.GetNumBackBuffers();
		desc.pCmdQueue      = &ctx.PresentQueue;
		desc.pDevice        = ctx.pDevice->GetDevicePtr();
		desc.hwnd           = pWnd->GetHWND();
		desc.windowHeight   = pWnd->GetHeight();
		desc.windowWidth    = pWnd->GetWidth();
		desc.bHDR           = Swapchain.IsHDRFormat();

		constexpr bool bHDR10 = false; // TODO;
		if(desc.bHDR)
			desc.bitDepth = bHDR10 ? _10 : _16;

		Swapchain.Destroy();
		Swapchain.Create(desc);
		
		const FSetHDRMetaDataParams HDRMetaData = this->GatherHDRMetaDataParameters(hwnd);
		if (pWnd->GetIsOnHDRCapableDisplay())
			Swapchain.SetHDRMetaData(HDRMetaData);
		else if (GetWindowSettings(hwnd).bEnableHDR)
			Swapchain.ClearHDRMetaData();
		Swapchain.EnsureSwapChainColorSpace(Swapchain.GetFormat() == DXGI_FORMAT_R16G16B16A16_FLOAT ? _16 : _8, false);
	}

	mSettings.gfx.Display.bVsync = bVsyncState;
	Log::Info("Toggle VSync: %d", bVsyncState);
}

void VQEngine::RenderThread_HandleSetSwapchainFormatEvent(const IEvent* pEvent)
{
	const SetSwapchainFormatEvent* pSwapchainEvent = static_cast<const SetSwapchainFormatEvent*>(pEvent);
	const HWND&                      hwnd = pEvent->hwnd;
	const std::unique_ptr<Window>&   pWnd = GetWindow(hwnd);
	const int                       WIDTH = pWnd->GetWidth();
	const int                      HEIGHT = pWnd->GetHeight();
	SwapChain&                  Swapchain = mpRenderer->GetWindowSwapChain(hwnd);
	const FSetHDRMetaDataParams HDRMetaData = this->GatherHDRMetaDataParameters(hwnd);
	const bool                bFormatChange = pSwapchainEvent->format != Swapchain.GetFormat();

	Swapchain.WaitForGPU();
	Swapchain.Resize(WIDTH, HEIGHT, pSwapchainEvent->format);
	
	pWnd->SetIsOnHDRCapableDisplay(VQSystemInfo::FMonitorInfo::CheckHDRSupport(hwnd));
	if (bFormatChange)
	{
		if (pWnd->GetIsOnHDRCapableDisplay())
			Swapchain.SetHDRMetaData(HDRMetaData);
		else if(GetWindowSettings(hwnd).bEnableHDR)
			Swapchain.ClearHDRMetaData();
		Swapchain.EnsureSwapChainColorSpace(pSwapchainEvent->format == DXGI_FORMAT_R16G16B16A16_FLOAT? _16 : _8, false);
	}

	mbMainWindowHDRTransitionInProgress.store(false);

	const int NUM_BACK_BUFFERS  = Swapchain.GetNumBackBuffers();
	const int BACK_BUFFER_INDEX = Swapchain.GetCurrentBackBufferIndex();
	const EDisplayCurve OutputDisplayCurve = Swapchain.IsHDRFormat()
		? mSettings.gfx.PostProcessing.HDROutputDisplayCurve
		: mSettings.gfx.PostProcessing.SDROutputDisplayCurve;
	
	
	Log::Info("Set Swapchain Format: %s | OutputDisplayCurve: %s"
		, VQRenderer::DXGIFormatAsString(pSwapchainEvent->format).data()
		, GetDisplayCurveString(OutputDisplayCurve)
	);
}
void VQEngine::RenderThread_HandleSetSwapchainQueueEvent(const IEvent* pEvent)
{
	SCOPED_CPU_MARKER("HandleSetSwapchainQueueEvent");
	const SetSwapchainPresentationQueueEvent* pSwapchainEvent = static_cast<const SetSwapchainPresentationQueueEvent*>(pEvent);
	const HWND&                      hwnd = pEvent->hwnd;
	const std::unique_ptr<Window>&   pWnd = GetWindow(hwnd);
	const int                       WIDTH = pWnd->GetWidth();
	const int                      HEIGHT = pWnd->GetHeight();
	SwapChain&                  Swapchain = mpRenderer->GetWindowSwapChain(hwnd);
	CommandQueue& q = pSwapchainEvent->bUseDedicatedPresentationQueue 
		? mpRenderer->GetPresentationCommandQueue() 
		: mpRenderer->GetCommandQueue(GFX);

	{
		SCOPED_CPU_MARKER_C("WAIT_SWAPCHAIN_DONE", 0xFF00AA00);
		Swapchain.WaitForGPU();
	}
	Swapchain.UpdatePresentQueue(q.pQueue);
	{
		SCOPED_CPU_MARKER("Swapchain.Resize");
		Swapchain.Resize(WIDTH, HEIGHT, Swapchain.GetFormat());
	}
}

void VQEngine::RenderThread_HandleSetHDRMetaDataEvent(const IEvent* pEvent)
{
	const SetStaticHDRMetaDataEvent* pSetMetaDataEvent = static_cast<const SetStaticHDRMetaDataEvent*>(pEvent);
	const HWND&                      hwnd = pEvent->hwnd;
	const std::unique_ptr<Window>&   pWnd = GetWindow(hwnd);
	SwapChain&                  Swapchain = mpRenderer->GetWindowSwapChain(hwnd);

	if (pWnd->GetIsOnHDRCapableDisplay())
	{
		Swapchain.WaitForGPU();
		Swapchain.SetHDRMetaData(pSetMetaDataEvent->payload);
	}
}
