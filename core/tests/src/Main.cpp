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

#include "Modes.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
void PrintUsage()
{
    std::printf(
        "usage:\n"
        "  cluster_tests test [name filter]\n"
        "      runs the assertion suite, or the tests whose name contains the filter; exits nonzero on failure\n"
        "  cluster_tests bench [--frames N] [--warmup N] [--seed N]\n"
        "      prints mean and p99 microseconds per Update for 64 to 1024 inputs\n"
        "  cluster_tests scenario <name> [--csv file] [--trace file] [--seed N] [--frames N] [--join-angle D] [--max-outputs N]\n"
        "                         [--shuffle] [--quiet]\n"
        "      prints per-frame metrics of one scenario; --trace writes every frame as JSON for viewer.html\n"
        "  cluster_tests list\n"
        "      prints the scenario names, default seeds and descriptions\n");
}
}

bool ParseNumber(const std::string& in_text, uint32_t& out_value)
{
    if (in_text.empty() || in_text[0] < '0' || in_text[0] > '9')
    {
        return false;
    }
    errno = 0;
    char* pEnd = nullptr;
    const unsigned long long value = std::strtoull(in_text.c_str(), &pEnd, 10);
    if (errno != 0 || pEnd == nullptr || *pEnd != '\0' || value > 0xFFFFFFFFull)
    {
        return false;
    }
    out_value = static_cast<uint32_t>(value);
    return true;
}

int main(int in_argc, char** in_argv)
{
    if (in_argc < 2)
    {
        PrintUsage();
        return 2;
    }
    const std::string mode = in_argv[1];
    std::vector<std::string> args;
    for (int index = 2; index < in_argc; ++index)
    {
        args.emplace_back(in_argv[index]);
    }
    if (mode == "test")
    {
        return RunTests(args);
    }
    if (mode == "bench")
    {
        return RunBench(args);
    }
    if (mode == "scenario")
    {
        return RunScenarioMode(args);
    }
    if (mode == "list")
    {
        return RunListMode();
    }
    PrintUsage();
    return 2;
}
