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
#include "HarnessMath.h"
#include "HarnessModes.h"
#include "Scenarios.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <iterator>
#include <limits>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define EXPECT(context, condition, ...) \
    do                                  \
    {                                   \
        if (!(condition))               \
        {                               \
            (context).Fail(__VA_ARGS__); \
        }                               \
    } while (false)

namespace
{
/** @brief Number of failed checks per test that are printed. */
constexpr int kMaxReportedFailures = 12;

/** @brief Counts and prints the failed checks of one test. */
class TestContext
{
public:
    /** @brief Creates a context for the test with the given name. */
    explicit TestContext(const char* in_name)
        : m_name(in_name)
    {
    }

    /** @brief Records a failed check and prints its message unless too many were printed. */
    void Fail(const char* in_format, ...)
    {
        ++m_failures;
        if (m_failures > kMaxReportedFailures)
        {
            return;
        }
        std::printf("    FAIL %s: ", m_name);
        va_list args;
        va_start(args, in_format);
        std::vprintf(in_format, args);
        va_end(args);
        std::printf("\n");
    }

    /** @brief Returns the number of failed checks. */
    int GetFailures() const
    {
        return m_failures;
    }

private:
    const char* m_name; ///< Name of the test.
    int m_failures = 0; ///< Number of failed checks.
};

ClusterInput MakeInput(uint64_t in_key, double in_degrees, double in_range, float in_loudness = 0.1f, uint32_t in_group = 0,
                       uint32_t in_priority = 50, bool in_clusterable = true)
{
    ClusterInput input;
    input.key = in_key;
    const double radians = in_degrees * kPi / 180.0;
    input.position = {static_cast<float>(std::sin(radians) * in_range), 0.f, static_cast<float>(std::cos(radians) * in_range)};
    input.loudness = in_loudness;
    input.priority = in_priority;
    input.group = in_group;
    input.clusterable = in_clusterable;
    return input;
}

ClusterInput MakeRawInput(uint64_t in_key, float in_x, float in_y, float in_z, float in_loudness = 0.1f)
{
    ClusterInput input;
    input.key = in_key;
    input.position = {in_x, in_y, in_z};
    input.loudness = in_loudness;
    input.priority = 50;
    input.group = 0;
    input.clusterable = true;
    return input;
}

/** @brief One contribution of an input, with the cluster it goes to. */
struct Membership
{
    int cluster = -1;      ///< Index of the cluster in the frame's reported clusters.
    uint32_t id = 0;       ///< ID of the cluster.
    float gainStart = 0.f; ///< Gain at the start of the frame.
    float gainEnd = 0.f;   ///< Gain at the end of the frame.
};

std::vector<Membership> FindMemberships(const FrameView& in_view, uint64_t in_key)
{
    std::vector<Membership> result;
    for (const ClusterContribution& contribution : in_view.contributions)
    {
        if (contribution.input < in_view.inputs.size() && in_view.inputs[contribution.input].key == in_key)
        {
            Membership membership;
            membership.cluster = static_cast<int>(contribution.cluster);
            membership.id = in_view.clusters[contribution.cluster].id;
            membership.gainStart = contribution.gainStart;
            membership.gainEnd = contribution.gainEnd;
            result.push_back(membership);
        }
    }
    return result;
}

int HomeIdOf(const FrameView& in_view, uint64_t in_key)
{
    const std::vector<int> home = FindHomeClusters(in_view);
    for (size_t index = 0; index < in_view.inputs.size(); ++index)
    {
        if (in_view.inputs[index].key == in_key)
        {
            return home[index] < 0 ? -1 : static_cast<int>(in_view.clusters[static_cast<size_t>(home[index])].id);
        }
    }
    return -1;
}

float SanitizedLoudness(float in_loudness)
{
    if (!std::isfinite(in_loudness) || in_loudness <= 0.f)
    {
        return 0.f;
    }
    return std::min(in_loudness, 1e9f);
}

/** @brief Checks the engine output of consecutive frames against the rules every frame must follow. */
class InvariantChecker
{
public:
    /** @brief Checks one frame against the rules and against the previous frame. */
    void Check(TestContext& io_context, const char* in_label, int in_frame, const FrameView& in_view, const ClusterParams& in_params)
    {
        const size_t inputCount = in_view.inputs.size();
        const size_t clusterCount = in_view.clusters.size();
        EXPECT(io_context, in_view.updated, "%s frame %d: Update returned false", in_label, in_frame);

        std::unordered_set<uint64_t> seenKeys;
        std::vector<bool> isDuplicate(inputCount, false);
        std::unordered_map<uint64_t, uint32_t> groups;
        for (size_t index = 0; index < inputCount; ++index)
        {
            const ClusterInput& input = in_view.inputs[index];
            isDuplicate[index] = !seenKeys.insert(input.key).second;
            if (isDuplicate[index])
            {
                continue;
            }
            groups[input.key] = input.group;
            const auto previous = m_previousGroups.find(input.key);
            if (previous != m_previousGroups.end() && previous->second != input.group)
            {
                m_groupChangeFrames[input.key] = in_frame;
            }
        }
        m_maxCrossfadeFrames = std::max(m_maxCrossfadeFrames, in_params.crossfadeFrames);
        const int groupChangeAllowance = 2 * static_cast<int>(m_maxCrossfadeFrames) + 2;

        std::unordered_set<uint32_t> ids;
        for (size_t index = 0; index < clusterCount; ++index)
        {
            const ClusterInfo& cluster = in_view.clusters[index];
            ids.insert(cluster.id);
            EXPECT(io_context, index == 0 || cluster.id > in_view.clusters[index - 1].id,
                   "[ids] %s frame %d: cluster ids not strictly ascending at %zu", in_label, in_frame, index);
            const bool wasPresent = m_previousIds.count(cluster.id) != 0;
            EXPECT(io_context, cluster.isNew == !wasPresent, "[is-new] %s frame %d: cluster %u isNew %d but present before %d",
                   in_label, in_frame, cluster.id, cluster.isNew ? 1 : 0, wasPresent ? 1 : 0);
            EXPECT(io_context, IsFiniteVec(cluster.position), "[report] %s frame %d: cluster %u position not finite", in_label,
                   in_frame, cluster.id);
            EXPECT(io_context, cluster.angularRadius >= 0.f && cluster.angularRadius <= 180.f,
                   "[report] %s frame %d: cluster %u radius %f", in_label, in_frame, cluster.id, cluster.angularRadius);
        }

        std::vector<double> sumStart(inputCount, 0.0);
        std::vector<double> sumEnd(inputCount, 0.0);
        std::vector<int> contributionCount(inputCount, 0);
        std::vector<uint32_t> perCluster(clusterCount, 0);
        std::vector<uint32_t> maxPriority(clusterCount, 0);
        std::vector<bool> representativeFound(clusterCount, false);
        std::vector<long long> clusterGroup(clusterCount, -1);
        std::vector<double> bestShare(clusterCount, -1.0);
        std::unordered_map<uint64_t, std::unordered_map<uint32_t, float>> pairs;

        for (const ClusterContribution& contribution : in_view.contributions)
        {
            const bool valid = contribution.input < inputCount && contribution.cluster < clusterCount;
            EXPECT(io_context, valid, "[report] %s frame %d: contribution index out of range", in_label, in_frame);
            if (!valid)
            {
                continue;
            }
            const ClusterInput& input = in_view.inputs[contribution.input];
            const ClusterInfo& cluster = in_view.clusters[contribution.cluster];
            const float gainStart = contribution.gainStart;
            const float gainEnd = contribution.gainEnd;
            EXPECT(io_context, std::isfinite(gainStart) && std::isfinite(gainEnd) && gainStart >= 0.f && gainStart <= 1.f &&
                                   gainEnd >= 0.f && gainEnd <= 1.f && (gainStart > 0.f || gainEnd > 0.f),
                   "[report] %s frame %d: bad gains %f %f", in_label, in_frame, gainStart, gainEnd);
            EXPECT(io_context, input.clusterable, "[passthrough] %s frame %d: passthrough input %zu has a contribution",
                   in_label, in_frame, static_cast<size_t>(contribution.input));
            EXPECT(io_context, !isDuplicate[contribution.input], "[duplicates] %s frame %d: repeated key has a contribution",
                   in_label, in_frame);

            sumStart[contribution.input] += gainStart;
            sumEnd[contribution.input] += gainEnd;
            ++contributionCount[contribution.input];
            ++perCluster[contribution.cluster];
            maxPriority[contribution.cluster] = std::max(maxPriority[contribution.cluster], input.priority);
            if (contribution.input == cluster.representative)
            {
                representativeFound[contribution.cluster] = true;
            }
            const auto groupChange = m_groupChangeFrames.find(input.key);
            const bool recentGroupChange =
                groupChange != m_groupChangeFrames.end() && in_frame - groupChange->second <= groupChangeAllowance;
            if (!recentGroupChange)
            {
                if (clusterGroup[contribution.cluster] < 0)
                {
                    clusterGroup[contribution.cluster] = input.group;
                }
                EXPECT(io_context, clusterGroup[contribution.cluster] == static_cast<long long>(input.group),
                       "[groups] %s frame %d: cluster %u mixes groups", in_label, in_frame, cluster.id);
            }

            const double weight = in_params.loudnessWeighting ? SanitizedLoudness(input.loudness) + 1e-6 : 1.0;
            bestShare[contribution.cluster] = std::max(bestShare[contribution.cluster], weight * gainEnd);

            if (IsFiniteVec(input.position) && LengthOf(input.position) > 1e-3 && LengthOf(cluster.position) > 1e-3)
            {
                const double angle = AngleBetween(input.position, cluster.position);
                EXPECT(io_context, angle <= cluster.angularRadius + 0.05,
                       "[report] %s frame %d: member at %.3f deg outside radius %.3f of cluster %u", in_label, in_frame,
                       angle, cluster.angularRadius, cluster.id);
            }

            pairs[input.key][cluster.id] = gainEnd;
            const auto previous = m_previousPairs.find(input.key);
            const bool hadPair = previous != m_previousPairs.end() && previous->second.count(cluster.id) != 0;
            if (hadPair)
            {
                const float previousEnd = previous->second.at(cluster.id);
                EXPECT(io_context, std::fabs(gainStart - previousEnd) <= 1e-6f,
                       "[continuity] %s frame %d: key %llx cluster %u starts at %f after ending at %f", in_label, in_frame,
                       static_cast<unsigned long long>(input.key), cluster.id, gainStart, previousEnd);
            }
            else if (m_previousClusterable.count(input.key) != 0)
            {
                EXPECT(io_context, gainStart == 0.f, "[continuity] %s frame %d: key %llx new pair starts at %f", in_label,
                       in_frame, static_cast<unsigned long long>(input.key), gainStart);
            }
            else
            {
                EXPECT(io_context, gainStart == 1.f && gainEnd == 1.f,
                       "[continuity] %s frame %d: new key %llx starts with %f %f", in_label, in_frame,
                       static_cast<unsigned long long>(input.key), gainStart, gainEnd);
            }
        }

        for (size_t index = 0; index < clusterCount; ++index)
        {
            const ClusterInfo& cluster = in_view.clusters[index];
            EXPECT(io_context, perCluster[index] > 0, "[report] %s frame %d: cluster %u has no contribution", in_label, in_frame,
                   cluster.id);
            EXPECT(io_context, perCluster[index] == cluster.memberCount, "[report] %s frame %d: cluster %u memberCount %u vs %u",
                   in_label, in_frame, cluster.id, cluster.memberCount, perCluster[index]);
            EXPECT(io_context, representativeFound[index], "[report] %s frame %d: cluster %u representative not a member",
                   in_label, in_frame, cluster.id);
            EXPECT(io_context, cluster.priority == maxPriority[index], "[report] %s frame %d: cluster %u priority %u vs %u",
                   in_label, in_frame, cluster.id, cluster.priority, maxPriority[index]);
            if (representativeFound[index] && in_params.loudnessSmoothing == 0.f && cluster.representative < inputCount)
            {
                const ClusterInput& representative = in_view.inputs[cluster.representative];
                double representativeEnd = 0.0;
                for (const ClusterContribution& contribution : in_view.contributions)
                {
                    if (contribution.cluster == index && contribution.input == cluster.representative)
                    {
                        representativeEnd = contribution.gainEnd;
                    }
                }
                const double weight =
                    in_params.loudnessWeighting ? SanitizedLoudness(representative.loudness) + 1e-6 : 1.0;
                EXPECT(io_context, weight * representativeEnd >= bestShare[index] * (1.0 - 1e-5),
                       "[report] %s frame %d: cluster %u representative is not the loudest", in_label, in_frame, cluster.id);
            }
        }

        std::unordered_set<uint64_t> clusterable;
        for (size_t index = 0; index < inputCount; ++index)
        {
            const ClusterInput& input = in_view.inputs[index];
            if (!input.clusterable || isDuplicate[index])
            {
                EXPECT(io_context, contributionCount[index] == 0, "[passthrough] %s frame %d: input %zu has %d contributions",
                       in_label, in_frame, index, contributionCount[index]);
                continue;
            }
            clusterable.insert(input.key);
            EXPECT(io_context, contributionCount[index] == 1 || contributionCount[index] == 2,
                   "[gain-sum] %s frame %d: input %zu has %d contributions", in_label, in_frame, index, contributionCount[index]);
            EXPECT(io_context, std::fabs(sumStart[index] - 1.0) <= 1e-5 && std::fabs(sumEnd[index] - 1.0) <= 1e-5,
                   "[gain-sum] %s frame %d: input %zu sums %f %f", in_label, in_frame, index, sumStart[index], sumEnd[index]);
        }

        for (const auto& previous : m_previousPairs)
        {
            if (clusterable.count(previous.first) == 0)
            {
                continue;
            }
            const auto current = pairs.find(previous.first);
            for (const auto& pair : previous.second)
            {
                const bool stillThere = current != pairs.end() && current->second.count(pair.first) != 0;
                EXPECT(io_context, stillThere || pair.second <= 1e-6f,
                       "[continuity] %s frame %d: key %llx left cluster %u at gain %f without a fade", in_label, in_frame,
                       static_cast<unsigned long long>(previous.first), pair.first, pair.second);
            }
        }

        for (auto change = m_groupChangeFrames.begin(); change != m_groupChangeFrames.end();)
        {
            change = in_frame - change->second > groupChangeAllowance ? m_groupChangeFrames.erase(change) : std::next(change);
        }
        m_previousPairs.swap(pairs);
        m_previousIds.swap(ids);
        m_previousClusterable.swap(clusterable);
        m_previousGroups.swap(groups);
    }

private:
    std::unordered_map<uint64_t, std::unordered_map<uint32_t, float>> m_previousPairs; ///< End gain per cluster ID of each input key in the previous frame.
    std::unordered_set<uint32_t> m_previousIds;                                         ///< Cluster IDs reported in the previous frame.
    std::unordered_set<uint64_t> m_previousClusterable;                                 ///< Keys of clusterable inputs in the previous frame.
    std::unordered_map<uint64_t, uint32_t> m_previousGroups;                            ///< Group of each input key in the previous frame.
    std::unordered_map<uint64_t, int> m_groupChangeFrames;                              ///< Frame of the recent group change of each input key.
    uint32_t m_maxCrossfadeFrames = 1;                                                  ///< Longest crossfade in frames seen so far.
};

/** @brief Engine with a counting allocator that can check every frame it computes. */
class TestEngine
{
public:
    /** @brief Creates an engine that checks every frame when a test context is given. */
    explicit TestEngine(uint32_t in_reserve = 64, TestContext* io_pContext = nullptr, const char* in_label = "")
        : m_pContext(io_pContext)
        , m_label(in_label)
    {
        m_engine.Init(&m_allocator, in_reserve);
    }

