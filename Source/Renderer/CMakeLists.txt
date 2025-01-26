cmake_minimum_required (VERSION 3.4)

project (VQRenderer)

add_compile_options(/MP)
add_compile_options(/std:c++20)

# Libs
set (LibHeadersD3D12X
    "Libs/D3DX12/d3dx12.h"
)

# Core system
set(CoreHeaders
    "Core/CommandQueue.h"
    "Core/Common.h"
    "Core/Device.h"
    "Core/Fence.h"
    "Core/SwapChain.h"
)
set(CoreSources
    "Core/CommandQueue.cpp"
    "Core/Device.cpp"
    "Core/Fence.cpp"
    "Core/SwapChain.cpp"
)

# Resources management
set(ResourceHeaders
    "Resources/Buffer.h"
    "Resources/CubemapUtility.h"
    "Resources/ResourceHeaps.h"
    "Resources/ResourceViews.h"
    "Resources/Texture.h"
)
set(ResourceSources
    "Resources/Buffer.cpp"
    "Resources/CubemapUtility.cpp"
    "Resources/ResourceHeaps.cpp"
    "Resources/ResourceViews.cpp"
    "Resources/Texture.cpp"
    "Resources/Renderer_Resources.cpp"
)

# Pipeline configuration
set(PipelineHeaders
    "Pipeline/Renderer_PSOs.h"
    "Pipeline/Shader.h"
    "Pipeline/ShaderCompileUtils.h"
    "Pipeline/Tessellation.h"
)
set(PipelineSources
    "Pipeline/Renderer_PSOs.cpp"
    "Pipeline/Renderer_RootSignatures.cpp"
    "Pipeline/Shader.cpp"
    "Pipeline/ShaderCompileUtils.cpp"
)

# Rendering logic
set(RenderingHeaders
    "Rendering/WindowRenderContext.h"
    "Rendering/HDR.h"
)
set(RenderingSources
    "Rendering/WindowRenderContext.cpp"
)

# Renderer
set(Renderer
    "Renderer.h"
    "Renderer.cpp"
)

# Shaders (TODO)
set(Shaders

)

source_group("Libs\\D3D12X" FILES ${LibHeadersD3D12X})
source_group("Core" FILES ${CoreHeaders} ${CoreSources})
source_group("Resources" FILES ${ResourceHeaders} ${ResourceSources})
source_group("Pipeline" FILES ${PipelineHeaders} ${PipelineSources})
source_group("Rendering" FILES ${RenderingHeaders} ${RenderingSources})
source_group("Renderer" FILES ${Renderer})
#source_group("Shaders" FILES ${Shaders} )

set_source_files_properties(${LibHeadersD3D12X} PROPERTIES VS_TOOL_OVERRIDE "Text")
#set_source_files_properties(${Shaders} PROPERTIES VS_TOOL_OVERRIDE "Text")

# set ouput directory
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Bin)
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_SOURCE_DIR}/Bin/${OUTPUTCONFIG} )
    set( CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_SOURCE_DIR}/Bin/${OUTPUTCONFIG} )
    set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_SOURCE_DIR}/Bin/${OUTPUTCONFIG} )
endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )

# add_link_options(/SUBSYSTEM:WINDOWS)

add_library(${PROJECT_NAME} STATIC 
    ${LibHeadersD3D12X} 
    ${CoreHeaders} ${CoreSources}
    ${PipelineHeaders} ${PipelineSources}
    ${RenderingHeaders} ${RenderingSources}
    ${ResourceHeaders} ${ResourceSources}
    ${Renderer}
)

add_definitions(-DFFX_CACAO_ENABLE_D3D12)

target_link_libraries(${PROJECT_NAME} PRIVATE D3D12MA)
target_include_directories(${PROJECT_NAME} PUBLIC "../") # Engine / Renderer / Scenes
target_include_directories(${PROJECT_NAME} PUBLIC "../../") # VQE root
target_include_directories(${PROJECT_NAME} PUBLIC "./") # Renderer root