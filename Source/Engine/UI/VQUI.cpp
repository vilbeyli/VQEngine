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
#include "Engine/Scene/SceneViews.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Core/Window.h"

#include "Renderer/Rendering/RenderPass/MagnifierPass.h"
#include "Renderer/Renderer.h"

#include "VQUtils/Include/utils.h"

#include "Libs/imgui/backends/imgui_impl_win32.h"
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



// static data - labels
static const char* szAALabels[] =
{
	  "None ##0"
	, "MSAA x4"
	, "FidelityFX Super Resolution 3 AA"
	, ""
};
static const char* szSSAOLabels[] =
{
	"None ##1"
	, "FidelityFX CACAO"
	, ""
};
static const char* szReflectionsLabels[]
{
	  "Off ##0"
	, "FidelityFX SSSR"
	//, "Ray Traced" // TODO: enable when ray tracing is added
	, ""
};

static bool ImGui_LeftAlignedCombo(const char* szLabel, int* pLabelIndex, const char** pszLabels, int numLabels)
{
	float fullWidth = ImGui::GetContentRegionAvail().x; // Get available width
	float halfWidth = fullWidth * 0.5f; // Calculate half width
	float comboWidth = fullWidth - halfWidth; // Remaining width for the combo

	ImGui::Text(szLabel);
	float spacing = fullWidth - ImGui::CalcTextSize(szLabel).x - comboWidth;
	ImGui::SameLine(/*spacing*/);
	ImGui::SetNextItemWidth(comboWidth);
	return ImGui::Combo("##hidden_label", pLabelIndex, pszLabels, numLabels);
}
static bool ImGui_RightAlignedCombo(const char* szLabel, int* pLabelIndex, const char** pszLabels, int numLabels)
{
	return ImGui::Combo(szLabel, pLabelIndex, pszLabels, numLabels);
}

// -----------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------

struct VERTEX_CONSTANT_BUFFER{ float mvp[4][4]; };

#if !VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
constexpr int FRAME_DATA_INDEX = 0;
#endif

static constexpr float MAGNIFICATION_AMOUNT_MIN = 1.0f;
static constexpr float MAGNIFICATION_AMOUNT_MAX = 32.0f;
static constexpr float MAGNIFIER_RADIUS_MIN = 0.01f;
static constexpr float MAGNIFIER_RADIUS_MAX = 0.85f;

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
	s.bWindowVisible_Editor = false;
	s.bProfiler_ShowEngineStats = true;

	for (int i = 0; i < FUIState::EEditorMode::NUM_EDITOR_MODES; ++i)
		s.SelectedEditeeIndex[i] = 0;
}

void FUIState::GetMouseScreenPosition(int& X, int& Y) const
{
	ImGuiIO& io = ImGui::GetIO();
	X = static_cast<int>(io.MousePos.x);
	Y = static_cast<int>(io.MousePos.y);
}

//----------------------------------------------------------------
//----------------------------------------------------------------
//----------------------------------------------------------------

void VQEngine::InitializeImGUI(HWND hwnd)
{
	SCOPED_CPU_MARKER("InitializeImGUI");
	mpImGuiContext = ImGui::CreateContext();
	ImGui::SetCurrentContext(mpImGuiContext);
	ImGui_ImplWin32_Init(hwnd);

	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr; // don't save out to a .ini file
	// Hide OS mouse cursor if ImGui is drawing it
	if (io.MouseDrawCursor)
		SetCursor(NULL);
}

void VQEngine::InitializeUI(HWND hwnd)
{
	SCOPED_CPU_MARKER("InitializeUI");
	ImGuiIO& io = ImGui::GetIO();
	// Get UI texture 
	//
	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	// Create the texture object
	//
	FTextureRequest rDescs("texUI");
	rDescs.D3D12Desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1);
	rDescs.DataArray.push_back( pixels );

	TextureID texUI = mpRenderer->CreateTexture(rDescs);


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

	mUIState.ResolutionScaleSliderValue = mSettings.gfx.Rendering.RenderResolutionScale;
	InitializeEngineUIState(mUIState);
	
	mpRenderer->WaitHeapsInitialized();
	SRV_ID srvUI = mpRenderer->AllocateAndInitializeSRV(texUI);
	io.Fonts->TexID = (ImTextureID)mpRenderer->GetSRV(srvUI).GetGPUDescHandle().ptr;
}
void VQEngine::ExitUI()
{
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext(mpImGuiContext);
}


void VQEngine::UpdateUIState(HWND hwnd, float dt)
{
	SCOPED_CPU_MARKER_C("UpdateUIState()", 0xFF007777);

	// Data for the UI controller to update
#if VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
	const int NUM_BACK_BUFFERS = mpRenderer->GetSwapChainBackBufferCountmpWinMain->GetHWND());
	const int FRAME_DATA_INDEX = mNumUpdateLoopsExecuted % NUM_BACK_BUFFERS;