    /** @brief Runs one update, checks it when a test context was given and returns the frame. */
    const FrameView& Step(const std::vector<ClusterInput>& in_inputs, const ClusterParams& in_params)
    {
        const bool updated = m_engine.Update(in_inputs.data(), static_cast<uint32_t>(in_inputs.size()), in_params);
        CaptureFrame(m_engine, in_inputs, updated, m_view);
        if (m_pContext != nullptr)
        {
            m_checker.Check(*m_pContext, m_label, m_frame, m_view, in_params);
        }
        ++m_frame;
        return m_view;
    }

    /** @brief Returns the engine. */
    ClusterEngine& GetEngine()
    {
        return m_engine;
    }

    /** @brief Returns the allocator of the engine. */
    CountingAllocator& GetAllocator()
    {
        return m_allocator;
    }

private:
    CountingAllocator m_allocator; ///< Allocator of the engine.
    ClusterEngine m_engine;        ///< Engine under test.
    FrameView m_view;              ///< Frame of the last update.
    InvariantChecker m_checker;    ///< Checks every frame when a test context is given.
    TestContext* m_pContext;       ///< Test context for the checks, or nullptr.
    const char* m_label;           ///< Label printed with failed checks.
    int m_frame = 0;               ///< Number of updates run.
};

/** @brief Order in which a scenario hands its inputs to the engine. */
enum class Order
{
    Scenario, ///< Shuffled only if the scenario asks for it.
    Sorted,   ///< Never shuffled.
    Shuffled  ///< Shuffled every frame.
};

/** @brief Options for playing a scenario. */
struct RunOptions
{
    Order order = Order::Scenario; ///< Order of the inputs.
    uint32_t orderSeed = 77;       ///< Seed of the shuffle.
    int maxFrames = -1;            ///< Largest number of frames to play, or -1 for the whole scenario.
    uint32_t reserve = 64;         ///< Number of inputs the engine reserves memory for.
};

using FrameCallback = std::function<void(int, const FrameView&, const ClusterParams&)>;

void PlayScenario(const Scenario& in_scenario, const RunOptions& in_options, const FrameCallback& in_callback)
{
    CountingAllocator allocator;
    ClusterEngine engine;
    engine.Init(&allocator, in_options.reserve);
    const bool shuffle = in_options.order == Order::Shuffled || (in_options.order == Order::Scenario && in_scenario.shuffle);
    ScenarioPlayer player(in_scenario, in_options.orderSeed, shuffle);
    const int frames = in_options.maxFrames >= 0 ? std::min(in_options.maxFrames, in_scenario.frames) : in_scenario.frames;
    FrameView view;
    for (int frame = 0; frame < frames; ++frame)
    {
        const std::vector<ClusterInput>& inputs = player.NextFrame();
        const bool updated = engine.Update(inputs.data(), static_cast<uint32_t>(inputs.size()), player.GetParams());
        CaptureFrame(engine, inputs, updated, view);
        in_callback(frame, view, player.GetParams());
    }
    engine.Term();
}

Scenario LoadScenario(const std::string& in_name, uint32_t in_seed = 0)
{
    Scenario scenario;
    BuildScenario(in_name, in_seed == 0 ? GetDefaultSeed(in_name) : in_seed, scenario);
    return scenario;
}

/** @brief Engine output of one frame in a form that does not depend on the input order. */
struct CanonicalFrame
{
    std::vector<std::tuple<uint32_t, bool, float, float, float, float, uint32_t, uint32_t, uint64_t>> clusters; ///< Clusters with the representative as a key.
    std::vector<std::tuple<uint64_t, uint32_t, float, float>> contributions; ///< Contributions by input key and cluster ID, sorted.
    bool overBudget = false;                                                  ///< Value of IsOverBudget.

