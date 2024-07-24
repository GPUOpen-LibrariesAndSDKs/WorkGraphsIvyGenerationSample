# This file is part of the AMD Work Graph Ivy Generation Sample.
#
# Copyright (C) 2023 Advanced Micro Devices, Inc.
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files(the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions :
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

# Declare project
project(IvySample)

# ---------------------------------------------
# Import FFX config
# ---------------------------------------------
# FidelityFX uses CMAKE_HOME_DIRECTORY as base for all paths
set(CMAKE_HOME_DIRECTORY ${FFX_ROOT})
include(${FFX_ROOT}/common.cmake)
include(${FFX_ROOT}/sample.cmake)

# Set compile definitions from Cauldron/FidelityFX
set_property(DIRECTORY APPEND PROPERTY COMPILE_DEFINITIONS
	$<$<CONFIG:DebugDX12>:_DX12 _WIN>
	$<$<CONFIG:ReleaseDX12>:_DX12 _WIN _RELEASE>
	$<$<CONFIG:RelWithDebInfoDX12>:_DX12 _WIN _RELEASE>
	FFX_API_CAULDRON
	NOMINMAX
)

# Output exe to bin directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BIN_OUTPUT})
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${BIN_OUTPUT} )
endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )

# ---------------------------------------------
# Sample render module
# ---------------------------------------------

file(GLOB ivysample_src
	${CMAKE_CURRENT_SOURCE_DIR}/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file(GLOB ivysample_shaders
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.hlsl)
set_source_files_properties(${ivysample_shaders} PROPERTIES VS_TOOL_OVERRIDE "Text")
copyCommand("${ivysample_shaders}" ${SHADER_OUTPUT})

# Add config file
set(config_file ${CMAKE_CURRENT_SOURCE_DIR}/config/ivysampleconfig.json)
copyCommand("${config_file}" ${CONFIG_OUTPUT})

# Add the sample to the solution
add_executable(${PROJECT_NAME} WIN32 ${default_icon_src} ${config_file} ${ivysample_src} ${ivysample_shaders} ${ffx_remap})

# Setup the correct exe based on backend name
set(EXE_OUT_NAME ${PROJECT_NAME}_)

# Link everything (including the compiler for now)
target_link_libraries(${PROJECT_NAME} LINK_PUBLIC Framework RenderModules d3dcompiler)
set_target_properties(${PROJECT_NAME} PROPERTIES
					OUTPUT_NAME_DEBUGDX12 "${EXE_OUT_NAME}DX12D"
					OUTPUT_NAME_DEBUGVK "${EXE_OUT_NAME}VKD"
					OUTPUT_NAME_RELEASEDX12 "${EXE_OUT_NAME}DX12"
					OUTPUT_NAME_RELEASEVK "${EXE_OUT_NAME}VK"
					VS_DEBUGGER_WORKING_DIRECTORY "${BIN_OUTPUT}")

# Add manifest info
addManifest(${PROJECT_NAME})

# Add dependency information
add_dependencies(${PROJECT_NAME} Framework)
add_dependencies(${PROJECT_NAME} RenderModules)

# And solution layout definitions
source_group(""					FILES ${ffx_remap})
source_group("Icon"    			FILES ${default_icon_src})
source_group("Config" 			FILES ${config_file})
source_group("Sample"			FILES ${ivysample_src})
source_group("Sample\\Shaders"	FILES ${ivysample_shaders})
