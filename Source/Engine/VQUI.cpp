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
static inline void ImGuiSpacing(int NumSpaces) { for (int i = 0; i < NumSpaces; ++i) ImGui::Spacing(); }
static        bool IsAnyMouseButtonDown()
{
	ImGuiIO& io = ImGui::GetIO();
	for (int n = 0; n < IM_ARRAYSIZE(io.MouseDown); n++)
		if (io.MouseDown[n])
			return true;
	return false;
}

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
	// filled by UpdateThread_HandleEvents() -----------------
	// io.KeysDown   : tied to WM_KEYDOWN/WM_KEYUP events
	// io.MousePos   : tied to WM_MOUSEMOVE events
	// io.MouseDown  : tied to WM_*BUTTON* events
	// io.MouseWheel : tied to WM_MOUSEWHEEL / raw input events
	// --------------------------------------------------------

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
	s.bWindowVisible_KeyMappings = false;
	s.bWindowVisible_SceneControls = true;
	s.bWindowVisible_GraphicsSettingsPanel = false;
	s.bWindowVisible_Profiler = false;
	s.bWindowVisible_DebugPanel = false;
	s.bProfiler_ShowEngineStats = true;
}

void VQEngine::InitializeUI(HWND hwnd)
{
	mpImGuiContext = ImGui::CreateContext();
	ImGui::SetCurrentContext(mpImGuiContext);

	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr; // don't save out to a .ini file

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
	SRV_ID srvUI = mRenderer.AllocateAndInitializeSRV(texUI);

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
		if (mUIState.bWindowVisible_KeyMappings)           DrawKeyMappingsWindow();
		if (mUIState.bWindowVisible_SceneControls)         DrawSceneControlsWindow(mpScene->GetActiveCameraIndex(), mpScene->GetActiveEnvironmentMapPresetIndex(), SceneParams);
		if (mUIState.bWindowVisible_Profiler)              DrawProfilerWindow(mpScene->GetSceneRenderStats(FRAME_DATA_INDEX), dt);
		if (mUIState.bWindowVisible_DebugPanel)            DrawDebugPanelWindow(SceneParams, PPParams);
		if (mUIState.bWindowVisible_GraphicsSettingsPanel) DrawGraphicsSettingsWindow(SceneParams, PPParams);
	}

	// If we fired an event that would trigger loading,
	// i.e. changing the scene or the environment map, call ImGui::EndFrame()
	// here to prevent calling NewFrame() before Render/EndFrame is called.
	// Render() won't be called if we're changing the level because the appstate
	// will cause VQengine to render the loading screen which doesn't call ImGui::Render(),
	// and as a result ImGui crashes on a double NewFrame() call.
	if (!mQueue_SceneLoad.empty() || mbLoadingEnvironmentMap.load())
	{
		ImGui::EndFrame();
	}
}

// ============================================================================================================================
// ============================================================================================================================
// ============================================================================================================================

// Window Padding
//---------------------------------------------
const uint32_t CONTROLS_WINDOW_PADDING_X = 10;
const uint32_t CONTROLS_WINDOW_PADDING_Y = 10;
const uint32_t CONTROLS_WINDOW_SIZE_X    = 380;
const uint32_t CONTROLS_WINDOW_SIZE_Y    = 360;
//---------------------------------------------
const uint32_t GFX_WINDOW_PADDING_X      = 10;
const uint32_t GFX_WINDOW_PADDING_Y      = 10;
const uint32_t GFX_WINDOW_SIZE_X         = 380;
const uint32_t GFX_WINDOW_SIZE_Y         = 500;
//---------------------------------------------
const uint32_t PROFILER_WINDOW_PADDIG_X  = 10;
const uint32_t PROFILER_WINDOW_PADDIG_Y  = 10;
const uint32_t PROFILER_WINDOW_SIZE_X    = 330;
const uint32_t PROFILER_WINDOW_SIZE_Y    = 850;
//---------------------------------------------
const uint32_t DBG_WINDOW_PADDING_X      = 10;
const uint32_t DBG_WINDOW_PADDING_Y      = 10;
const uint32_t DBG_WINDOW_SIZE_X         = 330;
const uint32_t DBG_WINDOW_SIZE_Y         = 180;
//---------------------------------------------


