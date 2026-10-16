# Copyright 2026 Fenris Creations
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Builds the runtime plug-in as a static library for games that build with CMake, and on
# Windows optionally the authoring plug-in. The premake scripts in wwise/ remain the build
# for the Wwise Launcher bundle.

cmake_minimum_required(VERSION 3.16)

file(STRINGS wwise/SpatialClusteringVersion.h SPATIALCLUSTERING_VERSION_LINE
    REGEX "#define SPATIALCLUSTERING_VERSION_STRING")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" SPATIALCLUSTERING_VERSION "${SPATIALCLUSTERING_VERSION_LINE}")

project(SpatialClustering VERSION ${SPATIALCLUSTERING_VERSION} LANGUAGES CXX)

option(SPATIALCLUSTERING_BUILD_AUTHORING "Build the Wwise authoring plug-in (Windows only)" OFF)
option(SPATIALCLUSTERING_BUILD_SHARED "Build the runtime plug-in as a dynamic library that the sound engine loads at run time" OFF)

# A game that adds the plug-in with add_subdirectory links it into its own binaries, so by
# default only a standalone build installs it.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(SPATIALCLUSTERING_STANDALONE ON)
else()
    set(SPATIALCLUSTERING_STANDALONE OFF)
endif()
option(SPATIALCLUSTERING_INSTALL "Generate the install rules" ${SPATIALCLUSTERING_STANDALONE})

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# The Wwise SDK comes from a Wwise CMake package (such as the wwise vcpkg port) when one is
# found, and otherwise from the SDK folder of a Wwise install.
if(NOT TARGET Wwise::Wwise)
    find_package(Wwise CONFIG QUIET)
endif()
if(NOT TARGET Wwise::Wwise)
    set(WWISE_SDK_DIR "$ENV{WWISESDK}" CACHE PATH "SDK folder of a Wwise install, the one that contains include/AK")
    if(NOT EXISTS "${WWISE_SDK_DIR}/include/AK/AkWwiseSDKVersion.h")
        message(FATAL_ERROR "Wwise SDK not found. Set WWISE_SDK_DIR, or the WWISESDK environment variable, to the SDK folder of a Wwise install.")
    endif()
    add_library(Wwise::Wwise INTERFACE IMPORTED)
    set_target_properties(Wwise::Wwise PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${WWISE_SDK_DIR}/include"
        INTERFACE_COMPILE_DEFINITIONS "$<$<CONFIG:Release>:AK_OPTIMIZED>")
endif()

set(SPATIALCLUSTERING_RUNTIME_SOURCES
    core/src/ClusterEngine.cpp
    core/src/ClusterEngine.h
    wwise/SoundEnginePlugin/SpatialClusteringFX.cpp
    wwise/SoundEnginePlugin/SpatialClusteringFX.h
    wwise/SoundEnginePlugin/SpatialClusteringFXParams.cpp
    wwise/SoundEnginePlugin/SpatialClusteringFXParams.h
    wwise/SpatialClusteringConfig.h)

# Games include the factory header as <AK/Plugin/SpatialClusteringFXFactory.h>, like the
# Audiokinetic plug-ins.
set(SPATIALCLUSTERING_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/include)
configure_file(wwise/SoundEnginePlugin/SpatialClusteringFXFactory.h
    ${SPATIALCLUSTERING_INCLUDE_DIR}/AK/Plugin/SpatialClusteringFXFactory.h COPYONLY)