    /** @brief Returns true if both frames are equal. */
    bool operator==(const CanonicalFrame& in_other) const
    {
        return clusters == in_other.clusters && contributions == in_other.contributions && overBudget == in_other.overBudget;
    }
};

CanonicalFrame MakeCanonical(const FrameView& in_view)
{
    CanonicalFrame canonical;
    canonical.overBudget = in_view.overBudget;
    for (const ClusterInfo& cluster : in_view.clusters)
    {
        const uint64_t representativeKey =
            cluster.representative < in_view.inputs.size() ? in_view.inputs[cluster.representative].key : ~0ull;
        canonical.clusters.emplace_back(cluster.id, cluster.isNew, cluster.position.x, cluster.position.y, cluster.position.z,
                                        cluster.angularRadius, cluster.priority, cluster.memberCount, representativeKey);
    }
    for (const ClusterContribution& contribution : in_view.contributions)
    {
        canonical.contributions.emplace_back(in_view.inputs[contribution.input].key, in_view.clusters[contribution.cluster].id,
                                             contribution.gainStart, contribution.gainEnd);
    }
    std::sort(canonical.contributions.begin(), canonical.contributions.end());
    return canonical;
}

void TestInvariants(TestContext& io_context)
{
    for (const std::string& name : GetScenarioNames())
    {
        const Scenario scenario = LoadScenario(name);
        InvariantChecker checker;
        PlayScenario(scenario, RunOptions(), [&](int in_frame, const FrameView& in_view, const ClusterParams& in_params) {
            checker.Check(io_context, name.c_str(), in_frame, in_view, in_params);
        });
    }
}

void TestInvariantsParameterSweep(TestContext& io_context)
{
    struct Variant
    {
        const char* label;
        std::function<void(ClusterParams&)> apply;
    };
    const std::vector<Variant> variants = {
        {"dwell0-fade0", [](ClusterParams& io_params) { io_params.dwellFrames = 0; io_params.crossfadeFrames = 0; }},
        {"dwell1-fade1", [](ClusterParams& io_params) { io_params.dwellFrames = 1; io_params.crossfadeFrames = 1; }},
        {"dwell30-fade10", [](ClusterParams& io_params) { io_params.dwellFrames = 30; io_params.crossfadeFrames = 10; }},
        {"distance0.5", [](ClusterParams& io_params) { io_params.distanceWeight = 0.5f; }},
        {"loudness-off", [](ClusterParams& io_params) { io_params.loudnessWeighting = false; }},
        {"smoothing0.9", [](ClusterParams& io_params) { io_params.loudnessSmoothing = 0.9f; }},
        {"join15-leave1", [](ClusterParams& io_params) { io_params.joinAngle = 15.f; io_params.leaveRatio = 1.f; }},
        {"join180-max1", [](ClusterParams& io_params) { io_params.joinAngle = 180.f; io_params.maxOutputs = 1; }},
        {"join0", [](ClusterParams& io_params) { io_params.joinAngle = 0.f; }},
        {"max6", [](ClusterParams& io_params) { io_params.maxOutputs = 6; }},
        {"nearfield50", [](ClusterParams& io_params) { io_params.nearFieldRadius = 50.f; io_params.excludePriorityAbove = 70; }},
    };
    const char* names[] = {"cloud", "crossing", "oneshots", "nearfield"};
    for (const char* name : names)
    {
        for (const Variant& variant : variants)
        {
            Scenario scenario = LoadScenario(name, GetDefaultSeed(name) + 17u);
            variant.apply(scenario.params);
            for (ParamChange& change : scenario.changes)
            {
                variant.apply(change.params);
            }
            const std::string label = std::string(name) + "/" + variant.label;
            InvariantChecker checker;
            RunOptions options;
            options.maxFrames = 500;
            options.order = Order::Shuffled;
            PlayScenario(scenario, options, [&](int in_frame, const FrameView& in_view, const ClusterParams& in_params) {
                checker.Check(io_context, label.c_str(), in_frame, in_view, in_params);
            });
        }
    }
}

void TestOrderIndependence(TestContext& io_context)
{
    const char* names[] = {"oneshots", "crossing", "groups", "nearfield", "squeeze", "cloud", "passthrough", "passby"};
    for (const char* name : names)
    {
        const Scenario scenario = LoadScenario(name);
        const int frames = std::min(scenario.frames, 800);
        CountingAllocator allocators[3];
        ClusterEngine engines[3];
        ScenarioPlayer players[3] = {ScenarioPlayer(scenario, 1, false), ScenarioPlayer(scenario, 99, true),
                                     ScenarioPlayer(scenario, 12345, true)};
        for (int index = 0; index < 3; ++index)
        {
            engines[index].Init(&allocators[index], 64);
        }
        int mismatches = 0;
        for (int frame = 0; frame < frames && mismatches < 3; ++frame)
        {
            CanonicalFrame canonical[3];
            for (int index = 0; index < 3; ++index)
            {
                const std::vector<ClusterInput>& inputs = players[index].NextFrame();
                const bool updated =
                    engines[index].Update(inputs.data(), static_cast<uint32_t>(inputs.size()), players[index].GetParams());
                FrameView view;
                CaptureFrame(engines[index], inputs, updated, view);
                canonical[index] = MakeCanonical(view);
            }
            for (int index = 1; index < 3; ++index)
            {
                if (!(canonical[0] == canonical[index]))
                {
                    ++mismatches;
                    io_context.Fail("%s frame %d: shuffled run %d differs from sorted run", name, frame, index);
                }
            }
        }
    }
}

void TestStaticSettles(TestContext& io_context)
{
    struct Expectation
    {
        const char* name;
        size_t clusters;
    };
    const Expectation expectations[] = {{"static", 12}, {"groups", 12}, {"jitter", 10}};
    for (const Expectation& expectation : expectations)
    {
        const Scenario scenario = LoadScenario(expectation.name);
        FrameTracker tracker;
        std::vector<uint32_t> settledIds;
        PlayScenario(scenario, RunOptions(), [&](int in_frame, const FrameView& in_view, const ClusterParams&) {
            const FrameMetrics metrics = tracker.Observe(in_frame, in_view, 0.0);
            if (in_frame < scenario.settleFrame)
            {
                return;
            }
            std::vector<uint32_t> ids;
            for (const ClusterInfo& cluster : in_view.clusters)
            {
                ids.push_back(cluster.id);
            }
            if (in_frame == scenario.settleFrame)
            {
                settledIds = ids;
                EXPECT(io_context, ids.size() == expectation.clusters, "%s: %zu clusters after settling, expected %zu",
                       expectation.name, ids.size(), expectation.clusters);
                return;
            }
            EXPECT(io_context, metrics.moves == 0, "%s frame %d: %u moves after settling", expectation.name, in_frame,
                   metrics.moves);
            EXPECT(io_context, metrics.newClusters == 0, "%s frame %d: %u new clusters after settling", expectation.name,
                   in_frame, metrics.newClusters);
            EXPECT(io_context, ids == settledIds, "%s frame %d: cluster set changed after settling", expectation.name, in_frame);
        });
    }
}

void TestJitterMakesNoMoves(TestContext& io_context)
{
    for (uint32_t seedOffset = 0; seedOffset < 3; ++seedOffset)
    {
        const Scenario scenario = LoadScenario("jitter", GetDefaultSeed("jitter") + seedOffset * 31u);
        FrameTracker tracker;
        uint64_t moves = 0;
        PlayScenario(scenario, RunOptions(), [&](int in_frame, const FrameView& in_view, const ClusterParams&) {
            const FrameMetrics metrics = tracker.Observe(in_frame, in_view, 0.0);
            if (in_frame > scenario.settleFrame)
            {
                moves += metrics.moves + metrics.newClusters;
            }
        });
        EXPECT(io_context, moves == 0, "jitter seed %u: %llu moves or new clusters after settling", scenario.seed,
               static_cast<unsigned long long>(moves));
    }
}

struct ProbeResult
{
    bool joinedBase = false;
    bool overBudget = false;
    size_t clusters = 0;
    float gainStart = 0.f;
    float gainEnd = 0.f;
};

ProbeResult RunJoinProbe(TestContext& io_context, const ClusterParams& in_settleParams, const ClusterParams& in_probeParams,
                         const ClusterInput& in_probe)
{
    TestEngine engine(64, &io_context, "join-probe");
    std::vector<ClusterInput> base = {MakeInput(1, 0.0, 10.0), MakeInput(2, 0.5, 10.0), MakeInput(3, -0.5, 10.0)};
    for (int frame = 0; frame < 20; ++frame)
    {
        engine.Step(base, in_settleParams);
    }
    const int baseId = HomeIdOf(engine.Step(base, in_settleParams), 1);
    std::vector<ClusterInput> withProbe = base;
    withProbe.push_back(in_probe);
    const FrameView& view = engine.Step(withProbe, in_probeParams);
    ProbeResult result;
    const std::vector<Membership> memberships = FindMemberships(view, in_probe.key);
    result.joinedBase = memberships.size() == 1 && static_cast<int>(memberships[0].id) == baseId;
    result.overBudget = view.overBudget;
    result.clusters = view.clusters.size();
    if (!memberships.empty())
    {
        result.gainStart = memberships[0].gainStart;
        result.gainEnd = memberships[0].gainEnd;
    }
    return result;
}

void TestJoinsWithinJoinAngle(TestContext& io_context)
{
    const ClusterParams params;
    ClusterParams budget = params;
    budget.maxOutputs = 1;
    ClusterParams distance = params;
    distance.distanceWeight = 1.f;

    ProbeResult result = RunJoinProbe(io_context, params, params, MakeInput(10, 3.0, 10.0));
    EXPECT(io_context, result.joinedBase && result.clusters == 1, "input at 3 deg did not join a cluster at 0 deg");
    EXPECT(io_context, result.gainStart == 1.f && result.gainEnd == 1.f, "joining input does not start at full gain");

    result = RunJoinProbe(io_context, params, params, MakeInput(11, 4.9, 10.0));
    EXPECT(io_context, result.joinedBase, "input at 4.9 deg did not join with joinAngle 5");

    result = RunJoinProbe(io_context, params, params, MakeInput(12, 5.1, 10.0));
    EXPECT(io_context, !result.joinedBase && result.clusters == 2, "input at 5.1 deg joined with joinAngle 5");

    result = RunJoinProbe(io_context, params, params, MakeInput(13, -40.0, 10.0));
    EXPECT(io_context, !result.joinedBase && result.clusters == 2, "input at -40 deg joined without a budget");

    result = RunJoinProbe(io_context, params, budget, MakeInput(14, -40.0, 10.0));
    EXPECT(io_context, result.joinedBase && result.clusters == 1 && !result.overBudget,
           "input at -40 deg was not forced into the only cluster by maxOutputs 1");

    result = RunJoinProbe(io_context, params, params, MakeInput(15, 0.0, 10.0, 0.1f, 2));
    EXPECT(io_context, !result.joinedBase && result.clusters == 2, "input of another group joined");

    result = RunJoinProbe(io_context, distance, distance, MakeInput(16, 1.0, 40.0));
    EXPECT(io_context, !result.joinedBase, "input at 1 deg and 4x the range joined with distanceWeight 1");

    result = RunJoinProbe(io_context, distance, distance, MakeInput(17, 1.0, 11.0));
    EXPECT(io_context, result.joinedBase, "input at 1 deg and 1.1x the range did not join with distanceWeight 1");

    ClusterParams nearField = params;
    nearField.nearFieldRadius = 5.f;
    result = RunJoinProbe(io_context, nearField, nearField, MakeInput(18, 0.0, 30.0));
    EXPECT(io_context, result.joinedBase, "input outside the near field did not join");
    result = RunJoinProbe(io_context, nearField, nearField, MakeInput(19, 0.0, 4.0));
    EXPECT(io_context, !result.joinedBase && result.clusters == 2, "input inside the near field joined");
}

void TestLeaveBound(TestContext& io_context)
{
    const char* names[] = {"crossing", "passby", "cloud", "oneshots"};
    for (const char* name : names)
    {
        const Scenario scenario = LoadScenario(name);
        const ClusterParams& params = scenario.params;
        const double leaveAngle = static_cast<double>(params.joinAngle) * std::max(1.0f, params.leaveRatio);
        const int allowed = static_cast<int>(params.dwellFrames + params.crossfadeFrames) + 3;
        std::unordered_map<uint64_t, int> outside;
        int worst = 0;
        PlayScenario(scenario, RunOptions(), [&](int in_frame, const FrameView& in_view, const ClusterParams& in_params) {
            const std::vector<int> home = FindHomeClusters(in_view);
            std::unordered_map<uint64_t, int> next;
            for (size_t index = 0; index < in_view.inputs.size(); ++index)
            {
                const ClusterInput& input = in_view.inputs[index];
                if (!input.clusterable || home[index] < 0 || IsExcludedInput(input, in_params))
                {
                    continue;
                }
                const ClusterInfo& cluster = in_view.clusters[static_cast<size_t>(home[index])];
                if (AngleBetween(input.position, cluster.position) <= leaveAngle + 0.05)
                {
                    continue;
                }
                const auto previous = outside.find(input.key);
                const int count = (previous == outside.end() ? 0 : previous->second) + 1;
                next[input.key] = count;
                worst = std::max(worst, count);
                EXPECT(io_context, count <= allowed, "%s frame %d: key %llx outside leaveAngle for %d frames", name, in_frame,
                       static_cast<unsigned long long>(input.key), count);
            }
            outside.swap(next);
        });
        EXPECT(io_context, worst <= allowed, "%s: worst stay outside leaveAngle %d frames", name, worst);
    }
}

void TestBudgetRespected(TestContext& io_context)
{
    const char* names[] = {"squeeze", "passthrough"};
    for (const char* name : names)
    {
        const Scenario scenario = LoadScenario(name);
        int budgetFrames = 0;
        PlayScenario(scenario, RunOptions(), [&](int in_frame, const FrameView& in_view, const ClusterParams& in_params) {
            if (in_params.maxOutputs == 0)
            {
                return;
            }
            ++budgetFrames;
            uint32_t passthrough = 0;
            for (const ClusterInput& input : in_view.inputs)
            {
                passthrough += input.clusterable ? 0u : 1u;
            }
            std::vector<bool> hasSteadyMember(in_view.clusters.size(), false);
            bool allSteady = true;
            for (const ClusterContribution& contribution : in_view.contributions)
            {
                const bool steady = contribution.gainStart == 1.f && contribution.gainEnd == 1.f;
                allSteady = allSteady && steady;
                if (steady)
                {
                    hasSteadyMember[contribution.cluster] = true;
                }
            }
            const uint32_t steadyClusters =
                static_cast<uint32_t>(std::count(hasSteadyMember.begin(), hasSteadyMember.end(), true));
            const bool feasible = passthrough < in_params.maxOutputs;
            EXPECT(io_context, !in_view.overBudget || !feasible, "%s frame %d: over budget although %u passthrough < %u", name,
                   in_frame, passthrough, in_params.maxOutputs);
            if (in_view.overBudget)
            {
                return;
            }
            EXPECT(io_context, steadyClusters + passthrough <= in_params.maxOutputs,
                   "%s frame %d: %u settled clusters + %u passthrough > %u", name, in_frame, steadyClusters, passthrough,
                   in_params.maxOutputs);
            if (allSteady)
            {
                EXPECT(io_context, in_view.clusters.size() + passthrough <= in_params.maxOutputs,
                       "%s frame %d: %zu clusters + %u passthrough > %u without fades", name, in_frame, in_view.clusters.size(),
                       passthrough, in_params.maxOutputs);
            }
        });
        EXPECT(io_context, budgetFrames > 0, "%s: no frame ran with a budget", name);
    }
}

void TestBudgetOverflowFlagged(TestContext& io_context)
{
    ClusterParams params;
    params.nearFieldRadius = 5.f;
    params.maxOutputs = 3;
    {
        TestEngine engine(64, &io_context, "overflow");
        std::vector<ClusterInput> inputs;
        for (uint64_t key = 1; key <= 6; ++key)
        {
            inputs.push_back(MakeInput(key, static_cast<double>(key) * 0.5, 1.0));
        }
        for (int frame = 0; frame < 30; ++frame)
        {
            const FrameView& view = engine.Step(inputs, params);
            EXPECT(io_context, view.overBudget, "frame %d: six excluded inputs with maxOutputs 3 not flagged", frame);
            EXPECT(io_context, view.clusters.size() == 6, "frame %d: six excluded inputs gave %zu clusters", frame,
                   view.clusters.size());
            for (const ClusterInfo& cluster : view.clusters)
            {
                EXPECT(io_context, cluster.memberCount == 1, "frame %d: excluded cluster %u has %u members", frame, cluster.id,
                       cluster.memberCount);
            }
        }
    }
    {
        ClusterParams groups;
        groups.maxOutputs = 2;
        TestEngine engine(64, &io_context, "overflow");
        std::vector<ClusterInput> inputs;
        for (uint32_t group = 0; group < 3; ++group)
        {
            inputs.push_back(MakeInput(10 + group * 2, 0.0, 10.0, 0.1f, group));
            inputs.push_back(MakeInput(11 + group * 2, 30.0, 10.0, 0.1f, group));
        }
        for (int frame = 0; frame < 30; ++frame)
        {
            const FrameView& view = engine.Step(inputs, groups);
            EXPECT(io_context, view.overBudget, "frame %d: three groups with maxOutputs 2 not flagged", frame);
            EXPECT(io_context, frame < 5 || view.clusters.size() == 3, "frame %d: three groups gave %zu clusters", frame,
                   view.clusters.size());
        }
    }
    {
        ClusterParams passthrough;
        passthrough.maxOutputs = 2;
        TestEngine engine(64, &io_context, "overflow");
        std::vector<ClusterInput> inputs;
        for (uint64_t key = 1; key <= 4; ++key)
        {
            inputs.push_back(MakeInput(key, key * 20.0, 10.0, 0.1f, 0, 50, false));
        }
        inputs.push_back(MakeInput(20, 0.0, 10.0));
        inputs.push_back(MakeInput(21, 90.0, 10.0));
        for (int frame = 0; frame < 30; ++frame)
        {
            const FrameView& view = engine.Step(inputs, passthrough);
            EXPECT(io_context, view.overBudget, "frame %d: four passthrough inputs with maxOutputs 2 not flagged", frame);
            EXPECT(io_context, view.clusters.size() == 1, "frame %d: clusterable inputs did not share one cluster", frame);
        }
    }
}

bool IsAlone(const FrameView& in_view, size_t in_input)
{
    int count = 0;
    uint32_t cluster = 0;
    for (const ClusterContribution& contribution : in_view.contributions)
    {
        if (contribution.input == in_input)
        {
            ++count;
            cluster = contribution.cluster;
        }
    }
    return count == 1 && in_view.clusters[cluster].memberCount == 1;
}

void TestExcludedInputsAlone(TestContext& io_context)
{
    const Scenario scenario = LoadScenario("nearfield");
    const int allowed = static_cast<int>(scenario.params.dwellFrames + 2 * scenario.params.crossfadeFrames) + 2;
    std::unordered_map<uint64_t, int> excludedSince;
    std::unordered_map<uint64_t, bool> excludedFromStart;
    std::unordered_set<uint64_t> seen;
    int excludedChecks = 0;
    PlayScenario(scenario, RunOptions(), [&](int in_frame, const FrameView& in_view, const ClusterParams& in_params) {
        for (size_t index = 0; index < in_view.inputs.size(); ++index)
        {
            const ClusterInput& input = in_view.inputs[index];
            const bool firstFrame = seen.insert(input.key).second;
            if (!IsExcludedInput(input, in_params))
            {
                excludedSince.erase(input.key);
                excludedFromStart[input.key] = false;
                continue;
            }
            if (excludedSince.count(input.key) == 0)
            {
                excludedSince[input.key] = in_frame;
                if (firstFrame)
                {
                    excludedFromStart[input.key] = true;
                }
            }
            const bool fromStart = excludedFromStart[input.key];
            if (fromStart || in_frame - excludedSince[input.key] > allowed)
            {
                ++excludedChecks;
                EXPECT(io_context, IsAlone(in_view, index), "nearfield frame %d: excluded key %llx shares its cluster", in_frame,
                       static_cast<unsigned long long>(input.key));
            }
        }
    });
    EXPECT(io_context, excludedChecks > 1000, "nearfield: only %d excluded checks ran", excludedChecks);

    ClusterParams params;
    params.excludePriorityAbove = 80;
    TestEngine engine(64, &io_context, "priority-exclude");
    std::vector<ClusterInput> inputs = {MakeInput(1, 0.0, 10.0), MakeInput(2, 0.3, 10.0, 0.1f, 0, 95),
                                        MakeInput(3, -0.3, 10.0), MakeInput(4, 0.1, 10.0, 0.1f, 0, 81)};
    for (int frame = 0; frame < 40; ++frame)
    {
        const FrameView& view = engine.Step(inputs, params);
        EXPECT(io_context, IsAlone(view, 1) && IsAlone(view, 3), "frame %d: high priority inputs share a cluster", frame);
        EXPECT(io_context, view.clusters.size() == 3, "frame %d: expected 3 clusters, got %zu", frame, view.clusters.size());
    }

    params.maxOutputs = 2;
    for (int frame = 0; frame < 40; ++frame)
    {
        const FrameView& view = engine.Step(inputs, params);
        EXPECT(io_context, IsAlone(view, 1) && IsAlone(view, 3), "frame %d: high priority inputs share under a budget", frame);
        EXPECT(io_context, view.overBudget, "frame %d: two excluded inputs plus one cluster with maxOutputs 2 not flagged", frame);
    }
}

void TestGroupsNeverShare(TestContext& io_context)
{
    const Scenario scenario = LoadScenario("groups");
    PlayScenario(scenario, RunOptions(), [&](int in_frame, const FrameView& in_view, const ClusterParams&) {
        std::vector<long long> groups(in_view.clusters.size(), -1);
        for (const ClusterContribution& contribution : in_view.contributions)
        {
            const long long group = in_view.inputs[contribution.input].group;
            long long& clusterGroup = groups[contribution.cluster];
            EXPECT(io_context, clusterGroup < 0 || clusterGroup == group, "groups frame %d: cluster %u mixes groups", in_frame,
                   in_view.clusters[contribution.cluster].id);
            clusterGroup = group;
        }
    });

    ClusterParams params;
    params.joinAngle = 180.f;
    params.maxOutputs = 1;
    TestEngine engine(64, &io_context, "groups-wide");
    std::vector<ClusterInput> inputs = {MakeInput(1, 0.0, 10.0, 0.1f, 1), MakeInput(2, 0.0, 10.0, 0.1f, 2),
                                        MakeInput(3, 1.0, 10.0, 0.1f, 1), MakeInput(4, 1.0, 10.0, 0.1f, 2)};
    for (int frame = 0; frame < 20; ++frame)
    {
        const FrameView& view = engine.Step(inputs, params);
        EXPECT(io_context, view.clusters.size() == 2, "frame %d: two groups with joinAngle 180 gave %zu clusters", frame,
               view.clusters.size());
        EXPECT(io_context, view.overBudget, "frame %d: two groups with maxOutputs 1 not flagged", frame);
    }
}

void TestNoAllocationsInUpdate(TestContext& io_context)
{
    const char* names[] = {"oneshots", "crossing", "nearfield", "squeeze", "cloud", "passthrough", "stress"};
    for (const char* name : names)
    {
        const Scenario scenario = LoadScenario(name);
        uint32_t peak = 0;
        {
            ScenarioPlayer dryRun(scenario, 1, false);
            for (int frame = 0; frame < scenario.frames; ++frame)
            {
                peak = std::max(peak, static_cast<uint32_t>(dryRun.NextFrame().size()));
            }
        }
        CountingAllocator allocator;
        {
            ClusterEngine engine;
            EXPECT(io_context, engine.Init(&allocator, peak), "%s: Init failed", name);
            const uint64_t afterInit = allocator.GetAllocCount();
            EXPECT(io_context, afterInit == 1, "%s: Init made %llu allocations", name, static_cast<unsigned long long>(afterInit));
            ScenarioPlayer player(scenario, 5, true);
            for (int frame = 0; frame < scenario.frames; ++frame)
            {
                const std::vector<ClusterInput>& inputs = player.NextFrame();
                engine.Update(inputs.data(), static_cast<uint32_t>(inputs.size()), player.GetParams());
                if (allocator.GetAllocCount() != afterInit)
                {
                    io_context.Fail("%s frame %d: Update allocated with %zu inputs and %u reserved", name, frame, inputs.size(),
                                    peak);
                    break;
                }
            }

            std::vector<ClusterInput> grown;
            for (uint32_t index = 0; index <= peak; ++index)
            {
                grown.push_back(MakeInput(1000000 + index, index * 0.7, 10.0 + index));
            }
            InvariantChecker checker;
            FrameView view;
            for (int frame = 0; frame < 5; ++frame)
            {
                const bool updated = engine.Update(grown.data(), static_cast<uint32_t>(grown.size()), scenario.params);
                EXPECT(io_context, updated, "%s: Update failed to grow", name);
                CaptureFrame(engine, grown, updated, view);
                checker.Check(io_context, name, frame, view, scenario.params);
            }
            EXPECT(io_context, allocator.GetAllocCount() == afterInit + 1 && allocator.GetOutstandingBlocks() == 1,
                   "%s: growing did not replace the block once (allocs %llu, blocks %zu)", name,
                   static_cast<unsigned long long>(allocator.GetAllocCount()), allocator.GetOutstandingBlocks());
            engine.Term();
        }
        EXPECT(io_context, allocator.GetOutstandingBytes() == 0 && allocator.GetBadFreeCount() == 0,
               "%s: %zu bytes outstanding after Term", name, allocator.GetOutstandingBytes());
    }
}

void TestAllocationFailureKeepsState(TestContext& io_context)
{
    const ClusterParams params;
    std::vector<ClusterInput> small;
    for (uint64_t key = 1; key <= 8; ++key)
    {
        small.push_back(MakeInput(key, key * 2.0, 10.0));
    }
    std::vector<ClusterInput> large = small;
    for (uint64_t key = 100; key < 120; ++key)
    {
        large.push_back(MakeInput(key, key * 3.0, 20.0));
    }

    TestEngine failing(8);
    TestEngine reference(8);
    for (int frame = 0; frame < 5; ++frame)
    {
        failing.Step(small, params);
        reference.Step(small, params);
    }
    FrameView before;
    CaptureFrame(failing.GetEngine(), small, true, before);
    failing.GetAllocator().SetFailing(true);
    const bool updated = failing.GetEngine().Update(large.data(), static_cast<uint32_t>(large.size()), params);
    EXPECT(io_context, !updated, "Update succeeded although the allocator failed");
    FrameView after;
    CaptureFrame(failing.GetEngine(), small, true, after);
    EXPECT(io_context, MakeCanonical(before) == MakeCanonical(after), "failed Update changed the reported frame");
    failing.GetAllocator().SetFailing(false);

    for (int frame = 0; frame < 20; ++frame)
    {
        const FrameView& first = failing.Step(small, params);
        const CanonicalFrame left = MakeCanonical(first);
        const FrameView& second = reference.Step(small, params);
        EXPECT(io_context, left == MakeCanonical(second), "frame %d after a failed Update differs from the reference", frame);
    }
    EXPECT(io_context, failing.GetEngine().Update(large.data(), static_cast<uint32_t>(large.size()), params),
           "Update failed after the allocator recovered");
}

void TestResetClearsState(TestContext& io_context)
{
    const Scenario warmup = LoadScenario("oneshots");
    const Scenario after = LoadScenario("crossing");
    TestEngine resetEngine(128);
    TestEngine freshEngine(128);
    {
        ScenarioPlayer player(warmup, 3, true);
        for (int frame = 0; frame < 300; ++frame)
        {
            resetEngine.Step(player.NextFrame(), player.GetParams());
        }
    }
    const uint64_t allocationsBefore = resetEngine.GetAllocator().GetAllocCount();
    resetEngine.GetEngine().Reset();
    EXPECT(io_context, resetEngine.GetEngine().GetClusterCount() == 0 && resetEngine.GetEngine().GetContributionCount() == 0,
           "Reset kept reported clusters");
    EXPECT(io_context, resetEngine.GetAllocator().GetOutstandingBlocks() == 1, "Reset freed memory");

    ScenarioPlayer leftPlayer(after, 8, true);
    ScenarioPlayer rightPlayer(after, 8, true);
    for (int frame = 0; frame < 300; ++frame)
    {
        const FrameView& left = resetEngine.Step(leftPlayer.NextFrame(), leftPlayer.GetParams());
        if (frame == 0)
        {
            for (const ClusterInfo& cluster : left.clusters)
            {
                EXPECT(io_context, cluster.isNew, "cluster %u not new on the first frame after Reset", cluster.id);
            }
        }
        const CanonicalFrame leftCanonical = MakeCanonical(left);
        const FrameView& right = freshEngine.Step(rightPlayer.NextFrame(), rightPlayer.GetParams());
        if (!(leftCanonical == MakeCanonical(right)))
        {
            io_context.Fail("frame %d after Reset differs from a fresh engine", frame);
            break;
        }
    }
    EXPECT(io_context, resetEngine.GetAllocator().GetAllocCount() == allocationsBefore, "Reset or later updates allocated");
}

void TestTermBalance(TestContext& io_context)
{
    const Scenario scenario = LoadScenario("oneshots");
    {
        CountingAllocator allocator;
        ClusterEngine engine;
        engine.Init(&allocator, 256);
        ScenarioPlayer player(scenario, 1, false);
        for (int frame = 0; frame < 100; ++frame)
        {
            const std::vector<ClusterInput>& inputs = player.NextFrame();
            engine.Update(inputs.data(), static_cast<uint32_t>(inputs.size()), player.GetParams());
        }
        engine.Term();
        EXPECT(io_context, allocator.GetOutstandingBlocks() == 0 && allocator.GetOutstandingBytes() == 0,
               "Term left %zu blocks", allocator.GetOutstandingBlocks());
        engine.Term();
        EXPECT(io_context, allocator.GetBadFreeCount() == 0, "second Term freed twice");
    }
    {
        CountingAllocator allocator;
        ClusterEngine engine;
        engine.Init(&allocator, 32);
        engine.Init(&allocator, 64);
        EXPECT(io_context, allocator.GetOutstandingBlocks() == 1, "second Init kept %zu blocks", allocator.GetOutstandingBlocks());
        engine.Term();
        EXPECT(io_context, allocator.GetOutstandingBlocks() == 0, "Term after a second Init left memory");
    }
    {
        CountingAllocator allocator;
        {
            ClusterEngine engine;
            engine.Init(&allocator, 0);
            ScenarioPlayer player(scenario, 1, false);
            for (int frame = 0; frame < 100; ++frame)
            {
                const std::vector<ClusterInput>& inputs = player.NextFrame();
                EXPECT(io_context, engine.Update(inputs.data(), static_cast<uint32_t>(inputs.size()), player.GetParams()),
                       "Update failed to grow from an empty reserve");
            }
            EXPECT(io_context, allocator.GetOutstandingBlocks() == 1, "growing kept %zu blocks", allocator.GetOutstandingBlocks());
        }
        EXPECT(io_context, allocator.GetOutstandingBlocks() == 0 && allocator.GetBadFreeCount() == 0,
               "destructor left %zu blocks", allocator.GetOutstandingBlocks());
    }
    {
        ClusterEngine engine;
        engine.Term();
        engine.Reset();
        const ClusterInput input = MakeInput(1, 0.0, 1.0);
        EXPECT(io_context, !engine.Update(&input, 1, ClusterParams{}), "Update without Init succeeded");
        EXPECT(io_context, engine.Update(nullptr, 0, ClusterParams{}), "empty Update without Init failed");
        EXPECT(io_context, engine.GetClusterCount() == 0 && engine.GetContributionCount() == 0, "engine without Init reports");
        EXPECT(io_context, !engine.Init(nullptr, 4), "Init without allocator succeeded");
    }
}

void TestDegenerateInputs(TestContext& io_context)
{
    const ClusterParams params;
    {
        ClusterParams wide = params;
        wide.joinAngle = 180.f;
        wide.maxOutputs = 1;
        TestEngine engine;
        InvariantChecker checker;
        const std::vector<ClusterInput> inputs = {MakeRawInput(1, 0.f, 0.f, 10.f), MakeRawInput(2, 0.f, 0.f, -10.f)};
        for (int frame = 0; frame < 10; ++frame)
        {
            const FrameView& view = engine.Step(inputs, wide);
            checker.Check(io_context, "opposite-budget", frame, view, wide);
            EXPECT(io_context, view.clusters.size() == 1, "opposite inputs with maxOutputs 1 gave %zu clusters", view.clusters.size());
        }
        wide.maxOutputs = 0;
        TestEngine unlimited;
        for (int frame = 0; frame < 10; ++frame)
        {
            const FrameView& view = unlimited.Step(inputs, wide);
            EXPECT(io_context, view.clusters.size() == 2, "opposite inputs with joinAngle 180 gave %zu clusters",
                   view.clusters.size());
        }
    }
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        std::vector<ClusterInput> inputs = {MakeRawInput(1, nan, 0.f, 1.f),       MakeRawInput(2, inf, 0.f, 0.f),
                                            MakeRawInput(3, 0.f, -inf, 0.f),      MakeRawInput(4, 0.f, 0.f, 10.f),
                                            MakeRawInput(5, 0.1f, 0.f, 10.f),     MakeRawInput(6, 0.f, 0.f, 0.f),
                                            MakeRawInput(0, 1e30f, 0.f, 0.f),     MakeRawInput(~0ull, FLT_MAX, FLT_MAX, FLT_MAX),
                                            MakeRawInput(7, -FLT_MAX, 0.f, 1.f, nan), MakeRawInput(8, 1e-30f, 0.f, 0.f, -1.f),
                                            MakeRawInput(9, 0.f, 1.f, 0.f, inf),   MakeRawInput(10, 0.f, 1.f, 1e-3f, 1e30f)};
        TestEngine engine;
        InvariantChecker checker;
        for (int frame = 0; frame < 20; ++frame)
        {
            const FrameView& view = engine.Step(inputs, params);
            checker.Check(io_context, "non-finite", frame, view, params);
            for (size_t index : {size_t(0), size_t(1), size_t(2)})
            {
                const std::vector<Membership> memberships = FindMemberships(view, inputs[index].key);
                EXPECT(io_context, memberships.size() == 1, "non-finite input %zu has %zu memberships", index, memberships.size());
                if (memberships.size() == 1)
                {
                    const ClusterInfo& cluster = view.clusters[static_cast<size_t>(memberships[0].cluster)];
                    EXPECT(io_context, cluster.memberCount == 1, "non-finite input %zu shares a cluster", index);
                    EXPECT(io_context, cluster.position.x == 0.f && cluster.position.y == 0.f && cluster.position.z == 0.f,
                           "non-finite input %zu not at the origin", index);
                }
            }
            EXPECT(io_context, HomeIdOf(view, 4) == HomeIdOf(view, 5), "finite neighbours did not share a cluster");
            EXPECT(io_context, HomeIdOf(view, 6) >= 0 && HomeIdOf(view, 6) == HomeIdOf(view, 8),
                   "inputs at and next to the listener position did not share a cluster");
        }
        inputs[0] = MakeRawInput(1, 0.f, 0.f, 10.f);
        bool joined = false;
        for (int frame = 20; frame < 60; ++frame)
        {
            const FrameView& view = engine.Step(inputs, params);
            checker.Check(io_context, "recovered", frame, view, params);
            joined = joined || (HomeIdOf(view, 1) == HomeIdOf(view, 4));
        }
        EXPECT(io_context, joined, "input with a recovered position never joined its neighbours");
    }
    {
        ClusterParams broken = params;
        broken.joinAngle = std::numeric_limits<float>::quiet_NaN();
        broken.leaveRatio = std::numeric_limits<float>::quiet_NaN();
        broken.distanceWeight = std::numeric_limits<float>::infinity();
        broken.nearFieldRadius = std::numeric_limits<float>::quiet_NaN();
        broken.loudnessSmoothing = 5.f;
        Scenario scenario = LoadScenario("cloud");
        scenario.params = broken;
        InvariantChecker checker;
        RunOptions options;
        options.maxFrames = 200;
        PlayScenario(scenario, options, [&](int in_frame, const FrameView& in_view, const ClusterParams& in_params) {
            checker.Check(io_context, "bad-params", in_frame, in_view, in_params);
        });
        broken.joinAngle = 1e30f;
        broken.leaveRatio = -3.f;
        broken.distanceWeight = -1.f;
        broken.nearFieldRadius = std::numeric_limits<float>::infinity();
        scenario.params = broken;
        InvariantChecker secondChecker;
        PlayScenario(scenario, options, [&](int in_frame, const FrameView& in_view, const ClusterParams& in_params) {
            secondChecker.Check(io_context, "extreme-params", in_frame, in_view, in_params);
            EXPECT(io_context, in_view.clusters.size() == in_view.inputs.size(),
                   "infinite near field did not exclude every input (%zu clusters, %zu inputs)", in_view.clusters.size(),
                   in_view.inputs.size());
        });
    }
    {
        TestEngine engine;
        InvariantChecker checker;
        const std::vector<ClusterInput> inputs = {MakeInput(1, 0.0, 10.0), MakeInput(1, 30.0, 10.0), MakeInput(2, 0.2, 10.0),
                                                  MakeInput(2, 60.0, 10.0)};
        for (int frame = 0; frame < 10; ++frame)
        {
            const FrameView& view = engine.Step(inputs, params);
            checker.Check(io_context, "duplicates", frame, view, params);
            EXPECT(io_context, view.clusters.size() == 1, "duplicate keys gave %zu clusters", view.clusters.size());
        }
    }
    {
        TestEngine engine;
        const FrameView& empty = engine.Step({}, params);
        EXPECT(io_context, empty.updated && empty.clusters.empty() && empty.contributions.empty(), "empty update reported output");
        EXPECT(io_context, !engine.GetEngine().Update(nullptr, 3, params), "Update with null inputs succeeded");
        engine.Step({MakeInput(1, 0.0, 10.0), MakeInput(2, 1.0, 10.0)}, params);
        const FrameView& cleared = engine.Step({}, params);
        EXPECT(io_context, cleared.clusters.empty(), "clusters survived an empty update");
    }
}