#endif

	// TODO: remove this hack, properly sync
	if (!mpScene)
		return;
	FSceneRenderOptions& SceneParams = mpScene->GetSceneView(FRAME_DATA_INDEX).sceneRenderOptions;
	ImGuiStyle& style = ImGui::GetStyle();

	// ----------------------------------

	UpdateImGUIState(hwnd);

	ImGui::NewFrame();
	style.FrameBorderSize = 1.0f;

	if (!mUIState.bHideAllWindows)
	{
		if (mUIState.bWindowVisible_KeyMappings)           DrawKeyMappingsWindow();
		if (mUIState.bWindowVisible_SceneControls)         DrawSceneControlsWindow(mpScene->GetActiveCameraIndex(), mpScene->GetActiveEnvironmentMapPresetIndex(), SceneParams);
		if (mUIState.bWindowVisible_Profiler)              DrawProfilerWindow(mpScene->GetSceneRenderStats(FRAME_DATA_INDEX), mSettings.gfx.Rendering.RenderResolutionScale, dt);
		if (mUIState.bWindowVisible_GraphicsSettingsPanel) DrawGraphicsSettingsWindow(SceneParams);
		if (mUIState.bWindowVisible_Editor)                DrawEditorWindow();
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
const uint32_t DBG_WINDOW_SIZE_Y         = 380;
//---------------------------------------------
const uint32_t EDITOR_WINDOW_PADDING_X   = 10;
const uint32_t EDITOR_WINDOW_PADDING_Y   = 10;
const uint32_t EDITOR_WINDOW_SIZE_X      = 500;
const uint32_t EDITOR_WINDOW_SIZE_Y      = 600;
//---------------------------------------------

// Dropdown data ----------------------------------------------------------------------------------------------
const     ImVec4 UI_COLLAPSING_HEADER_COLOR_VALUE = ImVec4(0.0, 0.00, 0.0, 0.7f);
constexpr size_t NUM_MAX_ENV_MAP_NAMES    = 10;
constexpr size_t NUM_MAX_LEVEL_NAMES      = 8;
constexpr size_t NUM_MAX_CAMERA_NAMES     = 10;
constexpr size_t NUM_MAX_DRAW_MODE_NAMES  = static_cast<size_t>(FDebugVisualizationSettings::EDrawMode::NUM_DRAW_MODES);
static const char* szSceneNames [NUM_MAX_LEVEL_NAMES  ] = {};
static const char* szEnvMapNames[NUM_MAX_ENV_MAP_NAMES] = {};
static const char* szCameraNames[NUM_MAX_CAMERA_NAMES] = {};
static const char* szDrawModes  [NUM_MAX_DRAW_MODE_NAMES] = {};
static const char* szUpscalingLabels[EUpscalingAlgorithm::NUM_UPSCALING_ALGORITHMS] = {};
static const char* szFSR1QualityLabels[AMD_FidelityFX_SuperResolution1::EPreset::NUM_FSR1_PRESET_OPTIONS] = {};
static const char* szFSR3QualityLabels[AMD_FidelityFX_SuperResolution3::EPreset::NUM_FSR3_PRESET_OPTIONS] = {};
static const char* szMaxFrameRateOptionLabels[3] = {}; // see Settings.h:FGraphicsSettings


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
		FillCStrArray<NUM_MAX_LEVEL_NAMES>(szSceneNames, vNames_Levels);
		bLevelNamesInitialized = true;
	}

	if (!bEnvMapNamesInitialized)
	{
		FillCStrArray<NUM_MAX_ENV_MAP_NAMES>(szEnvMapNames, vNames_EnvMap);
		bEnvMapNamesInitialized = true;
	}

	// TODO: initialize from scene cameras
	{
		for (int i = 0; i < NUM_MAX_CAMERA_NAMES; ++i) szCameraNames[i] = "";
		szCameraNames[0] = "Main Camera";
		szCameraNames[1] = "Secondary Camera 0";
		szCameraNames[2] = "Secondary Camera 1";
		szCameraNames[3] = "Secondary Camera 2";
		szCameraNames[4] = "Secondary Camera 3";
	};
}
static void InitializeStaticCStringData_PostProcessingControls()
{
	static bool bPostPRocessLabelsInitialized = false;

	if (!bPostPRocessLabelsInitialized)
	{
		{
			using namespace AMD_FidelityFX_SuperResolution1;
			szFSR1QualityLabels[EPreset::ULTRA_QUALITY] = "Ultra Quality";
			szFSR1QualityLabels[EPreset::QUALITY] = "Quality";
			szFSR1QualityLabels[EPreset::BALANCED] = "Balanced";
			szFSR1QualityLabels[EPreset::PERFORMANCE] = "Performance";
			szFSR1QualityLabels[EPreset::CUSTOM] = "Custom";
		}
		{
			using namespace AMD_FidelityFX_SuperResolution3;
			for (EPreset p = EPreset::NATIVE_AA; p < EPreset::NUM_FSR3_PRESET_OPTIONS; p = (EPreset)((uint)p + 1))
				szFSR3QualityLabels[p] = GetPresetName(p);
		}
		szUpscalingLabels[EUpscalingAlgorithm::NONE] = "None";
		szUpscalingLabels[EUpscalingAlgorithm::FIDELITYFX_SUPER_RESOLUTION_1] = "AMD FidelityFX Super Resolution 1";
		szUpscalingLabels[EUpscalingAlgorithm::FIDELITYFX_SUPER_RESOLUTION_3] = "AMD FidelityFX Super Resolution 3";

		bPostPRocessLabelsInitialized = true;
	}
}
static void InitializeStaticCStringData_GraphicsSettings()
{
	static bool GraphicsSettingsDropdownDataInitialized = false;
	if (!GraphicsSettingsDropdownDataInitialized)
	{
		szMaxFrameRateOptionLabels[0] = "Auto (Refresh Rate x 1.15)";
		szMaxFrameRateOptionLabels[1] = "Unlimited";
		szMaxFrameRateOptionLabels[2] = "Custom";
		GraphicsSettingsDropdownDataInitialized = true;
	} 
}
static void InitializeStaticCStringData_EDrawMode()
{
	static bool EDrawModeDropdownDataInitialized = false;
	if (!EDrawModeDropdownDataInitialized)
	{
		auto fnToStr = [](FDebugVisualizationSettings::EDrawMode m) {
			switch (m)
			{
			case FDebugVisualizationSettings::EDrawMode::LIT_AND_POSTPROCESSED: return "LIT_AND_POSTPROCESSED";
			//case FDebugVisualizationSettings::EDrawMode::WIREFRAME: return "WIREFRAME";
			//case FDebugVisualizationSettings::EDrawMode::NO_MATERIALS: return "NO_MATERIALS";
			case FDebugVisualizationSettings::EDrawMode::DEPTH: return "DEPTH";
			case FDebugVisualizationSettings::EDrawMode::NORMALS: return "NORMALS";
			case FDebugVisualizationSettings::EDrawMode::ROUGHNESS: return "ROUGHNESS";
			case FDebugVisualizationSettings::EDrawMode::METALLIC: return "METALLIC";
			case FDebugVisualizationSettings::EDrawMode::AO: return "AO";
			case FDebugVisualizationSettings::EDrawMode::ALBEDO: return "ALBEDO";
			case FDebugVisualizationSettings::EDrawMode::REFLECTIONS: return "REFLECTIONS";
			case FDebugVisualizationSettings::EDrawMode::MOTION_VECTORS: return "MOTION_VECTORS";
			case FDebugVisualizationSettings::EDrawMode::NUM_DRAW_MODES: return "NUM_DRAW_MODES";
			}
			return "";
		};
		for (int i = 0; i < (int)FDebugVisualizationSettings::EDrawMode::NUM_DRAW_MODES; ++i)
			szDrawModes[i] = fnToStr((FDebugVisualizationSettings::EDrawMode)i);
		
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

void VQEngine::DrawSceneControlsWindow(int& iSelectedCamera, int& iSelectedEnvMap, FSceneRenderOptions& SceneRenderParams)
{
	const uint32 W = mpWinMain->GetWidth();
	const uint32 H = mpWinMain->GetHeight();

	const uint32_t CONTROLS_WINDOW_POS_X = CONTROLS_WINDOW_PADDING_X;
	const uint32_t CONTROLS_WINDOW_POS_Y = CONTROLS_WINDOW_PADDING_Y;
	ImGui::SetNextWindowPos(ImVec2((float)CONTROLS_WINDOW_POS_X, (float)CONTROLS_WINDOW_POS_Y), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(CONTROLS_WINDOW_SIZE_X, CONTROLS_WINDOW_SIZE_Y), ImGuiCond_FirstUseEver);

	InitializeStaticCStringData_SceneControls(mResourceNames.mEnvironmentMapPresetNames, mResourceNames.mSceneNames);

	int iEnvMap = iSelectedEnvMap == -1 ? static_cast<int>(mResourceNames.mEnvironmentMapPresetNames.size()) : iSelectedEnvMap;
	assert(iSelectedCamera < _countof(szCameraNames) && iSelectedCamera >= 0);


	ImGui::Begin("SCENE CONTROLS", &mUIState.bWindowVisible_SceneControls);

	ImGui::Text("Help");
	ImGui::Separator();
	
	if (ImGui::Button((mUIState.bWindowVisible_KeyMappings ? "Hide Key Mapping" : "Show Key Mapping")))
	{
		mUIState.bWindowVisible_KeyMappings = !mUIState.bWindowVisible_KeyMappings;
	}
	
	if (ImGui::BeginTable("ButtonCheckboxesTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody)) {
		ImGui::TableSetupColumn("##", ImGuiTableColumnFlags_NoResize);
		ImGui::TableSetupColumn("##", ImGuiTableColumnFlags_NoResize);

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Checkbox("F1: Scene Controls", &mUIState.bWindowVisible_SceneControls);
		ImGui::TableSetColumnIndex(1);
		ImGui::Checkbox("F2: Profiler", &mUIState.bWindowVisible_Profiler);

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Checkbox("F3: Settings", &mUIState.bWindowVisible_GraphicsSettingsPanel);
		ImGui::TableSetColumnIndex(1);
		ImGui::Checkbox("F4: Editor", &mUIState.bWindowVisible_Editor);
 
		ImGui::EndTable();
	}


	ImGuiSpacing3();

	ImGui::Text("Scene");
	ImGui::Separator();
	if (ImGui_RightAlignedCombo("Scene File", &mIndex_SelectedScene, szSceneNames, (int)std::min(_countof(szSceneNames), mResourceNames.mSceneNames.size())))
	{
		this->StartLoadingScene(mIndex_SelectedScene);
	}
	
	if (ImGui_RightAlignedCombo("HDRI Map (Page Up/Down)", &iEnvMap, szEnvMapNames, (int)std::min(_countof(szEnvMapNames), mResourceNames.mEnvironmentMapPresetNames.size()+1)))
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

	if (iSelectedEnvMap != INVALID_ID)
	{
		ImGui::SliderFloat("HDRI Rotation", &SceneRenderParams.Lighting.fYawSliderValue, 0.0f, 1.0f);
	}

	const float MaxAmbientLighting = this->ShouldRenderHDR(mpWinMain->GetHWND()) ? 150.0f : 2.0f;
	MathUtil::Clamp(SceneRenderParams.Lighting.fAmbientLightingFactor, 0.0f, MaxAmbientLighting);
	ImGui::SliderFloat("Ambient Lighting Factor", &SceneRenderParams.Lighting.fAmbientLightingFactor, 0.0f, MaxAmbientLighting, "%.3f");

	ImGui::ColorEdit4("Outline Color", reinterpret_cast<float*>(&mSettings.Editor.OutlineColor), ImGuiColorEditFlags_DefaultOptions_);

	//ImGui::ColorEdit4();

	ImGuiSpacing3();

	ImGui::Text("Camera");
	ImGui::Separator();
	ImGui_RightAlignedCombo("Camera (C)", &iSelectedCamera, szCameraNames, _countof(szCameraNames));
	MathUtil::Clamp(iSelectedCamera, 0, (int)mpScene->GetNumSceneCameras() - 1);

	static const char* pszCamerControllerModes[] = { "First Person", "Orbit"};
	Camera& cam = mpScene->GetActiveCamera();
	ECameraControllerType eController = cam.GetControllerType();
	int iController = static_cast<int>(eController);
	if (ImGui_RightAlignedCombo("Camera Controller", &iController, pszCamerControllerModes, _countof(pszCamerControllerModes)))
	{
		cam.SetControllerType(static_cast<ECameraControllerType>(iController));
	}

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

		static char SearchText[128];
		if (ImGui::InputText("Search", SearchText, 128))
		{
		}

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
		ImGui::Text(" Right Click : Operate Camera");
		ImGui::Text("  Left Click : Pick object");
		ImGui::Text("      Scroll : Adjust distance (Orbit Camera)");
		ImGui::Text("      WASDEQ : Move Camera (First Person Camera)");
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


void VQEngine::DrawProfilerWindow(const FSceneStats& FrameStats, float RenderRenderResolutionScale, float dt)
{
	const FSceneStats& s = FrameStats; // shorthand rename

	const uint32 W = mpWinMain->GetWidth();
	const uint32 H = mpWinMain->GetHeight();
	const uint32 RenderW = W * RenderRenderResolutionScale;
	const uint32 RenderH = H * RenderRenderResolutionScale;

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
		ImGui::TextColored(DataTextColor      , "Display Resolution : %ix%i", W, H);
		ImGui::TextColored(DataTextColor      , "Render Resolution  : %ix%i", RenderW, RenderH);
		ImGui::TextColored(SelectFPSColor(fps), "FPS                : %d (%.2f ms)", fps, frameTime_ms);
		
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
			const FRenderStats& rs = *s.pRenderStats;
			ImGui::TextColored(DataTextColor, "Lit Mesh         : %d", rs.NumLitMeshDrawCommands);
			ImGui::TextColored(DataTextColor, "Shadow Mesh      : %d", rs.NumShadowMeshDrawCommands);
			ImGui::TextColored(DataTextColor, "Bounding Box     : %d", rs.NumBoundingBoxDrawCommands);
			ImGui::TextColored(DataTextColor, "Total Draws      : %d", rs.NumDraws);
			ImGui::TextColored(DataTextColor, "Total Dispatches : %d", rs.NumDispatches);
		}
	}
	ImGui::End();
}

void VQEngine::DrawPostProcessSettings(FGraphicsSettings& GFXSettings)
{
	// constants
	const bool bFSREnabled = GFXSettings.IsFSR1Enabled();
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

	ImGui::Text("Upscaling");
	ImGui::Separator();
	if (ImGui_RightAlignedCombo("Algorithm", (int*) &GFXSettings.PostProcessing.UpscalingAlgorithm, szUpscalingLabels, _countof(szUpscalingLabels)))
	{
		switch (GFXSettings.PostProcessing.UpscalingAlgorithm)
		{
		case EUpscalingAlgorithm::NONE:
			GFXSettings.Rendering.RenderResolutionScale = 1.0f;
			break;
		case EUpscalingAlgorithm::FIDELITYFX_SUPER_RESOLUTION_1:
			GFXSettings.Rendering.RenderResolutionScale = AMD_FidelityFX_SuperResolution1::GetScreenPercentage(GFXSettings.PostProcessing.FSR1UpscalingQualityEnum);
			break;
		case EUpscalingAlgorithm::FIDELITYFX_SUPER_RESOLUTION_3:
			GFXSettings.Rendering.RenderResolutionScale = AMD_FidelityFX_SuperResolution3::GetScreenPercentage(GFXSettings.PostProcessing.FSR3UpscalingQualityEnum);
			break;
		}
		GFXSettings.Validate();
		mUIState.ResolutionScaleSliderValue = GFXSettings.Rendering.RenderResolutionScale;
		fnSendWindowResizeEvents();
	}

	if (GFXSettings.PostProcessing.UpscalingAlgorithm != EUpscalingAlgorithm::NONE)
	{
		// preset: ultra quality / quality / balanced / performance / custom
		int* pIndexQualityPreset = nullptr;
		const char** pszQualityLabels = nullptr;
		int NumQualities = 0;
		switch (GFXSettings.PostProcessing.UpscalingAlgorithm)
		{
		case EUpscalingAlgorithm::FIDELITYFX_SUPER_RESOLUTION_1:
			pIndexQualityPreset = (int*)&GFXSettings.PostProcessing.FSR1UpscalingQualityEnum;
			pszQualityLabels = szFSR1QualityLabels;
			NumQualities = AMD_FidelityFX_SuperResolution1::EPreset::NUM_FSR1_PRESET_OPTIONS;
			break;
		case EUpscalingAlgorithm::FIDELITYFX_SUPER_RESOLUTION_3:
			pIndexQualityPreset = (int*)&GFXSettings.PostProcessing.FSR3UpscalingQualityEnum;
			pszQualityLabels = szFSR3QualityLabels;
			NumQualities = AMD_FidelityFX_SuperResolution3::EPreset::NUM_FSR3_PRESET_OPTIONS;
			break;
		}

		bool bShouldUpdateGlobalMipBias = false;
		if (ImGui_RightAlignedCombo("Quality", pIndexQualityPreset, pszQualityLabels, NumQualities))
		{
			switch (GFXSettings.PostProcessing.UpscalingAlgorithm)
			{
			case EUpscalingAlgorithm::FIDELITYFX_SUPER_RESOLUTION_1:
				if (GFXSettings.PostProcessing.FSR1UpscalingQualityEnum != AMD_FidelityFX_SuperResolution1::EPreset::CUSTOM)
					GFXSettings.Rendering.RenderResolutionScale = AMD_FidelityFX_SuperResolution1::GetScreenPercentage(GFXSettings.PostProcessing.FSR1UpscalingQualityEnum);
				bShouldUpdateGlobalMipBias = true;
				break;
			case EUpscalingAlgorithm::FIDELITYFX_SUPER_RESOLUTION_3:
				if (GFXSettings.PostProcessing.FSR3UpscalingQualityEnum != AMD_FidelityFX_SuperResolution3::EPreset::CUSTOM)
					GFXSettings.Rendering.RenderResolutionScale = AMD_FidelityFX_SuperResolution3::GetScreenPercentage(GFXSettings.PostProcessing.FSR3UpscalingQualityEnum);				
				bShouldUpdateGlobalMipBias = true;
				break;
			}
			
			mUIState.ResolutionScaleSliderValue = GFXSettings.Rendering.RenderResolutionScale;
			fnSendWindowResizeEvents();
		}

		// resolution scale
		bool bDrawResolutionScaleSlider = false;
		switch (GFXSettings.PostProcessing.UpscalingAlgorithm)
		{
		case EUpscalingAlgorithm::FIDELITYFX_SUPER_RESOLUTION_1:
			bDrawResolutionScaleSlider = GFXSettings.PostProcessing.FSR1UpscalingQualityEnum == AMD_FidelityFX_SuperResolution1::EPreset::CUSTOM;
			break;
		case EUpscalingAlgorithm::FIDELITYFX_SUPER_RESOLUTION_3:
			bDrawResolutionScaleSlider = GFXSettings.PostProcessing.FSR3UpscalingQualityEnum == AMD_FidelityFX_SuperResolution3::EPreset::CUSTOM;
			break;
		}
		if (bDrawResolutionScaleSlider)
		{
			ImGui::SliderFloat("Resolution Scale", &mUIState.ResolutionScaleSliderValue, 0.25f, 1.00f, "%.2f");

			// to avoid updating resolution scale depending resources every tick,
			// we do it only after the user let the slider go.
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				GFXSettings.Rendering.RenderResolutionScale = mUIState.ResolutionScaleSliderValue;
				if (GFXSettings.IsFSR3Enabled() || GFXSettings.IsFSR1Enabled())
					bShouldUpdateGlobalMipBias = true; 
				fnSendWindowResizeEvents();
			}
		}

		if(bShouldUpdateGlobalMipBias)
			GFXSettings.Rendering.GlobalMipBias = AMD_FidelityFX_SuperResolution3::GetMipBias(GFXSettings.GetRenderResolutionX(), GFXSettings.Display.DisplayResolutionX);

		if (GFXSettings.IsFSR3Enabled())
		{
			FPostProcessingSettings::FFSR3Settings& s = GFXSettings.PostProcessing.FSR3Settings;

			ImGui::Checkbox("Generate Reactive Mask", &s.bGenerateReactivityMask);
			if (s.bGenerateReactivityMask)
			{
				ImGui::SliderFloat("Scale", &s.GeneratedReactiveMaskScale, 0.0f, 1.0f, "%.3f");
				ImGui::SliderFloat("Cutoff Threshold", &s.GeneratedReactiveMaskCutoffThreshold, 0.0f, 1.0f, "%.3f");
				ImGui::SliderFloat("Binary Value", &s.GeneratedReactiveMaskBinaryValue, 0.0f, 1.0f, "%.3f");
			}
		}
	}

	ImGuiSpacing3();

	ImGui::Text("Sharpness");
	ImGui::Separator();
	
	ImGui::SliderFloat("Amount##", &GFXSettings.PostProcessing.Sharpness, 0.01f, 1.00f, "%.2f");


	//
	// TONEMAPPER
	//
	ImGuiSpacing3();
	const bool bHDR = this->ShouldRenderHDR(mpWinMain->GetHWND());
	ImGui::Text((bHDR ? "Tonemapper (HDR)" : "Tonemapper"));
	ImGui::Separator();
	{
		if (bHDR)
		{
			const std::string strDispalyCurve = GetDisplayCurveString(GFXSettings.PostProcessing.HDROutputDisplayCurve);
			const std::string strColorSpace   = GetColorSpaceString(GFXSettings.PostProcessing.ContentColorSpace);
			ImGui::Text("OutputDevice : %s", strDispalyCurve.c_str() );
			ImGui::Text("Color Space  : %s", strColorSpace.c_str() );
			ImGui::SliderFloat("UI Brightness", &GFXSettings.PostProcessing.UIHDRBrightness, 0.1f, 20.f, "%.1f");
		}
		else
		{
			ImGui::Checkbox("[SDR] Apply Gamma (G)", &GFXSettings.PostProcessing.EnableGammaCorrection);
		}
	}

}

void VQEngine::DrawGraphicsSettingsWindow(FSceneRenderOptions& SceneRenderParams)
{
	const uint32 W = mpWinMain->GetWidth();
	const uint32 H = mpWinMain->GetHeight();
	HWND hwnd = mpWinMain->GetHWND();

	FGraphicsSettings& gfx = mSettings.gfx;

	InitializeStaticCStringData_GraphicsSettings();
	// static data

	int iSSAOLabel = SceneRenderParams.Lighting.bScreenSpaceAO ? 1 : 0;
	int iReflections = gfx.Rendering.Reflections;

	const uint32_t GFX_WINDOW_POS_X = W - GFX_WINDOW_SIZE_X - PROFILER_WINDOW_SIZE_X - PROFILER_WINDOW_PADDIG_X - GFX_WINDOW_PADDING_X;
	const uint32_t GFX_WINDOW_POS_Y = H - GFX_WINDOW_PADDING_Y*2 - GFX_WINDOW_SIZE_Y;
	ImGui::SetNextWindowPos(ImVec2((float)GFX_WINDOW_POS_X, (float)GFX_WINDOW_POS_Y), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(GFX_WINDOW_SIZE_X, GFX_WINDOW_SIZE_Y), ImGuiCond_FirstUseEver);
	

	ImGui::Begin("GRAPHICS SETTINGS", &mUIState.bWindowVisible_GraphicsSettingsPanel);
	
	ImGui::BeginTabBar("s*", ImGuiTabBarFlags_None);
	
	
	if (ImGui::BeginTabItem("Debug"))
	{
		InitializeStaticCStringData_EDrawMode();
		ImGui_RightAlignedCombo("Draw Mode", (int*)&gfx.DebugVizualization.DrawModeEnum, szDrawModes, _countof(szDrawModes));
		
		switch (gfx.DebugVizualization.DrawModeEnum)
		{
		case FDebugVisualizationSettings::EDrawMode::NORMALS:
			ImGui::Checkbox("Unpack Normals", &gfx.DebugVizualization.bUnpackNormals);
			break;
		case FDebugVisualizationSettings::EDrawMode::MOTION_VECTORS:
			ImGui::SliderFloat("MoVec Intensity", &gfx.DebugVizualization.fInputStrength, 0.0f, 200.0f);
			break;
		}

		ImGui::Checkbox("Show GameObject Bounding Boxes (Shift+N)", &SceneRenderParams.Debug.bDrawGameObjectBoundingBoxes);
		ImGui::Checkbox("Show Mesh Bounding Boxes (N)", &SceneRenderParams.Debug.bDrawMeshBoundingBoxes);
		ImGui::Checkbox("Show Light Bounding Volumes (L)", &SceneRenderParams.Debug.bDrawLightBounds);
		ImGui::Checkbox("Draw Lights", &SceneRenderParams.Debug.bDrawLightMeshes);
		ImGui::Checkbox("Draw Vertex Axes", &SceneRenderParams.Debug.bDrawVertexLocalAxes);
		if (SceneRenderParams.Debug.bDrawVertexLocalAxes)
		{
			ImGui::SliderFloat("Axis Size", &SceneRenderParams.Debug.fVertexLocalAxisSize, 1.0f, 10.0f);
		}

		//
		// MAGNIFIER
		//
		ImGuiSpacing3();
		ImGuiIO& io = ImGui::GetIO();

		ImGui::Text("Magnifier");
		ImGui::Separator();
		{
			FRenderDebugOptions::FMagnifierOptions& MagnifierOptions = SceneRenderParams.Debug.Magnifier;
			ImGui::Checkbox("Show Magnifier (Middle Mouse)", &MagnifierOptions.bEnable);

			BeginDisabledUIState(MagnifierOptions.bEnable);
			{
				// Use a local bool state here to track locked state through the UI widget,
				// and then call ToggleMagnifierLockedState() to update the persistent state (m_UIstate).
				// The keyboard input for toggling lock directly operates on the persistent state.
				const bool bIsMagnifierCurrentlyLocked = MagnifierOptions.bLockPosition;
				bool bMagnifierToggle = bIsMagnifierCurrentlyLocked;
				ImGui::Checkbox("Lock Position (Shift + Middle Mouse)", &bMagnifierToggle);

				if (bMagnifierToggle != bIsMagnifierCurrentlyLocked)
					MagnifierOptions.ToggleLock(io.MousePos.x, io.MousePos.y);

				
				ImGui::SliderFloat("Screen Size"  , &MagnifierOptions.fMagnifierScreenRadius, MAGNIFIER_RADIUS_MIN, MAGNIFIER_RADIUS_MAX);
				ImGui::SliderFloat("Magnification", &MagnifierOptions.fMagnificationAmount, MAGNIFICATION_AMOUNT_MIN, MAGNIFICATION_AMOUNT_MAX);
				if (bMagnifierToggle)
				{
					ImGui::SliderInt("OffsetX", &MagnifierOptions.ScreenOffsetY, -(int)W, W);
					ImGui::SliderInt("OffsetY", &MagnifierOptions.ScreenOffsetY, -(int)H, H);
				}
			}
			EndDisabledUIState(SceneRenderParams.Debug.Magnifier.bEnable);
		}
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Display"))
	{
		BeginDisabledUIState(!gfx.Display.bVsync);
		{
			static int iLimiter = mSettings.gfx.Rendering.MaxFrameRate == -1 ? 0 : (mSettings.gfx.Rendering.MaxFrameRate == 0 ? 1 : 2); // see Settings.h
			static int CustomFrameLimit = mSettings.gfx.Rendering.MaxFrameRate;
			if (ImGui_RightAlignedCombo("FrameRate Limit", &iLimiter, szMaxFrameRateOptionLabels, _countof(szMaxFrameRateOptionLabels)))
			{
				switch (iLimiter)
				{
				case 0: mSettings.gfx.Rendering.MaxFrameRate = -1; break;
				case 1: mSettings.gfx.Rendering.MaxFrameRate = 0; break;
				case 2: mSettings.gfx.Rendering.MaxFrameRate = CustomFrameLimit; break;
				default:
					break;
				}
				SetEffectiveFrameRateLimit(mSettings.gfx.Rendering.MaxFrameRate);
			}
			if (iLimiter == 2) // custom frame limit value
			{
				if (ImGui::SliderInt("MaxFrames", &CustomFrameLimit, 10, 1000))
				{
					mSettings.gfx.Rendering.MaxFrameRate = CustomFrameLimit;
					SetEffectiveFrameRateLimit(mSettings.gfx.Rendering.MaxFrameRate);
				}
			}
		}
		EndDisabledUIState(!gfx.Display.bVsync);

		if (ImGui::Checkbox("VSync (V)", &gfx.Display.bVsync))
		{
			mEventQueue_WinToVQE_Renderer.AddItem(std::make_shared<SetVSyncEvent>(hwnd, gfx.Display.bVsync));
		}
		bool bFS = mpWinMain->IsFullscreen();
		if (ImGui::Checkbox("Fullscreen (Alt+Enter)", &bFS))
		{
			mEventQueue_WinToVQE_Renderer.AddItem(std::make_shared<ToggleFullscreenEvent>(hwnd));
		}

		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Rendering"))
	{
		const bool bShouldEnableAntiAliasingOptions = !gfx.IsFSR3Enabled();
		BeginDisabledUIState(bShouldEnableAntiAliasingOptions);
		{
			if (ImGui_RightAlignedCombo("AntiAliasing (M)", (int*)&gfx.Rendering.AntiAliasing, szAALabels, _countof(szAALabels) - 1))
			{
				Log::Info("AA Changed: %d", gfx.Rendering.AntiAliasing);
			}
		}
		EndDisabledUIState(bShouldEnableAntiAliasingOptions);

		if (ImGui_RightAlignedCombo("Ambient Occlusion", &iSSAOLabel, szSSAOLabels, _countof(szSSAOLabels) - 1))
		{
			SceneRenderParams.Lighting.bScreenSpaceAO = iSSAOLabel == 1;
			Log::Info("AO Changed: %d", SceneRenderParams.Lighting.bScreenSpaceAO);
		}
		int iRefl = gfx.Rendering.Reflections;
		if (ImGui_RightAlignedCombo("Reflections", &iRefl, szReflectionsLabels, _countof(szReflectionsLabels)-1))
		{
			gfx.Rendering.Reflections = static_cast<EReflections>(iRefl);
			Log::Info("Reflections Changed: %d", gfx.Rendering.Reflections);
		}
		switch (gfx.Rendering.Reflections)
		{
		case EReflections::SCREEN_SPACE_REFLECTIONS__FFX:
		{
			FRenderingSettings::FFFX_SSSR_Options& FFXParams = mSettings.gfx.Rendering.FFX_SSSR_Options;

			ImGui::PushStyleColor(ImGuiCol_Header, UI_COLLAPSING_HEADER_COLOR_VALUE);
			if (ImGui::CollapsingHeader("SSSR Settings"))
			{
				ImGui::PopStyleColor();
				ImGui::SliderFloat("Roughness Threshold"        , &FFXParams.RoughnessThreshold, 0.0f, 1.f);
				ImGui::SliderInt("Max Traversal Iterations"     , &FFXParams.MaxTraversalIterations, 0, 256);
				ImGui::SliderInt("Min Traversal Occupancy"      , &FFXParams.MinTraversalOccupancy, 0, 32);
				ImGui::SliderInt("Most Detailed Level"          , &FFXParams.MostDetailedDepthHierarchyMipLevel, 0, 5);
				ImGui::SliderFloat("Depth Buffer Thickness"     , &FFXParams.DepthBufferThickness, 0.0f, 5.0f);
				ImGui::SliderFloat("Temporal Stability"         , &FFXParams.TemporalStability, 0.0f, 1.0f);
				ImGui::SliderFloat("Temporal Variance Threshold", &FFXParams.TemporalVarianceThreshold, 0.0f, 0.01f);
				ImGui::Checkbox("Enable Variance Guided Tracing", &FFXParams.bEnableTemporalVarianceGuidedTracing);
				ImGui::Text("Samples per Quad"); ImGui::SameLine();
				ImGui::RadioButton("1", &FFXParams.SamplesPerQuad, 1); ImGui::SameLine();
				ImGui::RadioButton("2", &FFXParams.SamplesPerQuad, 2); ImGui::SameLine();
				ImGui::RadioButton("4", &FFXParams.SamplesPerQuad, 4);
				ImGui::Separator();
			}
			else
			{
				ImGui::PopStyleColor();
			}
		}
		break;
		case EReflections::RAY_TRACED_REFLECTIONS:
			break;
		default:
			break;
		}

		ImGuiSpacing3();
		ImGui::Checkbox("Async Compute", &mSettings.gfx.bEnableAsyncCompute);
		ImGui::Checkbox("Async Copy", &mSettings.gfx.bEnableAsyncCopy);
		if (ImGui::Checkbox("Separate Submission Queue", &mSettings.gfx.bUseSeparateSubmissionQueue))
		{
			mEventQueue_WinToVQE_Renderer.AddItem(std::make_shared<SetSwapchainPresentationQueueEvent>(hwnd, mSettings.gfx.bUseSeparateSubmissionQueue));
		}

		ImGui::Checkbox("ForceLOD0 Shadow View", &SceneRenderParams.Debug.bForceLOD0_ShadowView);
		ImGui::Checkbox("ForceLOD0 Scene View", &SceneRenderParams.Debug.bForceLOD0_SceneView);

		const bool bShouldEnableGlobalMipBias = !gfx.IsFSR3Enabled();
		BeginDisabledUIState(bShouldEnableGlobalMipBias);
		{
			ImGui::SliderFloat("Global Mip Bias", &mSettings.gfx.Rendering.GlobalMipBias, -5.0f, 5.0f, "%.2f");
		}
		EndDisabledUIState(bShouldEnableGlobalMipBias);
		ImGui::EndTabItem();
	}


	if (ImGui::BeginTabItem("Post Processing"))
	{
		DrawPostProcessSettings(mSettings.gfx);
		ImGui::EndTabItem();
	}


	ImGui::EndTabBar();
	ImGui::End();
}

static void DrawTextureViewer(
	const char* szTextureName,
	const char* szTexturePath,
	ImTextureID ImTexID,
	const char* szTextureFormat,
	int TexSizeX,
	int TexSizeY,
	int TexMIPs
)
{
	const int largeTextureViewSize = 64 * 4;

	ImGui::BeginTooltip();

	ImGui::Text("%s", szTextureName);
	ImGui::Separator();

	if (ImGui::BeginTable("Texture View Table", 2))
	{
		ImGui::TableSetupColumn("Texture Large View", ImGuiTableColumnFlags_WidthFixed, largeTextureViewSize + 4);
		ImGui::TableSetupColumn("Texture Details", ImGuiTableColumnFlags_WidthStretch, 1.0f);

		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		ImGui::Image(ImTexID, ImVec2(largeTextureViewSize, largeTextureViewSize));
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("Format : %s", szTextureFormat);
		ImGui::Text("Size   : %dx%d", TexSizeX, TexSizeY);
		ImGui::Text("Mips   : %d", TexMIPs);
		//ImGui::Text("Samples: %d", TexSamples);
		ImGui::EndTable();
	}

	ImGui::Text("%s", szTexturePath);

	ImGui::EndTooltip();
}

static void StartDrawingMaterialEditorRow(const char* szLabel)
{
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Text(szLabel);
	ImGui::TableSetColumnIndex(1);
	ImGui::SetNextItemWidth(-1);
}


void VQEngine::DrawEditorWindow()
{
	const uint32 W = mpWinMain->GetWidth();
	const uint32 H = mpWinMain->GetHeight();
	const uint32_t EDITOR_WINDOW_POS_X = EDITOR_WINDOW_PADDING_X;
	const uint32_t EDITOR_WINDOW_POS_Y = H - EDITOR_WINDOW_PADDING_Y * 2 - EDITOR_WINDOW_SIZE_Y;
	
	ImGui::SetNextWindowPos(ImVec2((float)EDITOR_WINDOW_POS_X, (float)EDITOR_WINDOW_POS_Y), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(EDITOR_WINDOW_SIZE_X, EDITOR_WINDOW_SIZE_Y), ImGuiCond_FirstUseEver);

	ImGui::Begin("EDITOR", &mUIState.bWindowVisible_Editor);
	
	bool bEditMaterial = mUIState.EditorMode == FUIState::EEditorMode::MATERIALS;
	bool bEditLight    = mUIState.EditorMode == FUIState::EEditorMode::LIGHTS;
	bool bEditObject   = mUIState.EditorMode == FUIState::EEditorMode::OBJECTS;
	if (ImGui::RadioButton("Material", bEditMaterial))
	{
		mUIState.EditorMode = FUIState::EEditorMode::MATERIALS;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Light", bEditLight))
	{
		mUIState.EditorMode = FUIState::EEditorMode::LIGHTS;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Object", bEditObject))
	{
		mUIState.EditorMode = FUIState::EEditorMode::OBJECTS;
	}


	ImGui::Spacing();

	if (!ImGui::BeginTable("EditorTableLayout", 2, ImGuiTableFlags_Resizable, ImVec2(-1, -1)))
	{
		ImGui::End();
		return;
	}

	if (bEditMaterial) DrawMaterialEditor();
	if (bEditLight   ) DrawLightEditor();
	if (bEditObject  ) DrawObjectEditor();

	ImGui::EndTable();

	ImGui::End();
}

static void DrawTessellationEditorGUI(FUIState& mUIState, Material& mat)
{
	StartDrawingMaterialEditorRow("Tessellation");
	bool bEnableTessellation = mat.IsTessellationEnabled();
	if (ImGui::Checkbox("Enable##", &bEnableTessellation))
		mat.SetTessellationEnabled(bEnableTessellation);
	ImGui::SameLine();
	ImGui::Checkbox("Wireframe", &mat.bWireframe);
	if (bEnableTessellation)
	{
		const char* pszTessellationModeNames[] = {
			"Triangle",
			"Quad",
			// "Line"
			""
		};

		ETessellationDomain Domain = mat.GetTessellationDomain();
		if (ImGui::BeginCombo("Domain", pszTessellationModeNames[(size_t)Domain]))
		{
			if (ImGui::Selectable(pszTessellationModeNames[0], Domain == ETessellationDomain::TRIANGLE_PATCH)) { mat.SetTessellationDomain( ETessellationDomain::TRIANGLE_PATCH ); }
			if (ImGui::Selectable(pszTessellationModeNames[1], Domain == ETessellationDomain::QUAD_PATCH)) { mat.SetTessellationDomain( ETessellationDomain::QUAD_PATCH ); }
			ImGui::EndCombo();
		}

		Domain = mat.GetTessellationDomain();
		constexpr float MAX_TESSELLATION = Tessellation::MAX_TESSELLATION_FACTOR;
		switch (Domain)
		{
		case::ETessellationDomain::TRIANGLE_PATCH:
		{
			if (mUIState.bTessellationSliderFloatVec)
			{
				ImGui::SliderFloat3("Outer", &mat.TessellationData.EdgeTessFactor.x, 0.0f, MAX_TESSELLATION, "%.1f");
			}
			else
			{
				float fOuterMin = std::fminf(mat.TessellationData.EdgeTessFactor.x, std::fminf(mat.TessellationData.EdgeTessFactor.y, mat.TessellationData.EdgeTessFactor.z));
				if (ImGui::SliderFloat("Outer##", &fOuterMin, 0.0f, MAX_TESSELLATION, "%.1f"))
				{
					mat.TessellationData.EdgeTessFactor.x = mat.TessellationData.EdgeTessFactor.y = mat.TessellationData.EdgeTessFactor.z = fOuterMin;
					if (mUIState.bLockTessellationSliders)
					{
						mat.TessellationData.InsideTessFactor.x = fOuterMin;
					}
				}
			}
			ImGui::SameLine();

			ImGui::Checkbox("[]", &mUIState.bTessellationSliderFloatVec);
			if (ImGui::SliderFloat("Inner", &mat.TessellationData.InsideTessFactor.x, 0.0f, MAX_TESSELLATION, "%.1f"))
			{
				if (mUIState.bLockTessellationSliders)
				{
					mat.TessellationData.SetAllTessellationFactors(mat.TessellationData.InsideTessFactor.x);
				}
			}
		}	break;
		case::ETessellationDomain::QUAD_PATCH:
		{
			if (mUIState.bTessellationSliderFloatVec)
			{
				ImGui::SliderFloat4("Outer", &mat.TessellationData.EdgeTessFactor.x, 0.0f, MAX_TESSELLATION, "%.1f");
				ImGui::SameLine();
				ImGui::Checkbox("[]", &mUIState.bTessellationSliderFloatVec);
				ImGui::SliderFloat2("Inner", &mat.TessellationData.InsideTessFactor.x, 0.0f, MAX_TESSELLATION, "%.1f");
			}
			else
			{
				float fOuterMin = std::fminf(mat.TessellationData.EdgeTessFactor.x, std::fminf(mat.TessellationData.EdgeTessFactor.y, std::fminf(mat.TessellationData.EdgeTessFactor.z, mat.TessellationData.EdgeTessFactor.w)));
				if (ImGui::SliderFloat("Outer", &fOuterMin, 0.0f, MAX_TESSELLATION, "%.1f"))
				{
					mat.TessellationData.EdgeTessFactor.x = mat.TessellationData.EdgeTessFactor.y = mat.TessellationData.EdgeTessFactor.z = mat.TessellationData.EdgeTessFactor.w = fOuterMin;
					if (mUIState.bLockTessellationSliders)
					{
						mat.TessellationData.SetAllTessellationFactors(fOuterMin);
					}
				}
				ImGui::SameLine();
				ImGui::Checkbox("[]", &mUIState.bTessellationSliderFloatVec);

				float fInnerMin = std::fminf(mat.TessellationData.InsideTessFactor.x, mat.TessellationData.InsideTessFactor.y);
				if (ImGui::SliderFloat("Inner", &fInnerMin, 0.0f, MAX_TESSELLATION, "%.1f"))
				{
					mat.TessellationData.InsideTessFactor.x = mat.TessellationData.InsideTessFactor.y = fInnerMin;
					if (mUIState.bLockTessellationSliders)
					{
						mat.TessellationData.SetAllTessellationFactors(fInnerMin);
					}
				}
			}
		}	break;
		case::ETessellationDomain::ISOLINE_PATCH:
		{

		} break;
		}
		ImGui::SameLine();
		if (ImGui::Checkbox("Sync", &mUIState.bLockTessellationSliders))
		{
			// TODO
		}

		const char* pszPartitioningNames[] =
		{
			"Integer",
			"Fractional Even",
			"Fractional Odd",
			"Pow2",
			""
		};

		ETessellationPartitioning partitioning = mat.GetTessellationPartitioning();
		if (ImGui::BeginCombo("Partitioning", pszPartitioningNames[(size_t)partitioning]))
		{
			if (ImGui::Selectable(pszPartitioningNames[0], partitioning == ETessellationPartitioning::INTEGER))        { mat.SetTessellationPartitioning(ETessellationPartitioning::INTEGER); }
			if (ImGui::Selectable(pszPartitioningNames[1], partitioning == ETessellationPartitioning::FRACTIONAL_EVEN)){ mat.SetTessellationPartitioning(ETessellationPartitioning::FRACTIONAL_EVEN); }
			if (ImGui::Selectable(pszPartitioningNames[2], partitioning == ETessellationPartitioning::FRACTIONAL_ODD)) { mat.SetTessellationPartitioning(ETessellationPartitioning::FRACTIONAL_ODD); }
			if (ImGui::Selectable(pszPartitioningNames[3], partitioning == ETessellationPartitioning::POWER_OF_TWO))   { mat.SetTessellationPartitioning(ETessellationPartitioning::POWER_OF_TWO); }
			ImGui::EndCombo();
		}

		// topo
		const char* pszOutputTopologyNames[] = {
			"Point",
			"Line (not implemented)",
			"Triangle CW",
			"Triangle CCW",
			""
		};
		const bool bLineDomain = mat.GetTessellationDomain() == ETessellationDomain::ISOLINE_PATCH;
		ETessellationOutputTopology topology = mat.GetTessellationOutputTopology();
		if (ImGui::BeginCombo("Output Topology", pszOutputTopologyNames[(size_t)topology]))
		{
			if (ImGui::Selectable(pszOutputTopologyNames[0], topology == ETessellationOutputTopology::TESSELLATION_OUTPUT_POINT)) { mat.SetTessellationOutputTopology(ETessellationOutputTopology::TESSELLATION_OUTPUT_POINT); }
			BeginDisabledUIState(bLineDomain);
			if (ImGui::Selectable(pszOutputTopologyNames[1], topology == ETessellationOutputTopology::TESSELLATION_OUTPUT_LINE)) { mat.SetTessellationOutputTopology(ETessellationOutputTopology::TESSELLATION_OUTPUT_LINE); }
			EndDisabledUIState(bLineDomain);
			if (ImGui::Selectable(pszOutputTopologyNames[2], topology == ETessellationOutputTopology::TESSELLATION_OUTPUT_TRIANGLE_CW)) { mat.SetTessellationOutputTopology(ETessellationOutputTopology::TESSELLATION_OUTPUT_TRIANGLE_CW); }
			if (ImGui::Selectable(pszOutputTopologyNames[3], topology == ETessellationOutputTopology::TESSELLATION_OUTPUT_TRIANGLE_CCW)) { mat.SetTessellationOutputTopology(ETessellationOutputTopology::TESSELLATION_OUTPUT_TRIANGLE_CCW); }
			ImGui::EndCombo();
		}
		
		topology = mat.GetTessellationOutputTopology();
		const bool bShouldDisableCullingOptionsForTessellation = topology == ETessellationOutputTopology::TESSELLATION_OUTPUT_POINT || topology == ETessellationOutputTopology::TESSELLATION_OUTPUT_LINE;
		if (bShouldDisableCullingOptionsForTessellation)
		{
			mat.TessellationData.SetFaceCulling(false);
			mat.TessellationData.SetFrustumCulling(false);
		}

		// cull toggles
		bool bFrustumCull = mat.TessellationData.IsFaceCullingOn();
		BeginDisabledUIState(!bShouldDisableCullingOptionsForTessellation);
		if (ImGui::Checkbox("Frustum Cull", &bFrustumCull)) { mat.TessellationData.SetFrustumCulling(bFrustumCull); }
		ImGui::SameLine();
		bool bBackFaceCull = mat.TessellationData.IsFaceCullingOn();
		if (ImGui::Checkbox("BackFace Cull", &bBackFaceCull)) { mat.TessellationData.SetFaceCulling(bBackFaceCull); }
		EndDisabledUIState(!bShouldDisableCullingOptionsForTessellation);

		// cull thresholdes
		float fCullFrustumAndBackfaceThresholds[2] = { mat.TessellationData.fHSFrustumCullEpsilon, mat.TessellationData.fHSFaceCullEpsilon };
		if (bBackFaceCull && bFrustumCull)
		{
			if (ImGui::InputFloat2("Thresholds (Frustum, BackFace)", fCullFrustumAndBackfaceThresholds))
			{
				mat.TessellationData.fHSFrustumCullEpsilon = fCullFrustumAndBackfaceThresholds[0];
				mat.TessellationData.fHSFaceCullEpsilon = fCullFrustumAndBackfaceThresholds[1];
			}
		}
		else if (bBackFaceCull)
		{
			ImGui::InputFloat("Threshold", &mat.TessellationData.fHSFaceCullEpsilon);
		}
		else if (bFrustumCull)
		{
			ImGui::InputFloat("Threshold", &mat.TessellationData.fHSFrustumCullEpsilon);
		}

		bool bAdaptiveTess = mat.TessellationData.IsAdaptiveTessellationOn();
		if (ImGui::Checkbox("Adaptive", &bAdaptiveTess)) { mat.TessellationData.SetAdaptiveTessellation(bAdaptiveTess); }
		if (bAdaptiveTess)
		{
			float fMinMaxDistanceThresholds[2] = { mat.TessellationData.fHSAdaptiveTessellationMinDist, mat.TessellationData.fHSAdaptiveTessellationMaxDist };
			if (ImGui::InputFloat2("Min/Max Distance", fMinMaxDistanceThresholds))
			{
				mat.TessellationData.fHSAdaptiveTessellationMinDist = fMinMaxDistanceThresholds[0];
				mat.TessellationData.fHSAdaptiveTessellationMaxDist = fMinMaxDistanceThresholds[1];
			}
		}
	}
}

void VQEngine::DrawMaterialEditor()
{
	// gather material data
	std::vector<MaterialID> matIDsAll = mpScene->GetMaterialIDs();
	
	// filter materials by selected objects
	std::vector<MaterialID> filteredMatIDs;
	for(MaterialID matID : matIDsAll)
	for (size_t hObj : mpScene->mSelectedObjects)
	{
		GameObject* pObj = mpScene->GetGameObject(hObj);
		if (!pObj)
			continue;
		const Model& m = mpScene->GetModel(pObj->mModelID);
		const std::unordered_set<MaterialID>& materialSet = m.mData.GetMaterials();
		if (materialSet.find(matID) != materialSet.end())
		{
			filteredMatIDs.push_back(matID);
		}
	}

	// if no objects are selected, list all the materials
	if (filteredMatIDs.empty())
	{
		filteredMatIDs = std::move(matIDsAll);
	}
	
	std::vector<const char*> szMaterialNames(filteredMatIDs.size() + 1, nullptr);
	std::vector<MaterialID> MaterialIDs(filteredMatIDs.size() + 1, INVALID_ID);
	{
		int iMatName = 0;
		for (MaterialID matID : filteredMatIDs)
		{
			const std::string& matName = mpScene->GetMaterialName(matID);
			szMaterialNames[iMatName] = matName.c_str();
			MaterialIDs[iMatName++] = matID;
		}
		szMaterialNames[iMatName] = "";
	}

	int& i = mUIState.SelectedEditeeIndex[FUIState::EEditorMode::MATERIALS];
	if (i == INVALID_ID) // if nothing is selected, default-select the first material
	{
		i = 0;
	}

	// gui layout
	ImGui::TableSetupColumn(mpScene->GetMaterialName(MaterialIDs[i]).c_str(), ImGuiTableColumnFlags_WidthStretch, 0.7f); // 70% width
	ImGui::TableSetupColumn("Materials", ImGuiTableColumnFlags_WidthStretch, 0.3f); // 30% width
	ImGui::TableHeadersRow();
	ImGui::TableNextRow();
	

	// draw selector
	ImGui::TableSetColumnIndex(1);
	ImGui::SetNextItemWidth(-1); // Make the controls take the full width of the column
	ImGui::ListBox("##", &i, szMaterialNames.data(), (int)szMaterialNames.size(), 18);
	if (ImGui::Button("Unselect##", ImVec2(-1, 0)))
	{
		i = (int)(szMaterialNames.size() - 1);
	}


	// draw editor
	ImGui::TableSetColumnIndex(0);

	ImGui::Text("ID : %d", MaterialIDs[i]);
	ImGuiSpacing(2);
	if (i == szMaterialNames.size() - 1 || i == INVALID_ID)
	{
		return;
	}

	Material& mat = mpScene->GetMaterial(MaterialIDs[i]);
	if (!ImGui::BeginTable("MaterialEditorTable", 2, ImGuiTableFlags_Resizable, ImVec2(0, -1)))
	{
		return;
	}

	ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
	ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);

	// Diffuse Color & Alpha
	DirectX::XMFLOAT4 ColorAlpha(mat.diffuse.x, mat.diffuse.y, mat.diffuse.z, mat.alpha);
	StartDrawingMaterialEditorRow("Diffuse, Alpha");
	if (ImGui::ColorEdit4("##diffuse", reinterpret_cast<float*>(&ColorAlpha), ImGuiColorEditFlags_DefaultOptions_ | ImGuiColorEditFlags_NoLabel))
	{
		mat.diffuse = DirectX::XMFLOAT3(ColorAlpha.x, ColorAlpha.y, ColorAlpha.z);
		mat.alpha = ColorAlpha.w;
	}

	StartDrawingMaterialEditorRow("Emmissive Color");
	ImGui::ColorEdit3("##emissiveColor", reinterpret_cast<float*>(&mat.emissiveColor), ImGuiColorEditFlags_DefaultOptions_ | ImGuiColorEditFlags_NoLabel);

	StartDrawingMaterialEditorRow("Emmissive Intensity");
	ImGui::DragFloat("##emissiveIntensity", &mat.emissiveIntensity, 0.01f, 0.0f, 1000.0f, "%.2f");

	StartDrawingMaterialEditorRow("Metalness");
	ImGui::DragFloat("##metalness", &mat.metalness, 0.01f, 0.00f, 1.0f, "%.2f");
			
	StartDrawingMaterialEditorRow("Roughness");
	ImGui::DragFloat("##roughness", &mat.roughness, 0.01f, 0.04f, 1.0f, "%.2f");

	StartDrawingMaterialEditorRow("Mip Bias (Normals)");
	ImGui::DragFloat("##mip_bias_normal", &mat.normalMapMipBias, 0.01f, -5.0f, 5.0f, "%.2f");

	StartDrawingMaterialEditorRow("Mip Bias");
	const float fullWidth = ImGui::GetContentRegionAvail().x;
	const float checkboxLabelWidth = ImGui::CalcTextSize("Override ").x;
	const float spacing = ImGui::GetStyle().ItemInnerSpacing.x; // spacing between items
	const float checkboxWidth = ImGui::GetFrameHeight(); // checkbox square width
	const float widthForDrag = ImMax(fullWidth - checkboxLabelWidth - spacing - checkboxWidth - spacing, 50.0f);
	ImGui::SetNextItemWidth(widthForDrag);
	if (mat.bOverrideGlobalMipBias)
	{
		ImGui::DragFloat("##mip_bias", &mat.mipMapBias, 0.01f, -5.0f, 5.0f, "%.2f");
	}
	else
	{
		ImGui::LabelText("##global_mip_bias_value", "%.2f", mSettings.gfx.Rendering.GlobalMipBias);
	}
	ImGui::SameLine();
	ImGui::Checkbox("Override##", &mat.bOverrideGlobalMipBias);

	StartDrawingMaterialEditorRow("Tiling");
	ImGui::DragFloat2("##tiling", reinterpret_cast<float*>(&mat.tiling), 0.01f, 0.0f, 10.0f, "%.2f");
			
	StartDrawingMaterialEditorRow("UV Bias");
	ImGui::DragFloat2("##uv_bias", reinterpret_cast<float*>(&mat.uv_bias), 0.01f, -10.0f, 10.0f, "%.2f");

	StartDrawingMaterialEditorRow("Displacement");
	ImGui::InputFloat("##displecement", &mat.displacement, 0.05f, 5.0f, "%.2f");

	ImGuiSpacing(3);

	DrawTessellationEditorGUI(mUIState, mat);

	ImGuiSpacing(6);

	// Texture
	static const char* textureLabels[] = 
	{
		"Diffuse Map",  //EMaterialTextureMapBindings::ALBEDO
		"Normal Map",  //EMaterialTextureMapBindings::NORMALS
		"Emissive Map",  //EMaterialTextureMapBindings::EMISSIVE
		"Alpha Mask Map", //EMaterialTextureMapBindings::ALPHA_MASK
		"Metallic Map",  //EMaterialTextureMapBindings::METALLIC
		"Roughness Map",  //EMaterialTextureMapBindings::ROUGHNESS
		"Occlusion Roughness Metalness Map",  //EMaterialTextureMapBindings::OCCLUSION_ROUGHNESS_METALNESS
		"Ambient Occlusion Map", //EMaterialTextureMapBindings::AMBIENT_OCCLUSION
		"Height Map",
		""
	};
	const int textureIDs[] = 
	{
		mat.TexDiffuseMap,
		mat.TexNormalMap,
		mat.TexEmissiveMap,
		mat.TexAlphaMaskMap,
		mat.TexMetallicMap,
		mat.TexRoughnessMap,
		mat.TexOcclusionRoughnessMetalnessMap,
		mat.TexAmbientOcclusionMap,
		mat.TexHeightMap,
		INVALID_ID
	};

	const int NumTextures = _countof(textureLabels);
	for (int i = 0; i < NumTextures; ++i)
	{
		if (textureIDs[i] == INVALID_ID)
			continue;

		const std::string_view& textureFormat = mpRenderer->DXGIFormatAsString(mpRenderer->GetTextureFormat(textureIDs[i]));
		const std::string& texturePath = mpScene->GetTexturePath(textureIDs[i]);
		const std::string textureName = mpScene->GetTextureName(textureIDs[i]);
		int textureSizeX, textureSizeY;
		mpRenderer->GetTextureDimensions(textureIDs[i], textureSizeX, textureSizeY);
		int textureMIPs = mpRenderer->GetTextureMips(textureIDs[i]);

		const bool bIsHeightMap = i == NumTextures - 1;
		const CBV_SRV_UAV& srv = bIsHeightMap
			? mpRenderer->GetShaderResourceView(mat.SRVHeightMap)
			: mpRenderer->GetShaderResourceView(mat.SRVMaterialMaps);

		ImTextureID ImTexID = (ImTextureID)(bIsHeightMap 
			? srv.GetGPUDescHandle().ptr
			: srv.GetGPUDescHandle(i).ptr
		);

		const int texturePreviewSize = 64;

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("%s", textureLabels[i]);

		ImGui::TableSetColumnIndex(1);
		ImGui::Image(ImTexID, ImVec2(texturePreviewSize, texturePreviewSize));
		if (ImGui::IsItemHovered())
		{
			DrawTextureViewer(
				textureName.c_str(), 
				texturePath.c_str(), 
				ImTexID, 
				textureFormat.data(), 
				textureSizeX, 
				textureSizeY, 
				textureMIPs
			);
		}
	}

	ImGui::EndTable();
}

const char* LightTypeToString(Light::EType type) 
{
	switch (type) 
	{
	case Light::EType::DIRECTIONAL: return "Directional";
	case Light::EType::SPOT: return "Spot";
	case Light::EType::POINT: return "Point";
	default: return "Unknown";
	}
}
void VQEngine::DrawLightEditor()
{
	const std::vector<Light*> Lights = mpScene->GetLights();

	// build light names & count lights
	std::array<int, Light::EType::LIGHT_TYPE_COUNT> NumShadowCastingLight;
	std::array<int, Light::EType::LIGHT_TYPE_COUNT> NumLightsPerType;
	auto fnBuildLightNamesAndCountLights = [this](const std::vector<Light*>& Lights, 
		std::array<int, Light::EType::LIGHT_TYPE_COUNT>& NumShadowCastingLight,
		std::array<int, Light::EType::LIGHT_TYPE_COUNT>& NumLightsPerType
	) -> std::vector<std::string>
	{
		// reset counts
		for (int& i : NumShadowCastingLight) i = 0;
		for (int& i : NumLightsPerType) i = 0;

		std::vector<std::string> LightNames;
		for (Light* l : Lights)
		{
			const std::string lightCount = std::to_string(NumLightsPerType[l->Type]++);
			NumShadowCastingLight[l->Type] += l->bCastingShadows ? 1 : 0;
			
			std::string LightName;
			switch (l->Type)
			{
				// this naming scheme isnt great: changing light type renames them...
				case Light::EType::POINT       : LightName += "Point #"; break;
				case Light::EType::SPOT        : LightName += "Spot #" ; break;
				case Light::EType::DIRECTIONAL : LightName += "Directional #"; break;
				default: Log::Error("DrawLightEditor(): undefined light type"); break;
			}
			LightName += lightCount;
			LightNames.push_back(LightName);
		}
		LightNames.push_back("");
		return LightNames;
	};
	std::vector<std::string> LightNames = fnBuildLightNamesAndCountLights(Lights, NumShadowCastingLight, NumLightsPerType);


	// validate selected light index
	int& i = mUIState.SelectedEditeeIndex[FUIState::EEditorMode::LIGHTS];
	auto fnCalculateSelectedLightIndex = [](int& i, const std::vector<std::string>& LightNames)
	{
		if (i >= LightNames.size()-1)
		{
			Log::Warning("SelectedLightIndex > ObjNames.size() : capping SelectedLightIndex ");
			i = (int)(LightNames.size() - 2);
		}
		if (i < 0)
		{
			// Log::Warning("SelectedLightIndex negative : Setting to 0");
			// i = 0;
		}
	};
	fnCalculateSelectedLightIndex(i, LightNames);
	
	// get light data from selected index
	const std::string& SelectedLightName = i >= 0 ?  LightNames[i] : LightNames.back();
	Light* l = Lights.empty() ? nullptr : Lights[i];

	// gui layout
	ImGui::TableSetupColumn(SelectedLightName.c_str(), ImGuiTableColumnFlags_WidthStretch, 0.7f); // 70% width
	ImGui::TableSetupColumn("Lights", ImGuiTableColumnFlags_WidthStretch, 0.3f); // 30% width
	ImGui::TableHeadersRow();
	ImGui::TableNextRow();

	// right side: list & selection
	ImGui::TableSetColumnIndex(1);
	ImGui::SetNextItemWidth(-1); // Make the controls take the full width of the column
	if (ImGui::Button("+##0", ImVec2(20, 0)))
	{
		if(l) mpScene->CreateLight(l->Type, l->Mobility);
		else  mpScene->CreateLight();
	}
	ImGui::SameLine();

	bool bEnableRemoveButton = !Lights.empty();
	BeginDisabledUIState(bEnableRemoveButton);
	if (ImGui::Button("-##0", ImVec2(20, 0)))
	{
		mpScene->RemoveLight(l);
		LightNames = fnBuildLightNamesAndCountLights(Lights, NumShadowCastingLight, NumLightsPerType);
		fnCalculateSelectedLightIndex(i, LightNames);
		l = Lights[i];
	}
	EndDisabledUIState(bEnableRemoveButton);

	ImGui::SameLine();

	// get pointers to light name data 
	std::vector<const char*> szLightNames(LightNames.size());
	for (int i = 0; i < LightNames.size(); ++i) szLightNames[i] = LightNames[i].c_str();
	if (ImGui::Button("Unselect##", ImVec2(-1, 0)))
	{
		i = (int)(szLightNames.size() - 1);
	}
	ImGui::SetNextItemWidth(-1); // Make the controls take the full width of the column
	ImGui::ListBox("##", &i, szLightNames.data(), (int)szLightNames.size()-1, 18);

	if (!l)
		return;

	// draw editor
	ImGui::TableSetColumnIndex(0);

	const bool bValidLight = i != szLightNames.size() - 1 && i != INVALID_ID;
	if (!bValidLight)
	{
		return;
	}

	if (ImGui::Checkbox("Enabled", &l->bEnabled))
	{
		// directional light is disabled (in shader) by setting brightness to 0.
		// it doesn't have a boolean to track its enabled state, hence reset its value 
		// to a fixed value here to toggle enable/disable.
		if (l->Type == Light::EType::DIRECTIONAL)
		{
			l->Brightness = l->bEnabled ? 5.0f : 0.0f;
		}
	}
	ImGui::SameLine();
	ImGui::Checkbox("Show Volume", &mUIState.bDrawLightVolume);
	ImGuiSpacing(2);

	ImGui::ColorEdit3("Color", reinterpret_cast<float*>(&l->Color));
	ImGui::DragFloat("Brightness", &l->Brightness, 0.1f, 0.0f, 10000.0f, "%.1f");
	if (l->Type != Light::EType::DIRECTIONAL) {
		ImGui::DragFloat("Range", &l->Range, 0.5f, 0.0f, 10000.0f, "%.1f");
	}

	ImGuiSpacing(2);

	// Light type
	assert(NumLightsPerType[Light::EType::DIRECTIONAL] >= 0 && NumLightsPerType[Light::EType::DIRECTIONAL] <= 1);
	const bool bEnableSpotLightDropdown        = !l->bCastingShadows || (NumShadowCastingLight[Light::EType::SPOT  ] < NUM_SHADOWING_LIGHTS__SPOT        && l->Type != Light::EType::SPOT);
	const bool bEnablePointLightDropdown       = !l->bCastingShadows || (NumShadowCastingLight[Light::EType::POINT ] < NUM_SHADOWING_LIGHTS__POINT       && l->Type != Light::EType::POINT);
	const bool bEnableDirectionalLightDropdown = !l->bCastingShadows || (NumLightsPerType[Light::EType::DIRECTIONAL] < NUM_SHADOWING_LIGHTS__DIRECTIONAL && l->Type != Light::EType::DIRECTIONAL); // only 1 supported
	if (ImGui::BeginCombo("Type", LightTypeToString(l->Type))) 
	{
		BeginDisabledUIState(bEnableDirectionalLightDropdown);
		if (ImGui::Selectable("Directional", l->Type == Light::EType::DIRECTIONAL)) { l->Type = Light::EType::DIRECTIONAL; }
		EndDisabledUIState(bEnableDirectionalLightDropdown);

		BeginDisabledUIState(bEnableSpotLightDropdown);
		if (ImGui::Selectable("Spot", l->Type == Light::EType::SPOT)) { l->Type = Light::EType::SPOT; }
		EndDisabledUIState(bEnableSpotLightDropdown);

		BeginDisabledUIState(bEnablePointLightDropdown);
		if (ImGui::Selectable("Point", l->Type == Light::EType::POINT)) { l->Type = Light::EType::POINT; }
		EndDisabledUIState(bEnablePointLightDropdown);
		ImGui::EndCombo();
	}

	// Light type specific properties
	switch (l->Type) 
	{
	case Light::EType::DIRECTIONAL:
		ImGui::DragInt("Viewport X", &l->ViewportX, 1, 0, 2048);
		ImGui::DragInt("Viewport Y", &l->ViewportY, 1, 0, 2048);
		ImGui::DragFloat("Distance From Origin", &l->DistanceFromOrigin, 0.5f, 0.0f, 10000.0f, "%.1f");
		break;
	case Light::EType::SPOT:
		ImGui::DragFloat("Outer Cone Angle", &l->SpotOuterConeAngleDegrees, 0.1f, 0.0f, 180.0f, "%.1f deg");
		ImGui::DragFloat("Inner Cone Angle", &l->SpotInnerConeAngleDegrees, 0.1f, 0.0f, 180.0f, "%.1f deg");
		break;
	case Light::EType::POINT:
		// Point lights have no additional specific properties in this example
		break;
	}

	ImGuiSpacing(2);

	ImGui::Text("Transform");
	if (l->Type != Light::EType::DIRECTIONAL) {
		ImGui::DragFloat3("Position", reinterpret_cast<float*>(&l->Position), 0.1f);
	}
	if (l->Type != Light::EType::POINT) {
		DirectX::XMFLOAT3 eulerRotation = Quaternion::ToEulerDeg(l->RotationQuaternion);
		#if 0 // TODO: fix Euler->Quaternion code
		if (ImGui::DragFloat3("Rotation", reinterpret_cast<float*>(&eulerRotation), 0.1f)) {
			l->RotationQuaternion = Quaternion::FromEulerDeg(eulerRotation); 
		}
		#endif
	}

	ImGuiSpacing(2);

	ImGui::Checkbox("Cast Shadows", &l->bCastingShadows);
	if (l->bCastingShadows) {
		ImGui::DragFloat("Depth Bias", &l->ShadowData.DepthBias, 0.0001f, 0.0f, 1.0f, "%.4f");
		ImGui::DragFloat("Near Plane", &l->ShadowData.NearPlane, 0.1f, 0.1f, 100.0f, "%.1f");
		ImGui::DragFloat("Far Plane", &l->ShadowData.FarPlane  , 1.0f, 1.0f, 10000.0f, "%.1f");
		if (l->ShadowData.FarPlane == 0.0f)
			l->ShadowData.FarPlane = 1.0f;
		if (l->ShadowData.NearPlane - l->ShadowData.FarPlane >= 0.0f)
			l->ShadowData.FarPlane = l->ShadowData.NearPlane + 0.001f;
	}
}

void VQEngine::DrawObjectEditor()
{
	// build obj names
	const std::vector<size_t>& GameObjectHs = mpScene->mGameObjectHandles;
	std::vector<std::string> ObjNames;
	for (size_t hObj : GameObjectHs)
	{
		const GameObject* pObj = mpScene->GetGameObject(hObj);
		const Model& m = mpScene->GetModel(pObj->mModelID);
		ObjNames.push_back(m.mModelName);
	}
	ObjNames.push_back("");

	// validate selected index
	int& i = mUIState.SelectedEditeeIndex[FUIState::EEditorMode::OBJECTS];
	if (i >= ObjNames.size())
	{
		Log::Warning("SelectedObjectIndex > ObjNames.size() : capping SelectedObjectIndex ");
		i = (int)(ObjNames.size() - 1);
	}
	if (i < 0)
	{
		// Log::Warning("SelectedLightIndex negative : Setting to 0");
		// i = 0;
	}

	// get pointers to light name data 
	const std::string& SelectedObjName = i >= 0 ? ObjNames[i] : ObjNames.back();
	std::vector<const char*> szObjNames(ObjNames.size());
	for (int i = 0; i < ObjNames.size(); ++i)
		szObjNames[i] = ObjNames[i].c_str();


	// gui layout
	ImGui::TableSetupColumn(SelectedObjName.c_str(), ImGuiTableColumnFlags_WidthStretch, 0.7f); // 70% width
	ImGui::TableSetupColumn("Objects", ImGuiTableColumnFlags_WidthStretch, 0.3f); // 30% width
	ImGui::TableHeadersRow();
	ImGui::TableNextRow();

	// draw selector
	ImGui::TableSetColumnIndex(1);
	ImGui::SetNextItemWidth(-1); // Make the controls take the full width of the column
	ImGui::ListBox("##", &i, szObjNames.data(), (int)szObjNames.size(), 18);
	if (ImGui::Button("Unselect##", ImVec2(-1, 0)))
	{
		i = (int)(szObjNames.size() - 1);
	}


	// draw editor
	ImGui::TableSetColumnIndex(0);

	const bool bValidLight = i != szObjNames.size() - 1 && i != INVALID_ID;
	if (!bValidLight)
	{
		return;
	}

	const size_t hObj = mpScene->mGameObjectHandles[i];
	Transform* pTF = mpScene->GetGameObjectTransform(hObj);

	ImGuiSpacing(2);

	ImGui::Text("Transform");
	ImGui::InputFloat3("Position", reinterpret_cast<float*>(&pTF->_position));
	float3 eulerRotation = Quaternion::ToEulerDeg(pTF->_rotation);
	if (ImGui::InputFloat3("Rotation", reinterpret_cast<float*>(&eulerRotation))) {
		pTF->_rotation = Quaternion::FromEulerDeg(eulerRotation);
	}
	ImGui::InputFloat3("Scale", reinterpret_cast<float*>(&pTF->_scale));


	ImGuiSpacing(2);

	ImGui::Text("Model");
	
}