# Runtime plug-in. It takes AK_OPTIMIZED from Wwise::Wwise, so it matches the sound engine
# libraries of the configuration. Games link Wwise themselves, so it is not part of the
# installed interface.
add_library(SpatialClusteringFX STATIC ${SPATIALCLUSTERING_RUNTIME_SOURCES})
add_library(SpatialClustering::SpatialClusteringFX ALIAS SpatialClusteringFX)
target_include_directories(SpatialClusteringFX
    PUBLIC
        $<BUILD_INTERFACE:${SPATIALCLUSTERING_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        core/src)
target_link_libraries(SpatialClusteringFX PRIVATE $<BUILD_INTERFACE:Wwise::Wwise>)
target_compile_features(SpatialClusteringFX PRIVATE cxx_std_17)
target_compile_options(SpatialClusteringFX PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

if(SPATIALCLUSTERING_INSTALL)
    install(TARGETS SpatialClusteringFX EXPORT SpatialClusteringTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime)
    install(FILES ${SPATIALCLUSTERING_INCLUDE_DIR}/AK/Plugin/SpatialClusteringFXFactory.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/AK/Plugin COMPONENT Runtime)

    set(SPATIALCLUSTERING_CONFIG_DIR ${CMAKE_INSTALL_DATADIR}/SpatialClustering)
    install(EXPORT SpatialClusteringTargets
        NAMESPACE SpatialClustering::
        DESTINATION ${SPATIALCLUSTERING_CONFIG_DIR}
        COMPONENT Runtime)
    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/SpatialClusteringConfigVersion.cmake
        COMPATIBILITY SameMajorVersion)
    install(FILES
            cmake/SpatialClusteringConfig.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/SpatialClusteringConfigVersion.cmake
        DESTINATION ${SPATIALCLUSTERING_CONFIG_DIR}
        COMPONENT Runtime)
endif()

# Runtime plug-in as a dynamic library. The sound engine loads it from next to the executable,
# or from AkInitSettings::szPluginDLLPath, when a SoundBank needs it and it is not linked in.
# Release builds it for the Release sound engine and RelWithDebInfo for the Profile one.
if(SPATIALCLUSTERING_BUILD_SHARED)
    add_library(SpatialClusteringShared SHARED
        wwise/SoundEnginePlugin/SpatialClusteringFXShared.cpp
        wwise/SoundEnginePlugin/SpatialClusteringFXFactory.h)
    set_target_properties(SpatialClusteringShared PROPERTIES OUTPUT_NAME SpatialClustering)
    target_link_libraries(SpatialClusteringShared PRIVATE SpatialClusteringFX Wwise::Wwise)
    target_compile_features(SpatialClusteringShared PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(SpatialClusteringFX PRIVATE /Z7)
        target_compile_options(SpatialClusteringShared PRIVATE /utf-8 /Z7)
        target_link_options(SpatialClusteringShared PRIVATE /DEBUG /INCREMENTAL:NO /OPT:REF /OPT:ICF)
    endif()

    if(SPATIALCLUSTERING_INSTALL)
        # The library only, without the import library that nothing links against.
        if(WIN32)
            set(SPATIALCLUSTERING_SHARED_DIR ${CMAKE_INSTALL_BINDIR})
        else()
            set(SPATIALCLUSTERING_SHARED_DIR ${CMAKE_INSTALL_LIBDIR})
        endif()
        install(FILES $<TARGET_FILE:SpatialClusteringShared>
            DESTINATION ${SPATIALCLUSTERING_SHARED_DIR}
            COMPONENT SharedRuntime)
        if(MSVC)
            install(FILES $<TARGET_PDB_FILE:SpatialClusteringShared>
                DESTINATION ${SPATIALCLUSTERING_SHARED_DIR}
                COMPONENT SharedRuntime
                OPTIONAL)
        endif()
    endif()
endif()

# Authoring plug-in. Install it into a Wwise install, or a copy of one, with
#   cmake --install <build> --config Release --component Authoring --prefix <Wwise install>
if(SPATIALCLUSTERING_BUILD_AUTHORING)
    if(NOT WIN32)
        message(FATAL_ERROR "The authoring plug-in builds on Windows only.")
    endif()

    # Wwise Authoring runs the Profile sound engine, so the runtime plug-in it embeds never
    # gets AK_OPTIMIZED and only takes the SDK headers from Wwise::Wwise.
    set(SPATIALCLUSTERING_WWISE_INCLUDE_DIRS $<TARGET_PROPERTY:Wwise::Wwise,INTERFACE_INCLUDE_DIRECTORIES>)

    add_library(SpatialClusteringFXAuthoring STATIC ${SPATIALCLUSTERING_RUNTIME_SOURCES})
    target_include_directories(SpatialClusteringFXAuthoring PRIVATE core/src ${SPATIALCLUSTERING_WWISE_INCLUDE_DIRS})
    target_compile_definitions(SpatialClusteringFXAuthoring PRIVATE WIN64 WIN32_LEAN_AND_MEAN AK_ENABLE_ASSERTS)
    target_compile_features(SpatialClusteringFXAuthoring PRIVATE cxx_std_17)
    target_compile_options(SpatialClusteringFXAuthoring PRIVATE /utf-8 /Zc:inline)

    # The GUI uses MFC through AK::Wwise::Plugin::PluginMFCWindows. The definitions are the
    # ones from the Wwise SDK property sheets for authoring plug-ins.
    set(CMAKE_MFC_FLAG 2)
    add_library(SpatialClusteringAuthoring SHARED
        wwise/WwisePlugin/SpatialClusteringPlugin.cpp
        wwise/WwisePlugin/SpatialClusteringPlugin.h
        wwise/WwisePlugin/Win32/SpatialClusteringPluginGUI.cpp
        wwise/WwisePlugin/Win32/SpatialClusteringPluginGUI.h
        wwise/WwisePlugin/resource.h
        wwise/WwisePlugin/SpatialClustering.def
        wwise/WwisePlugin/SpatialClustering.rc)
    set_target_properties(SpatialClusteringAuthoring PROPERTIES OUTPUT_NAME SpatialClustering)
    target_include_directories(SpatialClusteringAuthoring PRIVATE ${SPATIALCLUSTERING_WWISE_INCLUDE_DIRS})
    target_compile_definitions(SpatialClusteringAuthoring PRIVATE
        WIN64 WIN32_LEAN_AND_MEAN AK_ENABLE_ASSERTS
        _WINDOWS _USRDLL _WINDLL _AFXDLL UNICODE _UNICODE WWISE_AUTHORING AUDIOKINETIC
        _CRT_STDIO_ARBITRARY_WIDE_SPECIFIERS _CRT_SECURE_NO_DEPRECATE _CRT_NON_CONFORMING_SWPRINTFS)
    target_compile_features(SpatialClusteringAuthoring PRIVATE cxx_std_17)
    target_compile_options(SpatialClusteringAuthoring PRIVATE /utf-8 /Zc:inline /Z7)
    target_link_options(SpatialClusteringAuthoring PRIVATE /DEBUG /OPT:REF /OPT:ICF)
    target_link_libraries(SpatialClusteringAuthoring PRIVATE SpatialClusteringFXAuthoring)

    if(SPATIALCLUSTERING_INSTALL)
        # Same layout as a Wwise install: the plug-in and its XML next to the other authoring
        # plug-ins, and the property help where the property editor looks for it.
        set(SPATIALCLUSTERING_AUTHORING_PLUGIN_DIR Authoring/x64/Release/bin/Plugins)
        install(FILES
                $<TARGET_FILE:SpatialClusteringAuthoring>
                wwise/WwisePlugin/SpatialClustering.xml
            DESTINATION ${SPATIALCLUSTERING_AUTHORING_PLUGIN_DIR}
            COMPONENT Authoring)
        install(FILES $<TARGET_PDB_FILE:SpatialClusteringAuthoring>
            DESTINATION ${SPATIALCLUSTERING_AUTHORING_PLUGIN_DIR}
            COMPONENT Authoring
            OPTIONAL)

        # wp.py build Documentation generates the help pages from wwise/WwisePlugin/res/Md.
        set(SPATIALCLUSTERING_HELP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/wwise/WwisePlugin/res/Html)
        if(EXISTS ${SPATIALCLUSTERING_HELP_DIR})
            install(DIRECTORY ${SPATIALCLUSTERING_HELP_DIR}/
                DESTINATION Authoring/Data/Plugins/SpatialClustering/Html
                COMPONENT Authoring)
        else()
            message(WARNING "wwise/WwisePlugin/res/Html does not exist, so the property help is not installed. Run wp.py build Documentation in the wwise folder to generate it.")
        endif()
    endif()
endif()