void TestCrossfadeRamp(TestContext& io_context)
{
    for (uint32_t fade : {4u, 1u, 0u})
    {
        ClusterParams params;
        params.dwellFrames = 4;
        params.crossfadeFrames = fade;
        TestEngine engine(64, &io_context, "crossfade");
        std::vector<ClusterInput> inputs = {MakeInput(1, 0.0, 10.0), MakeInput(2, 0.3, 10.0), MakeInput(3, -0.3, 10.0)};
        for (int frame = 0; frame < 10; ++frame)
        {
            engine.Step(inputs, params);
        }
        const int oldId = HomeIdOf(engine.Step(inputs, params), 3);
        inputs[2] = MakeInput(3, 20.0, 10.0);
        const int moveFrame = static_cast<int>(params.dwellFrames) - 1;
        const uint32_t steps = std::max(fade, 1u);
        for (int frame = 0; frame < moveFrame; ++frame)
        {
            const FrameView& view = engine.Step(inputs, params);
            const std::vector<Membership> memberships = FindMemberships(view, 3);
            EXPECT(io_context, memberships.size() == 1 && static_cast<int>(memberships[0].id) == oldId,
                   "fade %u: input moved %d frames after leaving, before the dwell time", fade, frame);
        }
        int newId = -1;
        for (uint32_t step = 0; step <= steps; ++step)
        {
            const FrameView& view = engine.Step(inputs, params);
            const std::vector<Membership> memberships = FindMemberships(view, 3);
            if (step == steps)
            {
                EXPECT(io_context, memberships.size() == 1 && static_cast<int>(memberships[0].id) == newId &&
                                       memberships[0].gainStart == 1.f && memberships[0].gainEnd == 1.f,
                       "fade %u: input did not settle in the new cluster after %u frames", fade, steps);
                continue;
            }
            EXPECT(io_context, memberships.size() == 2, "fade %u step %u: %zu memberships while fading", fade, step,
                   memberships.size());
            if (memberships.size() != 2)
            {
                break;
            }
            const Membership& source = static_cast<int>(memberships[0].id) == oldId ? memberships[0] : memberships[1];
            const Membership& target = static_cast<int>(memberships[0].id) == oldId ? memberships[1] : memberships[0];
            if (step == 0)
            {
                newId = static_cast<int>(target.id);
                EXPECT(io_context, view.clusters[static_cast<size_t>(target.cluster)].isNew, "fade %u: target cluster not new",
                       fade);
            }
            const float expectedStart = static_cast<float>(step) / steps;
            const float expectedEnd = static_cast<float>(step + 1) / steps;
            EXPECT(io_context, std::fabs(target.gainStart - expectedStart) < 1e-6f && std::fabs(target.gainEnd - expectedEnd) < 1e-6f,
                   "fade %u step %u: target gains %f %f, expected %f %f", fade, step, target.gainStart, target.gainEnd,
                   expectedStart, expectedEnd);
            EXPECT(io_context, std::fabs(source.gainStart - (1.f - expectedStart)) < 1e-6f &&
                                   std::fabs(source.gainEnd - (1.f - expectedEnd)) < 1e-6f,
                   "fade %u step %u: source gains %f %f", fade, step, source.gainStart, source.gainEnd);
        }
    }
}

