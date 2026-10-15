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
#include "MathConstants.h"
#include "Modes.h"
#include "Scenarios.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace
{
/** @brief Parameter variant the benchmark runs. */
struct BenchConfig
{
    const char* name;     ///< Label printed in the table.
    uint32_t maxOutputs;  ///< Value of maxOutputs.
    float distanceWeight; ///< Value of distanceWeight.
};

/** @brief Measurements of one benchmark run. */
struct BenchResult
{
    double firstMicros = 0.0;  ///< Time of the first update, where every input is new.
    TimingSummary timing;      ///< Times of the measured updates.
    double meanClusters = 0.0; ///< Mean number of reported clusters.
    uint32_t maxClusters = 0;  ///< Largest number of reported clusters.
    uint64_t moves = 0;        ///< Existing inputs that started fading into another cluster.
    double frameSeconds = 0.0; ///< Frame length of the scenario in seconds.
    bool failed = false;       ///< True if an update failed.
};

/** @brief Parameter variant of the budget drop benchmark. */
struct DropConfig
{
    const char* name; ///< Label printed in the table.
    float joinAngle;  ///< Value of joinAngle.
};

/** @brief Measurements of the budget drop benchmark for one input count. */
struct DropResult
{
    double meanClusters = 0.0; ///< Mean number of clusters before the drop.
    double meanMicros = 0.0;   ///< Mean time of the update that lowers maxOutputs.
    double maxMicros = 0.0;    ///< Largest time of the update that lowers maxOutputs.
    bool overBudget = false;   ///< True if any run could not reach the new maxOutputs.
    bool failed = false;       ///< True if an update failed.
};

/** @brief Number of frames that settle the clusters before the budget drop. */
constexpr int kDropSettleFrames = 30;

/** @brief Number of runs with different inputs per budget drop measurement. */
constexpr uint32_t kDropRuns = 5;

/** @brief Value of maxOutputs after the budget drop. */
constexpr uint32_t kDropMaxOutputs = 16;

/** @brief Runs the benchmark scenario for one input count and parameter variant. */
BenchResult RunOne(uint32_t in_inputs, const BenchConfig& in_config, uint32_t in_seed, int in_warmup, int in_frames)
{
    Scenario scenario;
    BuildBenchScenario(in_inputs, in_seed, in_warmup + in_frames, scenario);
    scenario.params.maxOutputs = in_config.maxOutputs;
    scenario.params.distanceWeight = in_config.distanceWeight;

    CountingAllocator allocator;
    ClusterEngine engine;
    engine.Init(&allocator, in_inputs);
    ScenarioPlayer player(scenario, in_seed ^ 0x0BADu, true);
    FrameTracker tracker;
    FrameView view;
    std::vector<double> times;
    times.reserve(static_cast<size_t>(in_frames));
    BenchResult result;
    result.frameSeconds = scenario.frameSeconds;
    double clusterSum = 0.0;

    for (int frame = 0; frame < in_warmup + in_frames; ++frame)
    {
        const std::vector<ClusterInput>& inputs = player.NextFrame();
        const auto start = std::chrono::steady_clock::now();
        const bool updated = engine.Update(inputs.data(), static_cast<uint32_t>(inputs.size()), player.GetParams());
        const auto stop = std::chrono::steady_clock::now();
        if (!updated)
        {
            result.failed = true;
            break;
        }
        const double micros = std::chrono::duration<double, std::micro>(stop - start).count();
        if (frame == 0)
        {
            result.firstMicros = micros;
        }
        if (frame < in_warmup)
        {
            continue;
        }
        times.push_back(micros);
        CaptureFrame(engine, inputs, updated, view);
        const FrameMetrics metrics = tracker.Observe(frame, view, micros);
        clusterSum += metrics.clusters;
        result.maxClusters = std::max(result.maxClusters, metrics.clusters);
        result.moves += metrics.moves;
    }
    engine.Term();
    if (times.empty())
    {
        return result;
    }
    result.timing = SummarizeTimings(times);
    result.meanClusters = clusterSum / static_cast<double>(times.size());
    return result;
}

/** @brief Returns inputs in uniformly random directions and distances from 10 to 510. */
std::vector<ClusterInput> MakeRandomInputs(uint32_t in_count, std::mt19937& io_random)
{
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::vector<ClusterInput> inputs;
    for (uint32_t index = 0; index < in_count; ++index)
    {
        const double z = unit(io_random) * 2.0 - 1.0;
        const double azimuth = unit(io_random) * 2.0 * kPi;
        const double planar = std::sqrt(std::max(0.0, 1.0 - z * z));
        const double range = 10.0 + unit(io_random) * 500.0;
        ClusterInput input;
        input.key = index + 1u;
        input.position = {static_cast<float>(planar * std::cos(azimuth) * range),
                          static_cast<float>(planar * std::sin(azimuth) * range), static_cast<float>(z * range)};
        input.loudness = static_cast<float>(0.01 + unit(io_random));
        input.priority = 50;
        input.group = 0;
        input.clusterable = true;
        inputs.push_back(input);
    }
    return inputs;
}

/** @brief Times the update that lowers maxOutputs from 0 to kDropMaxOutputs after the clusters have settled. */
DropResult RunDrop(uint32_t in_inputs, const DropConfig& in_config, uint32_t in_seed)
{
    DropResult result;
    double clusterSum = 0.0;
    double timeSum = 0.0;
    for (uint32_t run = 0; run < kDropRuns; ++run)
    {
        std::mt19937 random(in_seed + run * 7919u);
        const std::vector<ClusterInput> inputs = MakeRandomInputs(in_inputs, random);
        CountingAllocator allocator;
        ClusterEngine engine;
        engine.Init(&allocator, in_inputs);
        ClusterParams params;
        params.joinAngle = in_config.joinAngle;
        for (int frame = 0; frame < kDropSettleFrames; ++frame)
        {
            result.failed = !engine.Update(inputs.data(), in_inputs, params) || result.failed;
        }
        clusterSum += engine.GetClusterCount();
        params.maxOutputs = kDropMaxOutputs;
        const auto start = std::chrono::steady_clock::now();
        const bool updated = engine.Update(inputs.data(), in_inputs, params);
        const auto stop = std::chrono::steady_clock::now();
        const double micros = std::chrono::duration<double, std::micro>(stop - start).count();
        result.failed = !updated || result.failed;
        result.overBudget = engine.IsOverBudget() || result.overBudget;
        timeSum += micros;
        result.maxMicros = std::max(result.maxMicros, micros);
        engine.Term();
    }
    result.meanClusters = clusterSum / kDropRuns;
    result.meanMicros = timeSum / kDropRuns;
    return result;
}
}

