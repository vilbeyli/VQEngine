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
#define NOMINMAX
#include <utility>
#include "VQEngine.h"
#include "GPUMarker.h"

#include "VQUtils/Source/utils.h"

#include "Libs/imgui/imgui.h"
// To use the 'disabled UI state' functionality (ImGuiItemFlags_Disabled), include internal header
// https://github.com/ocornut/imgui/issues/211#issuecomment-339241929
#include "Libs/imgui/imgui_internal.h"
static        void BeginDisabledUIState(bool bEnable)
{
	if (!bEnable)
	{
		ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
	}
}
static        void EndDisabledUIState(bool bEnable)
{
	if (!bEnable)
	{
		ImGui::PopItemFlag();
		ImGui::PopStyleVar();
	}
}
static inline void ImGuiSpacing3() { ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing(); }
static        bool IsAnyMouseButtonDown()
{
	ImGuiIO& io = ImGui::GetIO();
	for (int n = 0; n < IM_ARRAYSIZE(io.MouseDown); n++)
		if (io.MouseDown[n])
			return true;
	return false;
}
#if 0
LRESULT ImGUI_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	ImGuiIO& io = ImGui::GetIO();
	switch (msg)
	{
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
	{
		int button = 0;
		if (msg == WM_LBUTTONDOWN) button = 0;
		else if (msg == WM_RBUTTONDOWN) button = 1;
		else if (msg == WM_MBUTTONDOWN) button = 2;
		if (!IsAnyMouseButtonDown() && GetCapture() == NULL)
			;// SetCapture(hwnd);
		io.MouseDown[button] = true;
		return 0;
	}
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MBUTTONUP:
	{
		int button = 0;
		if (msg == WM_LBUTTONUP) button = 0;
		else if (msg == WM_RBUTTONUP) button = 1;
		else if (msg == WM_MBUTTONUP) button = 2;
		io.MouseDown[button] = false;
		if (!IsAnyMouseButtonDown() && GetCapture() == hwnd)
			ReleaseCapture();
		return 0;
	}
	case WM_MOUSEWHEEL:
		io.MouseWheel += GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? +1.0f : -1.0f;
		return 0;
	case WM_MOUSEMOVE:
		io.MousePos.x = (signed short)(lParam);
		io.MousePos.y = (signed short)(lParam >> 16);
		return 0;
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		if (wParam < 256)
			io.KeysDown[wParam] = true;
		return 0;
	case WM_KEYUP:
	case WM_SYSKEYUP:
		if (wParam < 256)
			io.KeysDown[wParam] = false;
		return 0;
	case WM_CHAR:
		// You can also use ToAscii()+GetKeyboardState() to retrieve characters.
		if (wParam > 0 && wParam < 0x10000)
			io.AddInputCharacter((unsigned short)wParam);
		return 0;
	}
	return 0;
}
#endif
static void UpdateImGUIState(HWND hwnd)
{
	ImGuiIO& io = ImGui::GetIO();

	// Setup display size (every frame to accommodate for window resizing)
	RECT rect;
	GetClientRect(hwnd, &rect);
	io.DisplaySize = ImVec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));

	// Read keyboard modifiers inputs
	io.KeyCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	io.KeyShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	io.KeyAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
	io.KeySuper = false;
	// io.KeysDown : filled by WM_KEYDOWN/WM_KEYUP events
	// io.MousePos : filled by WM_MOUSEMOVE events
	// io.MouseDown : filled by WM_*BUTTON* events
	// io.MouseWheel : filled by WM_MOUSEWHEEL events

	// Hide OS mouse cursor if ImGui is drawing it
	if (io.MouseDrawCursor)
		SetCursor(NULL);
}

// -----------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------

struct VERTEX_CONSTANT_BUFFER{ float mvp[4][4]; };

#if !VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
constexpr int FRAME_DATA_INDEX = 0;
#endif

// -----------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------


static void InitializeEngineUIState(FUIState& s)
{
	s.bUIOnSeparateWindow = false;
	
	s.bHideAllWindows = false;
	s.bWindowVisible_DebugPanel = true;
	s.bWindowVisible_GraphicsSettingsPanel = true;
	s.bWindowVisible_PostProcessControls = true;
	s.bWindowVisible_SceneControls = true;
	s.bWindowVisible_Profiler = true;
	s.bProfiler_ShowEngineStats = true;
}