void TestMergeAfterDwell(TestContext& io_context)
{
    ClusterParams narrow;
    narrow.joinAngle = 3.f;
    narrow.dwellFrames = 6;
    narrow.crossfadeFrames = 2;
    ClusterParams wide = narrow;
    wide.joinAngle = 6.f;
    TestEngine engine(64, &io_context, "merge");
    const std::vector<ClusterInput> inputs = {MakeInput(1, 0.0, 10.0, 0.5f), MakeInput(2, 0.2, 10.0, 0.5f),
                                              MakeInput(3, 4.5, 10.0, 0.05f), MakeInput(4, 4.7, 10.0, 0.05f)};
    for (int frame = 0; frame < 10; ++frame)
    {
        const FrameView& view = engine.Step(inputs, narrow);
        EXPECT(io_context, view.clusters.size() == 2, "frame %d: expected two clusters with joinAngle 3", frame);
    }
    const FrameView& settled = engine.Step(inputs, narrow);
    const int heavyId = HomeIdOf(settled, 1);
    const int lightId = HomeIdOf(settled, 3);
    for (int frame = 0; frame < static_cast<int>(wide.dwellFrames) - 1; ++frame)
    {
        const FrameView& view = engine.Step(inputs, wide);
        EXPECT(io_context, HomeIdOf(view, 3) == lightId, "merge started after %d frames, before the dwell time", frame + 1);
    }
    const FrameView& merging = engine.Step(inputs, wide);
    EXPECT(io_context, HomeIdOf(merging, 3) == heavyId && HomeIdOf(merging, 4) == heavyId,
           "lighter cluster did not start merging into the heavier one after the dwell time");
    EXPECT(io_context, HomeIdOf(merging, 1) == heavyId, "heavier cluster members moved");
    for (int frame = 0; frame < 3; ++frame)
    {
        engine.Step(inputs, wide);
    }
    const FrameView& merged = engine.Step(inputs, wide);
    EXPECT(io_context, merged.clusters.size() == 1 && static_cast<int>(merged.clusters[0].id) == heavyId,
           "after the merge %zu clusters remain", merged.clusters.size());
}

