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
#include "Modes.h"
#include "Scenarios.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
/** @brief Writes every frame of a scenario run as JSON for viewer.html.
 *
 * Each frame holds:
 * - "p": maxOutputs, joinAngle and leaveRatio;
 * - "i": inputs as [id, x, y, z, loudness, flags, priority], where id is stable across frames and flags
 *   is 1 for clusterable and 2 for excluded;
 * - "c": clusters as [cluster ID, x, y, z, angular radius, member count];
 * - "l": contributions as [input index, cluster index, gain at the end of the frame].
 */
class TraceWriter
{
public:
    ~TraceWriter()
    {
        Close();
    }

    /** @brief Creates the file and writes the scenario header, and returns false if the file cannot be created. */
    bool Open(const std::string& in_path, const Scenario& in_scenario)
    {
        m_file.open(in_path);
        if (!m_file)
        {
            return false;
        }
        Write("{\"scenario\":\"%s\",\"description\":\"%s\",\"seed\":%u,\"frameSeconds\":%.6f,\"frames\":[\n",
              Escape(in_scenario.name).c_str(), Escape(in_scenario.description).c_str(), in_scenario.seed, in_scenario.frameSeconds);
        return true;
    }

    /** @brief Writes one frame. */
    void WriteFrame(const FrameView& in_view, const ClusterParams& in_params)
    {
        Write("%s{\"p\":[%u,%g,%g],\"i\":[", m_frames++ > 0 ? ",\n" : "", in_params.maxOutputs, in_params.joinAngle,
              in_params.leaveRatio);
        for (size_t index = 0; index < in_view.inputs.size(); ++index)
        {
            const ClusterInput& input = in_view.inputs[index];
            const uint32_t id = m_ids.emplace(input.key, static_cast<uint32_t>(m_ids.size())).first->second;
            const int flags = (input.clusterable ? 1 : 0) | (IsExcludedInput(input, in_params) ? 2 : 0);
            Write("%s[%u,%.2f,%.2f,%.2f,%.3f,%d,%u]", index > 0 ? "," : "", id, input.position.x, input.position.y,
                  input.position.z, input.loudness, flags, input.priority);
        }
        Write("],\"c\":[");
        for (size_t index = 0; index < in_view.clusters.size(); ++index)
        {
            const ClusterInfo& cluster = in_view.clusters[index];
            Write("%s[%u,%.2f,%.2f,%.2f,%.2f,%u]", index > 0 ? "," : "", cluster.id, cluster.position.x, cluster.position.y,
                  cluster.position.z, cluster.angularRadius, cluster.memberCount);
        }
        Write("],\"l\":[");
        for (size_t index = 0; index < in_view.contributions.size(); ++index)
        {
            const ClusterContribution& contribution = in_view.contributions[index];
            Write("%s[%u,%u,%.3f]", index > 0 ? "," : "", contribution.input, contribution.cluster, contribution.gainEnd);
        }
        Write("]}");
    }

    /** @brief Ends the JSON and closes the file. */
    void Close()
    {
        if (m_file.is_open())
        {
            Write("\n]}\n");
            m_file.close();
        }
    }

private:
    template <typename... Args>
    void Write(const char* in_format, Args... in_args)
    {
        char buffer[512];
        const int length = std::snprintf(buffer, sizeof(buffer), in_format, in_args...);
        m_file.write(buffer, std::min(length, static_cast<int>(sizeof(buffer)) - 1));
    }

    void Write(const char* in_text)
    {
        m_file << in_text;
    }

    static std::string Escape(const std::string& in_text)
    {
        std::string escaped;
        for (char character : in_text)
        {
            if (character == '"' || character == '\\')
            {
                escaped += '\\';
            }
            escaped += character;
        }
        return escaped;
    }

    std::ofstream m_file;                         ///< Trace file.
    std::unordered_map<uint64_t, uint32_t> m_ids; ///< Stable small ID of each input key seen so far.
    int m_frames = 0;                             ///< Number of frames written.
};

/** @brief Reads an angle in degrees from 0 to 180 and returns false if the text is not one. */
bool ParseAngle(const std::string& in_text, float& out_degrees)
{
    char* pEnd = nullptr;
    const double value = std::strtod(in_text.c_str(), &pEnd);
    if (in_text.empty() || pEnd != in_text.c_str() + in_text.size() || !(value >= 0.0 && value <= 180.0))
    {
        return false;
    }
    out_degrees = static_cast<float>(value);
    return true;
}

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
        std::printf("scenario mode needs a name; run 'cluster_tests list' to see them\n");
        return 2;
    }
    const std::string name = in_args[0];
    uint32_t seed = GetDefaultSeed(name);
    uint32_t frames = 0;
    bool hasFrames = false;
    bool shuffle = false;
    bool quiet = false;
    std::string csvPath;
    std::string tracePath;
    float joinAngle = 0.f;
    bool hasJoinAngle = false;
    uint32_t maxOutputs = 0;
    bool hasMaxOutputs = false;
    for (size_t index = 1; index < in_args.size(); ++index)
    {
        const std::string& arg = in_args[index];
        const bool hasValue = index + 1 < in_args.size();
        bool valid = true;
        if (arg == "--csv" && hasValue)
        {
            csvPath = in_args[++index];
        }
        else if (arg == "--trace" && hasValue)
        {
            tracePath = in_args[++index];
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
        else if (arg == "--join-angle" && hasValue)
        {
            valid = ParseAngle(in_args[++index], joinAngle);
            hasJoinAngle = true;
        }
        else if (arg == "--max-outputs" && hasValue)
        {
            valid = ParseNumber(in_args[++index], maxOutputs);
            hasMaxOutputs = true;
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
        std::printf("unknown scenario '%s'; run 'cluster_tests list' to see them\n", name.c_str());
        return 2;
    }
    if (hasFrames)
    {
        scenario.frames = static_cast<int>(frames);
    }
    // Overrides apply for the whole run, including after the scenario's own parameter changes.
    const auto applyOverrides = [&](ClusterParams& io_params) {
        if (hasJoinAngle)
        {
            io_params.joinAngle = joinAngle;
        }
        if (hasMaxOutputs)
        {
            io_params.maxOutputs = maxOutputs;
        }
    };
    applyOverrides(scenario.params);
    for (ParamChange& change : scenario.changes)
    {
        applyOverrides(change.params);
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

    TraceWriter trace;
    if (!tracePath.empty() && !trace.Open(tracePath, scenario))
    {
        std::printf("cannot open '%s'\n", tracePath.c_str());
        return 1;
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
        if (!tracePath.empty())
        {
            trace.WriteFrame(view, player.GetParams());
        }
    }
    engine.Term();
    trace.Close();

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