void VQEngine::InitializeUI(HWND hwnd)
{
	mpImGuiContext = ImGui::CreateContext();
	ImGui::SetCurrentContext(mpImGuiContext);

	ImGuiIO& io = ImGui::GetIO();

	// Get UI texture 
	//
	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	// Create the texture object
	//
	TextureCreateDesc rDescs("texUI");
	rDescs.d3d12Desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1);
	rDescs.pData = pixels;
	TextureID texUI = mRenderer.CreateTexture(rDescs);
	SRV_ID srvUI = mRenderer.CreateAndInitializeSRV(texUI);

	// Tell ImGUI what the image view is
	//
	io.Fonts->TexID = (ImTextureID)&mRenderer.GetSRV(srvUI);


	// Create sampler
	//
	D3D12_STATIC_SAMPLER_DESC SamplerDesc = {};
	SamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	SamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	SamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	SamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	SamplerDesc.MipLODBias = 0;
	SamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	SamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	SamplerDesc.MinLOD = 0.0f;
	SamplerDesc.MaxLOD = 0.0f;
	SamplerDesc.ShaderRegister = 0;
	SamplerDesc.RegisterSpace = 0;
	SamplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

#if 0 // do we need imgui keymapping?
	io.KeyMap[ImGuiKey_Tab] = VK_TAB;
	io.KeyMap[ImGuiKey_LeftArrow] = VK_LEFT;
	io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
	io.KeyMap[ImGuiKey_UpArrow] = VK_UP;
	io.KeyMap[ImGuiKey_DownArrow] = VK_DOWN;
	io.KeyMap[ImGuiKey_PageUp] = VK_PRIOR;
	io.KeyMap[ImGuiKey_PageDown] = VK_NEXT;
	io.KeyMap[ImGuiKey_Home] = VK_HOME;
	io.KeyMap[ImGuiKey_End] = VK_END;
	io.KeyMap[ImGuiKey_Delete] = VK_DELETE;
	io.KeyMap[ImGuiKey_Backspace] = VK_BACK;
	io.KeyMap[ImGuiKey_Enter] = VK_RETURN;
	io.KeyMap[ImGuiKey_Escape] = VK_ESCAPE;
	io.KeyMap[ImGuiKey_A] = 'A';
	io.KeyMap[ImGuiKey_C] = 'C';
	io.KeyMap[ImGuiKey_V] = 'V';
	io.KeyMap[ImGuiKey_X] = 'X';
	io.KeyMap[ImGuiKey_Y] = 'Y';
	io.KeyMap[ImGuiKey_Z] = 'Z';

#endif
	io.ImeWindowHandle = hwnd;
	// Hide OS mouse cursor if ImGui is drawing it
	if (io.MouseDrawCursor)
		SetCursor(NULL);

	InitializeEngineUIState(mUIState);
}
void VQEngine::ExitUI()
{
	ImGui::DestroyContext(mpImGuiContext);
}


void VQEngine::UpdateUIState(HWND hwnd, float dt)
{
	SCOPED_CPU_MARKER_C("UpdateUIState()", 0xFF007777);

	// Data for the UI controller to update
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCount(mpWinMain->GetHWND());
	const int FRAME_DATA_INDEX = mNumUpdateLoopsExecuted % NUM_BACK_BUFFERS;
#endif
	FPostProcessParameters& PPParams = mpScene->GetPostProcessParameters(FRAME_DATA_INDEX);
	FSceneRenderParameters& SceneParams = mpScene->GetSceneView(FRAME_DATA_INDEX).sceneParameters;
	ImGuiStyle& style = ImGui::GetStyle();

	// ----------------------------------

	UpdateImGUIState(hwnd);

	ImGui::NewFrame();
	style.FrameBorderSize = 1.0f;

	if (!mUIState.bHideAllWindows)
	{
		if (mUIState.bWindowVisible_SceneControls)         DrawSceneControlsWindow(mpScene->GetActiveCameraIndex(), mpScene->GetActiveEnvironmentMapPresetIndex(), SceneParams);
		if (mUIState.bWindowVisible_PostProcessControls)   DrawPostProcessControlsWindow(PPParams);
		if (mUIState.bWindowVisible_Profiler)              DrawProfilerWindow(mpScene->GetSceneRenderStats(FRAME_DATA_INDEX), dt);
		if (mUIState.bWindowVisible_DebugPanel)            DrawDebugPanelWindow(SceneParams);
		if (mUIState.bWindowVisible_GraphicsSettingsPanel) DrawGraphicsSettingsWindow(SceneParams);
	}
}

