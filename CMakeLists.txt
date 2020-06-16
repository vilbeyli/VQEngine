# https://cmake.org/examples/

# CMakeLists files in this project can
# refer to the root source directory of the project as ${HELLO_SOURCE_DIR} and
# to the root binary directory of the project as ${HELLO_BINARY_DIR}.
cmake_minimum_required (VERSION 3.4)
set(CMAKE_GENERATOR_PLATFORM x64)

project (VQE)

add_compile_options(/MP)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

# ouput exe to bin directory
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_HOME_DIRECTORY}/Bin)
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_HOME_DIRECTORY}/Bin/${OUTPUTCONFIG} )
endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )

set (Config
    "Data/EngineSettings.ini"
)

set (HeaderVQE 
    "Source/Application/Platform.h"
    "Source/Application/Window.h"
    "Source/Application/Settings.h"
    "Source/Application/Types.h"
    "Source/Application/VQEngine.h"
    "Source/Application/ThreadPool.h"
)

set (SourceVQE 
    "Source/Application/Platform.cpp"
    "Source/Application/Window.cpp"
    "Source/Application/Main.cpp"
    "Source/Application/VQEngine_Main.cpp"
    "Source/Application/VQEngine_Render.cpp"
    "Source/Application/VQEngine_Update.cpp"
    "Source/Application/VQEngine_WindowEvents.cpp"
    "Source/Application/ThreadPool.cpp"
)

add_link_options(/SUBSYSTEM:WINDOWS)

add_subdirectory(Libs/VQUtils)
add_subdirectory(Libs/D3D12MA)
add_subdirectory(Source/Renderer)

source_group("Config" FILES ${Config})

set_source_files_properties(${Config} PROPERTIES VS_TOOL_OVERRIDE "Text")

link_directories(${CMAKE_CURRENT_SOURCE_DIR}/Libs/VQUtils/Bin/)

# Create a library with the project name that is build with the Headers and Source files
add_executable( ${PROJECT_NAME} ${HeaderVQE} ${SourceVQE} ${Config})

set_target_properties(${PROJECT_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_HOME_DIRECTORY}/Bin/ )

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Data ${CMAKE_SOURCE_DIR}/Bin/Data)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Data $<TARGET_FILE_DIR:${PROJECT_NAME}>/Data)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Source/Shaders $<TARGET_FILE_DIR:${PROJECT_NAME}>/Shaders)

# TODO: the above commands must run even if the project is up to date which is not the case now.
#       if Data/EngineSettings.ini is updated, the copy in the target destination of the commands above
#       is not updated.

# Make sure the compiler can find include files for the libraries
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${Includes})


target_link_libraries(${PROJECT_NAME} PRIVATE VQUtils VQRenderer )
