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

set (Config
    "Data/EngineSettings.ini"
    "Data/RendererSettings.ini"
    "Data/HDRDisplayProfiles.ini"
    "Data/EnvironmentMaps.ini"
)

set (Shaders
    "Source/Shaders/hello-triangle.hlsl"
    "Source/Shaders/hello-cube.hlsl"
    "Source/Shaders/FullscreenTriangle.hlsl"
    "Source/Shaders/Tonemapper.hlsl"
    "Source/Shaders/Skydome.hlsl"
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
    "Source/Application/Geometry.h"
    "Source/Application/Transform.h"
    "Source/Application/Quaternion.h"
    "Source/Application/Camera.h"
    "Source/Application/Input.h"
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
    "Source/Application/Events.cpp"
    "Source/Application/Mesh.cpp"
    "Source/Application/Geometry.cpp"
    "Source/Application/Transform.cpp"
    "Source/Application/Math.cpp"
    "Source/Application/Quaternion.cpp"
    "Source/Application/Camera.cpp"
    "Source/Application/Input.cpp"
)

# ouput exe to bin directory
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_HOME_DIRECTORY}/Bin/${OUTPUTCONFIG} )
endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )

add_link_options(/SUBSYSTEM:WINDOWS)

add_subdirectory(Libs/VQUtils)
add_subdirectory(Libs/D3D12MA)
add_subdirectory(Source/Renderer)

source_group("Config"   FILES ${Config})
source_group("Resource" FILES ${Resource})
source_group("Icons"    FILES ${Icons})
source_group("Shaders"  FILES ${Shaders})

set_source_files_properties(${Config} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${Shaders} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(Data/Resources/VQE.rc PROPERTIES VS_TOOL_OVERRIDE "Resource compiler")
set_source_files_properties(Data/Icons/VQE32.ico  PROPERTIES VS_TOOL_OVERRIDE "Image")

link_directories(${CMAKE_CURRENT_SOURCE_DIR}/Libs/VQUtils/Bin/)

# Create a library with the project name that is build with the Headers and Source files
add_executable( ${PROJECT_NAME} ${HeaderVQE} ${SourceVQE} ${Config} ${Resource} ${Shaders} )

set_target_properties(${PROJECT_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_HOME_DIRECTORY} )

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)

# Make sure the compiler can find include files for the libraries
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${Includes})


target_link_libraries(${PROJECT_NAME} PRIVATE VQUtils VQRenderer )
