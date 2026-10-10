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

#include "FrameAnalysis.h"
#include "HarnessModes.h"
#include "Scenarios.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace
{
void PrintMetrics(const FrameMetrics& in_metrics)
{
    std::printf("%6d %6u %6u %6u %6u %6u %6u %9.3f %4d %10.2f\n", in_metrics.frame, in_metrics.inputs, in_metrics.passthrough,
                in_metrics.clusters, in_metrics.outputs, in_metrics.moves, in_metrics.newClusters, in_metrics.maxMemberAngle,
                in_metrics.overBudget ? 1 : 0, in_metrics.updateMicroseconds);
}

void WriteCsvRow(std::ofstream& io_csv, const FrameMetrics& in_metrics)
{
    io_csv << in_metrics.frame << ',' << in_metrics.inputs << ',' << in_metrics.passthrough << ',' << in_metrics.clusters << ','
           << in_metrics.outputs << ',' << in_metrics.moves << ',' << in_metrics.newClusters << ',' << in_metrics.maxMemberAngle
           << ',' << (in_metrics.overBudget ? 1 : 0) << ',' << in_metrics.updateMicroseconds << '\n';
}
}

int RunListMode()
{
    for (const std::string& name : GetScenarioNames())
    {
        Scenario scenario;
        BuildScenario(name, GetDefaultSeed(name), scenario);
        std::printf("%-12s seed %-6u frames %-5d %s\n", name.c_str(), scenario.seed, scenario.frames, scenario.description.c_str());
    }
    return 0;
}

int RunScenarioMode(const std::vector<std::string>& in_args)
{
    if (in_args.empty())
    {
        std::printf("scenario mode needs a name; run 'cluster_harness list' to see them\n");
        return 2;
    }
    const std::string name = in_args[0];
    uint32_t seed = GetDefaultSeed(name);
    uint32_t frames = 0;
    bool hasFrames = false;
    bool shuffle = false;
    bool quiet = false;
    std::string csvPath;
    for (size_t index = 1; index < in_args.size(); ++index)
    {
        const std::string& arg = in_args[index];
        const bool hasValue = index + 1 < in_args.size();
        bool valid = true;
        if (arg == "--csv" && hasValue)
        {
            csvPath = in_args[++index];
        }
        else if (arg == "--seed" && hasValue)
        {
            valid = ParseNumber(in_args[++index], seed);
        }
        else if (arg == "--frames" && hasValue)
        {
            valid = ParseNumber(in_args[++index], frames);
            hasFrames = true;
        }
        else if (arg == "--shuffle")
        {
            shuffle = true;
        }
        else if (arg == "--quiet")
        {
            quiet = true;
        }
        else
        {
            valid = false;
        }
        if (!valid)
        {
            std::printf("bad argument '%s'\n", arg.c_str());
            return 2;
        }
    }

    Scenario scenario;
    if (!BuildScenario(name, seed, scenario))
    {
        std::printf("unknown scenario '%s'; run 'cluster_harness list' to see them\n", name.c_str());
        return 2;
    }
    if (hasFrames)
    {
        scenario.frames = static_cast<int>(frames);
    }

    const uint32_t orderSeed = seed ^ 0x0BADu;
    std::printf("scenario %s: %s\n", scenario.name.c_str(), scenario.description.c_str());
    std::printf("seed %u, order seed %u, frames %d, frame length %.3f ms, shuffle %s\n", seed, orderSeed, scenario.frames,
                scenario.frameSeconds * 1000.0, (shuffle || scenario.shuffle) ? "on" : "off");

    std::ofstream csv;
    if (!csvPath.empty())
    {
        csv.open(csvPath);
        if (!csv)
        {
            std::printf("cannot open '%s'\n", csvPath.c_str());
            return 1;
        }
        csv << "frame,inputs,passthrough,clusters,outputs,moves,new_clusters,max_member_angle,over_budget,update_us\n";
    }

    CountingAllocator allocator;
    ClusterEngine engine;
    engine.Init(&allocator, 256);
    ScenarioPlayer player(scenario, orderSeed, shuffle || scenario.shuffle);
    FrameTracker tracker;
    FrameView view;
    std::vector<double> times;
    double clusterSum = 0.0;
    uint32_t clusterMax = 0;
    uint64_t moveSum = 0;
    uint64_t movesAfterSettle = 0;
    double angleMax = 0.0;
    int overBudgetFrames = 0;
    bool failed = false;

    if (!quiet)
    {
        std::printf(" frame inputs  pass. clust. output  moves newcl. max_angle over  update_us\n");
    }
    for (int frame = 0; frame < scenario.frames; ++frame)
    {
        const std::vector<ClusterInput>& inputs = player.NextFrame();
        const auto start = std::chrono::steady_clock::now();
        const bool updated = engine.Update(inputs.data(), static_cast<uint32_t>(inputs.size()), player.GetParams());
        const auto stop = std::chrono::steady_clock::now();
        const double micros = std::chrono::duration<double, std::micro>(stop - start).count();
        if (!updated)
        {
            std::printf("Update failed at frame %d\n", frame);
            failed = true;
            break;
        }
        CaptureFrame(engine, inputs, updated, view);
        const FrameMetrics metrics = tracker.Observe(frame, view, micros);
        times.push_back(micros);
        clusterSum += metrics.clusters;
        clusterMax = std::max(clusterMax, metrics.clusters);
        moveSum += metrics.moves;
        if (scenario.settleFrame >= 0 && frame > scenario.settleFrame)
        {
            movesAfterSettle += metrics.moves;
        }
        angleMax = std::max(angleMax, metrics.maxMemberAngle);
        overBudgetFrames += metrics.overBudget ? 1 : 0;
        if (!quiet)
        {
            PrintMetrics(metrics);
        }
        if (csv.is_open())
        {
            WriteCsvRow(csv, metrics);
        }
    }
    engine.Term();

    if (!times.empty())
    {
        const TimingSummary timing = SummarizeTimings(times);
        std::printf("summary: clusters mean %.1f max %u, moves %llu", clusterSum / times.size(), clusterMax,
                    static_cast<unsigned long long>(moveSum));
        if (scenario.settleFrame >= 0)
        {
            std::printf(" (%llu after frame %d)", static_cast<unsigned long long>(movesAfterSettle), scenario.settleFrame);
        }
        std::printf(", max member angle %.3f, over budget frames %d, update mean %.2f us p99 %.2f us\n", angleMax,
                    overBudgetFrames, timing.mean, timing.p99);
    }
    return failed ? 1 : 0;
}
