# PAD Cutin - armor break cut-in camera for Procedural Armor Damage
# Camera work adapted from Kagura Style Armor Break 1.2.0 (C) 2026 miyata0105
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.21)
project(PADCutin VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

set(PADCUTIN_COMMONLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE-NG" CACHE PATH "CommonLibSSE-NG source tree (v9.3.0)")
if(NOT EXISTS "${PADCUTIN_COMMONLIB_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "CommonLibSSE-NG not found at ${PADCUTIN_COMMONLIB_DIR}")
endif()

if(MSVC)
    add_link_options("/PDBALTPATH:%_PDB%")
endif()

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${PADCUTIN_COMMONLIB_DIR}" commonlib EXCLUDE_FROM_ALL)
if(NOT COMMAND add_commonlibsse_plugin)
    include("${PADCUTIN_COMMONLIB_DIR}/cmake/CommonLibSSE.cmake")
endif()

add_commonlibsse_plugin(${PROJECT_NAME} NAME "PADCutin" AUTHOR "Kaku" VERSION ${PROJECT_VERSION} SOURCES src/main.cpp)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_options(${PROJECT_NAME} PRIVATE /Zc:preprocessor /permissive- /EHsc /utf-8 /W3)
target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE PADCUTIN_VERSION_STRING="${PROJECT_VERSION}")
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
