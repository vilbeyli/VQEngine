# https://cmake.org/examples/

# CMakeLists files in this project can
# refer to the root source directory of the project as ${HELLO_SOURCE_DIR} and
# to the root binary directory of the project as ${HELLO_BINARY_DIR}.
cmake_minimum_required (VERSION 3.4)

project (VQE)

add_compile_options(/MP)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

#
# DATA
#
set (Resource 
    "Data/Resources/VQE.rc"
    "Data/Resources/resource.h"
    "Data/Icons/VQE32.ico"
)

set (Scenes 
    "Data/Levels/Default.xml"
    "Data/Levels/Sponza.xml"
    "Data/Levels/EnvironmentMapUnitTest.xml"
    "Data/Levels/StressTest.xml"
    "Data/Levels/Terrain.xml"
    "Source/Scenes/Scenes.h"
    "Source/Scenes/DefaultScene.cpp"
    "Source/Scenes/SponzaScene.cpp"
    "Source/Scenes/EnvironmentMapUnitTestScene.cpp"
    "Source/Scenes/StressTestScene.cpp"
    "Source/Scenes/TerrainScene.cpp"
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

#
# SOURCE
#

add_definitions(-DFFX_CACAO_ENABLE_D3D12) # TODO: move render passes to renderer project

set (CoreFiles 
    "Source/Engine/Core/Platform.h"
    "Source/Engine/Core/Window.h"
    "Source/Engine/Core/Input.h"
    "Source/Engine/Core/Events.h"
    "Source/Engine/Core/Types.h"
    "Source/Engine/Core/FileParser.h"
    "Source/Engine/Core/Memory.h"
    "Libs/imgui/backends/imgui_impl_win32.h"

    "Source/Engine/Core/Platform.cpp"
    "Source/Engine/Core/Window.cpp"
    "Source/Engine/Core/Input.cpp"
    "Source/Engine/Core/Events.cpp"
    "Source/Engine/Core/VQEngine_WindowEvents.cpp"
    "Source/Engine/Core/VQEngine_EventHandlers.cpp"
    "Source/Engine/Core/FileParser.cpp"
    "Source/Engine/Core/Memory.cpp"
    "Libs/imgui/backends/imgui_impl_win32.cpp"
)

set (SceneFiles   
    "Shaders/LightingConstantBufferData.h"
    
    "Source/Engine/Scene/Transform.h"
    "Source/Engine/Scene/Quaternion.h"
    "Source/Engine/Scene/Scene.h"
    "Source/Engine/Scene/SceneBoundingBoxHierarchy.h"
    "Source/Engine/Scene/SceneViews.h"
    "Source/Engine/Scene/Light.h"
    "Source/Engine/Scene/Camera.h"
    "Source/Engine/Scene/Mesh.h"
    "Source/Engine/Scene/MeshGenerator.h"
    "Source/Engine/Scene/MeshGeometryData.h"
    "Source/Engine/Scene/Material.h"
    "Source/Engine/Scene/Model.h"
    "Source/Engine/Scene/GameObject.h"
    "Source/Engine/Scene/Serialization.h"

    "Source/Engine/Scene/Scene.cpp"
    "Source/Engine/Scene/SceneLoading.cpp"
    "Source/Engine/Scene/Light.cpp"
    "Source/Engine/Scene/Camera.cpp"
    "Source/Engine/Scene/Mesh.cpp"
    "Source/Engine/Scene/Material.cpp"
    "Source/Engine/Scene/Model.cpp"
    "Source/Engine/Scene/GameObject.cpp"
    "Source/Engine/Scene/Transform.cpp"
    "Source/Engine/Scene/Quaternion.cpp"
)

set (PostProcessFiles
    "Source/Engine/PostProcess/PostProcess.h"
    "Source/Engine/PostProcess/PostProcess.cpp"
)

set (UIFiles
    "Source/Engine/UI/VQUI.cpp"
    "Source/Engine/UI/VQUI.h"
)

set (SourceVQE 
    "Source/Engine/VQEngine.h"
    "Source/Engine/Settings.h"
    "Source/Engine/Math.h"
    "Source/Engine/Culling.h"
    "Source/Engine/MeshSorting.h"
    "Source/Engine/CullingData.h"
    "Source/Engine/AssetLoader.h"
    "Source/Engine/GPUMarker.h"
    "Source/Engine/EnvironmentMap.h"
    "Source/Engine/LoadingScreen.h"

    "Source/Engine/Main.cpp"
    "Source/Engine/VQEngine_Main.cpp"
    "Source/Engine/VQEngine_Render.cpp"
    "Source/Engine/VQEngine_Update.cpp"
    "Source/Engine/VQEngine_Input.cpp"
    "Source/Engine/VQEngine_Simulation.cpp"
    "Source/Engine/EnvironmentMap.cpp"
    "Source/Engine/LoadingScreen.cpp"
    "Source/Engine/Math.cpp"
    "Source/Engine/Culling.cpp"
    "Source/Engine/AssetLoader.cpp"
    "Source/Engine/GPUMarker.cpp"
)

set (LibsIncl
    "Libs/"
)


# ouput exe to bin directory
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_HOME_DIRECTORY}/Bin/${OUTPUTCONFIG} )
endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )

