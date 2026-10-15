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

#include <algorithm>
#include <cmath>
#include <cstdlib>

void* CountingAllocator::Alloc(size_t in_size)
{
    if (m_failing)
    {
        return nullptr;
    }
    void* pBlock = std::malloc(in_size == 0 ? 1 : in_size);
    if (pBlock == nullptr)
    {
        return nullptr;
    }
    m_blocks[pBlock] = in_size;
    m_outstandingBytes += in_size;
    ++m_allocCount;
    return pBlock;
}

void CountingAllocator::Free(void* in_pBlock)
{
    const auto found = m_blocks.find(in_pBlock);
    if (found == m_blocks.end())
    {
        ++m_badFreeCount;
        return;
    }
    m_outstandingBytes -= found->second;
    m_blocks.erase(found);
    std::free(in_pBlock);
}

void CountingAllocator::SetFailing(bool in_failing)
{
    m_failing = in_failing;
}

uint64_t CountingAllocator::GetAllocCount() const
{
    return m_allocCount;
}

size_t CountingAllocator::GetOutstandingBlocks() const
{
    return m_blocks.size();
}

size_t CountingAllocator::GetOutstandingBytes() const
{
    return m_outstandingBytes;
}

uint64_t CountingAllocator::GetBadFreeCount() const
{
    return m_badFreeCount;
}

void CaptureFrame(const ClusterEngine& in_engine, const std::vector<ClusterInput>& in_inputs, bool in_updated, FrameView& out_view)
{
    out_view.inputs = in_inputs;
    out_view.updated = in_updated;
    out_view.overBudget = in_engine.IsOverBudget();
    out_view.clusters.resize(in_engine.GetClusterCount());
    for (uint32_t index = 0; index < in_engine.GetClusterCount(); ++index)
    {
        out_view.clusters[index] = in_engine.GetCluster(index);
    }
    out_view.contributions.resize(in_engine.GetContributionCount());
    for (uint32_t index = 0; index < in_engine.GetContributionCount(); ++index)
    {
        out_view.contributions[index] = in_engine.GetContribution(index);
    }
}

bool IsFiniteVec(const ClusterVec3& in_v)
{
    return std::isfinite(in_v.x) && std::isfinite(in_v.y) && std::isfinite(in_v.z);
}

double LengthOf(const ClusterVec3& in_v)
{
    const double x = in_v.x;
    const double y = in_v.y;
    const double z = in_v.z;
    return std::sqrt(x * x + y * y + z * z);
}

double AngleBetween(const ClusterVec3& in_a, const ClusterVec3& in_b)
{
    if (!IsFiniteVec(in_a) || !IsFiniteVec(in_b))
    {
        return 0.0;
    }
    const double lengthA = LengthOf(in_a);
    const double lengthB = LengthOf(in_b);
    if (lengthA <= 0.0 || lengthB <= 0.0)
    {
        return 0.0;
    }
    const double dot = (static_cast<double>(in_a.x) * in_b.x + static_cast<double>(in_a.y) * in_b.y +
                        static_cast<double>(in_a.z) * in_b.z) /
                       (lengthA * lengthB);
    return std::acos(std::max(-1.0, std::min(1.0, dot))) * kDegreesPerRadian;
}

bool IsExcludedInput(const ClusterInput& in_input, const ClusterParams& in_params)
{
    if (!in_input.clusterable)
    {
        return false;
    }
    if (!IsFiniteVec(in_input.position))
    {
        return true;
    }
    const bool nearField = in_params.nearFieldRadius > 0.f && LengthOf(in_input.position) < in_params.nearFieldRadius;
    return nearField || in_input.priority > in_params.excludePriorityAbove;
}

std::vector<int> FindHomeClusters(const FrameView& in_view)
{
    std::vector<int> home(in_view.inputs.size(), -1);
    std::vector<float> best(in_view.inputs.size(), -1.f);
    for (const ClusterContribution& contribution : in_view.contributions)
    {
        if (contribution.input >= in_view.inputs.size())
        {
            continue;
        }
        if (contribution.gainEnd >= contribution.gainStart && contribution.gainEnd > 0.f &&
            contribution.gainEnd > best[contribution.input])
        {
            best[contribution.input] = contribution.gainEnd;
            home[contribution.input] = static_cast<int>(contribution.cluster);
        }
    }
    return home;
}

TimingSummary SummarizeTimings(const std::vector<double>& in_micros)
{
    TimingSummary summary;
    if (in_micros.empty())
    {
        return summary;
    }
    std::vector<double> sorted = in_micros;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0.0;
    for (double time : in_micros)
    {
        sum += time;
    }
    summary.mean = sum / static_cast<double>(in_micros.size());
    summary.p50 = sorted[sorted.size() / 2];
    summary.p99 = sorted[std::min(sorted.size() - 1, static_cast<size_t>(static_cast<double>(sorted.size()) * 0.99))];
    summary.max = sorted.back();
    return summary;
}

FrameMetrics FrameTracker::Observe(int in_frame, const FrameView& in_view, double in_updateMicroseconds)
{
    FrameMetrics metrics;
    metrics.frame = in_frame;
    metrics.inputs = static_cast<uint32_t>(in_view.inputs.size());
    metrics.clusters = static_cast<uint32_t>(in_view.clusters.size());
    metrics.overBudget = in_view.overBudget;
    metrics.updateMicroseconds = in_updateMicroseconds;

    std::unordered_set<uint64_t> clusterable;
    for (const ClusterInput& input : in_view.inputs)
    {
        if (input.clusterable)
        {
            clusterable.insert(input.key);
        }
        else
        {
            ++metrics.passthrough;
        }
    }
    metrics.outputs = metrics.clusters + metrics.passthrough;
    for (const ClusterInfo& cluster : in_view.clusters)
    {
        if (cluster.isNew)
        {
            ++metrics.newClusters;
        }
    }
    for (const ClusterContribution& contribution : in_view.contributions)
    {
        const ClusterInput& input = in_view.inputs[contribution.input];
        const ClusterInfo& cluster = in_view.clusters[contribution.cluster];
        if (contribution.gainStart == 0.f && m_previousClusterable.count(input.key) != 0)
        {
            ++metrics.moves;
        }
        if (contribution.gainEnd > 0.f)
        {
            metrics.maxMemberAngle = std::max(metrics.maxMemberAngle, AngleBetween(input.position, cluster.position));
        }
    }
    m_previousClusterable.swap(clusterable);
    return metrics;
}
