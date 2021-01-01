# https://cmake.org/examples/

# CMakeLists files in this project can
# refer to the root source directory of the project as ${HELLO_SOURCE_DIR} and
# to the root binary directory of the project as ${HELLO_BINARY_DIR}.
cmake_minimum_required (VERSION 3.4)

project (VQE)

add_compile_options(/MP)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

set (Resource 
    "Data/Resources/VQE.rc"
    "Data/Resources/resource.h"
    "Data/Icons/VQE32.ico"
)

set (Scenes 
    "Data/Levels/Default.xml"
    "Data/Levels/Sponza.xml"
    "Data/Levels/GeometryUnitTest.xml"
    "Data/Levels/StressTest.xml"
    "Source/Scenes/Scenes.h"
    "Source/Scenes/DefaultScene.cpp"
    "Source/Scenes/SponzaScene.cpp"
    "Source/Scenes/GeometryUnitTestScene.cpp"
    "Source/Scenes/StressTestScene.cpp"
)

set (Config
    "Data/EngineSettings.ini"
    "Data/RendererSettings.ini"
    "Data/HDRDisplayProfiles.ini"
    "Data/EnvironmentMaps.ini"
    "Data/Scenes.ini"
)

set (Materials
    "Data/Materials/RoughMaterials.xml"
    "Data/Materials/PBRMaterials.xml"
    "Data/Materials/MetallicMaterials.xml"
    "Data/Materials/DefaultMaterials.xml"
)

set (Shaders
    "Shaders/hello-triangle.hlsl"
    "Shaders/hello-cube.hlsl"
    "Shaders/FullscreenTriangle.hlsl"
    "Shaders/Tonemapper.hlsl"
    "Shaders/Skydome.hlsl"
    "Shaders/Object.hlsl"
    "Shaders/ForwardLighting.hlsl"
    "Shaders/BRDF.hlsl"
    "Shaders/Common.hlsl"
    "Shaders/ShadingMath.hlsl"
    "Shaders/Lighting.hlsl"
    "Shaders/LightingConstantBufferData.h"
    "Shaders/Unlit.hlsl"
    "Shaders/DepthPass.hlsl"
    "Shaders/CubemapConvolution.hlsl"
    "Shaders/GaussianBlur.hlsl"
    "Shaders/AMDFidelityFX.hlsl"
    "Shaders/FidelityFX/ffx_a.h"
)

set (HeaderVQE 
    "Source/Application/Platform.h"
    "Source/Application/Window.h"
    "Source/Application/Settings.h"
    "Source/Application/Types.h"
    "Source/Application/Math.h"
    "Source/Application/VQEngine.h"
    "Source/Application/Events.h"
    "Source/Application/Mesh.h"
    "Source/Application/Material.h"
    "Source/Application/Model.h"
    "Source/Application/Geometry.h"
    "Source/Application/Transform.h"
    "Source/Application/Quaternion.h"
    "Source/Application/Camera.h"
    "Source/Application/Input.h"
    "Source/Application/AssetLoader.h"
    "Source/Application/Scene.h"
    "Source/Application/Light.h"
    "Source/Application/GameObject.h"
    "Source/Application/Memory.h"
    "Source/Application/GPUMarker.h"
    "Shaders/LightingConstantBufferData.h"
)

set (SourceVQE 
    "Source/Application/Platform.cpp"
    "Source/Application/Window.cpp"
    "Source/Application/Main.cpp"
    "Source/Application/VQEngine_Main.cpp"
    "Source/Application/VQEngine_Render.cpp"
    "Source/Application/VQEngine_Update.cpp"
    "Source/Application/VQEngine_WindowEvents.cpp"
    "Source/Application/VQEngine_EventHandlers.cpp"
    "Source/Application/EnvironmentMap.cpp"
    "Source/Application/FileParser.cpp"
    "Source/Application/Events.cpp"
    "Source/Application/Mesh.cpp"
    "Source/Application/Material.cpp"
    "Source/Application/Model.cpp"
    "Source/Application/Geometry.cpp"
    "Source/Application/Transform.cpp"
    "Source/Application/Math.cpp"
    "Source/Application/Quaternion.cpp"
    "Source/Application/Camera.cpp"
    "Source/Application/Input.cpp"
    "Source/Application/AssetLoader.cpp"
    "Source/Application/Scene.cpp"
    "Source/Application/Light.cpp"
    "Source/Application/GameObject.cpp"
    "Source/Application/Memory.cpp"
    "Source/Application/GPUMarker.cpp"
)

