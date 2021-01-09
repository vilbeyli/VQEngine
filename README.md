![](Data/Icons/VQEngine-icon.png)

# VQE

VQE is **VQEngine**: A DX12 rewrite of [VQEngine-DX11](https://github.com/vilbeyli/VQEngine) for fast prototyping of rendering techniques and experimenting with cutting-edge technology.

Join the [VQE Discord Channel](https://discord.gg/U7pd8TV) for rendering, graphics and engine architecture discussions!

[![Discord Banner 2](https://discordapp.com/api/guilds/720409073756930079/widget.png?style=banner2)](https://discord.gg/U7pd8TV)


# Screenshots

![](Screenshots/HelloEnvMap1.png)
<p align="center">
<sub><i>Data-driven (XML) Scenes & glTF Model Loading, HDRI Environment Maps, UE4's PBR model w/ IBL </i></sub>
</p>

# Features

See [Releases](https://github.com/vilbeyli/VQE/releases) to download the source & pre-built executables.

## Graphics

- Physically-based Rendering (PBR) 
   - BRDF
     - NDF : Trowbridge-Reitz GGX 
     - G   : Smith
     - F   : Fresnel_Schlick / Fresnel_Gaussian
   - Image-based Lighting (IBL) w/ prefiltered environment cubemaps
     - Load-time diffuse & specular irradiance cubemap convolution
- Lighting & Shadow maps
  - Point Lights
  - Spot Lights
  - Directional Light
  - PCF Shadow Maps for Point/Spot/Directional lights
- HDR Environment Maps from [HDRI Haven](https://hdrihaven.com/)
- Anti Aliasing
  - MSAA x4
- PostProcess
  - Tonemapping & Gamma correction
  - [FidelityFX - Contrast Adaptive Sharpening (CAS)](https://github.com/GPUOpen-Effects/FidelityFX-CAS/)

## Display

 - HDR10 display support 
 ![](Screenshots/HDRDisplay.jpg)
 - Multiple window & monitor support
 - Refresh Rate
   - Custom 
   - Auto (1.33 x monitor refresh rate) 
   - Unlocked
 - VSync
 - Alt+Enter Borderless Fullscreen

## Engine 

 - Multi-threaded architecture based on [Natalya Tatarchuk's  Destiny's Multithreaded Rendering Architecture Talk](https://www.youtube.com/watch?v=0nTDFLMLX9k)
   - Main, Update & Render Threads
   - ThreadPool of worker threads
  - [glTF](https://en.wikipedia.org/wiki/GlTF) [2.0](https://github.com/KhronosGroup/glTF/tree/master/specification/2.0) model loading using [assimp](https://github.com/assimp/assimp)
  - Shader system
    - Shader cache
    - Multi-threaded shader compilation
    - Shader Model 5.0 (WIP: SM6.0)
  - Automated build & testing scripts



# Build

Make sure to have pre-requisites installed 

- [CMake 3.4](https://cmake.org/download/)
- [Visual Studio 2019](https://visualstudio.microsoft.com/downloads/)
- [Windows 10 SDK 10.0.18362.0](https://developer.microsoft.com/en-us/windows/downloads/windows-10-sdk)

To download the PBR & HDRI textures, run

 - `Scripts/DownloadAssets.bat`

Then, run one of the build scripts in `Build/` folder,

- `GenerateSolutions.bat` to build from source with Visual Studio
  - `VQE.sln` can be found in `Build/SolutionFiles` directory
- `PackageEngine.bat` to build and package the engine in release mode and open `_artifacts` folder
  - `VQE.exe` can be found in `Build/_artifacts` directory


# Run

Make sure to have installed

 - [Visual C++ 2019 Redistributiable (x64)](https://support.microsoft.com/en-us/help/2977003/the-latest-supported-visual-c-downloads)
 - A DX12-capable GPU

Double click `VQE.exe`. 

Or, if you're using a terminal, 
- `VQE.exe -LogConsole` for logging displayed on a console
- `VQE.exe -LogFile="FileName.txt"` to write out to a file.

## Controls

| Key | |
| :--: | :-- |
| **WASD+EQ** | Camera movement |
| **Page Up/Down** | Change Environment Map |
| **1-4** |	Change scenes <br>**1** - *Default Scene* <br>**2** - *Sponza* <br>**3** - *Geometry Test Scene* <br>**4** - *Stress Test Scene* |
| **Shift+R** | Reload level |
| **C** | Change scene camera |
| **V** | Toggle VSync |
| **M** | Toggle MSAA |
| **B** | Toggle FidelityFX-CAS |
| **Alt+Enter** | Toggle Fullscreen |
| **Esc** | Release mouse |


## Settings

VQE can be configured through `Data/EngineConfig.ini` file

| Graphics Settings | |
| :-- | :-- |
| `ResolutionX=<int>` | Sets application render resolution width | 
| `ResolutionY=<int>` | Sets application render resolution height |
| `VSync=<bool>` <br/> | Toggles VSync based on the specified `<bool>` |
| `AntiAliasing=<bool>` | Toggles MSAA based on the specified `<bool>` |
| `MaxFrameRate=<int>` | Sets maximum frame rate to the specified `<int>` |
| `HDR=<bool>` | Toggles HDR swapchain & HDR display support |

<br/>

| Engine | |
| :-- | :-- |
| `Width=<int>` | Sets application main window width | 
| `Height=<int>` | Sets application main window height |
| `DisplayMode=<Windowed/Fulscreen>` | Sets Sets application main window mode: Windowed or Fullscreen |

## Command Line 

VQE supports the following command line parameters:

| CMD Line Parameter | Description  |
| :-- | :-- |
| `-LogConsole` | Launches a console window that displays log messages |
| `-LogFile=<string>` | Writes logs into an output file specified by `%FILE_NAME%`. <br/><br/> ***Example**: `VQE.exe -LogFile=Logs/log.txt` <br/>will create `Logs/` directory if it doesn't exist, and write log messages to the `log.txt` file*
| `-Test` | Launches the application in test mode: <br/> The app renders a pre-defined amount of frames and then exits. |
| `-TestFrames=<int>` | Application runs the sepcified amount of frames and then exits. <br/>Used for Automated testing. <br/><br/> ***Example**: `VQE.exe -TestFrames=1000`* |
| `-W=<int>` <br/> `-Width=<int>` | Sets application main window width to the specified amount |
| `-H=<int>` <br/> `-Height=<int>` | Sets application main window height to the specified amount |
| `-ResX=<int>` | Sets application render resolution width |
| `-ResY=<int>` | Sets application render resolution height |
| `-FullScreen` | Launches in fullscreen mode |
| `-Windowed` | Launches in windowed mode |
| `-VSync` | Enables VSync |
| `-VSync=<bool>` | Sets Specified VSync State |
| `-AntiAliasing` or `-AA` | Enables [MSAA](https://mynameismjp.wordpress.com/2012/10/24/msaa-overview/) |
| `-TripleBuffering` | Initializes SwapChain with 3 back buffers |
| `-DoubleBuffering` | Initializes SwapChain with 2 back buffers |


**Note:** Command line parameters will override the `EngineSettings.ini` values.

# Scripts

| File |  |
| :-- | :-- |
| `GenerateSolutions.bat` | **What it does** <br/>- Initializes the submodule repos<br/> - Runs `CMake` to generate visual studio solution files in `Build/SolutionFiles` directory <br/> - Launches Visual Studio <br/> <br/> **Flags** <br/> - `noVS` : Updates/Generates `VQE.sln` without launching a Visual Studio instance <br/><br/> ***Example** : `GenerateSolutions.bat -noVS` <br/> while VS is open to update solution files after modifying CmakeLists.txt without closing/relaunching VS*
| `PackageEngine.bat` | **What it does** <br/>  - Runs `GenerateSolutions.bat` if the visual studio solution doesn't exist <br/> - Builds the engine in Release configuration <br/> - Moves build output into `Build/_artifacts` folder <br/> <br/> **Flags** <br/> `-Clean` : Runs Clean on `VQE.sln` projects before building <br/> `-DebugOnly` : Builds the Debug binaries only <br/> `-Debug` : Builds Debug binaries in addition to Release <br/> `-RelWithDebInfo` : Builds the Release binaries with Debug info in addition to Release    <br/><br/> ***Note**: Release build is always on by default, unless `-DebugOnly` is specified* <br/><br/> ***Example**: `PackageEngine.bat -Clean -Debug -RelWithDebInfo ` <br/>will build all configurations after running Clean and copy the binaries into `Build/_artifacts` folder*
| `TestVQE.bat` | **What it does** <br/> - Runs `VQE.exe` with testing parameters, making the engine exit after rendering specified number of frames (1000 default). <br/><br/> **Flags** <br/> `-Debug`: Tests the Debug build in addition to the Release build <br/> 

# 3rd-Party

- [D3DX12](https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Libraries/D3DX12)
- [D3D12MA](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator)
- [stb](https://github.com/nothings/stb)
- [tinyxml2](https://github.com/leethomason/tinyxml2)
- [assimp](https://github.com/assimp/assimp)
- [WinPixEventRuntime](https://devblogs.microsoft.com/pix/winpixeventruntime/)
- [Khronos glTF Sample Models](https://github.com/KhronosGroup/glTF-Sample-Models)
- [cgbookcase PBR Textures](https://www.cgbookcase.com/)
- [AMD-FidelityFX](https://github.com/GPUOpen-Effects/FidelityFX)