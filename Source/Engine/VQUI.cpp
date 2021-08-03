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
#include "GPUMarker.h"

#include "VQUtils/Source/utils.h"

#include "Libs/imgui/imgui.h"
// To use the 'disabled UI state' functionality (ImGuiItemFlags_Disabled), include internal header
// https://github.com/ocornut/imgui/issues/211#issuecomment-339241929
#include "Libs/imgui/imgui_internal.h"
static void BeginDisabledUIState(bool bEnable)
{
	if (!bEnable)
	{
		ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
	}
}
static void EndDisabledUIState(bool bEnable)
{
	if (!bEnable)
	{
		ImGui::PopItemFlag();
		ImGui::PopStyleVar();
	}
}

struct VERTEX_CONSTANT_BUFFER{ float mvp[4][4]; };

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

	InitializeEngineUIState(mUIState);
}
void VQEngine::ExitUI()
{
	ImGui::DestroyContext(mpImGuiContext);
}

#if !VQENGINE_MT_PIPELINED_UPDATE_AND_RENDER_THREADS
constexpr int FRAME_DATA_INDEX = 0;
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

static void ShowDemoWindowLayout();
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

	//ShowDemoWindowLayout();
}

// ============================================================================================================================
// ============================================================================================================================
// ============================================================================================================================
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

constexpr int FPS_GRAPH_MAX_FPS_THRESHOLDS[] = { 800, 240, 120, 90, 66, 45, 30, 15, 10, 5, 4, 3, 2, 1 };
constexpr const char* FPS_GRAPH_MAX_FPS_THRESHOLDS_STR[] = { "800", "240", "120", "90", "66", "45", "30", "15", "10", "5", "4", "3", "2", "1" };