// ============================================================================================================================
// ============================================================================================================================
// ============================================================================================================================

// Window Padding
//---------------------------------------------
const uint32_t CONTROLS_WINDOW_PADDING_X = 10;
const uint32_t CONTROLS_WINDOW_PADDING_Y = 10;
const uint32_t CONTROLS_WINDOW_SIZE_X    = 320;
const uint32_t CONTROLS_WINDOW_SIZE_Y    = 200;
//---------------------------------------------
const uint32_t DBG_WINDOW_PADDING_X      = 10;
const uint32_t DBG_WINDOW_PADDING_Y      = 10;
const uint32_t DBG_WINDOW_SIZE_X         = 350;
const uint32_t DBG_WINDOW_SIZE_Y         = 150;
//---------------------------------------------
const uint32_t PP_WINDOW_PADDING_X       = 10;
const uint32_t PP_WINDOW_PADDING_Y       = 10;
const uint32_t PP_WINDOW_SIZE_X          = 350;
const uint32_t PP_WINDOW_SIZE_Y          = 150;
//---------------------------------------------
const uint32_t GFX_WINDOW_PADDING_X      = 10;
const uint32_t GFX_WINDOW_PADDING_Y      = 10;
const uint32_t GFX_WINDOW_SIZE_X         = 350;
const uint32_t GFX_WINDOW_SIZE_Y         = 200;
//---------------------------------------------
const uint32_t PROFILER_WINDOW_PADDIG_X  = 10;
const uint32_t PROFILER_WINDOW_PADDIG_Y  = 10;
const uint32_t PROFILER_WINDOW_SIZE_X    = 330;
const uint32_t PROFILER_WINDOW_SIZE_Y    = 650;
//---------------------------------------------


// Dropdown data ----------------------------------------------------------------------------------------------
constexpr size_t NUM_MAX_ENV_MAP_NAMES = 10;
constexpr size_t NUM_MAX_LEVEL_NAMES   = 8;
constexpr size_t NUM_MAX_CAMERA_NAMES  = 10;
static const char* pStrSceneNames [NUM_MAX_LEVEL_NAMES  ] = {};
static const char* pStrEnvMapNames[NUM_MAX_ENV_MAP_NAMES] = {};
static const char* pStrCameraNames[NUM_MAX_CAMERA_NAMES ] = {};

template<size_t NUM_ARRAY_SIZE> 
static void FillCStrArray(const char* (&pCStrArray)[NUM_ARRAY_SIZE], const std::vector<std::string>& StrVector)
{
	assert(StrVector.size() < NUM_ARRAY_SIZE);
	size_t i = 0;
	for (const std::string& name : StrVector) pCStrArray[i++] = name.c_str();
	pCStrArray[i++] = "None"; // the 'unselected' option
	// initialize the rest with empty string so ImGui doesn't crash
	for (; i < NUM_ARRAY_SIZE; ++i)    pCStrArray[i] = "";
}

