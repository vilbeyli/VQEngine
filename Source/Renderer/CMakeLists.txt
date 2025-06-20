cmake_minimum_required (VERSION 3.24)

project (VQRenderer)

add_compile_options(/MP)
add_compile_options(/std:c++20)

# Libs
set (LibHeadersD3D12X
    "Libs/D3DX12/d3dx12.h"
)
set (LibFFX_CACAO
    "Libs/AMD/CACAO/ffx_cacao_defines.h"
    "Libs/AMD/CACAO/ffx_cacao.cpp"
    "Libs/AMD/CACAO/ffx_cacao.h"
    "Libs/AMD/CACAO/ffx_cacao_impl.cpp"
    "Libs/AMD/CACAO/ffx_cacao_impl.h"
)
set (LibFFX_SSSR
    "Libs/AMD/SSSR/samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_128spp.cpp"
    "Libs/AMD/SSSR/samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_16spp.cpp"
    "Libs/AMD/SSSR/samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp.cpp"
    "Libs/AMD/SSSR/samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_256spp.cpp"
    "Libs/AMD/SSSR/samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_2spp.cpp"
    "Libs/AMD/SSSR/samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_32spp.cpp"
    "Libs/AMD/SSSR/samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_4spp.cpp"
    "Libs/AMD/SSSR/samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_64spp.cpp"
    "Libs/AMD/SSSR/samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_8spp.cpp"
)
set (LibD3D12MA
    "Libs/D3D12MA/src/D3D12MemAlloc.h"
    "Libs/D3D12MA/src/D3D12MemAlloc.cpp"
    "Libs/D3D12MA/src/Common.h"
    "Libs/D3D12MA/src/Common.cpp"
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
    "Resources/TextureManager.h"
    "Resources/DXGIUtils.h"
)
set(ResourceSources
    "Resources/Buffer.cpp"
    "Resources/CubemapUtility.cpp"
    "Resources/ResourceHeaps.cpp"
    "Resources/ResourceViews.cpp"
    "Resources/TextureManager.cpp"
    "Resources/Texture.cpp"
    "Resources/Renderer_Resources.cpp"
    "Resources/DXGIUtils.cpp"
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
    "Pipeline/RootSignatures.cpp"
    "Pipeline/Shader.cpp"
    "Pipeline/ShaderCompileUtils.cpp"
)

# Rendering logic
set(RenderingHeaders
    "Rendering/WindowRenderContext.h"
    "Rendering/RenderResources.h"
    "Rendering/DrawData.h"
    "Rendering/EnvironmentMapRendering.h"
    "Rendering/HDR.h"
)
set(RenderingSources
    "Rendering/WindowRenderContext.cpp"
    "Rendering/RenderResources.cpp"
    "Rendering/EnvironmentMapRendering.cpp"
    "Rendering/SceneRendering.cpp"
    "Rendering/LoadingScreenRendering.cpp"
    "Rendering/Batching.cpp"
)

# Render Passes
set(RenderPassHeaders
    "Rendering/RenderPass/RenderPass.h"
    "Rendering/RenderPass/AmbientOcclusion.h"
    "Rendering/RenderPass/ApplyReflections.h"
    "Rendering/RenderPass/DepthPrePass.h"
    "Rendering/RenderPass/DepthMSAAResolve.h"
    "Rendering/RenderPass/ScreenSpaceReflections.h"
    "Rendering/RenderPass/MagnifierPass.h"
    "Rendering/RenderPass/ObjectIDPass.h"
    "Rendering/RenderPass/OutlinePass.h"
)
set(RenderPassSources
    "Rendering/RenderPass/RenderPass.cpp"
    "Rendering/RenderPass/AmbientOcclusion.cpp"
    "Rendering/RenderPass/ApplyReflections.cpp"
    "Rendering/RenderPass/DepthPrePass.cpp"
    "Rendering/RenderPass/DepthMSAAResolve.cpp"
    "Rendering/RenderPass/ScreenSpaceReflections.cpp"
    "Rendering/RenderPass/MagnifierPass.cpp"
    "Rendering/RenderPass/ObjectIDPass.cpp"
    "Rendering/RenderPass/OutlinePass.cpp"
)