// Dropdown data ----------------------------------------------------------------------------------------------
const     ImVec4 UI_COLLAPSING_HEADER_COLOR_VALUE = ImVec4(0.0, 0.00, 0.0, 0.7f);
constexpr size_t NUM_MAX_ENV_MAP_NAMES    = 10;
constexpr size_t NUM_MAX_LEVEL_NAMES      = 8;
constexpr size_t NUM_MAX_CAMERA_NAMES     = 10;
constexpr size_t NUM_MAX_DRAW_MODE_NAMES  = static_cast<size_t>(EDrawMode::NUM_DRAW_MODES);
constexpr size_t NUM_MAX_FSR_OPTION_NAMES = FPostProcessParameters::FFSR_EASU::EPresets::NUM_FSR_PRESET_OPTIONS;
static const char* pStrSceneNames [NUM_MAX_LEVEL_NAMES  ] = {};
static const char* pStrEnvMapNames[NUM_MAX_ENV_MAP_NAMES] = {};
static const char* pStrCameraNames[NUM_MAX_CAMERA_NAMES] = {};
static const char* pStrDrawModes  [NUM_MAX_DRAW_MODE_NAMES] = {};
static const char* pStrFSROptionNames[NUM_MAX_FSR_OPTION_NAMES] = {};
static const char* pStrMaxFrameRateOptionNames[3] = {}; // see Settings.h:FGraphicsSettings


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

	// TODO: initialize from scene cameras
	{
		for (int i = 0; i < NUM_MAX_CAMERA_NAMES; ++i) pStrCameraNames[i] = "";
		pStrCameraNames[0] = "Main Camera";
		pStrCameraNames[1] = "Secondary Camera 0";
		pStrCameraNames[2] = "Secondary Camera 1";
		pStrCameraNames[3] = "Secondary Camera 2";
		pStrCameraNames[4] = "Secondary Camera 3";
	};
}
static void InitializeStaticCStringData_PostProcessingControls()
{
	static bool bFSRNamesInitialized = false;

	if (!bFSRNamesInitialized)
	{
		pStrFSROptionNames[0] = "Ultra Quality";
		pStrFSROptionNames[1] = "Quality";
		pStrFSROptionNames[2] = "Balanced";
		pStrFSROptionNames[3] = "Performance";
		pStrFSROptionNames[4] = "Custom";
		bFSRNamesInitialized = true;
	}
}
static void InitializeStaticCStringData_GraphicsSettings()
{
	static bool GraphicsSettingsDropdownDataInitialized = false;
	if (!GraphicsSettingsDropdownDataInitialized)
	{
		pStrMaxFrameRateOptionNames[0] = "Auto (Refresh Rate x 1.15)";
		pStrMaxFrameRateOptionNames[1] = "Unlimited";
		pStrMaxFrameRateOptionNames[2] = "Custom";
		GraphicsSettingsDropdownDataInitialized = true;
	} 
}
static void InitializeStaticCStringData_EDrawMode()
{
	static bool EDrawModeDropdownDataInitialized = false;
	if (!EDrawModeDropdownDataInitialized)
	{
		auto fnToStr = [](EDrawMode m) {
			switch (m)
			{
			case EDrawMode::LIT_AND_POSTPROCESSED: return "LIT_AND_POSTPROCESSED";
			//case EDrawMode::WIREFRAME: return "WIREFRAME";
			//case EDrawMode::NO_MATERIALS: return "NO_MATERIALS";
			case EDrawMode::DEPTH: return "DEPTH";
			case EDrawMode::NORMALS: return "NORMALS";
			case EDrawMode::ROUGHNESS: return "ROUGHNESS";
			case EDrawMode::METALLIC: return "METALLIC";
			case EDrawMode::AO: return "AO";
			case EDrawMode::ALBEDO: return "ALBEDO";
			case EDrawMode::REFLECTIONS: return "REFLECTIONS";
			case EDrawMode::MOTION_VECTORS: return "MOTION_VECTORS";
			case EDrawMode::NUM_DRAW_MODES: return "NUM_DRAW_MODES";
			}
			return "";
		};
		for (int i = 0; i < (int)EDrawMode::NUM_DRAW_MODES; ++i)
			pStrDrawModes[i] = fnToStr((EDrawMode)i);
		
		EDrawModeDropdownDataInitialized = true;
	}
}