//
// Helpers
//
static ImVec4 SelectFPSColor(int FPS)
{
	constexpr int FPS_THRESHOLDS[] ={30, 45, 60, 144, 500};
	const ImVec4 FPSColors[6] =
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
			iColor = min(iThr+1, _countof(FPS_THRESHOLDS));
	}
	return FPSColors[iColor];
}
static inline void Spacing3() { ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing(); }
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
	FPS_HISTORY[FPS_HISTORY_INDEX] = fps;
	// TODO: proper sliding average

	FPS_HISTORY_INDEX = CircularIncrement(FPS_HISTORY_INDEX, FPS_HISTORY_SIZE);
	RECENT_HIGHEST_FPS = FPS_HISTORY[FPS_HISTORY_INDEX];
	size_t iFPSGraphMaxValue = 0;
	for (int i = _countof(FPS_GRAPH_MAX_FPS_THRESHOLDS)-1; i >= 0; --i)
	{
		if (RECENT_HIGHEST_FPS < FPS_GRAPH_MAX_FPS_THRESHOLDS[i]) // FPS_GRAPH_MAX_FPS_THRESHOLDS are in decreasing order
		{
			iFPSGraphMaxValue = min(_countof(FPS_GRAPH_MAX_FPS_THRESHOLDS) - 1, i);
			break;
		}
	}

	//ImGui::PlotHistogram("FPS Histo", FPS_HISTORY, IM_ARRAYSIZE(FPS_HISTORY));
	ImGui::PlotLines(FPS_GRAPH_MAX_FPS_THRESHOLDS_STR[iFPSGraphMaxValue], FPS_HISTORY, FPS_HISTORY_SIZE, 0, "FPS", 0.0f, FPS_GRAPH_MAX_FPS_THRESHOLDS[iFPSGraphMaxValue], GRAPH_SIZE);

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

	ImGui::SetNextWindowPos(ImVec2((float)PROFILER_WINDOW_POS_X, (float)PROFILER_WINDOW_POS_Y), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(PROFILER_WINDOW_SIZE_X, PROFILER_WINDOW_SIZE_Y), ImGuiCond_FirstUseEver);

	ImGui::Begin("PROFILER (F2)", &mUIState.bWindowVisible_Profiler);
	
	ImGui::Text("SYSTEM INFO");
	ImGui::Separator();
	//if (ImGui::CollapsingHeader("SYSTEM INFO", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::TextColored(DataTextColor, "API : %s", "DirectX 12");
		ImGui::TextColored(DataTextColor, "GPU : %s", mSysInfo.GPUs.back().DeviceName.c_str());
		ImGui::TextColored(DataTextColor, "CPU : %s", mSysInfo.CPU.DeviceName.c_str());
		ImGui::TextColored(DataTextColor, "RAM : 0B used of %s", StrUtil::FormatByte(mSysInfo.RAM.TotalPhysicalMemory).c_str());
	}
	
	Spacing3();

	ImGui::Text("PERFORMANCE");
	ImGui::Separator();
	//if (ImGui::CollapsingHeader("PERFORMANCE", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const int fps = 1.0f / dt;
		const float frameTime_ms = dt * 1000.0f;
		const float frameTime_us = frameTime_ms * 1000.0f;
		ImGui::TextColored(DataTextColor      , "Resolution : %ix%i", W, H);
		ImGui::TextColored(SelectFPSColor(fps), "FPS        : %d (%.2f ms)", fps, frameTime_ms);
		
		DrawFPSChart(fps);
		DrawFrameTimeChart();
	}

	Spacing3();

	ImGui::Checkbox("Show Engine Stats", &mUIState.bProfiler_ShowEngineStats);
	if (mUIState.bProfiler_ShowEngineStats)
	{
		//ImGui::Text("STATS");
		ImGui::Separator();
		if (ImGui::CollapsingHeader("SCENE ENTITIES", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextColored(DataTextColor, "Meshes    : %d", s.NumMeshes);
			ImGui::TextColored(DataTextColor, "Materials : %d", s.NumMaterials);
			//ImGui::TextColored(DataTextColor, "Models    : %d", s.NumModels);
			ImGui::TextColored(DataTextColor, "Objects   : %d", s.NumObjects);
			ImGui::TextColored(DataTextColor, "Cameras   : %d", s.NumCameras);
		}
		Spacing3();
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
		Spacing3();
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

	// initialize static string data---------------------------------------------------------------------
	static const char* pStrLevelNames[] = // TODO: initialize from Scenes.ini
	{
		  "Default"
		, "Sponza"
		, "GeometryUnitTest"
		, "StressTest"
		//, "Level4"
	};
	static const char* pStrCameraNames[] = // TODO: initialize from scene cameras
	{
		"Main Camera"
		, "Secondary Camera 0"
		, "Secondary Camera 1"
		, "Secondary Camera 2"
		, "Secondary Camera 3"
	};
	assert(iSelectedCamera < _countof(pStrCameraNames) && iSelectedCamera >= 0);

	const int iEnvMap = iSelectedEnvMap == -1 ? mResourceNames.mEnvironmentMapPresetNames.size() : iSelectedEnvMap;
	constexpr size_t NUM_MAX_ENV_MAP_NAMES = 10;
	static const char* pStrEnvMapNames[NUM_MAX_ENV_MAP_NAMES] = {};
	static bool bEnvMapNamesInitialized = false;
	if (!bEnvMapNamesInitialized)
	{
		assert(mResourceNames.mEnvironmentMapPresetNames.size() < NUM_MAX_ENV_MAP_NAMES);
		size_t i = 0;
		for (const std::string& name : mResourceNames.mEnvironmentMapPresetNames) pStrEnvMapNames[i++] = name.c_str();
		pStrEnvMapNames[i++] = "None"; // the 'unselected' option
		bEnvMapNamesInitialized = true;
	}
	// initialize static string data---------------------------------------------------------------------


	ImGui::Begin("SCENE CONTROLS (F1)", &mUIState.bWindowVisible_SceneControls);

	ImGui::BeginCombo("Scene", pStrLevelNames[mIndex_SelectedScene]);
	ImGui::BeginCombo("Camera (C)", pStrCameraNames[iSelectedCamera]);
	ImGui::BeginCombo("HDRI Map (Page Up/Down)", pStrEnvMapNames[iEnvMap]);

	ImGui::SliderFloat("Ambient Lighting Factor", &SceneRenderParams.fAmbientLightingFactor, 0.0f, 2.0f, "%.3f");

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

	Spacing3();

	ImGui::Text("Tonemapper");
	ImGui::Separator();
	{
		bool bGamma = PPParams.TonemapperParams.ToggleGammaCorrection;
		ImGui::Checkbox("[SDR] Apply Gamma (G)", &bGamma);
		PPParams.TonemapperParams.ToggleGammaCorrection = bGamma ? 1 : 0;
	}


	ImGui::End();
}


void VQEngine::DrawGraphicsSettingsWindow(FSceneRenderParameters& SceneRenderParams)
{
	const uint32 W = mpWinMain->GetWidth();
	const uint32 H = mpWinMain->GetHeight();

	FGraphicsSettings& gfx = mSettings.gfx;

	static const char* pStrAALabels[] =
	{
		  "No AA"
		, "MSAAx4"
	};
	const uint iAALabel = gfx.bAntiAliasing ? 1 : 0;
	
	static const char* pStrSSAOLabels[] =
	{
		"FidelityFX CACAO"
		, "None"
	};
	const uint iSSAOLabel = SceneRenderParams.bScreenSpaceAO ? 0 : 1;

	const uint32_t GFX_WINDOW_POS_X = GFX_WINDOW_PADDING_X;
	const uint32_t GFX_WINDOW_POS_Y = H - PP_WINDOW_PADDING_Y - PP_WINDOW_SIZE_Y - GFX_WINDOW_PADDING_Y - GFX_WINDOW_SIZE_Y;
	ImGui::SetNextWindowPos(ImVec2((float)GFX_WINDOW_POS_X, (float)GFX_WINDOW_POS_Y), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(GFX_WINDOW_SIZE_X, GFX_WINDOW_SIZE_Y), ImGuiCond_FirstUseEver);

	ImGui::Begin("GRAPHICS SETTINGS (F5)", &mUIState.bWindowVisible_DebugPanel);
	ImGui::Separator();

	ImGui::Text("DISPLAY");
	ImGui::Separator();
	ImGui::Checkbox("VSync (V)", &gfx.bVsync);
	bool b = false; // TODO
	ImGui::Checkbox("Fullscreen (Alt+Enter)", &b);

	Spacing3();

	ImGui::Text("RENDERING");
	ImGui::Separator();
	ImGui::BeginCombo("AntiAliasing (M)", pStrAALabels[iAALabel]);
	ImGui::BeginCombo("Ambient Occlusion", pStrSSAOLabels[iSSAOLabel]);


	ImGui::End();
}



#if 0
// =========================
// =========================
// =========================
// =========================

// Helper to display a little (?) mark which shows a tooltip when hovered.
// In your own code you may want to display an actual icon if you are using a merged icon fonts (see docs/FONTS.md)
static void HelpMarker(const char* desc)
{
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(desc);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

static void ShowDemoWindowLayout()
{
	if (!ImGui::CollapsingHeader("Layout & Scrolling", ImGuiTreeNodeFlags_DefaultOpen))
		return;

	if (ImGui::TreeNode("Child windows"))
	{
		HelpMarker("Use child windows to begin into a self-contained independent scrolling/clipping regions within a host window.");
		static bool disable_mouse_wheel = false;
		static bool disable_menu = false;
		ImGui::Checkbox("Disable Mouse Wheel", &disable_mouse_wheel);
		ImGui::Checkbox("Disable Menu", &disable_menu);

		// Child 1: no border, enable horizontal scrollbar
		{
			ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar;
			if (disable_mouse_wheel)
				window_flags |= ImGuiWindowFlags_NoScrollWithMouse;
			ImGui::BeginChild("ChildL", ImVec2(ImGui::GetWindowContentRegionWidth() * 0.5f, 260), false, window_flags);
			for (int i = 0; i < 100; i++)
				ImGui::Text("%04d: scrollable region", i);
			ImGui::EndChild();
		}

		ImGui::SameLine();

		// Child 2: rounded border
		{
			ImGuiWindowFlags window_flags = ImGuiWindowFlags_None;
			if (disable_mouse_wheel)
				window_flags |= ImGuiWindowFlags_NoScrollWithMouse;
			if (!disable_menu)
				window_flags |= ImGuiWindowFlags_MenuBar;
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
			ImGui::BeginChild("ChildR", ImVec2(0, 260), true, window_flags);
			if (!disable_menu && ImGui::BeginMenuBar())
			{
				if (ImGui::BeginMenu("Menu"))
				{
					assert(false); // TODO
					//ShowExampleMenuFile();
					ImGui::EndMenu();
				}
				ImGui::EndMenuBar();
			}
			if (ImGui::BeginTable("split", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				for (int i = 0; i < 100; i++)
				{
					char buf[32];
					sprintf(buf, "%03d", i);
					ImGui::TableNextColumn();
					ImGui::Button(buf, ImVec2(-FLT_MIN, 0.0f));
				}
				ImGui::EndTable();
			}
			ImGui::EndChild();
			ImGui::PopStyleVar();
		}

		ImGui::Separator();

		// Demonstrate a few extra things
		// - Changing ImGuiCol_ChildBg (which is transparent black in default styles)
		// - Using SetCursorPos() to position child window (the child window is an item from the POV of parent window)
		//   You can also call SetNextWindowPos() to position the child window. The parent window will effectively
		//   layout from this position.
		// - Using ImGui::GetItemRectMin/Max() to query the "item" state (because the child window is an item from
		//   the POV of the parent window). See 'Demo->Querying Status (Active/Focused/Hovered etc.)' for details.
		{
			static int offset_x = 0;
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
			ImGui::DragInt("Offset X", &offset_x, 1.0f, -1000, 1000);

			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (float)offset_x);
			ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 0, 0, 100));
			ImGui::BeginChild("Red", ImVec2(200, 100), true, ImGuiWindowFlags_None);
			for (int n = 0; n < 50; n++)
				ImGui::Text("Some test %d", n);
			ImGui::EndChild();
			bool child_is_hovered = ImGui::IsItemHovered();
			ImVec2 child_rect_min = ImGui::GetItemRectMin();
			ImVec2 child_rect_max = ImGui::GetItemRectMax();
			ImGui::PopStyleColor();
			ImGui::Text("Hovered: %d", child_is_hovered);
			ImGui::Text("Rect of child window is: (%.0f,%.0f) (%.0f,%.0f)", child_rect_min.x, child_rect_min.y, child_rect_max.x, child_rect_max.y);
		}

		ImGui::TreePop();
	}

	if (ImGui::TreeNode("Widgets Width"))
	{
		static float f = 0.0f;
		static bool show_indented_items = true;
		ImGui::Checkbox("Show indented items", &show_indented_items);

		// Use SetNextItemWidth() to set the width of a single upcoming item.
		// Use PushItemWidth()/PopItemWidth() to set the width of a group of items.
		// In real code use you'll probably want to choose width values that are proportional to your font size
		// e.g. Using '20.0f * GetFontSize()' as width instead of '200.0f', etc.

		ImGui::Text("SetNextItemWidth/PushItemWidth(100)");
		ImGui::SameLine(); HelpMarker("Fixed width.");
		ImGui::PushItemWidth(100);
		ImGui::DragFloat("float##1b", &f);
		if (show_indented_items)
		{
			ImGui::Indent();
			ImGui::DragFloat("float (indented)##1b", &f);
			ImGui::Unindent();
		}
		ImGui::PopItemWidth();

		ImGui::Text("SetNextItemWidth/PushItemWidth(-100)");
		ImGui::SameLine(); HelpMarker("Align to right edge minus 100");
		ImGui::PushItemWidth(-100);
		ImGui::DragFloat("float##2a", &f);
		if (show_indented_items)
		{
			ImGui::Indent();
			ImGui::DragFloat("float (indented)##2b", &f);
			ImGui::Unindent();
		}
		ImGui::PopItemWidth();

		ImGui::Text("SetNextItemWidth/PushItemWidth(GetContentRegionAvail().x * 0.5f)");
		ImGui::SameLine(); HelpMarker("Half of available width.\n(~ right-cursor_pos)\n(works within a column set)");
		ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
		ImGui::DragFloat("float##3a", &f);
		if (show_indented_items)
		{
			ImGui::Indent();
			ImGui::DragFloat("float (indented)##3b", &f);
			ImGui::Unindent();
		}
		ImGui::PopItemWidth();

		ImGui::Text("SetNextItemWidth/PushItemWidth(-GetContentRegionAvail().x * 0.5f)");
		ImGui::SameLine(); HelpMarker("Align to right edge minus half");
		ImGui::PushItemWidth(-ImGui::GetContentRegionAvail().x * 0.5f);
		ImGui::DragFloat("float##4a", &f);
		if (show_indented_items)
		{
			ImGui::Indent();
			ImGui::DragFloat("float (indented)##4b", &f);
			ImGui::Unindent();
		}
		ImGui::PopItemWidth();

		// Demonstrate using PushItemWidth to surround three items.
		// Calling SetNextItemWidth() before each of them would have the same effect.
		ImGui::Text("SetNextItemWidth/PushItemWidth(-FLT_MIN)");
		ImGui::SameLine(); HelpMarker("Align to right edge");
		ImGui::PushItemWidth(-FLT_MIN);
		ImGui::DragFloat("##float5a", &f);
		if (show_indented_items)
		{
			ImGui::Indent();
			ImGui::DragFloat("float (indented)##5b", &f);
			ImGui::Unindent();
		}
		ImGui::PopItemWidth();

		ImGui::TreePop();
	}

	if (ImGui::TreeNode("Basic Horizontal Layout"))
	{
		ImGui::TextWrapped("(Use ImGui::SameLine() to keep adding items to the right of the preceding item)");

		// Text
		ImGui::Text("Two items: Hello"); ImGui::SameLine();
		ImGui::TextColored(ImVec4(1, 1, 0, 1), "Sailor");

		// Adjust spacing
		ImGui::Text("More spacing: Hello"); ImGui::SameLine(0, 20);
		ImGui::TextColored(ImVec4(1, 1, 0, 1), "Sailor");

		// Button
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Normal buttons"); ImGui::SameLine();
		ImGui::Button("Banana"); ImGui::SameLine();
		ImGui::Button("Apple"); ImGui::SameLine();
		ImGui::Button("Corniflower");

		// Button
		ImGui::Text("Small buttons"); ImGui::SameLine();
		ImGui::SmallButton("Like this one"); ImGui::SameLine();
		ImGui::Text("can fit within a text block.");

		// Aligned to arbitrary position. Easy/cheap column.
		ImGui::Text("Aligned");
		ImGui::SameLine(150); ImGui::Text("x=150");
		ImGui::SameLine(300); ImGui::Text("x=300");
		ImGui::Text("Aligned");
		ImGui::SameLine(150); ImGui::SmallButton("x=150");
		ImGui::SameLine(300); ImGui::SmallButton("x=300");

		// Checkbox
		static bool c1 = false, c2 = false, c3 = false, c4 = false;
		ImGui::Checkbox("My", &c1); ImGui::SameLine();
		ImGui::Checkbox("Tailor", &c2); ImGui::SameLine();
		ImGui::Checkbox("Is", &c3); ImGui::SameLine();
		ImGui::Checkbox("Rich", &c4);

		// Various
		static float f0 = 1.0f, f1 = 2.0f, f2 = 3.0f;
		ImGui::PushItemWidth(80);
		const char* items[] = { "AAAA", "BBBB", "CCCC", "DDDD" };
		static int item = -1;
		ImGui::Combo("Combo", &item, items, IM_ARRAYSIZE(items)); ImGui::SameLine();
		ImGui::SliderFloat("X", &f0, 0.0f, 5.0f); ImGui::SameLine();
		ImGui::SliderFloat("Y", &f1, 0.0f, 5.0f); ImGui::SameLine();
		ImGui::SliderFloat("Z", &f2, 0.0f, 5.0f);
		ImGui::PopItemWidth();

		ImGui::PushItemWidth(80);
		ImGui::Text("Lists:");
		static int selection[4] = { 0, 1, 2, 3 };
		for (int i = 0; i < 4; i++)
		{
			if (i > 0) ImGui::SameLine();
			ImGui::PushID(i);
			ImGui::ListBox("", &selection[i], items, IM_ARRAYSIZE(items));
			ImGui::PopID();
			//if (ImGui::IsItemHovered()) ImGui::SetTooltip("ListBox %d hovered", i);
		}
		ImGui::PopItemWidth();

		// Dummy
		ImVec2 button_sz(40, 40);
		ImGui::Button("A", button_sz); ImGui::SameLine();
		ImGui::Dummy(button_sz); ImGui::SameLine();
		ImGui::Button("B", button_sz);

		// Manually wrapping
		// (we should eventually provide this as an automatic layout feature, but for now you can do it manually)
		ImGui::Text("Manually wrapping:");
		ImGuiStyle& style = ImGui::GetStyle();
		int buttons_count = 20;
		float window_visible_x2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
		for (int n = 0; n < buttons_count; n++)
		{
			ImGui::PushID(n);
			ImGui::Button("Box", button_sz);
			float last_button_x2 = ImGui::GetItemRectMax().x;
			float next_button_x2 = last_button_x2 + style.ItemSpacing.x + button_sz.x; // Expected position if next button was on same line
			if (n + 1 < buttons_count && next_button_x2 < window_visible_x2)
				ImGui::SameLine();
			ImGui::PopID();
		}

		ImGui::TreePop();
	}

	if (ImGui::TreeNode("Groups"))
	{
		HelpMarker(
			"BeginGroup() basically locks the horizontal position for new line. "
			"EndGroup() bundles the whole group so that you can use \"item\" functions such as "
			"IsItemHovered()/IsItemActive() or SameLine() etc. on the whole group.");
		ImGui::BeginGroup();
		{
			ImGui::BeginGroup();
			ImGui::Button("AAA");
			ImGui::SameLine();
			ImGui::Button("BBB");
			ImGui::SameLine();
			ImGui::BeginGroup();
			ImGui::Button("CCC");
			ImGui::Button("DDD");
			ImGui::EndGroup();
			ImGui::SameLine();
			ImGui::Button("EEE");
			ImGui::EndGroup();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("First group hovered");
		}
		// Capture the group size and create widgets using the same size
		ImVec2 size = ImGui::GetItemRectSize();
		const float values[5] = { 0.5f, 0.20f, 0.80f, 0.60f, 0.25f };
		ImGui::PlotHistogram("##values", values, IM_ARRAYSIZE(values), 0, NULL, 0.0f, 1.0f, size);

		ImGui::Button("ACTION", ImVec2((size.x - ImGui::GetStyle().ItemSpacing.x) * 0.5f, size.y));
		ImGui::SameLine();
		ImGui::Button("REACTION", ImVec2((size.x - ImGui::GetStyle().ItemSpacing.x) * 0.5f, size.y));
		ImGui::EndGroup();
		ImGui::SameLine();

		ImGui::Button("LEVERAGE\nBUZZWORD", size);
		ImGui::SameLine();

		if (ImGui::BeginListBox("List", size))
		{
			ImGui::Selectable("Selected", true);
			ImGui::Selectable("Not Selected", false);
			ImGui::EndListBox();
		}

		ImGui::TreePop();
	}

	if (ImGui::TreeNode("Text Baseline Alignment"))
	{
		{
			ImGui::BulletText("Text baseline:");
			ImGui::SameLine(); HelpMarker(
				"This is testing the vertical alignment that gets applied on text to keep it aligned with widgets. "
				"Lines only composed of text or \"small\" widgets use less vertical space than lines with framed widgets.");
			ImGui::Indent();

			ImGui::Text("KO Blahblah"); ImGui::SameLine();
			ImGui::Button("Some framed item"); ImGui::SameLine();
			HelpMarker("Baseline of button will look misaligned with text..");

			// If your line starts with text, call AlignTextToFramePadding() to align text to upcoming widgets.
			// (because we don't know what's coming after the Text() statement, we need to move the text baseline
			// down by FramePadding.y ahead of time)
			ImGui::AlignTextToFramePadding();
			ImGui::Text("OK Blahblah"); ImGui::SameLine();
			ImGui::Button("Some framed item"); ImGui::SameLine();
			HelpMarker("We call AlignTextToFramePadding() to vertically align the text baseline by +FramePadding.y");

			// SmallButton() uses the same vertical padding as Text
			ImGui::Button("TEST##1"); ImGui::SameLine();
			ImGui::Text("TEST"); ImGui::SameLine();
			ImGui::SmallButton("TEST##2");

			// If your line starts with text, call AlignTextToFramePadding() to align text to upcoming widgets.
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Text aligned to framed item"); ImGui::SameLine();
			ImGui::Button("Item##1"); ImGui::SameLine();
			ImGui::Text("Item"); ImGui::SameLine();
			ImGui::SmallButton("Item##2"); ImGui::SameLine();
			ImGui::Button("Item##3");

			ImGui::Unindent();
		}

		ImGui::Spacing();

		{
			ImGui::BulletText("Multi-line text:");
			ImGui::Indent();
			ImGui::Text("One\nTwo\nThree"); ImGui::SameLine();
			ImGui::Text("Hello\nWorld"); ImGui::SameLine();
			ImGui::Text("Banana");

			ImGui::Text("Banana"); ImGui::SameLine();
			ImGui::Text("Hello\nWorld"); ImGui::SameLine();
			ImGui::Text("One\nTwo\nThree");

			ImGui::Button("HOP##1"); ImGui::SameLine();
			ImGui::Text("Banana"); ImGui::SameLine();
			ImGui::Text("Hello\nWorld"); ImGui::SameLine();
			ImGui::Text("Banana");

			ImGui::Button("HOP##2"); ImGui::SameLine();
			ImGui::Text("Hello\nWorld"); ImGui::SameLine();
			ImGui::Text("Banana");
			ImGui::Unindent();
		}

		ImGui::Spacing();

		{
			ImGui::BulletText("Misc items:");
			ImGui::Indent();

			// SmallButton() sets FramePadding to zero. Text baseline is aligned to match baseline of previous Button.
			ImGui::Button("80x80", ImVec2(80, 80));
			ImGui::SameLine();
			ImGui::Button("50x50", ImVec2(50, 50));
			ImGui::SameLine();
			ImGui::Button("Button()");
			ImGui::SameLine();
			ImGui::SmallButton("SmallButton()");

			// Tree
			const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
			ImGui::Button("Button##1");
			ImGui::SameLine(0.0f, spacing);
			if (ImGui::TreeNode("Node##1"))
			{
				// Placeholder tree data
				for (int i = 0; i < 6; i++)
					ImGui::BulletText("Item %d..", i);
				ImGui::TreePop();
			}

			// Vertically align text node a bit lower so it'll be vertically centered with upcoming widget.
			// Otherwise you can use SmallButton() (smaller fit).
			ImGui::AlignTextToFramePadding();

			// Common mistake to avoid: if we want to SameLine after TreeNode we need to do it before we add
			// other contents below the node.
			bool node_open = ImGui::TreeNode("Node##2");
			ImGui::SameLine(0.0f, spacing); ImGui::Button("Button##2");
			if (node_open)
			{
				// Placeholder tree data
				for (int i = 0; i < 6; i++)
					ImGui::BulletText("Item %d..", i);
				ImGui::TreePop();
			}

			// Bullet
			ImGui::Button("Button##3");
			ImGui::SameLine(0.0f, spacing);
			ImGui::BulletText("Bullet text");

			ImGui::AlignTextToFramePadding();
			ImGui::BulletText("Node");
			ImGui::SameLine(0.0f, spacing); ImGui::Button("Button##4");
			ImGui::Unindent();
		}

		ImGui::TreePop();
	}

	if (ImGui::TreeNode("Scrolling"))
	{
		// Vertical scroll functions
		HelpMarker("Use SetScrollHereY() or SetScrollFromPosY() to scroll to a given vertical position.");

		static int track_item = 50;
		static bool enable_track = true;
		static bool enable_extra_decorations = false;
		static float scroll_to_off_px = 0.0f;
		static float scroll_to_pos_px = 200.0f;

		ImGui::Checkbox("Decoration", &enable_extra_decorations);

		ImGui::Checkbox("Track", &enable_track);
		ImGui::PushItemWidth(100);
		ImGui::SameLine(140); enable_track |= ImGui::DragInt("##item", &track_item, 0.25f, 0, 99, "Item = %d");

		bool scroll_to_off = ImGui::Button("Scroll Offset");
		ImGui::SameLine(140); scroll_to_off |= ImGui::DragFloat("##off", &scroll_to_off_px, 1.00f, 0, FLT_MAX, "+%.0f px");

		bool scroll_to_pos = ImGui::Button("Scroll To Pos");
		ImGui::SameLine(140); scroll_to_pos |= ImGui::DragFloat("##pos", &scroll_to_pos_px, 1.00f, -10, FLT_MAX, "X/Y = %.0f px");
		ImGui::PopItemWidth();

		if (scroll_to_off || scroll_to_pos)
			enable_track = false;

		ImGuiStyle& style = ImGui::GetStyle();
		float child_w = (ImGui::GetContentRegionAvail().x - 4 * style.ItemSpacing.x) / 5;
		if (child_w < 1.0f)
			child_w = 1.0f;
		ImGui::PushID("##VerticalScrolling");
		for (int i = 0; i < 5; i++)
		{
			if (i > 0) ImGui::SameLine();
			ImGui::BeginGroup();
			const char* names[] = { "Top", "25%", "Center", "75%", "Bottom" };
			ImGui::TextUnformatted(names[i]);

			const ImGuiWindowFlags child_flags = enable_extra_decorations ? ImGuiWindowFlags_MenuBar : 0;
			const ImGuiID child_id = ImGui::GetID((void*)(intptr_t)i);
			const bool child_is_visible = ImGui::BeginChild(child_id, ImVec2(child_w, 200.0f), true, child_flags);
			if (ImGui::BeginMenuBar())
			{
				ImGui::TextUnformatted("abc");
				ImGui::EndMenuBar();
			}
			if (scroll_to_off)
				ImGui::SetScrollY(scroll_to_off_px);
			if (scroll_to_pos)
				ImGui::SetScrollFromPosY(ImGui::GetCursorStartPos().y + scroll_to_pos_px, i * 0.25f);
			if (child_is_visible) // Avoid calling SetScrollHereY when running with culled items
			{
				for (int item = 0; item < 100; item++)
				{
					if (enable_track && item == track_item)
					{
						ImGui::TextColored(ImVec4(1, 1, 0, 1), "Item %d", item);
						ImGui::SetScrollHereY(i * 0.25f); // 0.0f:top, 0.5f:center, 1.0f:bottom
					}
					else
					{
						ImGui::Text("Item %d", item);
					}
				}
			}
			float scroll_y = ImGui::GetScrollY();
			float scroll_max_y = ImGui::GetScrollMaxY();
			ImGui::EndChild();
			ImGui::Text("%.0f/%.0f", scroll_y, scroll_max_y);
			ImGui::EndGroup();
		}
		ImGui::PopID();

		// Horizontal scroll functions
		ImGui::Spacing();
		HelpMarker(
			"Use SetScrollHereX() or SetScrollFromPosX() to scroll to a given horizontal position.\n\n"
			"Because the clipping rectangle of most window hides half worth of WindowPadding on the "
			"left/right, using SetScrollFromPosX(+1) will usually result in clipped text whereas the "
			"equivalent SetScrollFromPosY(+1) wouldn't.");
		ImGui::PushID("##HorizontalScrolling");
		for (int i = 0; i < 5; i++)
		{
			float child_height = ImGui::GetTextLineHeight() + style.ScrollbarSize + style.WindowPadding.y * 2.0f;
			ImGuiWindowFlags child_flags = ImGuiWindowFlags_HorizontalScrollbar | (enable_extra_decorations ? ImGuiWindowFlags_AlwaysVerticalScrollbar : 0);
			ImGuiID child_id = ImGui::GetID((void*)(intptr_t)i);
			bool child_is_visible = ImGui::BeginChild(child_id, ImVec2(-100, child_height), true, child_flags);
			if (scroll_to_off)
				ImGui::SetScrollX(scroll_to_off_px);
			if (scroll_to_pos)
				ImGui::SetScrollFromPosX(ImGui::GetCursorStartPos().x + scroll_to_pos_px, i * 0.25f);
			if (child_is_visible) // Avoid calling SetScrollHereY when running with culled items
			{
				for (int item = 0; item < 100; item++)
				{
					if (item > 0)
						ImGui::SameLine();
					if (enable_track && item == track_item)
					{
						ImGui::TextColored(ImVec4(1, 1, 0, 1), "Item %d", item);
						ImGui::SetScrollHereX(i * 0.25f); // 0.0f:left, 0.5f:center, 1.0f:right
					}
					else
					{
						ImGui::Text("Item %d", item);
					}
				}
			}
			float scroll_x = ImGui::GetScrollX();
			float scroll_max_x = ImGui::GetScrollMaxX();
			ImGui::EndChild();
			ImGui::SameLine();
			const char* names[] = { "Left", "25%", "Center", "75%", "Right" };
			ImGui::Text("%s\n%.0f/%.0f", names[i], scroll_x, scroll_max_x);
			ImGui::Spacing();
		}
		ImGui::PopID();

		// Miscellaneous Horizontal Scrolling Demo
		HelpMarker(
			"Horizontal scrolling for a window is enabled via the ImGuiWindowFlags_HorizontalScrollbar flag.\n\n"
			"You may want to also explicitly specify content width by using SetNextWindowContentWidth() before Begin().");
		static int lines = 7;
		ImGui::SliderInt("Lines", &lines, 1, 15);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 1.0f));
		ImVec2 scrolling_child_size = ImVec2(0, ImGui::GetFrameHeightWithSpacing() * 7 + 30);
		ImGui::BeginChild("scrolling", scrolling_child_size, true, ImGuiWindowFlags_HorizontalScrollbar);
		for (int line = 0; line < lines; line++)
		{
			// Display random stuff. For the sake of this trivial demo we are using basic Button() + SameLine()
			// If you want to create your own time line for a real application you may be better off manipulating
			// the cursor position yourself, aka using SetCursorPos/SetCursorScreenPos to position the widgets
			// yourself. You may also want to use the lower-level ImDrawList API.
			int num_buttons = 10 + ((line & 1) ? line * 9 : line * 3);
			for (int n = 0; n < num_buttons; n++)
			{
				if (n > 0) ImGui::SameLine();
				ImGui::PushID(n + line * 1000);
				char num_buf[16];
				sprintf(num_buf, "%d", n);
				const char* label = (!(n % 15)) ? "FizzBuzz" : (!(n % 3)) ? "Fizz" : (!(n % 5)) ? "Buzz" : num_buf;
				float hue = n * 0.05f;
				ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(hue, 0.6f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(hue, 0.7f, 0.7f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(hue, 0.8f, 0.8f));
				ImGui::Button(label, ImVec2(40.0f + sinf((float)(line + n)) * 20.0f, 0.0f));
				ImGui::PopStyleColor(3);
				ImGui::PopID();
			}
		}
		float scroll_x = ImGui::GetScrollX();
		float scroll_max_x = ImGui::GetScrollMaxX();
		ImGui::EndChild();
		ImGui::PopStyleVar(2);
		float scroll_x_delta = 0.0f;
		ImGui::SmallButton("<<");
		if (ImGui::IsItemActive())
			scroll_x_delta = -ImGui::GetIO().DeltaTime * 1000.0f;
		ImGui::SameLine();
		ImGui::Text("Scroll from code"); ImGui::SameLine();
		ImGui::SmallButton(">>");
		if (ImGui::IsItemActive())
			scroll_x_delta = +ImGui::GetIO().DeltaTime * 1000.0f;
		ImGui::SameLine();
		ImGui::Text("%.0f/%.0f", scroll_x, scroll_max_x);
		if (scroll_x_delta != 0.0f)
		{
			// Demonstrate a trick: you can use Begin to set yourself in the context of another window
			// (here we are already out of your child window)
			ImGui::BeginChild("scrolling");
			ImGui::SetScrollX(ImGui::GetScrollX() + scroll_x_delta);
			ImGui::EndChild();
		}
		ImGui::Spacing();

		static bool show_horizontal_contents_size_demo_window = false;
		ImGui::Checkbox("Show Horizontal contents size demo window", &show_horizontal_contents_size_demo_window);

		if (show_horizontal_contents_size_demo_window)
		{
			static bool show_h_scrollbar = true;
			static bool show_button = true;
			static bool show_tree_nodes = true;
			static bool show_text_wrapped = false;
			static bool show_columns = true;
			static bool show_tab_bar = true;
			static bool show_child = false;
			static bool explicit_content_size = false;
			static float contents_size_x = 300.0f;
			if (explicit_content_size)
				ImGui::SetNextWindowContentSize(ImVec2(contents_size_x, 0.0f));
			ImGui::Begin("Horizontal contents size demo window", &show_horizontal_contents_size_demo_window, show_h_scrollbar ? ImGuiWindowFlags_HorizontalScrollbar : 0);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 0));
			HelpMarker("Test of different widgets react and impact the work rectangle growing when horizontal scrolling is enabled.\n\nUse 'Metrics->Tools->Show windows rectangles' to visualize rectangles.");
			ImGui::Checkbox("H-scrollbar", &show_h_scrollbar);
			ImGui::Checkbox("Button", &show_button);            // Will grow contents size (unless explicitly overwritten)
			ImGui::Checkbox("Tree nodes", &show_tree_nodes);    // Will grow contents size and display highlight over full width
			ImGui::Checkbox("Text wrapped", &show_text_wrapped);// Will grow and use contents size
			ImGui::Checkbox("Columns", &show_columns);          // Will use contents size
			ImGui::Checkbox("Tab bar", &show_tab_bar);          // Will use contents size
			ImGui::Checkbox("Child", &show_child);              // Will grow and use contents size
			ImGui::Checkbox("Explicit content size", &explicit_content_size);
			ImGui::Text("Scroll %.1f/%.1f %.1f/%.1f", ImGui::GetScrollX(), ImGui::GetScrollMaxX(), ImGui::GetScrollY(), ImGui::GetScrollMaxY());
			if (explicit_content_size)
			{
				ImGui::SameLine();
				ImGui::SetNextItemWidth(100);
				ImGui::DragFloat("##csx", &contents_size_x);
				ImVec2 p = ImGui::GetCursorScreenPos();
				ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + 10, p.y + 10), IM_COL32_WHITE);
				ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x + contents_size_x - 10, p.y), ImVec2(p.x + contents_size_x, p.y + 10), IM_COL32_WHITE);
				ImGui::Dummy(ImVec2(0, 10));
			}
			ImGui::PopStyleVar(2);
			ImGui::Separator();
			if (show_button)
			{
				ImGui::Button("this is a 300-wide button", ImVec2(300, 0));
			}
			if (show_tree_nodes)
			{
				bool open = true;
				if (ImGui::TreeNode("this is a tree node"))
				{
					if (ImGui::TreeNode("another one of those tree node..."))
					{
						ImGui::Text("Some tree contents");
						ImGui::TreePop();
					}
					ImGui::TreePop();
				}
				ImGui::CollapsingHeader("CollapsingHeader", &open);
			}
			if (show_text_wrapped)
			{
				ImGui::TextWrapped("This text should automatically wrap on the edge of the work rectangle.");
			}
			if (show_columns)
			{
				ImGui::Text("Tables:");
				if (ImGui::BeginTable("table", 4, ImGuiTableFlags_Borders))
				{
					for (int n = 0; n < 4; n++)
					{
						ImGui::TableNextColumn();
						ImGui::Text("Width %.2f", ImGui::GetContentRegionAvail().x);
					}
					ImGui::EndTable();
				}
				ImGui::Text("Columns:");
				ImGui::Columns(4);
				for (int n = 0; n < 4; n++)
				{
					ImGui::Text("Width %.2f", ImGui::GetColumnWidth());
					ImGui::NextColumn();
				}
				ImGui::Columns(1);
			}
			if (show_tab_bar && ImGui::BeginTabBar("Hello"))
			{
				if (ImGui::BeginTabItem("OneOneOne")) { ImGui::EndTabItem(); }
				if (ImGui::BeginTabItem("TwoTwoTwo")) { ImGui::EndTabItem(); }
				if (ImGui::BeginTabItem("ThreeThreeThree")) { ImGui::EndTabItem(); }
				if (ImGui::BeginTabItem("FourFourFour")) { ImGui::EndTabItem(); }
				ImGui::EndTabBar();
			}
			if (show_child)
			{
				ImGui::BeginChild("child", ImVec2(0, 0), true);
				ImGui::EndChild();
			}
			ImGui::End();
		}

		ImGui::TreePop();
	}

	if (ImGui::TreeNode("Clipping"))
	{
		static ImVec2 size(100.0f, 100.0f);
		static ImVec2 offset(30.0f, 30.0f);
		ImGui::DragFloat2("size", (float*)&size, 0.5f, 1.0f, 200.0f, "%.0f");
		ImGui::TextWrapped("(Click and drag to scroll)");

		for (int n = 0; n < 3; n++)
		{
			if (n > 0)
				ImGui::SameLine();
			ImGui::PushID(n);
			ImGui::BeginGroup(); // Lock X position

			ImGui::InvisibleButton("##empty", size);
			if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			{
				offset.x += ImGui::GetIO().MouseDelta.x;
				offset.y += ImGui::GetIO().MouseDelta.y;
			}
			const ImVec2 p0 = ImGui::GetItemRectMin();
			const ImVec2 p1 = ImGui::GetItemRectMax();
			const char* text_str = "Line 1 hello\nLine 2 clip me!";
			const ImVec2 text_pos = ImVec2(p0.x + offset.x, p0.y + offset.y);
			ImDrawList* draw_list = ImGui::GetWindowDrawList();

			switch (n)
			{
			case 0:
				HelpMarker(
					"Using ImGui::PushClipRect():\n"
					"Will alter ImGui hit-testing logic + ImDrawList rendering.\n"
					"(use this if you want your clipping rectangle to affect interactions)");
				ImGui::PushClipRect(p0, p1, true);
				draw_list->AddRectFilled(p0, p1, IM_COL32(90, 90, 120, 255));
				draw_list->AddText(text_pos, IM_COL32_WHITE, text_str);
				ImGui::PopClipRect();
				break;
			case 1:
				HelpMarker(
					"Using ImDrawList::PushClipRect():\n"
					"Will alter ImDrawList rendering only.\n"
					"(use this as a shortcut if you are only using ImDrawList calls)");
				draw_list->PushClipRect(p0, p1, true);
				draw_list->AddRectFilled(p0, p1, IM_COL32(90, 90, 120, 255));
				draw_list->AddText(text_pos, IM_COL32_WHITE, text_str);
				draw_list->PopClipRect();
				break;
			case 2:
				HelpMarker(
					"Using ImDrawList::AddText() with a fine ClipRect:\n"
					"Will alter only this specific ImDrawList::AddText() rendering.\n"
					"(this is often used internally to avoid altering the clipping rectangle and minimize draw calls)");
				ImVec4 clip_rect(p0.x, p0.y, p1.x, p1.y); // AddText() takes a ImVec4* here so let's convert.
				draw_list->AddRectFilled(p0, p1, IM_COL32(90, 90, 120, 255));
				draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), text_pos, IM_COL32_WHITE, text_str, NULL, 0.0f, &clip_rect);
				break;
			}
			ImGui::EndGroup();
			ImGui::PopID();
		}

		ImGui::TreePop();
	}
}
#endif