# Renderer
set(Renderer
    "Renderer.h"
    "Renderer.cpp"
)

# Shaders
set (Shaders
    "../../Shaders/hello-triangle.hlsl"
    "../../Shaders/hello-cube.hlsl"
    "../../Shaders/FullscreenTriangle.hlsl"
    "../../Shaders/Tonemapper.hlsl"
    "../../Shaders/Skydome.hlsl"
    "../../Shaders/Object.hlsl"
    "../../Shaders/ForwardLighting.hlsl"
    "../../Shaders/BRDF.hlsl"
    "../../Shaders/Common.hlsl"
    "../../Shaders/ShadingMath.hlsl"
    "../../Shaders/Lighting.hlsl"
    "../../Shaders/LightingConstantBufferData.h"
    "../../Shaders/Unlit.hlsl"
    "../../Shaders/ShadowDepthPass.hlsl"
    "../../Shaders/DepthPrePass.hlsl"
    "../../Shaders/DepthResolve.hlsl"
    "../../Shaders/CubemapConvolution.hlsl"
    "../../Shaders/GaussianBlur.hlsl"
    "../../Shaders/AMDFidelityFX.hlsl"
    "../../Shaders/DownsampleDepth.hlsl"
    "../../Shaders/UI.hlsl"
    "../../Shaders/UIHDRComposite.hlsl"
    "../../Shaders/HDR.hlsl"
    "../../Shaders/Visualization.hlsl"
    "../../Shaders/ApplyReflections.hlsl"
    "../../Shaders/Magnifier.hlsl"
    "../../Shaders/ObjectID.hlsl"
    "../../Shaders/Outline.hlsl"
    "../../Shaders/VertexDebug.hlsl"
    "../../Shaders/Tessellation.hlsl"
    "../../Shaders/VQPlatform.h"
)
set (FFX_CAS_Shaders
    "../../Shaders/AMD/CAS/ffx_a.h"
    "../../Shaders/AMD/CAS/ffx_cas.h"
)
set (FFX_FSR_Shaders
    "../../Shaders/AMD/FSR1.0/ffx_a.h"
    "../../Shaders/AMD/FSR1.0/ffx_fsr1.h"
)
set (FFX_SPD_Shaders
    "../../Shaders/AMD/SPD/ffx_a.h"
    "../../Shaders/AMD/SPD/ffx_spd.h"
)
set (FFX_CACAO_Shaders
    "../../Shaders/AMD/CACAO/ffx_cacao.hlsl"
)
set (FFX_SSSR_Shaders
    "../../Shaders/AMD/SSSR/ffx_sssr.h"
)
set (FFX_DNSR_Shaders
    "../../Shaders/AMD/DNSR/ffx_denoiser_reflections_common.h"
    "../../Shaders/AMD/DNSR/ffx_denoiser_reflections_config.h"
    "../../Shaders/AMD/DNSR/ffx_denoiser_reflections_prefilter.h"
    "../../Shaders/AMD/DNSR/ffx_denoiser_reflections_reproject.h"
    "../../Shaders/AMD/DNSR/ffx_denoiser_reflections_resolve_temporal.h"
    "../../Shaders/AMD/DNSR/ffx_denoiser_shadows_filter.h"
    "../../Shaders/AMD/DNSR/ffx_denoiser_shadows_prepare.h"
    "../../Shaders/AMD/DNSR/ffx_denoiser_shadows_tileclassification.h"
    "../../Shaders/AMD/DNSR/ffx_denoiser_shadows_util.h"
)
set (ShadersScreenSpaceReflections
    "../../Shaders/ScreenSpaceReflections/ClassifyReflectionTiles.hlsl"
    "../../Shaders/ScreenSpaceReflections/Intersect.hlsl"
    "../../Shaders/ScreenSpaceReflections/Prefilter.hlsl"
    "../../Shaders/ScreenSpaceReflections/PrepareBlueNoiseTexture.hlsl"
    "../../Shaders/ScreenSpaceReflections/PrepareIndirectArgs.hlsl"
    "../../Shaders/ScreenSpaceReflections/Reproject.hlsl"
    "../../Shaders/ScreenSpaceReflections/ResolveTemporal.hlsl"
    "../../Shaders/ScreenSpaceReflections/Common.hlsl"
)