static void InitializeStaticCStringData_SceneControls(
	  const std::vector<std::string>& vNames_EnvMap
	, const std::vector<std::string>& vNames_Levels
)
{
	static bool bEnvMapNamesInitialized = false;
	static bool bLevelNamesInitialized = false;
	static bool bCameraNamesInitialized = false;
	if (!bLevelNamesInitialized)
	{
		FillCStrArray<NUM_MAX_LEVEL_NAMES>(pStrSceneNames, vNames_Levels);
		bLevelNamesInitialized = true;
	}

	if (!bEnvMapNamesInitialized)
	{
		FillCStrArray<NUM_MAX_ENV_MAP_NAMES>(pStrEnvMapNames, vNames_EnvMap);
		bEnvMapNamesInitialized = true;
	}

	// TODO: initialize from scene cameras, otherwise we'll crash on -non existing camera index
	{
		for (int i = 0; i < NUM_MAX_CAMERA_NAMES; ++i) pStrCameraNames[i] = "";
		pStrCameraNames[0] = "Main Camera";
		pStrCameraNames[1] = "Secondary Camera 0";
		pStrCameraNames[2] = "Secondary Camera 1";
		pStrCameraNames[3] = "Secondary Camera 2";
		pStrCameraNames[4] = "Secondary Camera 3";
	};
}
// Dropdown data ----------------------------------------------------------------------------------------------


constexpr int FPS_GRAPH_MAX_FPS_THRESHOLDS[] = { 800, 240, 120, 90, 66, 45, 30, 15, 10, 5, 4, 3, 2, 1 };
constexpr const char* FPS_GRAPH_MAX_FPS_THRESHOLDS_STR[] = { "800", "240", "120", "90", "66", "45", "30", "15", "10", "5", "4", "3", "2", "1" };

//
// Helpers
//
static ImVec4 SelectFPSColor(int FPS)
{
	constexpr int FPS_THRESHOLDS[] ={30, 45, 60, 144, 500};
	static const ImVec4 FPSColors[6] =
	{
		  ImVec4(0.8f, 0.0f, 0.0f, 1.0f) // RED    | FPS < 30
		, ImVec4(0.6f, 0.3f, 0.0f, 1.0f) // ORANGE | 30 < FPS < 45
		, ImVec4(0.8f, 0.8f, 0.0f, 1.0f) // YELLOW | 45 < FPS < 60
		, ImVec4(0.0f, 0.8f, 0.0f, 1.0f) // GREEN  | 60 < FPS < 144 
		, ImVec4(0.0f, 0.8f, 0.7f, 1.0f) // CYAN   | 144 < FPS < 500
		, ImVec4(0.4f, 0.1f, 0.8f, 1.0f) // PURPLE | 500 < FPS
	};

	int iColor = 0;
	for (int iThr = 0; iThr < _countof(FPS_THRESHOLDS); ++iThr)
	{
		if (FPS > FPS_THRESHOLDS[iThr])
			iColor = std::min(iThr+1, (int)_countof(FPS_THRESHOLDS));
	}
	return FPSColors[iColor];
}
static void DrawFPSChart(int fps)
{
	// ui
	const ImVec2 GRAPH_SIZE = ImVec2(0, 60);

	// data
	constexpr size_t FPS_HISTORY_SIZE = 160;
	static float FPS_HISTORY[FPS_HISTORY_SIZE] = {};
	static size_t FPS_HISTORY_INDEX = 0;

	static float RECENT_HIGHEST_FPS = 0.0f;


	// update data
	FPS_HISTORY[FPS_HISTORY_INDEX] = static_cast<float>(fps);
	// TODO: proper sliding average

	FPS_HISTORY_INDEX = CircularIncrement(FPS_HISTORY_INDEX, FPS_HISTORY_SIZE);
	RECENT_HIGHEST_FPS = FPS_HISTORY[FPS_HISTORY_INDEX];
	size_t iFPSGraphMaxValue = 0;
	for (int i = _countof(FPS_GRAPH_MAX_FPS_THRESHOLDS)-1; i >= 0; --i)
	{
		if (RECENT_HIGHEST_FPS < FPS_GRAPH_MAX_FPS_THRESHOLDS[i]) // FPS_GRAPH_MAX_FPS_THRESHOLDS are in decreasing order
		{
			iFPSGraphMaxValue = std::min((int)_countof(FPS_GRAPH_MAX_FPS_THRESHOLDS) - 1, i);
			break;
		}
	}

	//ImGui::PlotHistogram("FPS Histo", FPS_HISTORY, IM_ARRAYSIZE(FPS_HISTORY));
	ImGui::PlotLines(FPS_GRAPH_MAX_FPS_THRESHOLDS_STR[iFPSGraphMaxValue], FPS_HISTORY, FPS_HISTORY_SIZE, 0, "FPS", 0.0f, (float)FPS_GRAPH_MAX_FPS_THRESHOLDS[iFPSGraphMaxValue], GRAPH_SIZE);

}
static void DrawFrameTimeChart()
{
	// TODO
}