set (FFX
    "Shaders/FidelityFX/ffx_a.h"
    "Shaders/FidelityFX/CAS/ffx_cas.h"
    "Shaders/FidelityFX/SPD/ffx_spd.h"
)

set (PIXIncl 
    "Libs/WinPixEventRuntime/Include"
)

# ouput exe to bin directory
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_HOME_DIRECTORY}/Bin/${OUTPUTCONFIG} )
endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )

add_link_options(/SUBSYSTEM:WINDOWS)

# add submodules
add_subdirectory(Libs/VQUtils)
add_subdirectory(Libs/D3D12MA)
add_subdirectory(Source/Renderer)

#add_definitions(
#    -DASSIMP_BUILD_ASSIMP_TOOLS=OFF 
#    -DASSIMP_NO_EXPORT=ON 
#    -DBUILD_SHARED_LIBS=OFF 
#    -DASSIMP_BUILD_TESTS=OFF 
#    -DASSIMP_INSTALL=OFF
#)
add_subdirectory(Libs/assimp)

source_group("Config"   FILES ${Config})
source_group("Resource" FILES ${Resource})
source_group("Icons"    FILES ${Icons})
source_group("Shaders"  FILES ${Shaders})
source_group("AMD-FidelityFX"  FILES ${FFX})
source_group("Scenes"   FILES ${Scenes})
source_group("Materials" FILES ${Materials})

set_source_files_properties(${Config} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${Materials} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${Scenes} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${Shaders} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(Data/Resources/VQE.rc PROPERTIES VS_TOOL_OVERRIDE "Resource compiler")
set_source_files_properties(Data/Icons/VQE32.ico  PROPERTIES VS_TOOL_OVERRIDE "Image")

link_directories(${CMAKE_CURRENT_SOURCE_DIR}/Libs/VQUtils/Bin/)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/Libs/WinPixEventRuntime/bin/x64)

# Create a library with the project name that is build with the Headers and Source files
add_executable( ${PROJECT_NAME} ${HeaderVQE} ${SourceVQE} ${Config} ${Scenes} ${Resource} ${Shaders} ${Materials})

set_target_properties(${PROJECT_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_HOME_DIRECTORY} )


# set target executable name for debug/releasewithdebug builds, release is VQE.exe
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    if (${OUTPUTCONFIG} MATCHES "DEBUG")
        set_target_properties(${PROJECT_NAME} PROPERTIES ${OUTPUTCONFIG}_OUTPUT_NAME "VQE-d")
    elseif (${OUTPUTCONFIG} MATCHES "RELWITHDEBINFO")
        set_target_properties(${PROJECT_NAME} PROPERTIES ${OUTPUTCONFIG}_OUTPUT_NAME "VQE-rwd")
        add_compile_definitions(PROFILE_BUILD)
    endif()
endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set_target_properties(assimp PROPERTIES FOLDER Libs/assimp)
set_target_properties(IrrXML PROPERTIES FOLDER Libs/assimp)
set_target_properties(uninstall PROPERTIES FOLDER Libs/assimp)
set_target_properties(UpdateAssimpLibsDebugSymbolsAndDLLs PROPERTIES FOLDER Libs/assimp)
set_target_properties(zlibstatic PROPERTIES FOLDER Libs/assimp)

#set_target_properties(VQRenderer PROPERTIES FOLDER Libs)
#set_target_properties(VQUtils PROPERTIES FOLDER Libs)
set_target_properties(D3D12MA PROPERTIES FOLDER Libs)

# Make sure the compiler can find include files for the libraries
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${Includes})
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PIXIncl})

target_link_libraries(${PROJECT_NAME} PRIVATE VQUtils VQRenderer assimp WinPixEventRuntime)


add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/Libs/WinPixEventRuntime/bin/x64/WinPixEventRuntime.dll"
    $<TARGET_FILE_DIR:${PROJECT_NAME}>
)