void TestMergeWaitsForFadingMembers(TestContext& io_context)
{
    ClusterParams params;
    params.joinAngle = 4.f;
    params.dwellFrames = 2;
    params.crossfadeFrames = 10;
    TestEngine engine(64, &io_context, "merge-fading");
    std::vector<ClusterInput> inputs = {MakeInput(1, 0.0, 10.0, 0.5f), MakeInput(2, 0.2, 10.0, 0.5f),
                                        MakeInput(3, 8.0, 10.0, 0.05f), MakeInput(4, 8.2, 10.0, 0.05f),
                                        MakeInput(5, 0.1, 10.0, 0.01f)};
    for (int frame = 0; frame < 10; ++frame)
    {
        engine.Step(inputs, params);
    }
    const FrameView& settled = engine.Step(inputs, params);
    const int heavyId = HomeIdOf(settled, 1);
    const int lightId = HomeIdOf(settled, 3);
    EXPECT(io_context, heavyId != lightId && HomeIdOf(settled, 5) == heavyId,
           "setup did not give two clusters with key 5 in the heavy one");

    inputs[4] = MakeInput(5, 8.1, 10.0, 0.01f);
    bool moving = false;
    for (int frame = 0; frame < 10 && !moving; ++frame)
    {
        moving = HomeIdOf(engine.Step(inputs, params), 5) == lightId;
    }
    EXPECT(io_context, moving, "key 5 never started moving into the light cluster");

    ClusterParams wide = params;
    wide.joinAngle = 9.f;
    bool mergedWhileFading = false;
    bool followed = false;
    for (int frame = 0; frame < 40; ++frame)
    {
        const FrameView& view = engine.Step(inputs, wide);
        const std::vector<Membership> memberships = FindMemberships(view, 5);
        const int home = HomeIdOf(view, 5);
        if (HomeIdOf(view, 3) == heavyId && home == lightId)
        {
            mergedWhileFading = true;
        }
        if (!followed && home == heavyId)
        {
            followed = true;
            for (const Membership& membership : memberships)
            {
                EXPECT(io_context, static_cast<int>(membership.id) != lightId || membership.gainStart == 1.f,
                       "key 5 left the dissolving cluster before its fade into it finished");
            }
        }
    }
    EXPECT(io_context, mergedWhileFading, "the clusters did not merge while key 5 was still fading in");
    EXPECT(io_context, followed, "key 5 never followed the merge");
    const FrameView& merged = engine.Step(inputs, wide);
    EXPECT(io_context, merged.clusters.size() == 1 && HomeIdOf(merged, 5) == heavyId,
           "after following, %zu clusters remain", merged.clusters.size());
}

