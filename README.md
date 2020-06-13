# VQE

![](Data/Icons/VQEngine-icon.png) 

VQE stands for **VQEngine**: A DX12 rewrite of [VQEngine-Vanilla](https://github.com/vilbeyli/VQEngine) for fast prototyping of rendering techniques and experimenting with cutting-edge technology.

VQE will be focusing on the following

 - Multi-threaded execution
   - Update & Render Threads
   - ThreadPool of worker threads
 - HDR display support
 - Real-time and offline Ray Tracing



# Build

Make sure to have pre-requisites installed 

- [CMake 3.4](https://cmake.org/download/)
- [Visual Studio 2019](https://visualstudio.microsoft.com/downloads/)
- [Windows 10 SDK 10.0.18362.0](https://developer.microsoft.com/en-us/windows/downloads/windows-10-sdk)

Then, run the build scripts in `Build/` folder,

- `GenerateSolutions.bat` to build from source with Visual Studio
- `PackageEngine.bat` to build & package for distribution.

`VQE.sln` can be found in `Build/SolutionFiles` directory.

## Scripts

| File |  |
| :-- | :-- |
| `GenerateSolutions.bat` | **What it does** <br/>- Initializes the submodule repos<br/> - Runs `CMake` to generate visual studio solution files in `Build/SolutionFiles` directory <br/> - Launches Visual Studio <br/> <br/> **Flags** <br/> - `noVS` : Updates/Generates `VQE.sln` without launching a Visual Studio instance <br/><br/> ***Example** : `GenerateSolutions.bat -noVS` <br/> while VS is open to update solution files after modifying CmakeLists.txt without closing/relaunching VS*
| `PackageEngine.bat` | **What it does** <br/>  - Runs `GenerateSolutions.bat` if the visual studio solution doesn't exist <br/> - Builds the engine in Release configuration <br/> - Moves build output into `Build/_artifacts` folder <br/> <br/> **Flags** <br/> `-Clean` : Runs Clean on `VQE.sln` projects before building <br/> `-DebugOnly` : Builds the Debug binaries only <br/> `-Debug` : Builds Debug binaries in addition to Release <br/> `-RelWithDebInfo` : Builds the Release binaries with Debug info in addition to Release    <br/><br/> ***Note**: Release build is always on by default, unless `-DebugOnly` is specified* <br/><br/> ***Example**: `PackageEngine.bat -Clean -Debug -RelWithDebInfo ` <br/>will build all configurations after running Clean and copy the binaries into `Build/_artifacts` folder*

# Settings / Config

VQE supports the following command line parameters.

| Parameter |  |
| :-- | :-- |
| `-LogConsole` | Launches a console window that displays log messages |
| `-LogFile=<value>` | Writes logs into an output file specified by `%FILE_NAME%`. <br/><br/> ***Example**: `VQE.exe -LogFile=Logs/log.txt` <br/>will create `Logs/` directory if it doesn't exist, and write log messages to the `log.txt` file*
| `-Test` | Launches the application in test mode: <br/> The app renders a pre-defined amount of frames and then exits. |
| `-TestFrames=<value>` | Application runs the sepcified amount of frames and then exits. <br/>Used for Automated testing. <br/><br/> ***Example**: `VQE.exe -TestFrames=1000`* |
| `-W=<value>` <br/> `-Width=<value>` | Sets application main window width to the specified amount |
| `-H=<value>` <br/> `-Height=<value>` | Sets application main window height to the specified amount |
| `-ResX=<value>` | Sets application render resolution width |
| `-ResY=<value>` | Sets application render resolution height |
| `-FullScreen` | Launches in fullscreen |
| `-VSync` | Enables VSync  |
| `-TripleBuffering` | Initializes SwapChain with 3 back buffers |
| `-DoubleBuffering` | Initializes SwapChain with 2 back buffers |

<br>

VQE can be configured through `Data/EngineConfig.ini` file

| Graphics | |
| :-- | :-- |
| `ResolutionX=<value:int>` | Sets application render resolution width | 
| `ResolutionY=<value:int>` | Sets application render resolution height |
| `VSync=<value:bool>` | Toggles VSync on/off based on the specified `<value>` |

<br/>

| Engine | |
| :-- | :-- |
| `Width=<value:int>` | Sets application main window width | 
| `Height=<value:int>` | Sets application main window height |


**Note:** Command line parameters will override the `EngineSettings.ini` values.

# 3rd-Party

- [D3DX12](https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Libraries/D3DX12)