int RunBench(const std::vector<std::string>& in_args)
{
    uint32_t frames = 2000;
    uint32_t warmup = 200;
    uint32_t seed = 4242;
    for (size_t index = 0; index < in_args.size(); ++index)
    {
        const std::string& arg = in_args[index];
        const bool hasValue = index + 1 < in_args.size();
        bool valid = false;
        if (arg == "--frames" && hasValue)
        {
            valid = ParseNumber(in_args[++index], frames) && frames > 0;
        }
        else if (arg == "--warmup" && hasValue)
        {
            valid = ParseNumber(in_args[++index], warmup);
        }
        else if (arg == "--seed" && hasValue)
        {
            valid = ParseNumber(in_args[++index], seed);
        }
        if (!valid)
        {
            std::printf("bad argument '%s'\n", arg.c_str());
            return 2;
        }
    }

    const BenchConfig configs[] = {
        {"default", 0, 0.f},
        {"distance0.5", 0, 0.5f},
        {"budget64", 64, 0.f},
        {"budget64+distance0.5", 64, 0.5f},
    };
    const uint32_t sizes[] = {64, 128, 256, 512, 1024};

    std::printf("seed %u, warmup %u frames, measured %u frames, input order shuffled every frame\n", seed, warmup, frames);
    std::printf("time per Update in microseconds; 'first' is the first frame where every input is new\n\n");
    std::printf("%-20s %6s %9s %9s %9s %9s %9s %10s %8s %10s\n", "config", "inputs", "first", "mean", "p50", "p99", "max",
                "clusters", "max_cl", "moves/s");
    bool failed = false;
    for (const BenchConfig& config : configs)
    {
        for (uint32_t size : sizes)
        {
            const BenchResult result = RunOne(size, config, seed, static_cast<int>(warmup), static_cast<int>(frames));
            if (result.failed)
            {
                std::printf("%-20s %6u Update failed\n", config.name, size);
                failed = true;
                continue;
            }
            const double seconds = static_cast<double>(frames) * result.frameSeconds;
            std::printf("%-20s %6u %9.2f %9.2f %9.2f %9.2f %9.2f %10.1f %8u %10.1f\n", config.name, size, result.firstMicros,
                        result.timing.mean, result.timing.p50, result.timing.p99, result.timing.max, result.meanClusters,
                        result.maxClusters, static_cast<double>(result.moves) / seconds);
        }
    }

    const DropConfig dropConfigs[] = {
        {"join5", 5.f},
        {"join2", 2.f},
    };
    const uint32_t dropSizes[] = {512, 1024};
    std::printf("\nbudget drop: time of the Update that lowers maxOutputs from 0 to %u, after %d frames with uniformly random "
                "inputs, over %u runs\n",
                kDropMaxOutputs, kDropSettleFrames, kDropRuns);
    std::printf("%-20s %6s %10s %10s %10s\n", "config", "inputs", "clusters", "mean_us", "max_us");
    for (const DropConfig& config : dropConfigs)
    {
        for (uint32_t size : dropSizes)
        {
            const DropResult result = RunDrop(size, config, seed);
            if (result.failed || result.overBudget)
            {
                std::printf("%-20s %6u %s\n", config.name, size, result.failed ? "Update failed" : "maxOutputs not reached");
                failed = true;
                continue;
            }
            std::printf("%-20s %6u %10.1f %10.1f %10.1f\n", config.name, size, result.meanClusters, result.meanMicros,
                        result.maxMicros);
        }
    }
    return failed ? 1 : 0;
}
