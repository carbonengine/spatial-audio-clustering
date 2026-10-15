# Copyright 2026 CCP ehf.
#
# This software was developed by CCP Games for spatial audio object clustering
# in EVE Online and EVE Frontier.
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
#
# This license does not grant any rights to CCP's trademarks or game content.
# EVE Online and EVE Frontier are registered trademarks of CCP ehf.

cmake_minimum_required(VERSION 3.16)
project(ClusterTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_executable(cluster_tests
    src/Main.cpp
    src/MathConstants.h
    src/Modes.h
    src/Scenarios.cpp
    src/Scenarios.h
    src/FrameAnalysis.cpp
    src/FrameAnalysis.h
    src/ScenarioMode.cpp
    src/Bench.cpp
    src/Tests.cpp
    ${ENGINE_DIR}/ClusterEngine.cpp
    ${ENGINE_DIR}/ClusterEngine.h)

target_include_directories(cluster_tests PRIVATE ${ENGINE_DIR} src)

if(MSVC)
    target_compile_options(cluster_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(cluster_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME cluster_engine_tests COMMAND cluster_tests test)