void TestGroupChangeCrossfades(TestContext& io_context)
{
    const ClusterParams params;
    TestEngine engine(64, &io_context, "group-change");
    std::vector<ClusterInput> inputs = {MakeInput(1, 0.0, 10.0, 0.1f, 1), MakeInput(2, 0.5, 10.0, 0.1f, 1),
                                        MakeInput(3, -0.5, 10.0, 0.1f, 1)};
    for (int frame = 0; frame < 10; ++frame)
    {
        engine.Step(inputs, params);
    }
    const int oldId = HomeIdOf(engine.Step(inputs, params), 3);
    inputs[2].group = 2;
    const uint32_t steps = std::max(params.crossfadeFrames, 1u);
    for (uint32_t step = 0; step < steps; ++step)
    {
        const FrameView& view = engine.Step(inputs, params);
        const std::vector<Membership> memberships = FindMemberships(view, 3);
        EXPECT(io_context, memberships.size() == 2, "step %u: input that changed group has %zu memberships, expected a crossfade",
               step, memberships.size());
        if (memberships.size() != 2)
        {
            break;
        }
        const Membership& source = static_cast<int>(memberships[0].id) == oldId ? memberships[0] : memberships[1];
        const Membership& target = static_cast<int>(memberships[0].id) == oldId ? memberships[1] : memberships[0];
        const float expectedStart = static_cast<float>(step) / steps;
        const float expectedEnd = static_cast<float>(step + 1) / steps;
        EXPECT(io_context, static_cast<int>(source.id) == oldId && static_cast<int>(target.id) != oldId,
               "step %u: input that changed group does not fade out of its old cluster", step);
        EXPECT(io_context, std::fabs(target.gainStart - expectedStart) < 1e-6f && std::fabs(target.gainEnd - expectedEnd) < 1e-6f,
               "step %u: target gains %f %f, expected %f %f", step, target.gainStart, target.gainEnd, expectedStart, expectedEnd);
    }
    int aloneId = -1;
    for (int frame = 0; frame < 10; ++frame)
    {
        const FrameView& view = engine.Step(inputs, params);
        const std::vector<Membership> memberships = FindMemberships(view, 3);
        EXPECT(io_context, memberships.size() == 1 && memberships[0].gainStart == 1.f && memberships[0].gainEnd == 1.f,
               "frame %d: input that changed group is not settled at full gain", frame);
        EXPECT(io_context, view.clusters.size() == 2, "frame %d: expected two clusters after the group change", frame);
        EXPECT(io_context, HomeIdOf(view, 3) != HomeIdOf(view, 1), "frame %d: changed input still shares", frame);
        aloneId = HomeIdOf(view, 3);
    }

    inputs[2].group = 5;
    for (int frame = 0; frame < 5; ++frame)
    {
        const FrameView& view = engine.Step(inputs, params);
        const std::vector<Membership> memberships = FindMemberships(view, 3);
        EXPECT(io_context, memberships.size() == 1 && static_cast<int>(memberships[0].id) == aloneId &&
                               memberships[0].gainStart == 1.f && memberships[0].gainEnd == 1.f,
               "frame %d: input alone in its cluster moved when its group changed", frame);
    }

    inputs[1].clusterable = false;
    const FrameView& view = engine.Step(inputs, params);
    EXPECT(io_context, FindMemberships(view, 2).empty(), "input that became passthrough still has a contribution");

    ClusterParams slow;
    slow.dwellFrames = 2;
    slow.crossfadeFrames = 20;
    TestEngine fading(64, &io_context, "group-change-mid-fade");
    inputs = {MakeInput(1, 0.0, 10.0, 0.5f), MakeInput(2, 0.5, 10.0, 0.5f), MakeInput(3, -0.5, 10.0, 0.1f),
              MakeInput(10, 30.0, 10.0, 0.5f), MakeInput(20, 60.0, 10.0, 0.5f)};
    for (int frame = 0; frame < 10; ++frame)
    {
        fading.Step(inputs, slow);
    }
    inputs[2] = MakeInput(3, 29.0, 10.0, 0.1f);
    bool fadeStarted = false;
    for (int frame = 0; frame < 5; ++frame)
    {
        fadeStarted = fadeStarted || FindMemberships(fading.Step(inputs, slow), 3).size() == 2;
    }
    EXPECT(io_context, fadeStarted, "setup did not start a fade of key 3");
    inputs[2].group = 9;
    for (int frame = 0; frame < 60; ++frame)
    {
        fading.Step(inputs, slow);
    }
    const FrameView& settled = fading.Step(inputs, slow);
    const std::vector<Membership> memberships = FindMemberships(settled, 3);
    EXPECT(io_context, memberships.size() == 1 && memberships[0].gainEnd == 1.f &&
                           settled.clusters[static_cast<size_t>(memberships[0].cluster)].memberCount == 1,
           "input whose group changed during a fade did not end alone in its own cluster");
}

void TestCentredInputsShare(TestContext& io_context)
{
    for (float offset : {0.f, 1e-5f})
    {
        std::vector<ClusterInput> inputs;
        for (uint64_t key = 1; key <= 40; ++key)
        {
            inputs.push_back(MakeRawInput(key, 0.f, 0.f, offset));
        }
        ClusterParams params;
        TestEngine unlimited(64, &io_context, "centred");
        for (int frame = 0; frame < 30; ++frame)
        {
            const FrameView& view = unlimited.Step(inputs, params);
            EXPECT(io_context, view.clusters.size() == 1, "offset %g frame %d: 40 inputs at the listener gave %zu clusters",
                   offset, frame, view.clusters.size());
        }
        params.maxOutputs = 8;
        TestEngine budget(64, &io_context, "centred-budget");
        for (int frame = 0; frame < 30; ++frame)
        {
            const FrameView& view = budget.Step(inputs, params);
            EXPECT(io_context, !view.overBudget && view.clusters.size() <= 8,
                   "offset %g frame %d: 40 inputs at the listener with maxOutputs 8 gave %zu clusters, over budget %d", offset,
                   frame, view.clusters.size(), view.overBudget ? 1 : 0);
        }
        params.joinAngle = 0.f;
        TestEngine noJoin(64, &io_context, "centred-join0");
        for (int frame = 0; frame < 30; ++frame)
        {
            const FrameView& view = noJoin.Step(inputs, params);
            EXPECT(io_context, !view.overBudget && view.clusters.size() <= 8,
                   "offset %g frame %d: 40 inputs at the listener with joinAngle 0 and maxOutputs 8 gave %zu clusters", offset,
                   frame, view.clusters.size());
        }
        ClusterParams nearField;
        nearField.nearFieldRadius = 1.f;
        TestEngine excluded(64, &io_context, "centred-near-field");
        for (int frame = 0; frame < 5; ++frame)
        {
            const FrameView& view = excluded.Step(inputs, nearField);
            EXPECT(io_context, view.clusters.size() == 40,
                   "offset %g frame %d: 40 inputs inside the near field gave %zu clusters", offset, frame, view.clusters.size());
        }
    }

    ClusterParams wide;
    wide.joinAngle = 180.f;
    TestEngine mixed(64, &io_context, "centred-mixed");
    std::vector<ClusterInput> inputs = {MakeRawInput(1, 0.f, 0.f, 0.f), MakeRawInput(2, 0.f, 0.f, 0.f), MakeInput(3, 0.0, 10.0),
                                        MakeInput(4, 90.0, 10.0)};
    for (int frame = 0; frame < 30; ++frame)
    {
        const FrameView& view = mixed.Step(inputs, wide);
        EXPECT(io_context, view.clusters.size() == 2 && HomeIdOf(view, 1) == HomeIdOf(view, 2) &&
                               HomeIdOf(view, 3) == HomeIdOf(view, 4) && HomeIdOf(view, 1) != HomeIdOf(view, 3),
               "frame %d: inputs at the listener shared a cluster with inputs that have a direction", frame);
    }
    inputs[1] = MakeInput(2, 45.0, 10.0);
    for (int frame = 0; frame < 30; ++frame)
    {
        mixed.Step(inputs, wide);
    }
    const FrameView& moved = mixed.Step(inputs, wide);
    EXPECT(io_context, HomeIdOf(moved, 2) == HomeIdOf(moved, 3) && HomeIdOf(moved, 1) != HomeIdOf(moved, 3),
           "input that moved away from the listener did not join the inputs with a direction");
}