// Dropdown data ----------------------------------------------------------------------------------------------


constexpr int FPS_GRAPH_MAX_FPS_THRESHOLDS[] = { 800, 240, 120, 90, 66, 45, 30, 15, 10, 5, 4, 3, 2, 1 };
constexpr const char* FPS_GRAPH_MAX_FPS_THRESHOLDS_STR[] = { "800", "240", "120", "90", "66", "45", "30", "15", "10", "5", "4", "3", "2", "1" };
static size_t DetermineChartMaxValueIndex(int RecentHighestFPS)
{
	size_t iFPSGraphMaxValue = 0;
	for (int i = _countof(FPS_GRAPH_MAX_FPS_THRESHOLDS) - 1; i >= 0; --i)
	{
		if (RecentHighestFPS < FPS_GRAPH_MAX_FPS_THRESHOLDS[i]) // FPS_GRAPH_MAX_FPS_THRESHOLDS are in decreasing order
		{
			iFPSGraphMaxValue = std::min((int)_countof(FPS_GRAPH_MAX_FPS_THRESHOLDS) - 1, i);
			break;
		}
	}
	return iFPSGraphMaxValue;
}

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
	// data
	constexpr size_t FPS_HISTORY_SIZE = 160;
	static float  FPS_HISTORY[FPS_HISTORY_SIZE] = {};
	
	// update data
	float RecentHighestFPS = 0.0f;
	for (size_t i = 1; i < FPS_HISTORY_SIZE; ++i) 
	{
		FPS_HISTORY[i - 1] = FPS_HISTORY[i]; // slide
		RecentHighestFPS = std::max(RecentHighestFPS, FPS_HISTORY[i]);
	}
	FPS_HISTORY[FPS_HISTORY_SIZE-1] = static_cast<float>(fps); // log the last fps
	RecentHighestFPS = std::max(RecentHighestFPS, FPS_HISTORY[FPS_HISTORY_SIZE - 1]);

	const size_t iFPSGraphMaxValue = DetermineChartMaxValueIndex(static_cast<int>(RecentHighestFPS));

	// ui
	const ImVec2 GRAPH_SIZE = ImVec2(0, 60);
	ImGui::PlotLines(FPS_GRAPH_MAX_FPS_THRESHOLDS_STR[iFPSGraphMaxValue], FPS_HISTORY, FPS_HISTORY_SIZE, 0, "FPS", 0.0f, (float)FPS_GRAPH_MAX_FPS_THRESHOLDS[iFPSGraphMaxValue], GRAPH_SIZE);
}
static void DrawFrameTimeChart()
{
	// TODO
}


