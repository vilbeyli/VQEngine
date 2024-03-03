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
#include "../VQEngine.h"
#include "../GPUMarker.h"

#include "../RenderPass/MagnifierPass.h"

#include "VQUtils/Source/utils.h"

#include "../Core/imgui_impl_win32.h"
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
	, "MSAAx4"
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
void FMagnifierUIState::ToggleMagnifierLock()
{
	if (this->bUseMagnifier)
	{
		this->bLockMagnifierPositionHistory = this->bLockMagnifierPosition; // record histroy
		this->bLockMagnifierPosition = !this->bLockMagnifierPosition; // flip state
		const bool bLockSwitchedOn = !this->bLockMagnifierPositionHistory && this->bLockMagnifierPosition;
		const bool bLockSwitchedOff = this->bLockMagnifierPositionHistory && !this->bLockMagnifierPosition;
		if (bLockSwitchedOn)
		{
			const ImGuiIO& io = ImGui::GetIO();
			this->LockedMagnifiedScreenPositionX = static_cast<int>(io.MousePos.x);
			this->LockedMagnifiedScreenPositionY = static_cast<int>(io.MousePos.y);
			for (int ch = 0; ch < 3; ++ch) this->pMagnifierParams->fBorderColorRGB[ch] = MAGNIFIER_BORDER_COLOR__LOCKED[ch];
		}
		else if (bLockSwitchedOff)
		{
			for (int ch = 0; ch < 3; ++ch) this->pMagnifierParams->fBorderColorRGB[ch] = MAGNIFIER_BORDER_COLOR__FREE[ch];
		}
	}
}

template<class T> static T clamped(const T& v, const T& min, const T& max)
{
	if (v < min)      return min;
	else if (v > max) return max;
	else              return v;
}
// These are currently not bound to any mouse input and are here for convenience/reference.
// Mouse scroll is currently wired up to camera for panning and moving in the local Z direction.
// Any application that would prefer otherwise can utilize these for easily controlling the magnifier parameters through the desired input.
void FMagnifierUIState::AdjustMagnifierSize(float increment /*= 0.05f*/) { pMagnifierParams->fMagnifierScreenRadius = clamped(pMagnifierParams->fMagnifierScreenRadius + increment, MAGNIFIER_RADIUS_MIN, MAGNIFIER_RADIUS_MAX); }
void FMagnifierUIState::AdjustMagnifierMagnification(float increment /*= 1.00f*/) { pMagnifierParams->fMagnificationAmount = clamped(pMagnifierParams->fMagnificationAmount + increment, MAGNIFICATION_AMOUNT_MIN, MAGNIFICATION_AMOUNT_MAX); }


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
	s.bWindowVisible_MaterialEditor = false;
	s.bWindowVisible_LightEditor = false;
	s.bProfiler_ShowEngineStats = true;

	// couldn't bother using smart pointers due to inlined default destructors.
	// There's never a smooth way to work with them -- either too verbose or breaks compilation.
	// Raw ptrs will do for now
	
	s.mpMagnifierState = std::make_unique<FMagnifierUIState>();
	s.mpMagnifierState->pMagnifierParams = std::make_unique<FMagnifierParameters>();
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
	mpImGuiContext = ImGui::CreateContext();
	ImGui::SetCurrentContext(mpImGuiContext);
	ImGui_ImplWin32_Init(hwnd);

	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr; // don't save out to a .ini file


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
}

void VQEngine::InitializeUI(HWND hwnd)
{
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
	rDescs.pDataArray.push_back( pixels );
	TextureID texUI = mRenderer.CreateTexture(rDescs);
	SRV_ID srvUI = mRenderer.AllocateAndInitializeSRV(texUI);

	// Tell ImGUI what the image view is
	//
	io.Fonts->TexID = (ImTextureID)mRenderer.GetSRV(srvUI).GetGPUDescHandle().ptr;


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
	const int NUM_BACK_BUFFERS = mRenderer.GetSwapChainBackBufferCountmpWinMain->GetHWND());
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
		if (mUIState.bWindowVisible_GraphicsSettingsPanel) DrawGraphicsSettingsWindow(SceneParams, PPParams);
		if (mUIState.bWindowVisible_MaterialEditor)        DrawMaterialEditor();
		if (mUIState.bWindowVisible_LightEditor)           DrawLightEditor();
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
const uint32_t MATERIAL_WINDOW_PADDING_X = 10;
const uint32_t MATERIAL_WINDOW_PADDING_Y = 10;
const uint32_t MATERIAL_WINDOW_SIZE_X    = 550;
const uint32_t MATERIAL_WINDOW_SIZE_Y    = 400;
//---------------------------------------------
const uint32_t LIGHTS_WINDOW_PADDING_X   = 10;
const uint32_t LIGHTS_WINDOW_PADDING_Y   = 10;
const uint32_t LIGHTS_WINDOW_SIZE_X      = 550;
const uint32_t LIGHTS_WINDOW_SIZE_Y      = 400;
//---------------------------------------------