source_group("Core" FILES ${CoreHeaders} ${CoreSources})
source_group("Resources" FILES ${ResourceHeaders} ${ResourceSources})
source_group("Pipeline" FILES ${PipelineHeaders} ${PipelineSources})
source_group("Rendering" FILES ${RenderingHeaders} ${RenderingSources})
source_group("Rendering\\RenderPass" FILES ${RenderPassHeaders} ${RenderPassSources})
source_group("Renderer" FILES ${Renderer})

source_group("Shaders"  FILES ${Shaders})
source_group("Shaders\\ScreenSpaceReflections"  FILES ${ShadersScreenSpaceReflections})
source_group("Shaders\\AMD-FidelityFX\\CAS"   FILES ${FFX_CAS_Shaders})
source_group("Shaders\\AMD-FidelityFX\\FSR1"  FILES ${FFX_FSR_Shaders})
source_group("Shaders\\AMD-FidelityFX\\SPD"   FILES ${FFX_SPD_Shaders})
source_group("Shaders\\AMD-FidelityFX\\CACAO" FILES ${FFX_CACAO_Shaders})
source_group("Shaders\\AMD-FidelityFX\\SSSR" FILES ${FFX_SSSR_Shaders})
source_group("Shaders\\AMD-FidelityFX\\DNSR" FILES ${FFX_DNSR_Shaders})

source_group("Libs\\AMD-FidelityFX\\CACAO" FILES ${LibFFX_CACAO})
source_group("Libs\\AMD-FidelityFX\\SSSR" FILES ${LibFFX_SSSR})
source_group("Libs\\D3D12X" FILES ${LibHeadersD3D12X})
source_group("Libs\\D3D12MA" FILES ${LibD3D12MA})

set_source_files_properties(${LibHeadersD3D12X} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${Shaders} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${ShadersScreenSpaceReflections} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${FFX_CAS_Shaders} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${FFX_FSR_Shaders} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${FFX_SPD_Shaders} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${FFX_CACAO_Shaders} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${FFX_SSSR_Shaders} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${FFX_DNSR_Shaders} PROPERTIES VS_TOOL_OVERRIDE "Text")

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
    ${LibD3D12MA}

    ${CoreHeaders} ${CoreSources}
    ${ResourceHeaders} ${ResourceSources}
    ${PipelineHeaders} ${PipelineSources}
    ${RenderingHeaders} ${RenderingSources}
    ${RenderPassHeaders} ${RenderPassSources}
    ${Renderer}
    
    ${Shaders} 
    ${ShadersScreenSpaceReflections} 
    ${FFX_FSR_Shaders} 
    ${FFX_CAS_Shaders} 
    ${FFX_CACAO_Shaders} 
    ${FFX_SSSR_Shaders} 
    ${FFX_DNSR_Shaders} 
    ${FFX_SPD_Shaders} 
)

add_definitions(-DFFX_CACAO_ENABLE_D3D12)

target_include_directories(${PROJECT_NAME} PUBLIC "../") # Engine / Renderer / Scenes
target_include_directories(${PROJECT_NAME} PUBLIC "../../") # VQE root
target_include_directories(${PROJECT_NAME} PUBLIC "./") # Renderer root

add_custom_command(TARGET ${PROJECT_NAME} PRE_BUILD
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Libs/AMD/CACAO/build_shaders_dxil.bat)
