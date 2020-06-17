cmake_minimum_required (VERSION 3.4)
set(CMAKE_GENERATOR_PLATFORM x64)

project (VQRenderer)

add_compile_options(/MP)
add_compile_options(/std:c++17)

set (Headers
    "Renderer.h"
    "Device.h"
    "SwapChain.h"
    "CommandQueue.h"
    "Fence.h"
    "ResourceHeaps.h"
    "Buffer.h"
    "Common.h"
)

set (Source
    "Renderer.cpp"
    "Device.cpp"
    "SwapChain.cpp"
    "CommandQueue.cpp"
    "Fence.cpp"
    "ResourceHeaps.cpp"
    "Buffer.cpp"
)

# set ouput directory
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Bin)
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_SOURCE_DIR}/Bin/${OUTPUTCONFIG} )
    set( CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_SOURCE_DIR}/Bin/${OUTPUTCONFIG} )
    set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_SOURCE_DIR}/Bin/${OUTPUTCONFIG} )
endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )

# add_link_options(/SUBSYSTEM:WINDOWS)

add_library(${PROJECT_NAME} STATIC ${Headers} ${Source} )

target_link_libraries(${PROJECT_NAME} PRIVATE D3D12MA )