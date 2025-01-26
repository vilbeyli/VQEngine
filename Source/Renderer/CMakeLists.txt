cmake_minimum_required (VERSION 3.4)

project (VQRenderer)

add_compile_options(/MP)
add_compile_options(/std:c++20)

# Libs
set (LibHeadersD3D12X
    "Libs/D3DX12/d3dx12.h"
)
set (LibFFX_CACAO
    "Libs/AMDFidelityFX/CACAO/ffx_cacao_defines.h"
    "Libs/AMDFidelityFX/CACAO/ffx_cacao.cpp"
    "Libs/AMDFidelityFX/CACAO/ffx_cacao.h"
    "Libs/AMDFidelityFX/CACAO/ffx_cacao_impl.cpp"
    "Libs/AMDFidelityFX/CACAO/ffx_cacao_impl.h"
)
set (LibFFX_SSSR
    "Libs/AMDFidelityFX/SSSR/samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_128spp.cpp"
    "Libs/AMDFidelityFX/SSSR/samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_16spp.cpp"
    "Libs/AMDFidelityFX/SSSR/samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp.cpp"
    "Libs/AMDFidelityFX/SSSR/samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_256spp.cpp"
    "Libs/AMDFidelityFX/SSSR/samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_2spp.cpp"
    "Libs/AMDFidelityFX/SSSR/samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_32spp.cpp"
    "Libs/AMDFidelityFX/SSSR/samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_4spp.cpp"
    "Libs/AMDFidelityFX/SSSR/samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_64spp.cpp"
    "Libs/AMDFidelityFX/SSSR/samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_8spp.cpp"
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
    "Pipeline/PipelineStateObjects.h"
    "Pipeline/Shader.h"
    "Pipeline/ShaderCompileUtils.h"
    "Pipeline/Tessellation.h"
)
set(PipelineSources
    "Pipeline/PipelineStateObjects.cpp"
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
source_group("Core" FILES ${CoreHeaders} ${CoreSources})
source_group("Resources" FILES ${ResourceHeaders} ${ResourceSources})
source_group("Pipeline" FILES ${PipelineHeaders} ${PipelineSources})
source_group("Rendering" FILES ${RenderingHeaders} ${RenderingSources})
source_group("Renderer" FILES ${Renderer})
#source_group("Shaders" FILES ${Shaders} )

source_group("Libs\\AMD-FidelityFX\\CACAO" FILES ${LibFFX_CACAO})
source_group("Libs\\AMD-FidelityFX\\SSSR" FILES ${LibFFX_SSSR})
source_group("Libs\\D3D12X" FILES ${LibHeadersD3D12X})

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
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/Libs/DirectXCompiler/lib/x64)

add_library(${PROJECT_NAME} STATIC 
    ${LibHeadersD3D12X} 
    ${LibFFX_CACAO} 
    ${LibFFX_SSSR} 
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

add_custom_command(TARGET ${PROJECT_NAME} PRE_BUILD
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Libs/AMDFidelityFX/CACAO/build_shaders_dxil.bat)