// Dropdown data ----------------------------------------------------------------------------------------------
const     ImVec4 UI_COLLAPSING_HEADER_COLOR_VALUE = ImVec4(0.0, 0.00, 0.0, 0.7f);
constexpr size_t NUM_MAX_ENV_MAP_NAMES    = 10;
constexpr size_t NUM_MAX_LEVEL_NAMES      = 8;
constexpr size_t NUM_MAX_CAMERA_NAMES     = 10;
constexpr size_t NUM_MAX_DRAW_MODE_NAMES  = static_cast<size_t>(EDrawMode::NUM_DRAW_MODES);
constexpr size_t NUM_MAX_FSR_OPTION_NAMES = AMD_FidelityFX_SuperResolution1::EPreset::NUM_FSR1_PRESET_OPTIONS;
static const char* szSceneNames [NUM_MAX_LEVEL_NAMES  ] = {};
static const char* szEnvMapNames[NUM_MAX_ENV_MAP_NAMES] = {};
static const char* szCameraNames[NUM_MAX_CAMERA_NAMES] = {};
static const char* szDrawModes  [NUM_MAX_DRAW_MODE_NAMES] = {};
static const char* szUpscalingLabels[FPostProcessParameters::EUpscalingAlgorithm::NUM_UPSCALING_ALGORITHMS] = {};
static const char* szUpscalingQualityLabels[NUM_MAX_FSR_OPTION_NAMES] = {};
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
		using namespace AMD_FidelityFX_SuperResolution1;
		szUpscalingQualityLabels[EPreset::ULTRA_QUALITY] = "Ultra Quality";
		szUpscalingQualityLabels[EPreset::QUALITY] = "Quality";
		szUpscalingQualityLabels[EPreset::BALANCED] = "Balanced";
		szUpscalingQualityLabels[EPreset::PERFORMANCE] = "Performance";
		szUpscalingQualityLabels[EPreset::CUSTOM] = "Custom";

		szUpscalingLabels[FPostProcessParameters::EUpscalingAlgorithm::NONE] = "None";
		szUpscalingLabels[FPostProcessParameters::EUpscalingAlgorithm::FIDELITYFX_SUPER_RESOLUTION1] = "AMD FSR1";

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
			szDrawModes[i] = fnToStr((EDrawMode)i);
		
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
		ImGui::Checkbox("F1: Editor", &mUIState.bWindowVisible_SceneControls);
		ImGui::TableSetColumnIndex(1);
		ImGui::Checkbox("F2: Settings", &mUIState.bWindowVisible_GraphicsSettingsPanel);

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Checkbox("F3: Profiler", &mUIState.bWindowVisible_Profiler);
		ImGui::TableSetColumnIndex(1);
		ImGui::Checkbox("F4: Debug", &mUIState.bWindowVisible_GraphicsSettingsPanel);
		

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Checkbox("F5: Materials", &mUIState.bWindowVisible_MaterialEditor);
		ImGui::TableSetColumnIndex(1);
		ImGui::Checkbox("F6: Lights", &mUIState.bWindowVisible_LightEditor);
 
		ImGui::EndTable();
	}


	ImGuiSpacing3();

	ImGui::Text("Scene");
	ImGui::Separator();
	if (ImGui_RightAlignedCombo("Scene File", &mIndex_SelectedScene, szSceneNames, (int)std::min(_countof(szSceneNames), mResourceNames.mSceneNames.size())))
	{
		this->StartLoadingScene(mIndex_SelectedScene);
	}
	ImGui_RightAlignedCombo("Camera (C)", &iSelectedCamera, szCameraNames, _countof(szCameraNames));
	MathUtil::Clamp(iSelectedCamera, 0, (int)mpScene->GetNumSceneCameras()-1);


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
		ImGui::SliderFloat("HDRI Rotation", &SceneRenderParams.fYawSliderValue, 0.0f, 1.0f);
	}

	const float MaxAmbientLighting = this->ShouldRenderHDR(mpWinMain->GetHWND()) ? 150.0f : 2.0f;
	MathUtil::Clamp(SceneRenderParams.fAmbientLightingFactor, 0.0f, MaxAmbientLighting);
	ImGui::SliderFloat("Ambient Lighting Factor", &SceneRenderParams.fAmbientLightingFactor, 0.0f, MaxAmbientLighting, "%.3f");

	ImGui::ColorEdit4("Outline Color", reinterpret_cast<float*>(&SceneRenderParams.OutlineColor), ImGuiColorEditFlags_DefaultOptions_);

	ImGui::Checkbox("ForceLOD0 (Shadow)", &SceneRenderParams.bForceLOD0_ShadowView);
	ImGui::Checkbox("ForceLOD0 (Scene )", &SceneRenderParams.bForceLOD0_SceneView);

	//ImGui::ColorEdit4();

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

	ImGui::Text("Upscaling");
	ImGui::Separator();
	// upscaling dropdown : None / FidelityFX Super Resolution 1.0
	if (ImGui_RightAlignedCombo("Algorithm", (int*) &PPParams.UpscalingAlgorithm, szUpscalingLabels, _countof(szUpscalingLabels)))
	{
		fnSendWindowResizeEvents();
	}

	if (PPParams.UpscalingAlgorithm != FPostProcessParameters::EUpscalingAlgorithm::NONE)
	{
		// preset: ultra quality / quality / balanced / performance / custom
		if (ImGui_RightAlignedCombo("Quality", (int*)&PPParams.UpscalingQualityPresetEnum, szUpscalingQualityLabels, _countof(szUpscalingQualityLabels)))
		{
			if (PPParams.UpscalingQualityPresetEnum != AMD_FidelityFX_SuperResolution1::EPreset::CUSTOM)
			{
				PPParams.ResolutionScale = AMD_FidelityFX_SuperResolution1::GetAMDFSR1ScreenPercentage(PPParams.UpscalingQualityPresetEnum);
			}
			
			fnSendWindowResizeEvents();
		}

		// resolution scale
		if (PPParams.UpscalingQualityPresetEnum == AMD_FidelityFX_SuperResolution1::EPreset::CUSTOM)
		{
			// if we are to support resolution scale > 1.0f, we'll need to stop using FSR1 upscaling
			// and use a linear min filter for AA
			if (ImGui::SliderFloat("Resolution Scale", &PPParams.ResolutionScale, 0.25f, 1.00f, "%.2f"))
			{
				fnSendWindowResizeEvents();
			}
		}
	}

	ImGuiSpacing3();

	ImGui::Text("Sharpness");
	ImGui::Separator();
	float LinearSharpness = PPParams.FSR_RCASParams.GetLinearSharpness();
	if (ImGui::SliderFloat("Amount##", &LinearSharpness, 0.01f, 1.00f, "%.2f"))
	{
		PPParams.FSR_RCASParams.SetLinearSharpness(LinearSharpness);
		PPParams.FSR_RCASParams.UpdateRCASConstantBlock();
	}

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