void TestBudgetFarInputGetsOwnCluster(TestContext& io_context)
{
    ClusterParams params;
    params.maxOutputs = 3;
    TestEngine engine(64, &io_context, "budget-far");
    std::vector<ClusterInput> inputs = {MakeInput(1, 0.0, 10.0), MakeInput(2, 6.0, 10.0), MakeInput(3, 12.0, 10.0)};
    for (int frame = 0; frame < 30; ++frame)
    {
        engine.Step(inputs, params);
    }
    EXPECT(io_context, engine.Step(inputs, params).clusters.size() == 3, "setup did not give three clusters");
    inputs.push_back(MakeInput(4, 180.0, 10.0));
    double worst = 0.0;
    for (int frame = 0; frame < 600; ++frame)
    {
        const FrameView& view = engine.Step(inputs, params);
        EXPECT(io_context, !view.overBudget, "frame %d: flagged over budget although two clusters can merge", frame);
        const std::vector<Membership> far = FindMemberships(view, 4);
        EXPECT(io_context, far.size() == 1 && view.clusters[static_cast<size_t>(far[0].cluster)].memberCount == 1,
               "frame %d: input at 180 deg does not have a cluster of its own", frame);
        EXPECT(io_context, HomeIdOf(view, 1) == HomeIdOf(view, 2) && HomeIdOf(view, 3) != HomeIdOf(view, 1),
               "frame %d: the clusters at 0 and 6 deg did not merge", frame);
        EXPECT(io_context, frame < static_cast<int>(params.crossfadeFrames) || view.clusters.size() == 3,
               "frame %d: %zu clusters with maxOutputs 3", frame, view.clusters.size());
        const std::vector<int> home = FindHomeClusters(view);
        for (size_t index = 0; index < view.inputs.size(); ++index)
        {
            if (home[index] >= 0)
            {
                worst = std::max(worst, AngleBetween(view.inputs[index].position,
                                                     view.clusters[static_cast<size_t>(home[index])].position));
            }
        }
    }
    EXPECT(io_context, worst <= 12.05, "largest angle between an input and its cluster %.3f deg, above 12 deg", worst);
}

void TestOverBudgetOnlyWhenUnmet(TestContext& io_context)
{
    ClusterParams params;
    params.maxOutputs = 2;
    TestEngine engine(64, &io_context, "budget-flag");
    std::vector<ClusterInput> inputs = {MakeInput(1, 0.0, 10.0), MakeInput(2, 90.0, 10.0)};
    for (int frame = 0; frame < 20; ++frame)
    {
        engine.Step(inputs, params);
    }
    inputs.push_back(MakeInput(3, 45.0, 10.0, 0.1f, 7));
    for (int frame = 0; frame < 10; ++frame)
    {
        const FrameView& view = engine.Step(inputs, params);
        EXPECT(io_context, !view.overBudget, "frame %d: flagged over budget although the group 0 clusters can merge", frame);
        EXPECT(io_context, frame < static_cast<int>(params.crossfadeFrames) || view.clusters.size() == 2,
               "frame %d: %zu clusters with maxOutputs 2", frame, view.clusters.size());
    }
}

void TestLoudnessWeighting(TestContext& io_context)
{
    const double loudAngle = -2.0;
    const double quietAngle = 2.0;
    auto expectedAngle = [&](double in_loudWeight, double in_quietWeight) {
        const double loud = loudAngle * kPi / 180.0;
        const double quiet = quietAngle * kPi / 180.0;
        const double x = in_loudWeight * std::sin(loud) + in_quietWeight * std::sin(quiet);
        const double z = in_loudWeight * std::cos(loud) + in_quietWeight * std::cos(quiet);
        return std::atan2(x, z) * kDegreesPerRadian;
    };
    auto clusterAngle = [](const FrameView& in_view) {
        const ClusterVec3& position = in_view.clusters[0].position;
        return std::atan2(static_cast<double>(position.x), static_cast<double>(position.z)) * kDegreesPerRadian;
    };

    ClusterParams params;
    std::vector<ClusterInput> inputs = {MakeInput(1, loudAngle, 10.0, 1.0f), MakeInput(2, quietAngle, 10.0, 0.001f)};
    {
        TestEngine engine(64, &io_context, "loudness");
        const FrameView& view = engine.Step(inputs, params);
        EXPECT(io_context, view.clusters.size() == 1, "two inputs 4 deg apart gave %zu clusters", view.clusters.size());
        const double expected = expectedAngle(1.0 + 1e-6, 0.001 + 1e-6);
        EXPECT(io_context, std::fabs(clusterAngle(view) - expected) < 0.01, "weighted angle %f, expected %f",
               clusterAngle(view), expected);
        EXPECT(io_context, view.clusters[0].representative == 0, "representative is not the loudest input");
    }
    {
        params.loudnessWeighting = false;
        TestEngine engine(64, &io_context, "loudness");
        const FrameView& view = engine.Step(inputs, params);
        EXPECT(io_context, std::fabs(clusterAngle(view)) < 0.01, "unweighted angle %f, expected 0", clusterAngle(view));
    }
    {
        params.loudnessWeighting = true;
        params.loudnessSmoothing = 0.9f;
        TestEngine engine(64, &io_context, "loudness");
        for (int frame = 0; frame < 5; ++frame)
        {
            engine.Step(inputs, params);
        }
        inputs[1].loudness = 1.0f;
        const FrameView& view = engine.Step(inputs, params);
        const double smoothed = 0.9 * 0.001 + 0.1 * 1.0;
        const double expected = expectedAngle(1.0 + 1e-6, smoothed + 1e-6);
        EXPECT(io_context, std::fabs(clusterAngle(view) - expected) < 0.01, "smoothed angle %f, expected %f", clusterAngle(view),
               expected);

        TestEngine fresh(64, &io_context, "loudness-fresh");
        const std::vector<ClusterInput> single = {MakeInput(1, loudAngle, 10.0, 1.0f)};
        for (int frame = 0; frame < 5; ++frame)
        {
            fresh.Step(single, params);
        }
        const std::vector<ClusterInput> loudPair = {MakeInput(1, loudAngle, 10.0, 1.0f), MakeInput(2, quietAngle, 10.0, 1.0f)};
        const FrameView& first = fresh.Step(loudPair, params);
        EXPECT(io_context, first.clusters.size() == 1 && std::fabs(clusterAngle(first)) < 0.01,
               "first frame of a new input did not use its raw loudness (angle %f)", clusterAngle(first));
    }
}

/** @brief Test with the name used to select it. */
struct TestCase
{
    const char* name;               ///< Name of the test.
    void (*function)(TestContext&); ///< Function that runs the test.
};

const TestCase kTests[] = {
    {"invariants_all_scenarios", TestInvariants},
    {"invariants_parameter_sweep", TestInvariantsParameterSweep},
    {"order_independence", TestOrderIndependence},
    {"static_scene_settles", TestStaticSettles},
    {"jitter_makes_no_moves", TestJitterMakesNoMoves},
    {"joins_within_join_angle", TestJoinsWithinJoinAngle},
    {"leave_within_dwell_and_fade", TestLeaveBound},
    {"budget_respected", TestBudgetRespected},
    {"budget_overflow_flagged", TestBudgetOverflowFlagged},
    {"over_budget_only_when_unmet", TestOverBudgetOnlyWhenUnmet},
    {"budget_far_input_alone", TestBudgetFarInputGetsOwnCluster},
    {"excluded_inputs_alone", TestExcludedInputsAlone},
    {"centred_inputs_share", TestCentredInputsShare},
    {"groups_never_share", TestGroupsNeverShare},
    {"no_allocations_in_update", TestNoAllocationsInUpdate},
    {"allocation_failure_keeps_state", TestAllocationFailureKeepsState},
    {"reset_clears_state", TestResetClearsState},
    {"term_returns_allocator_balance", TestTermBalance},
    {"degenerate_inputs", TestDegenerateInputs},
    {"crossfade_ramp", TestCrossfadeRamp},
    {"merge_after_dwell", TestMergeAfterDwell},
    {"merge_waits_for_fading_members", TestMergeWaitsForFadingMembers},
    {"group_change_crossfades", TestGroupChangeCrossfades},
    {"loudness_weighting", TestLoudnessWeighting},
};
}

int RunTests(const std::vector<std::string>& in_args)
{
    const std::string filter = in_args.empty() ? std::string() : in_args[0];
    int failedTests = 0;
    int ranTests = 0;
    for (const TestCase& test : kTests)
    {
        if (!filter.empty() && std::string(test.name).find(filter) == std::string::npos)
        {
            continue;
        }
        ++ranTests;
        TestContext context(test.name);
        const auto start = std::chrono::steady_clock::now();
        test.function(context);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (context.GetFailures() == 0)
        {
            std::printf("PASS %-32s %7.2f s\n", test.name, seconds);
        }
        else
        {
            std::printf("FAIL %-32s %7.2f s (%d failed checks)\n", test.name, seconds, context.GetFailures());
            ++failedTests;
        }
    }
    std::printf("%d of %d tests passed\n", ranTests - failedTests, ranTests);
    return failedTests == 0 && ranTests > 0 ? 0 : 1;
}
