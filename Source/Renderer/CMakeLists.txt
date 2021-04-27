cmake_minimum_required (VERSION 3.4)

project (VQRenderer)

add_compile_options(/MP)
add_compile_options(/std:c++17)

set (LibHeadersD3D12X
    "Libs/D3DX12/d3dx12.h"
)

set (Headers
    "Renderer.h"
    "Device.h"
    "SwapChain.h"
    "CommandQueue.h"
    "Fence.h"
    "ResourceHeaps.h"
    "ResourceViews.h"
    "Buffer.h"
    "Common.h"
    "Texture.h"
    "HDR.h"
    "Shader.h"
)

set (Source
    "Renderer.cpp"
    "Renderer_Resources.cpp"
    "Device.cpp"
    "SwapChain.cpp"
    "CommandQueue.cpp"
    "Fence.cpp"
    "ResourceHeaps.cpp"
    "ResourceViews.cpp"
    "Buffer.cpp"
    "Texture.cpp"
    "Shader.cpp"
)


source_group("Libs\\D3D12X"              FILES ${LibHeadersD3D12X})
set_source_files_properties(${LibHeadersD3D12X} PROPERTIES VS_TOOL_OVERRIDE "Text")

# set ouput directory
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Bin)
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_SOURCE_DIR}/Bin/${OUTPUTCONFIG} )
    set( CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_SOURCE_DIR}/Bin/${OUTPUTCONFIG} )
    set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_SOURCE_DIR}/Bin/${OUTPUTCONFIG} )
endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )

# add_link_options(/SUBSYSTEM:WINDOWS)

add_library(${PROJECT_NAME} STATIC ${Headers} ${Source} ${LibHeadersD3D12X} )
add_definitions(-DFFX_CACAO_ENABLE_D3D12)

target_link_libraries(${PROJECT_NAME} PRIVATE D3D12MA )