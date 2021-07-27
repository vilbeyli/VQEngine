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

struct VERTEX_CONSTANT_BUFFER
{
    float mvp[4][4];
};

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

}


void VQEngine::ExitUI()
{
    ImGui::DestroyContext(mpImGuiContext);
}



void VQEngine::UpdateUIState(HWND hwnd)
{
    SCOPED_CPU_MARKER_C("UpdateUIState()", 0xFF007777);

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

    // Start the frame -----------------------------------------------
    ImGui::NewFrame();

    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameBorderSize = 1.0f;


    DrawProfilerWindow();
    DrawSceneControlsWindow();
    DrawPostProcessControlsWindow();
}

void VQEngine::DrawProfilerWindow()
{
    const uint32 W = mpWinMain->GetWidth();
    const uint32 H = mpWinMain->GetHeight();

    const uint32_t PROFILER_WINDOW_PADDIG_X = 10;
    const uint32_t PROFILER_WINDOW_PADDIG_Y = 10;
    const uint32_t PROFILER_WINDOW_SIZE_X = 330;
    const uint32_t PROFILER_WINDOW_SIZE_Y = 400;
    const uint32_t PROFILER_WINDOW_POS_X = W - PROFILER_WINDOW_PADDIG_X - PROFILER_WINDOW_SIZE_X;
    const uint32_t PROFILER_WINDOW_POS_Y = PROFILER_WINDOW_PADDIG_Y;

    ImGui::SetNextWindowPos(ImVec2((float)PROFILER_WINDOW_POS_X, (float)PROFILER_WINDOW_POS_Y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(PROFILER_WINDOW_SIZE_X, PROFILER_WINDOW_SIZE_Y), ImGuiCond_FirstUseEver);

    ImGui::Begin("PROFILER (F2)", &mUIState.bWindowVisible_Profiler);

    ImGui::Text("Resolution : %ix%i", W, H);
    ImGui::Text("API        : %s", "DirectX 12");
    ImGui::Text("GPU        : %s", mSysInfo.GPUs.back().DeviceName.c_str());
    ImGui::Text("CPU        : %s", mSysInfo.CPU.DeviceName.c_str());
    ImGui::Text("RAM        : 0B used of %s", StrUtil::FormatByte(mSysInfo.RAM.TotalPhysicalMemory).c_str());
    //ImGui::Text("FPS        : %d (%.2f ms)", fps, frameTime_ms);
    ImGui::End();
}

void VQEngine::DrawSceneControlsWindow()
{
    const uint32_t CONTROLS_WINDOW_POS_X = 10;
    const uint32_t CONTROLS_WINDOW_POS_Y = 10;
    const uint32_t CONTROLS_WINDOW_SIZE_X = 350;
    const uint32_t CONTROLS_WINDOW_SIZE_Y = 650;

    // TODO
}

void VQEngine::DrawPostProcessControlsWindow()
{
    const uint32_t CONTROLS_WINDOW_POS_X = 10;
    const uint32_t CONTROLS_WINDOW_POS_Y = 10;
    const uint32_t CONTROLS_WINDOW_SIZE_X = 350;
    const uint32_t CONTROLS_WINDOW_SIZE_Y = 650;

    // TODO
}