//
// VQEngine UI Drawing
//
void VQEngine::DrawProfilerWindow(const FSceneStats& FrameStats, float dt)
{
	const FSceneStats& s = FrameStats; // shorthand rename

	const uint32 W = mpWinMain->GetWidth();
	const uint32 H = mpWinMain->GetHeight();

	const uint32_t PROFILER_WINDOW_POS_X = W - PROFILER_WINDOW_PADDIG_X - PROFILER_WINDOW_SIZE_X;
	const uint32_t PROFILER_WINDOW_POS_Y = PROFILER_WINDOW_PADDIG_Y;

	const float WHITE_AMOUNT = 0.70f;
	const ImVec4 DataTextColor = ImVec4(WHITE_AMOUNT, WHITE_AMOUNT, WHITE_AMOUNT, 1.0f);
	const ImVec4 DataHighlightedColor = ImVec4(1, 1, 1, 1);

	ImGui::SetNextWindowPos(ImVec2((float)PROFILER_WINDOW_POS_X, (float)PROFILER_WINDOW_POS_Y), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(PROFILER_WINDOW_SIZE_X, PROFILER_WINDOW_SIZE_Y), ImGuiCond_FirstUseEver);

	ImGui::Begin("PROFILER (F2)", &mUIState.bWindowVisible_Profiler);


	ImGui::Text("PERFORMANCE");
	ImGui::Separator();
	//if (ImGui::CollapsingHeader("PERFORMANCE", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const int fps = static_cast<int>(1.0f / dt);
		const float frameTime_ms = dt * 1000.0f;
		const float frameTime_us = frameTime_ms * 1000.0f;
		ImGui::TextColored(DataTextColor      , "Resolution : %ix%i", W, H);
		ImGui::TextColored(SelectFPSColor(fps), "FPS        : %d (%.2f ms)", fps, frameTime_ms);
		
		DrawFPSChart(fps);
		DrawFrameTimeChart();
	}

	ImGuiSpacing3();

	if (ImGui::CollapsingHeader("SYSTEM INFO", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::TextColored(DataTextColor, "API        : %s", "DirectX 12");
		ImGui::TextColored(DataTextColor, "GPU        : %s", mSysInfo.GPUs.back().DeviceName.c_str());
		ImGui::TextColored(DataTextColor, "CPU        : %s", mSysInfo.CPU.DeviceName.c_str());
		ImGui::TextColored(DataTextColor, "Video  RAM : %s", StrUtil::FormatByte(mSysInfo.GPUs.back().DedicatedGPUMemory).c_str());
#if 0   // TODO: after memory tracking, display system and GPU memory that are in use
		ImGui::TextColored(DataTextColor, "System RAM : 0B used of %s", StrUtil::FormatByte(mSysInfo.RAM.TotalPhysicalMemory).c_str());
#else
		ImGui::TextColored(DataTextColor, "System RAM : %s", StrUtil::FormatByte(mSysInfo.RAM.TotalPhysicalMemory).c_str());
#endif

		int NumDisplay = 0;
		for (const VQSystemInfo::FMonitorInfo& mon : mSysInfo.Monitors)
		{
			const bool bIsWindowOnThisMonitor = false; // TODO: this state is not currently tracked, see DispatchHDRSwapchainTransitionEvents()
			const ImVec4& TextColor = bIsWindowOnThisMonitor ? DataHighlightedColor : DataTextColor;

			                             ImGui::TextColored(TextColor, "Display%d  ", NumDisplay++);
			if (!mon.DeviceName.empty()) ImGui::TextColored(TextColor, "  DeviceName : %s", mon.DeviceName.c_str());
			                             ImGui::TextColored(TextColor, "  Resolution : %dx%d", mon.NativeResolution.Width, mon.NativeResolution.Height);
			if (mon.bSupportsHDR)        ImGui::TextColored(TextColor, "  HDR        : Enabled");
		}
	}

	ImGuiSpacing3();

	ImGui::Checkbox("Show Engine Stats", &mUIState.bProfiler_ShowEngineStats);
	if (mUIState.bProfiler_ShowEngineStats)
	{
		ImGui::Separator();
		if (ImGui::CollapsingHeader("SCENE ENTITIES", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextColored(DataTextColor, "Meshes    : %d", s.NumMeshes);
			ImGui::TextColored(DataTextColor, "Materials : %d", s.NumMaterials);
			//ImGui::TextColored(DataTextColor, "Models    : %d", s.NumModels);
			ImGui::TextColored(DataTextColor, "Objects   : %d", s.NumObjects);
			ImGui::TextColored(DataTextColor, "Cameras   : %d", s.NumCameras);
		}
		ImGuiSpacing3();
		if (ImGui::CollapsingHeader("LIGHTS", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const int NumLights = s.NumDynamicLights + s.NumStaticLights + s.NumStationaryLights;
			const int NumDisabledLights = s.NumDisabledDirectionalLights + s.NumDisabledPointLights + s.NumDisabledSpotLights;

			ImGui::TextColored(DataTextColor, "Active/Total       : %d/%d", NumLights - NumDisabledLights, NumLights);
			ImGui::TextColored(DataTextColor, "---------------------------");
			ImGui::TextColored(DataTextColor, "Spot Lights        : %d/%d | Shadowing : %d", s.NumSpotLights - s.NumDisabledSpotLights, s.NumSpotLights, s.NumShadowingSpotLights);
			ImGui::TextColored(DataTextColor, "Point Lights       : %d/%d | Shadowing : %d", s.NumPointLights - s.NumDisabledPointLights, s.NumPointLights, s.NumShadowingPointLights);
			ImGui::TextColored(DataTextColor, "Directional Lights : %d/%d", s.NumDirectionalLights - s.NumDisabledDirectionalLights, s.NumDirectionalLights);
		}
		ImGuiSpacing3();
		if (ImGui::CollapsingHeader("RENDER COMMANDS", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextColored(DataTextColor, "Mesh         : %d", s.NumMeshRenderCommands);
			ImGui::TextColored(DataTextColor, "Shadow Mesh  : %d", s.NumShadowMeshRenderCommands);
			ImGui::TextColored(DataTextColor, "Bounding Box : %d", s.NumBoundingBoxRenderCommands);
#if 0 // TODO: track renderer API calls
			ImGui::TextColored(DataTextColor, "Draw Calls     : %d", mRenderStats.NumDraws);
			ImGui::TextColored(DataTextColor, "Dispatch Calls : %d", mRenderStats.NumDispatches);
#endif
		}
	}
	ImGui::End();
}

void VQEngine::DrawSceneControlsWindow(int& iSelectedCamera, int& iSelectedEnvMap, FSceneRenderParameters& SceneRenderParams)
{
	const uint32 W = mpWinMain->GetWidth();
	const uint32 H = mpWinMain->GetHeight();

	const uint32_t CONTROLS_WINDOW_POS_X = CONTROLS_WINDOW_PADDING_X;
	const uint32_t CONTROLS_WINDOW_POS_Y = CONTROLS_WINDOW_PADDING_Y;
	ImGui::SetNextWindowPos(ImVec2((float)CONTROLS_WINDOW_POS_X, (float)CONTROLS_WINDOW_POS_Y), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(CONTROLS_WINDOW_SIZE_X, CONTROLS_WINDOW_SIZE_Y), ImGuiCond_FirstUseEver);

	InitializeStaticCStringData_SceneControls(mResourceNames.mEnvironmentMapPresetNames, mResourceNames.mSceneNames);

	int iEnvMap = iSelectedEnvMap == -1 ? static_cast<int>(mResourceNames.mEnvironmentMapPresetNames.size()) : iSelectedEnvMap;
	assert(iSelectedCamera < _countof(pStrCameraNames) && iSelectedCamera >= 0);


	ImGui::Begin("SCENE CONTROLS (F1)", &mUIState.bWindowVisible_SceneControls);

	if (ImGui::Combo("Scene", &mIndex_SelectedScene, pStrSceneNames, (int)std::min(_countof(pStrSceneNames), mResourceNames.mSceneNames.size())))
	{
		// TODO: move StarLoadingScene() into some queue and issue at the end of frame as doing it 
		//       immediately causes ImGui missing EndFrame()/Render()
		// 
		#if 0
		this->StartLoadingScene(mIndex_SelectedScene);
		#endif
	}
	ImGui::Combo("Camera (C)", &iSelectedCamera, pStrCameraNames, _countof(pStrCameraNames));
	MathUtil::Clamp(iSelectedCamera, 0, (int)mpScene->GetNumSceneCameras()-1);
	if (ImGui::Combo("HDRI Map (Page Up/Down)", &iEnvMap, pStrEnvMapNames, (int)std::min(_countof(pStrEnvMapNames), mResourceNames.mEnvironmentMapPresetNames.size()+1)))
	{
		// TODO: move StartLoadingEnvironmentMap() into some queue and issue at the end of frame as doing it 
		//       immediately causes ImGui missing EndFrame()/Render()
		//
		#if 0
		if(iSelectedEnvMap != iEnvMap)
			StartLoadingEnvironmentMap(iEnvMap);
		#endif
	}

	const float MaxAmbientLighting = this->ShouldRenderHDR(mpWinMain->GetHWND()) ? 150.0f : 2.0f;
	MathUtil::Clamp(SceneRenderParams.fAmbientLightingFactor, 0.0f, MaxAmbientLighting);
	ImGui::SliderFloat("Ambient Lighting Factor", &SceneRenderParams.fAmbientLightingFactor, 0.0f, MaxAmbientLighting, "%.3f");

	ImGui::End();

}

void VQEngine::DrawDebugPanelWindow(FSceneRenderParameters& SceneParams)
{
	const uint32 W = mpWinMain->GetWidth();
	const uint32 H = mpWinMain->GetHeight();

	const uint32_t DBG_WINDOW_POS_X = DBG_WINDOW_PADDING_X;
	const uint32_t DBG_WINDOW_POS_Y = H - DBG_WINDOW_SIZE_Y - DBG_WINDOW_PADDING_Y;
	ImGui::SetNextWindowPos(ImVec2((float)DBG_WINDOW_POS_X, (float)DBG_WINDOW_POS_Y), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(DBG_WINDOW_SIZE_X, DBG_WINDOW_SIZE_Y), ImGuiCond_FirstUseEver);


	ImGui::Begin("DEBUG PANEL (F4)", &mUIState.bWindowVisible_DebugPanel);

	ImGui::Text("Debug Draw");
	ImGui::Separator();
	ImGui::Checkbox("Show GameObject Bounding Boxes (Shift+N)", &SceneParams.bDrawGameObjectBoundingBoxes);
	ImGui::Checkbox("Show Mesh Bounding Boxes (N)", &SceneParams.bDrawMeshBoundingBoxes);
	ImGui::Checkbox("Show Light Bounding Volumes (L)", &SceneParams.bDrawLightBounds);
	ImGui::Checkbox("Draw Lights", &SceneParams.bDrawLightMeshes);

	ImGui::End();
}
void VQEngine::DrawPostProcessControlsWindow(FPostProcessParameters& PPParams)
{
	const uint32 W = mpWinMain->GetWidth();
	const uint32 H = mpWinMain->GetHeight();

	const uint32_t PP_WINDOW_POS_X = W - PP_WINDOW_PADDING_X - PP_WINDOW_SIZE_X;
	const uint32_t PP_WINDOW_POS_Y = H - PP_WINDOW_PADDING_Y - PP_WINDOW_SIZE_Y;
	ImGui::SetNextWindowPos(ImVec2((float)PP_WINDOW_POS_X, (float)PP_WINDOW_POS_Y), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(PP_WINDOW_SIZE_X, PP_WINDOW_SIZE_Y), ImGuiCond_FirstUseEver);

	ImGui::Begin("Post Processing (F3)", &mUIState.bWindowVisible_PostProcessControls);
	
	ImGui::Text("FidelityFX CAS");
	ImGui::Separator();
	ImGui::Checkbox("Enabled ##0 (B)", &PPParams.bEnableCAS);
	{
		BeginDisabledUIState(PPParams.bEnableCAS);
		ImGui::SliderFloat("Sharpening", &PPParams.FFXCASParams.CASSharpen, 0.0f, 1.0f, "%.1f");
		EndDisabledUIState(PPParams.bEnableCAS);
	}

	ImGuiSpacing3();

	const bool bHDR = this->ShouldRenderHDR(mpWinMain->GetHWND());
	ImGui::Text((bHDR ? "HDR Tonemapper" : "Tonemapper"));
	ImGui::Separator();
	{
		if (bHDR)
		{
			const std::string strDispalyCurve = GetDisplayCurveString(PPParams.TonemapperParams.OutputDisplayCurve);
			const std::string strColorSpace   = GetColorSpaceString(PPParams.TonemapperParams.ContentColorSpace);
			ImGui::Text("OutputDevice : %s", strDispalyCurve.c_str() );
			ImGui::Text("Color Space  : %s", strColorSpace.c_str() );
		}
		else
		{
			bool bGamma = PPParams.TonemapperParams.ToggleGammaCorrection;
			ImGui::Checkbox("[SDR] Apply Gamma (G)", &bGamma);
			PPParams.TonemapperParams.ToggleGammaCorrection = bGamma ? 1 : 0;
		}
	}


	ImGui::End();
}


void VQEngine::DrawGraphicsSettingsWindow(FSceneRenderParameters& SceneRenderParams)
{
	const uint32 W = mpWinMain->GetWidth();
	const uint32 H = mpWinMain->GetHeight();
	HWND hwnd = mpWinMain->GetHWND();

	FGraphicsSettings& gfx = mSettings.gfx;

	// static data - labels
	static const char* pStrAALabels[] =
	{
		  "No AA"
		, "MSAAx4"
		, ""
	};
	static const char* pStrSSAOLabels[] =
	{
		"FidelityFX CACAO"
		, "None"
		, ""
	};
	// static data


	int iAALabel = gfx.bAntiAliasing ? 1 : 0;
	int iSSAOLabel = SceneRenderParams.bScreenSpaceAO ? 0 : 1;

	const uint32_t GFX_WINDOW_POS_X = GFX_WINDOW_PADDING_X;
	const uint32_t GFX_WINDOW_POS_Y = H - PP_WINDOW_PADDING_Y - PP_WINDOW_SIZE_Y - GFX_WINDOW_PADDING_Y - GFX_WINDOW_SIZE_Y;
	ImGui::SetNextWindowPos(ImVec2((float)GFX_WINDOW_POS_X, (float)GFX_WINDOW_POS_Y), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(GFX_WINDOW_SIZE_X, GFX_WINDOW_SIZE_Y), ImGuiCond_FirstUseEver);

	ImGui::Begin("GRAPHICS SETTINGS (F5)", &mUIState.bWindowVisible_DebugPanel);
	ImGui::Separator();

	ImGui::Text("DISPLAY");
	ImGui::Separator();
	if (ImGui::Checkbox("VSync (V)", &gfx.bVsync))
	{
		mEventQueue_WinToVQE_Renderer.AddItem(std::make_shared<SetVSyncEvent>(hwnd, gfx.bVsync));
	}
	bool bFS = mpWinMain->IsFullscreen();
	if (ImGui::Checkbox("Fullscreen (Alt+Enter)", &bFS))
	{
		mEventQueue_WinToVQE_Renderer.AddItem(std::make_shared<ToggleFullscreenEvent>(hwnd));
	}

	ImGuiSpacing3();

	ImGui::Text("RENDERING");
	ImGui::Separator();
	if (ImGui::Combo("AntiAliasing (M)", &iAALabel, pStrAALabels, _countof(pStrAALabels)))
	{
		Log::Info("AA Changed");
	}
	if (ImGui::Combo("Ambient Occlusion", &iSSAOLabel, pStrSSAOLabels, _countof(pStrSSAOLabels)))
	{
		Log::Info("AO Changed");
	}

	ImGui::End();
}