//
// VQEngine UI Drawing
//

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


	ImGui::Begin("SCENE CONTROLS", &mUIState.bWindowVisible_SceneControls);

	ImGui::Text("Help");
	ImGui::Separator();
	
	if (ImGui::Button((mUIState.bWindowVisible_KeyMappings ? "Hide Key Mapping" : "Show Key Mapping")))
	{
		mUIState.bWindowVisible_KeyMappings = !mUIState.bWindowVisible_KeyMappings;
	}

	ImGuiSpacing3();
	
	ImGui::Text("Windows");
	ImGui::Separator();
	ImGui::Checkbox("Show Scene Controls (F1)", &mUIState.bWindowVisible_SceneControls);
	ImGui::Checkbox("Show Graphics Settings (F3)", &mUIState.bWindowVisible_GraphicsSettingsPanel);
	ImGui::Checkbox("Show Profiler (F2)", &mUIState.bWindowVisible_Profiler);
	ImGui::Checkbox("Show Debug (F4)", &mUIState.bWindowVisible_DebugPanel);

	ImGuiSpacing3();

	ImGui::Text("Editor");
	ImGui::Separator();
	if (ImGui::Combo("Scene", &mIndex_SelectedScene, pStrSceneNames, (int)std::min(_countof(pStrSceneNames), mResourceNames.mSceneNames.size())))
	{
		this->StartLoadingScene(mIndex_SelectedScene);
	}
	ImGui::Combo("Camera (C)", &iSelectedCamera, pStrCameraNames, _countof(pStrCameraNames));
	MathUtil::Clamp(iSelectedCamera, 0, (int)mpScene->GetNumSceneCameras()-1);
	if (ImGui::Combo("HDRI Map (Page Up/Down)", &iEnvMap, pStrEnvMapNames, (int)std::min(_countof(pStrEnvMapNames), mResourceNames.mEnvironmentMapPresetNames.size()+1)))
	{
		if (iSelectedEnvMap != iEnvMap)
		{
			const bool bUnloadEnvMap = mResourceNames.mEnvironmentMapPresetNames.size() == iEnvMap;
			if (bUnloadEnvMap)
			{
				this->WaitUntilRenderingFinishes();
				UnloadEnvironmentMap();
				iSelectedEnvMap = INVALID_ID; // update iSelectedEnvMap
			}
			else
			{
				StartLoadingEnvironmentMap(iEnvMap); // update iSelectedEnvMap internally
			}
		}
	}

	ImGui::SliderFloat("HDRI Rotation", &SceneRenderParams.fYawSliderValue, 0.0f, 1.0f);

	const float MaxAmbientLighting = this->ShouldRenderHDR(mpWinMain->GetHWND()) ? 150.0f : 2.0f;
	MathUtil::Clamp(SceneRenderParams.fAmbientLightingFactor, 0.0f, MaxAmbientLighting);
	ImGui::SliderFloat("Ambient Lighting Factor", &SceneRenderParams.fAmbientLightingFactor, 0.0f, MaxAmbientLighting, "%.3f");

	ImGui::End();
}
void VQEngine::DrawKeyMappingsWindow()
{
	const uint32 W = mpWinMain->GetWidth();
	const uint32 H = mpWinMain->GetHeight();

	
	const uint32_t KEYMAPPINGS_WINDOW_POS_X = (W >> 1) - 150;
	const uint32_t KEYMAPPINGS_WINDOW_POS_Y = 20;

	if (mUIState.bWindowVisible_KeyMappings)
	{

		ImGui::SetNextWindowPos(ImVec2((float)KEYMAPPINGS_WINDOW_POS_X, (float)KEYMAPPINGS_WINDOW_POS_Y), ImGuiCond_FirstUseEver);
		
		const float fHeaderGray = 0.6f;
		//const ImVec4 ColHeader_Gray(fHeaderGray, fHeaderGray, fHeaderGray, 1.0f);
		const ImVec4 ColHeader(0.0f, 0.9f, 0.0f, 1.0f);

		ImGui::Begin("KEY MAPPINGS", &mUIState.bWindowVisible_KeyMappings);

		ImGui::PushStyleColor(ImGuiCol_Text, ColHeader);
		ImGui::Text("------------------ UI ----------------------");
		ImGui::PopStyleColor(1);
		ImGui::Text("          F1 : Toggle Scene Window");
		ImGui::Text("          F2 : Toggle Profiler Window");
		ImGui::Text("          F3 : Toggle Graphics Settings Window");
		ImGui::Text("          F4 : Toggle Debug Window");
		ImGui::Text("     Shift+Z : Show/Hide ALL UI windows");

		ImGui::PushStyleColor(ImGuiCol_Text, ColHeader);
		ImGui::Text("------------------ CAMERA ----------------------");
		ImGui::PopStyleColor(1);
		ImGui::Text(" Right Click : Free Camera");
		ImGui::Text("  Left Click : Orbit Camera");
		ImGui::Text("      Scroll : Adjust distance (Orbit Camera)");
		ImGui::Text("      WASDEQ : Move Camera (Free Camera)");
		ImGui::Text("       Space : Toggle animation (if available)");
		ImGui::Text("");

		ImGui::PushStyleColor(ImGuiCol_Text, ColHeader);
		ImGui::Text("------------------ DISPLAY ---------------------");
		ImGui::PopStyleColor(1);
		ImGui::Text("   Alt+Enter : Toggle fullscreen");
		ImGui::Text("           V : Toggle VSync");
		ImGui::Text("           M : Toggle MSAA");
		ImGui::Text("");

		ImGui::PushStyleColor(ImGuiCol_Text, ColHeader);
		ImGui::Text("------------------ SCENE -----------------------");
		ImGui::PopStyleColor(1);
		ImGui::Text("     Shift+R : Reload level");
		ImGui::Text("Page Up/Down : Change the HDRI Environment Map");
		ImGui::Text("         1-4 : Change between available scenes");
		ImGui::Text("           R : Reset camera");
		ImGui::Text("           C : Cycle scene cameras");
		ImGui::Text("           G : Toggle gamma correction");
		ImGui::Text("           B : Toggle FidelityFX Sharpening");
		ImGui::Text("           J : Toggle FidelityFX Super Resolution 1.0");
		ImGui::Text("");

		ImGui::PushStyleColor(ImGuiCol_Text, ColHeader);
		ImGui::Text("------------------ DEBUG  ----------------------");
		ImGui::PopStyleColor(1);
		ImGui::Text("           N : Toggle Mesh bounding boxes");
		ImGui::Text("           L : Toggle Light bounding volumes");
		ImGui::Text("     Shift+N : Toggle GameObject bounding boxes");
		ImGui::Text("      Ctrl+C : Log active camera values");

		ImGui::End();
	}
}


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

	ImGui::Begin("PROFILER", &mUIState.bWindowVisible_Profiler);


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