void VQEngine::DrawGraphicsSettingsWindow(FSceneRenderParameters& SceneRenderParams, FPostProcessParameters& PPParams)
{
	const uint32 W = mpWinMain->GetWidth();
	const uint32 H = mpWinMain->GetHeight();
	HWND hwnd = mpWinMain->GetHWND();

	FGraphicsSettings& gfx = mSettings.gfx;

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
	
	ImGui::BeginTabBar("s*", ImGuiTabBarFlags_None);
	
	
	if (ImGui::BeginTabItem("Debug"))
	{
		InitializeStaticCStringData_EDrawMode();
		int iDrawMode = (int)PPParams.DrawModeEnum;
		ImGui_RightAlignedCombo("Draw Mode", &iDrawMode, szDrawModes, _countof(szDrawModes));
		PPParams.DrawModeEnum = (EDrawMode)iDrawMode;
		if (PPParams.DrawModeEnum == EDrawMode::NORMALS)
		{
			bool bUnpackNormals = PPParams.VizParams.iUnpackNormals;
			ImGui::Checkbox("Unpack Normals", &bUnpackNormals);
			PPParams.VizParams.iUnpackNormals = bUnpackNormals;
		}
		if (PPParams.DrawModeEnum == EDrawMode::MOTION_VECTORS)
		{
			ImGui::SliderFloat("MoVec Intensity", &PPParams.VizParams.fInputStrength, 0.0f, 200.0f);
		}

		ImGui::Checkbox("Show GameObject Bounding Boxes (Shift+N)", &SceneRenderParams.bDrawGameObjectBoundingBoxes);
		ImGui::Checkbox("Show Mesh Bounding Boxes (N)", &SceneRenderParams.bDrawMeshBoundingBoxes);
		ImGui::Checkbox("Show Light Bounding Volumes (L)", &SceneRenderParams.bDrawLightBounds);
		ImGui::Checkbox("Draw Lights", &SceneRenderParams.bDrawLightMeshes);
		ImGui::Checkbox("Draw Vertex Axes", &SceneRenderParams.bDrawVertexLocalAxes);

		//
		// MAGNIFIER
		//
		ImGuiSpacing3();
		ImGuiIO& io = ImGui::GetIO();

		ImGui::Text("Magnifier");
		ImGui::Separator();
		{
			ImGui::Checkbox("Show Magnifier (Middle Mouse)", &mUIState.mpMagnifierState->bUseMagnifier);

			BeginDisabledUIState(mUIState.mpMagnifierState->bUseMagnifier);
			{
				FMagnifierParameters& params = *mUIState.mpMagnifierState->pMagnifierParams;

				// Use a local bool state here to track locked state through the UI widget,
				// and then call ToggleMagnifierLockedState() to update the persistent state (m_UIstate).
				// The keyboard input for toggling lock directly operates on the persistent state.
				const bool bIsMagnifierCurrentlyLocked = mUIState.mpMagnifierState->bLockMagnifierPosition;
				bool bMagnifierToggle = bIsMagnifierCurrentlyLocked;
				ImGui::Checkbox("Lock Position (Shift + Middle Mouse)", &bMagnifierToggle);

				if (bMagnifierToggle != bIsMagnifierCurrentlyLocked)
					mUIState.mpMagnifierState->ToggleMagnifierLock();

				ImGui::SliderFloat("Screen Size", &params.fMagnifierScreenRadius, MAGNIFIER_RADIUS_MIN, MAGNIFIER_RADIUS_MAX);
				ImGui::SliderFloat("Magnification", &params.fMagnificationAmount, MAGNIFICATION_AMOUNT_MIN, MAGNIFICATION_AMOUNT_MAX);
				if (bMagnifierToggle)
				{
					ImGui::SliderInt("OffsetX", &params.iMagnifierOffset[0], -(int)W, W);
					ImGui::SliderInt("OffsetY", &params.iMagnifierOffset[1], -(int)H, H);
				}
			}
			EndDisabledUIState(mUIState.mpMagnifierState->bUseMagnifier);
		}
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Display"))
	{
		BeginDisabledUIState(!gfx.bVsync);
		{
			static int iLimiter = mSettings.gfx.MaxFrameRate == -1 ? 0 : (mSettings.gfx.MaxFrameRate == 0 ? 1 : 2); // see Settings.h
			static int CustomFrameLimit = mSettings.gfx.MaxFrameRate;
			if (ImGui_RightAlignedCombo("FrameRate Limit", &iLimiter, szMaxFrameRateOptionLabels, _countof(szMaxFrameRateOptionLabels)))
			{
				switch (iLimiter)
				{
				case 0: mSettings.gfx.MaxFrameRate = -1; break;
				case 1: mSettings.gfx.MaxFrameRate = 0; break;
				case 2: mSettings.gfx.MaxFrameRate = CustomFrameLimit; break;
				default:
					break;
				}
				SetEffectiveFrameRateLimit(mSettings.gfx.MaxFrameRate);
			}
			if (iLimiter == 2) // custom frame limit value
			{
				if (ImGui::SliderInt("MaxFrames", &CustomFrameLimit, 10, 1000))
				{
					mSettings.gfx.MaxFrameRate = CustomFrameLimit;
					SetEffectiveFrameRateLimit(mSettings.gfx.MaxFrameRate);
				}
			}
		}
		EndDisabledUIState(!gfx.bVsync);

		if (ImGui::Checkbox("VSync (V)", &gfx.bVsync))
		{
			mEventQueue_WinToVQE_Renderer.AddItem(std::make_shared<SetVSyncEvent>(hwnd, gfx.bVsync));
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
		if (ImGui_RightAlignedCombo("AntiAliasing (M)", &iAALabel, szAALabels, _countof(szAALabels) - 1))
		{
			gfx.bAntiAliasing = iAALabel;
			Log::Info("AA Changed: %d", gfx.bAntiAliasing);
		}

		if (ImGui_RightAlignedCombo("Ambient Occlusion", &iSSAOLabel, szSSAOLabels, _countof(szSSAOLabels) - 1))
		{
			SceneRenderParams.bScreenSpaceAO = iSSAOLabel == 1;
			Log::Info("AO Changed: %d", SceneRenderParams.bScreenSpaceAO);
		}
		int iRefl = gfx.Reflections;
		if (ImGui_RightAlignedCombo("Reflections", &iRefl, szReflectionsLabels, _countof(szReflectionsLabels)-1))
		{
			gfx.Reflections = static_cast<EReflections>(iRefl);
			Log::Info("Reflections Changed: %d", gfx.Reflections);
		}
		switch (gfx.Reflections)
		{
		case EReflections::SCREEN_SPACE_REFLECTIONS__FFX:
		{
			FSceneRenderParameters::FFFX_SSSR_UIParameters& FFXParams = SceneRenderParams.FFX_SSSRParameters;

			ImGui::PushStyleColor(ImGuiCol_Header, UI_COLLAPSING_HEADER_COLOR_VALUE);
			if (ImGui::CollapsingHeader("SSSR Settings"))
			{
				ImGui::PopStyleColor();
				ImGui::SliderFloat("Roughness Threshold", &FFXParams.roughnessThreshold, 0.0f, 1.f);
				ImGui::SliderInt("Max Traversal Iterations", &FFXParams.maxTraversalIterations, 0, 256);
				ImGui::SliderInt("Min Traversal Occupancy", &FFXParams.minTraversalOccupancy, 0, 32);
				ImGui::SliderInt("Most Detailed Level", &FFXParams.mostDetailedDepthHierarchyMipLevel, 0, 5);
				ImGui::SliderFloat("Depth Buffer Thickness", &FFXParams.depthBufferThickness, 0.0f, 5.0f);
				ImGui::SliderFloat("Temporal Stability", &FFXParams.temporalStability, 0.0f, 1.0f);
				ImGui::SliderFloat("Temporal Variance Threshold", &FFXParams.temporalVarianceThreshold, 0.0f, 0.01f);
				ImGui::Checkbox("Enable Variance Guided Tracing", &FFXParams.bEnableTemporalVarianceGuidedTracing);
				ImGui::Text("Samples per Quad"); ImGui::SameLine();
				ImGui::RadioButton("1", &FFXParams.samplesPerQuad, 1); ImGui::SameLine();
				ImGui::RadioButton("2", &FFXParams.samplesPerQuad, 2); ImGui::SameLine();
				ImGui::RadioButton("4", &FFXParams.samplesPerQuad, 4);
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

		ImGui::Checkbox("Async Compute", &mSettings.gfx.bEnableAsyncCompute);
		ImGui::Checkbox("Async Copy", &mSettings.gfx.bEnableAsyncCopy);

		ImGui::EndTabItem();
	}


	if (ImGui::BeginTabItem("Post Processing"))
	{
		DrawPostProcessSettings(PPParams);
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

void VQEngine::DrawMaterialEditor()
{
	const uint32 W = mpWinMain->GetWidth();
	const uint32 H = mpWinMain->GetHeight();
	const uint32_t MATERIAL_WINDOW_POS_X = MATERIAL_WINDOW_PADDING_X + GFX_WINDOW_SIZE_X + GFX_WINDOW_PADDING_X;
	const uint32_t MATERIAL_WINDOW_POS_Y = H - MATERIAL_WINDOW_PADDING_Y * 2 - MATERIAL_WINDOW_SIZE_Y;
	
	const std::vector<FMaterialRepresentation>& matReps = mpScene->GetMaterialRepresentations();
	const std::vector<MaterialID> matIDsAll = mpScene->GetMaterialIDs();
	std::vector<MaterialID> matIDs;
	for(MaterialID matID : matIDsAll)
	for (size_t hObj : mpScene->mSelectedObjects)
	{
		const Model& m = mpScene->GetModel(mpScene->GetGameObject(hObj)->mModelID);
		auto materialSet = m.mData.GetMaterials();
		if (materialSet.find(matID) != materialSet.end())
		{
			matIDs.push_back(matID);
		}
	}
	if (mpScene->mSelectedObjects.empty())
	{
		matIDs = matIDsAll;
	}
	
	std::vector<const char*> szMaterialNames(matIDs.size() + 1, nullptr);
	std::vector<MaterialID> MaterialIDs(matIDs.size() + 1, INVALID_ID);
	{
		int iMatName = 0;
		for (MaterialID matID : matIDs)
		{
			const std::string& matName = mpScene->GetMaterialName(matID);
			szMaterialNames[iMatName] = matName.c_str();
			MaterialIDs[iMatName++] = matID;
		}
		szMaterialNames[iMatName] = "";
	}

	ImGui::SetNextWindowPos(ImVec2((float)MATERIAL_WINDOW_POS_X, (float)MATERIAL_WINDOW_POS_Y), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(MATERIAL_WINDOW_SIZE_X, MATERIAL_WINDOW_SIZE_Y), ImGuiCond_FirstUseEver);

	ImGui::Begin("MATERIAL EDITOR", &mUIState.bWindowVisible_MaterialEditor);
	
	if (!ImGui::BeginTable("MaterialEditorTableLayout", 2, ImGuiTableFlags_Resizable, ImVec2(-1, -1)))
	{
		ImGui::End();
		return;
	}

	ImGui::TableSetupColumn(mpScene->GetMaterialName(mUIState.SelectedMaterialIndex).c_str(), ImGuiTableColumnFlags_WidthStretch, 0.7f); // 70% width
	ImGui::TableSetupColumn("Materials", ImGuiTableColumnFlags_WidthStretch, 0.3f); // 30% width
	ImGui::TableHeadersRow();
	ImGui::TableNextRow();
	

	// draw selector
	ImGui::TableSetColumnIndex(1);
	ImGui::SetNextItemWidth(-1); // Make the controls take the full width of the column
	ImGui::ListBox("##", &mUIState.SelectedMaterialIndex, szMaterialNames.data(), (int)szMaterialNames.size(), 18);
	if (ImGui::Button("Unselect##", ImVec2(-1, 0)))
	{
		mUIState.SelectedMaterialIndex = (int)(szMaterialNames.size() - 1);
	}


	// draw editor
	ImGui::TableSetColumnIndex(0);

	ImGui::Text("ID : %d", mUIState.SelectedMaterialIndex);
	ImGuiSpacing(2);
	if (mUIState.SelectedMaterialIndex != szMaterialNames.size() - 1 && mUIState.SelectedMaterialIndex != INVALID_ID)
	{
		Material& mat = mpScene->GetMaterial(MaterialIDs[mUIState.SelectedMaterialIndex]);

		if (ImGui::BeginTable("MaterialEditorTable", 2, ImGuiTableFlags_Resizable,ImVec2(0, -1))) {
			ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			//ImGui::TableHeadersRow();

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

			StartDrawingMaterialEditorRow("Tiling");
			ImGui::DragFloat2("##tiling", reinterpret_cast<float*>(&mat.tiling), 0.05f, 0.0f, 10.0f, "%.2f");
			
			StartDrawingMaterialEditorRow("UV Bias");
			ImGui::DragFloat2("##uv_bias", reinterpret_cast<float*>(&mat.uv_bias), 0.05f, -10.0f, 10.0f, "%.2f");

			// Texture
			//EMaterialTextureMapBindings::ALBEDO
			//EMaterialTextureMapBindings::NORMALS
			//EMaterialTextureMapBindings::EMISSIVE
			//EMaterialTextureMapBindings::ALPHA_MASK
			//EMaterialTextureMapBindings::METALLIC
			//EMaterialTextureMapBindings::ROUGHNESS
			//EMaterialTextureMapBindings::OCCLUSION_ROUGHNESS_METALNESS
			//EMaterialTextureMapBindings::AMBIENT_OCCLUSION
			static const char* textureLabels[] = 
			{
				"Diffuse Map", 
				"Normal Map", 
				"Emissive Map", 
				//"Height Map", 
				"Alpha Mask Map",
				"Metallic Map", 
				"Roughness Map", 
				"Occlusion Roughness Metalness Map", 
				"Ambient Occlusion Map"
				, ""
			};
			const int textureIDs[] = 
			{
				mat.TexDiffuseMap, mat.TexNormalMap, mat.TexEmissiveMap, /*mat.TexHeightMap,*/
				mat.TexAlphaMaskMap, mat.TexMetallicMap, mat.TexRoughnessMap,
				mat.TexOcclusionRoughnessMetalnessMap, mat.TexAmbientOcclusionMap,
				INVALID_ID
			};
			for (int i = 0; i < _countof(textureLabels); ++i) 
			{
				if (textureIDs[i] == INVALID_ID)
					continue;

				const std::string_view& textureFormat = mRenderer.DXGIFormatAsString(mRenderer.GetTextureFormat(textureIDs[i]));
				const std::string& texturePath = mpScene->GetTexturePath(textureIDs[i]);
				const std::string textureName = mpScene->GetTextureName(textureIDs[i]);
				int textureSizeX, textureSizeY;
				mRenderer.GetTextureDimensions(textureIDs[i], textureSizeX, textureSizeY);
				int textureMIPs = mRenderer.GetTextureMips(textureIDs[i]);

				const CBV_SRV_UAV& srv = mRenderer.GetShaderResourceView(mat.SRVMaterialMaps);
				ImTextureID ImTexID = (ImTextureID)srv.GetGPUDescHandle(i).ptr;

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
				ImGui::SameLine();
				ImGui::Text("%s", textureName.c_str());

			}

			ImGui::EndTable();
		}
	}
	

	ImGui::EndTable();

	ImGui::End();
}

const char* LightTypeToString(Light::EType type) {
	switch (type) {
	case Light::EType::DIRECTIONAL: return "Directional";
	case Light::EType::SPOT: return "Spot";
	case Light::EType::POINT: return "Point";
	default: return "Unknown";
	}
}
void VQEngine::DrawLightEditor()
{
	const uint32 W = mpWinMain->GetWidth();
	const uint32 H = mpWinMain->GetHeight();
	const uint32_t LIGHTS_WINDOW_POS_X = LIGHTS_WINDOW_PADDING_X 
		+ GFX_WINDOW_SIZE_X + GFX_WINDOW_PADDING_X 
		+ MATERIAL_WINDOW_SIZE_X + MATERIAL_WINDOW_PADDING_X;
	const uint32_t LIGHTS_WINDOW_POS_Y = H - LIGHTS_WINDOW_PADDING_Y * 2 - LIGHTS_WINDOW_SIZE_Y;
	
	const std::vector<Light*> Lights = mpScene->GetLights();

	// build light names
	int iSpt = 0; int iPnt = 0; // this naming scheme isnt great: changing light type renames them...
	std::vector<std::string> LightNames;
	for (Light* l : Lights)
	{
		std::string LightName;
		switch (l->GetType())
		{
			case Light::EType::POINT       : LightName += "Point #" + std::to_string(iPnt++); break;
			case Light::EType::SPOT        : LightName += "Spot #" + std::to_string(iSpt++); break;
			case Light::EType::DIRECTIONAL : LightName += "Directional"; break;
			default: 
				Log::Error("DrawLightEditor(): undefined light type");
				break;
		}
		LightNames.push_back(LightName);
	}
	LightNames.push_back("");
	
	// validate selected light index
	if (mUIState.SelectedLightIndex >= LightNames.size())
	{
		Log::Warning("SelectedLightIndex > LightNames.size() : capping SelectedLightIndex ");
		mUIState.SelectedLightIndex = (int)(LightNames.size() - 1);
	}
	if (mUIState.SelectedLightIndex < 0)
	{
		// Log::Warning("SelectedLightIndex negative : Setting to 0");
		// mUIState.SelectedLightIndex = 0;
	}

	// get pointers to light name data 
	const std::string& SelectedLightName = mUIState.SelectedLightIndex >= 0 ?  LightNames[mUIState.SelectedLightIndex] : LightNames.back();
	std::vector<const char*> szLightNames(LightNames.size());
	for (int i = 0; i < LightNames.size(); ++i)
		szLightNames[i] = LightNames[i].c_str();
	
	// draw
	ImGui::SetNextWindowPos(ImVec2((float)LIGHTS_WINDOW_POS_X, (float)LIGHTS_WINDOW_POS_Y), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(LIGHTS_WINDOW_SIZE_X, LIGHTS_WINDOW_SIZE_Y), ImGuiCond_FirstUseEver);

	ImGui::Begin("LIGHT EDITOR", &mUIState.bWindowVisible_LightEditor);

	if (!ImGui::BeginTable("LightEditorTableLayout", 2, ImGuiTableFlags_Resizable, ImVec2(-1, -1)))
	{
		ImGui::End();
		return;
	}

	ImGui::TableSetupColumn(SelectedLightName.c_str(), ImGuiTableColumnFlags_WidthStretch, 0.7f); // 70% width
	ImGui::TableSetupColumn("Lights", ImGuiTableColumnFlags_WidthStretch, 0.3f); // 30% width
	ImGui::TableHeadersRow();
	ImGui::TableNextRow();

	// draw selector
	ImGui::TableSetColumnIndex(1);
	ImGui::SetNextItemWidth(-1); // Make the controls take the full width of the column
	ImGui::ListBox("##", &mUIState.SelectedLightIndex, szLightNames.data(), (int)szLightNames.size(), 18);
	if (ImGui::Button("Unselect##", ImVec2(-1, 0)))
	{
		mUIState.SelectedLightIndex = (int)(szLightNames.size() - 1);
	}


	// draw editor
	const bool bValidLight = mUIState.SelectedLightIndex != szLightNames.size() - 1 && mUIState.SelectedLightIndex != INVALID_ID;
	

	ImGui::TableSetColumnIndex(0);

	if (bValidLight)
	{
		Light* l = Lights[mUIState.SelectedLightIndex];

		ImGui::Checkbox("Enabled", &l->bEnabled);
		ImGui::SameLine();
		ImGui::Checkbox("Show Volume", &mUIState.bDrawLightVolume);
		//ImGui::SameLine();
		//ImGui::Checkbox("Show Outline", &l->bEnabled);
		ImGuiSpacing(2);

		ImGui::ColorEdit3("Color", reinterpret_cast<float*>(&l->Color));
		ImGui::DragFloat("Brightness", &l->Brightness, 0.1f, 0.0f, 10000.0f, "%.1f");
		if (l->Type != Light::EType::DIRECTIONAL) {
			ImGui::DragFloat("Range", &l->Range, 0.5f, 0.0f, 10000.0f, "%.1f");
		}

		ImGuiSpacing(2);

		// Light type specific properties
		if (ImGui::BeginCombo("Type", LightTypeToString(l->Type))) {
			if (ImGui::Selectable("Directional", l->Type == Light::EType::DIRECTIONAL)) { l->Type = Light::EType::DIRECTIONAL; }
			if (ImGui::Selectable("Spot"       , l->Type == Light::EType::SPOT       )) { l->Type = Light::EType::SPOT; }
			if (ImGui::Selectable("Point"      , l->Type == Light::EType::POINT      )) { l->Type = Light::EType::POINT; }
			ImGui::EndCombo();
		}
		switch (l->Type) {
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
			auto eulerRotation = Quaternion::ToEulerDeg(l->RotationQuaternion);
			if (ImGui::DragFloat3("Rotation", reinterpret_cast<float*>(&eulerRotation), 0.1f)) {
				// l->RotationQuaternion = Quaternion::FromEulerDeg(eulerRotation); // TODO:
			}
		}

		ImGuiSpacing(2);

		ImGui::Checkbox("Cast Shadows", &l->bCastingShadows);
		if (l->bCastingShadows) {
			ImGui::DragFloat("Depth Bias", &l->ShadowData.DepthBias, 0.005f, 0.0f, 1.0f, "%.3f");
			ImGui::DragFloat("Near Plane", &l->ShadowData.NearPlane, 0.1f, 0.1f, 100.0f, "%.1f");
			ImGui::DragFloat("Far Plane", &l->ShadowData.FarPlane, 1.0f, 1.0f, 10000.0f, "%.1f");
		}
	}

	ImGui::EndTable();
	ImGui::End();
}
