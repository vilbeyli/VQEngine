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