void VQEngine::DrawDebugPanelWindow(FSceneRenderParameters& SceneParams, FPostProcessParameters& PPParams)
{
	const uint32 W = mpWinMain->GetWidth();
	const uint32 H = mpWinMain->GetHeight();

	const uint32_t DBG_WINDOW_POS_X = std::min(W - PROFILER_WINDOW_SIZE_X - DBG_WINDOW_SIZE_X - DBG_WINDOW_PADDING_X*2, GFX_WINDOW_SIZE_X + GFX_WINDOW_PADDING_X + DBG_WINDOW_PADDING_X);
	const uint32_t DBG_WINDOW_POS_Y = H - DBG_WINDOW_SIZE_Y - DBG_WINDOW_PADDING_Y;
	ImGui::SetNextWindowPos(ImVec2((float)DBG_WINDOW_POS_X, (float)DBG_WINDOW_POS_Y), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(DBG_WINDOW_SIZE_X, DBG_WINDOW_SIZE_Y), ImGuiCond_FirstUseEver);

	InitializeStaticCStringData_EDrawMode();

	ImGui::Begin("DEBUG PANEL", &mUIState.bWindowVisible_DebugPanel);

	ImGui::Text("Debug Draw");
	ImGui::Separator();
	int iDrawMode = (int)PPParams.eDrawMode;
	ImGui::Combo("Draw Mode", &iDrawMode, pStrDrawModes, _countof(pStrDrawModes));
	PPParams.eDrawMode = (EDrawMode)iDrawMode;
	if (PPParams.eDrawMode == EDrawMode::NORMALS)
	{
		bool bUnpackNormals = PPParams.VizParams.iUnpackNormals;
		ImGui::Checkbox("Unpack Normals", &bUnpackNormals);
		PPParams.VizParams.iUnpackNormals = bUnpackNormals;
	}
	if (PPParams.eDrawMode == EDrawMode::MOTION_VECTORS)
	{
		ImGui::SliderFloat("MoVec Intensity", &PPParams.VizParams.fInputStrength, 0.0f, 200.0f);
	}

	ImGui::Checkbox("Show GameObject Bounding Boxes (Shift+N)", &SceneParams.bDrawGameObjectBoundingBoxes);
	ImGui::Checkbox("Show Mesh Bounding Boxes (N)", &SceneParams.bDrawMeshBoundingBoxes);
	ImGui::Checkbox("Show Light Bounding Volumes (L)", &SceneParams.bDrawLightBounds);
	ImGui::Checkbox("Draw Lights", &SceneParams.bDrawLightMeshes);

	ImGui::End();
}

