/*
 * Copyright 2026 CCP ehf.
 *
 * This software was developed by CCP Games for spatial audio object clustering
 * in EVE Online and EVE Frontier.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This license does not grant any rights to CCP's trademarks or game content.
 * EVE Online and EVE Frontier are registered trademarks of CCP ehf.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

/** @brief Reads a decimal number that fits 32 bits and returns false if the text is not one. */
bool ParseNumber(const std::string& in_text, uint32_t& out_value);

/** @brief Runs the assertion suite and returns the process exit code. */
int RunTests(const std::vector<std::string>& in_args);

/** @brief Runs the benchmark and returns the process exit code. */
int RunBench(const std::vector<std::string>& in_args);

/** @brief Runs one scenario, prints its per-frame metrics and returns the process exit code. */
int RunScenarioMode(const std::vector<std::string>& in_args);

/** @brief Prints the scenario names with their seeds and descriptions. */
int RunListMode();