add_link_options(/SUBSYSTEM:WINDOWS)

# add submodules
add_subdirectory(Libs/VQUtils)
add_subdirectory(Source/Renderer)

#add_definitions(
#    -DASSIMP_BUILD_ASSIMP_TOOLS=OFF 
#    -DASSIMP_NO_EXPORT=ON 
#    -DBUILD_SHARED_LIBS=OFF 
#    -DASSIMP_BUILD_TESTS=OFF 
#    -DASSIMP_INSTALL=OFF
#)
#add_definitions(-DNOMINMAX)
add_subdirectory(Libs/assimp)
add_subdirectory(Libs/imgui)

source_group("Config"   FILES ${Config})
source_group("Resource" FILES ${Resource})
source_group("Icons"    FILES ${Icons})
source_group("Scenes"   FILES ${Scenes})
source_group("Materials" FILES ${Materials})
source_group("Source\\RenderPass" FILES ${RenderPassFiles})
source_group("Source\\Core" FILES ${CoreFiles})
source_group("Source\\Scene" FILES ${SceneFiles})
source_group("Source\\UI" FILES ${UIFiles})
source_group("Source\\PostProcess" FILES ${PostProcessFiles})
source_group("Source" FILES ${SourceVQE})

set_source_files_properties(${Config} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${Materials} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${Scenes} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${CoreFiles} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${RenderPassFiles} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${SceneFiles} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${UIFiles} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${PostProcessFiles} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(Data/Resources/VQE.rc PROPERTIES VS_TOOL_OVERRIDE "Resource compiler")
set_source_files_properties(Data/Icons/VQE32.ico  PROPERTIES VS_TOOL_OVERRIDE "Image")

link_directories(${CMAKE_CURRENT_SOURCE_DIR}/Libs/VQUtils/Bin/)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/Libs/WinPixEventRuntime/bin/x64)

# Create a library with the project name that is build with the Headers and Source files
add_executable( ${PROJECT_NAME} 
    ${SourceVQE} 
    ${Config} 
    ${Scenes} 
    ${Resource} 
    ${Materials} 
    ${RenderPassFiles} 
    ${CoreFiles} 
    ${UIFiles} 
    ${PostProcessFiles} 
    ${SceneFiles}
)

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

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set_target_properties(assimp PROPERTIES FOLDER Libs/assimp)
set_target_properties(IrrXML PROPERTIES FOLDER Libs/assimp)
set_target_properties(uninstall PROPERTIES FOLDER Libs/assimp)
set_target_properties(UpdateAssimpLibsDebugSymbolsAndDLLs PROPERTIES FOLDER Libs/assimp)
set_target_properties(zlibstatic PROPERTIES FOLDER Libs/assimp)

#set_target_properties(VQRenderer PROPERTIES FOLDER Libs)
#set_target_properties(VQUtils PROPERTIES FOLDER Libs)
set_target_properties(ImGUI PROPERTIES FOLDER Libs)

# Make sure the compiler can find include files for the libraries
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${Includes})
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${LibsIncl})

target_link_libraries(${PROJECT_NAME} PRIVATE VQUtils VQRenderer assimp WinPixEventRuntime ImGUI)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/Libs/WinPixEventRuntime/bin/x64/WinPixEventRuntime.dll"
    $<TARGET_FILE_DIR:${PROJECT_NAME}>
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Renderer/Libs/DirectXCompiler/bin/x64/dxcompiler.dll"
    $<TARGET_FILE_DIR:${PROJECT_NAME}>
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Renderer/Libs/DirectXCompiler/bin/x64/dxil.dll"
    $<TARGET_FILE_DIR:${PROJECT_NAME}>
)