void VQEngine::DrawPostProcessSettings(FPostProcessParameters& PPParams)
{
	// constants
	const bool bFSREnabled = PPParams.IsFSREnabled();
	const uint32 W = mpWinMain->GetWidth();
	const uint32 H = mpWinMain->GetHeight();

	// fns
	auto fnSendWindowResizeEvents = [&]()
	{
		mEventQueue_WinToVQE_Renderer.AddItem(std::make_unique<WindowResizeEvent>(W, H, mpWinMain->GetHWND()));
		mEventQueue_WinToVQE_Update.AddItem(std::make_unique<WindowResizeEvent>(W, H, mpWinMain->GetHWND()));
	};

	// one time initialization
	InitializeStaticCStringData_PostProcessingControls();

	ImGui::PushStyleColor(ImGuiCol_Header, UI_COLLAPSING_HEADER_COLOR_VALUE);
	if (ImGui::CollapsingHeader("POST PROCESSING", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::PopStyleColor();
		ImGui::Text("FidelityFX Super Resolution 1.0");
		ImGui::Separator();
		if (ImGui::Checkbox("Enabled (J) ##1", &PPParams.bEnableFSR))
		{
			fnSendWindowResizeEvents();
		}
		BeginDisabledUIState(PPParams.bEnableFSR);
		{
			int iFSROption = PPParams.FFSR_EASUParams.SelectedFSRPreset;
			if (ImGui::Combo("Preset", &iFSROption, pStrFSROptionNames, _countof(pStrFSROptionNames)))
			{
				// update the PPParams data
				PPParams.FFSR_EASUParams.SelectedFSRPreset = static_cast<FPostProcessParameters::FFSR_EASU::EPresets>(iFSROption);
				fnSendWindowResizeEvents();
			}
			if (PPParams.FFSR_EASUParams.SelectedFSRPreset == FPostProcessParameters::FFSR_EASU::EPresets::CUSTOM)
			{
				if (ImGui::SliderFloat("Resolution Scale", &PPParams.FFSR_EASUParams.fCustomScaling, 0.50f, 1.0f, "%.2f"))
				{
					fnSendWindowResizeEvents();
				}
			}

			float LinearSharpness = PPParams.FFSR_RCASParams.GetLinearSharpness();
			if (ImGui::SliderFloat("Sharpness", &LinearSharpness, 0.01f, 1.00f, "%.2f"))
			{
				PPParams.FFSR_RCASParams.SetLinearSharpness(LinearSharpness);
				PPParams.FFSR_RCASParams.UpdateRCASConstantBlock();
			}
		}
		EndDisabledUIState(PPParams.bEnableFSR);

		ImGuiSpacing3();

		if (!bFSREnabled)
		{
			ImGui::Text("FidelityFX CAS");
			ImGui::Separator();
			bool bCASEnabled = PPParams.IsFFXCASEnabled();
			bool bCASEnabledBefore = bCASEnabled;
			BeginDisabledUIState(!bFSREnabled);
			ImGui::Checkbox("Enabled (B) ##0", &bCASEnabled);
			{
				BeginDisabledUIState(bCASEnabled);
				if (ImGui::SliderFloat("Sharpening", &PPParams.FFXCASParams.CASSharpen, 0.0f, 1.0f, "%.2f"))
				{
					PPParams.FFXCASParams.UpdateCASConstantBlock(W,H,W,H);
				}
				EndDisabledUIState(bCASEnabled);
			}
			EndDisabledUIState(!bFSREnabled);
			if (bCASEnabledBefore != bCASEnabled)
			{
				PPParams.bEnableCAS = bCASEnabled;
			}
			ImGuiSpacing3();
		}


		const bool bHDR = this->ShouldRenderHDR(mpWinMain->GetHWND());
		ImGui::Text((bHDR ? "Tonemapper (HDR)" : "Tonemapper"));
		ImGui::Separator();
		{
			if (bHDR)
			{
				const std::string strDispalyCurve = GetDisplayCurveString(PPParams.TonemapperParams.OutputDisplayCurve);
				const std::string strColorSpace   = GetColorSpaceString(PPParams.TonemapperParams.ContentColorSpace);
				ImGui::Text("OutputDevice : %s", strDispalyCurve.c_str() );
				ImGui::Text("Color Space  : %s", strColorSpace.c_str() );
				ImGui::SliderFloat("UI Brightness", &PPParams.TonemapperParams.UIHDRBrightness, 0.1f, 20.f, "%.1f");
			}
			else
			{
				bool bGamma = PPParams.TonemapperParams.ToggleGammaCorrection;
				ImGui::Checkbox("[SDR] Apply Gamma (G)", &bGamma);
				PPParams.TonemapperParams.ToggleGammaCorrection = bGamma ? 1 : 0;
			}
		}
	}
	else
	{
		ImGui::PopStyleColor();
	}
}


void VQEngine::DrawGraphicsSettingsWindow(FSceneRenderParameters& SceneRenderParams, FPostProcessParameters& PPParams)
{
	const uint32 W = mpWinMain->GetWidth();
	const uint32 H = mpWinMain->GetHeight();
	HWND hwnd = mpWinMain->GetHWND();

	FGraphicsSettings& gfx = mSettings.gfx;

	// static data - labels
	static const char* pStrAALabels[] =
	{
		  "None ##0"
		, "MSAAx4"
		, ""
	};
	static const char* pStrSSAOLabels[] =
	{
		"None ##1"
		, "FidelityFX CACAO"
		, ""
	};
	static const char* pStrReflectionsLabels[]
	{
		  "Off ##0"
		, "FidelityFX SSSR"
		//, "Ray Traced" // TODO: enable when ray tracing is added
		, ""
	};
	InitializeStaticCStringData_GraphicsSettings();
	// static data


	int iAALabel = gfx.bAntiAliasing ? 1 : 0;
	int iSSAOLabel = SceneRenderParams.bScreenSpaceAO ? 1 : 0;
	int iReflections = gfx.Reflections;

	const uint32_t GFX_WINDOW_POS_X = GFX_WINDOW_PADDING_X;
	const uint32_t GFX_WINDOW_POS_Y = H - GFX_WINDOW_PADDING_Y*2 - GFX_WINDOW_SIZE_Y;
	ImGui::SetNextWindowPos(ImVec2((float)GFX_WINDOW_POS_X, (float)GFX_WINDOW_POS_Y), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(GFX_WINDOW_SIZE_X, GFX_WINDOW_SIZE_Y), ImGuiCond_FirstUseEver);


	ImGui::Begin("GRAPHICS SETTINGS", &mUIState.bWindowVisible_GraphicsSettingsPanel);
	
	ImGui::PushStyleColor(ImGuiCol_Header, UI_COLLAPSING_HEADER_COLOR_VALUE);
	if (ImGui::CollapsingHeader("DISPLAY", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::PopStyleColor();
		if (!gfx.bVsync)
		{
			static int iLimiter = mSettings.gfx.MaxFrameRate == -1 ? 0 : (mSettings.gfx.MaxFrameRate == 0 ? 1 : 2); // see Settings.h
			static int CustomFrameLimit = mSettings.gfx.MaxFrameRate;
			if (ImGui::Combo("FrameRate Limit", &iLimiter, pStrMaxFrameRateOptionNames, _countof(pStrMaxFrameRateOptionNames)))
			{
				switch (iLimiter)
				{
				case 0: mSettings.gfx.MaxFrameRate = -1; break;
				case 1: mSettings.gfx.MaxFrameRate = 0; break;
				case 2: mSettings.gfx.MaxFrameRate = CustomFrameLimit; break;
				default:
					break;
				}
				SetEffectiveFrameRateLimit();
			}
			if (iLimiter == 2) // custom frame limit value
			{
				if (ImGui::SliderInt("MaxFrames", &CustomFrameLimit, 10, 1000))
				{
					mSettings.gfx.MaxFrameRate = CustomFrameLimit;
					SetEffectiveFrameRateLimit();
				}
			}
		}

		if (ImGui::Checkbox("VSync (V)", &gfx.bVsync))
		{
			mEventQueue_WinToVQE_Renderer.AddItem(std::make_shared<SetVSyncEvent>(hwnd, gfx.bVsync));
		}
		bool bFS = mpWinMain->IsFullscreen();
		if (ImGui::Checkbox("Fullscreen (Alt+Enter)", &bFS))
		{
			mEventQueue_WinToVQE_Renderer.AddItem(std::make_shared<ToggleFullscreenEvent>(hwnd));
		}
	}
	else
	{
		ImGui::PopStyleColor();
	}

	ImGuiSpacing(6);

	ImGui::PushStyleColor(ImGuiCol_Header, UI_COLLAPSING_HEADER_COLOR_VALUE);
	if (ImGui::CollapsingHeader("RENDERING", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::PopStyleColor();
		if (ImGui::Combo("AntiAliasing (M)", &iAALabel, pStrAALabels, _countof(pStrAALabels) - 1))
		{
			gfx.bAntiAliasing = iAALabel;
			Log::Info("AA Changed: %d", gfx.bAntiAliasing);
		}
		if (ImGui::Combo("Ambient Occlusion", &iSSAOLabel, pStrSSAOLabels, _countof(pStrSSAOLabels) - 1))
		{
			SceneRenderParams.bScreenSpaceAO = iSSAOLabel == 1;
			Log::Info("AO Changed: %d", SceneRenderParams.bScreenSpaceAO);
		}
		int iRefl = gfx.Reflections;
		if (ImGui::Combo("Reflections", &iRefl, pStrReflectionsLabels, _countof(pStrReflectionsLabels)-1))
		{
			gfx.Reflections = static_cast<EReflections>(iRefl);
			Log::Info("Reflections Changed: %d", gfx.Reflections);
		}
		switch (gfx.Reflections)
		{
		case EReflections::SCREEN_SPACE_REFLECTIONS__FFX:
		{
			FSceneRenderParameters::FFFX_SSSR_UIParameters& FFXParams = SceneRenderParams.FFX_SSSRParameters;

			ImGui::SliderInt("Max Traversal Iterations", &FFXParams.maxTraversalIterations, 0, 256);
			ImGui::SliderInt("Min Traversal Occupancy", &FFXParams.minTraversalOccupancy, 0, 32);
			ImGui::SliderInt("Most Detailed Level", &FFXParams.mostDetailedDepthHierarchyMipLevel, 0, 5);
			ImGui::SliderFloat("Depth Buffer Thickness", &FFXParams.depthBufferThickness, 0.0f, 5.0f);
			ImGui::SliderFloat("Roughness Threshold", &FFXParams.roughnessThreshold, 0.0f, 1.f);
			ImGui::SliderFloat("Temporal Stability", &FFXParams.temporalStability, 0.0f, 1.0f);
			ImGui::SliderFloat("Temporal Variance Threshold", &FFXParams.temporalVarianceThreshold, 0.0f, 0.01f);
			ImGui::Checkbox("Enable Variance Guided Tracing", &FFXParams.bEnableTemporalVarianceGuidedTracing);

			ImGui::Text("Samples per Quad"); ImGui::SameLine();
			ImGui::RadioButton("1", &FFXParams.samplesPerQuad, 1); ImGui::SameLine();
			ImGui::RadioButton("2", &FFXParams.samplesPerQuad, 2); ImGui::SameLine();
			ImGui::RadioButton("4", &FFXParams.samplesPerQuad, 4);

		}
		break;
		case EReflections::RAY_TRACED_REFLECTIONS:
			break;
		default:
			break;
		}
	}
	else
	{
		ImGui::PopStyleColor();
	}
	
	ImGuiSpacing(6);

	DrawPostProcessSettings(PPParams);

	ImGui::End();
}


