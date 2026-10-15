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

#include "ClusterEngine.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/** @brief Allocator that counts blocks and bytes and can be told to fail. */
class CountingAllocator final : public ClusterAllocator
{
public:
    /** @brief Allocates a block with malloc, or returns nullptr when failing is enabled. */
    void* Alloc(size_t in_size) override;

    /** @brief Frees a block returned by Alloc. */
    void Free(void* in_pBlock) override;

    /** @brief Makes every following Alloc fail while set. */
    void SetFailing(bool in_failing);

    /** @brief Returns the number of successful Alloc calls. */
    uint64_t GetAllocCount() const;

    /** @brief Returns the number of blocks not yet freed. */
    size_t GetOutstandingBlocks() const;

    /** @brief Returns the number of bytes not yet freed. */
    size_t GetOutstandingBytes() const;

    /** @brief Returns the number of Free calls with a pointer that was not allocated here. */
    uint64_t GetBadFreeCount() const;

private:
    std::unordered_map<void*, size_t> m_blocks; ///< Live blocks and their sizes.
    size_t m_outstandingBytes = 0;              ///< Bytes not yet freed.
    uint64_t m_allocCount = 0;                  ///< Successful Alloc calls.
    uint64_t m_badFreeCount = 0;                ///< Free calls with unknown pointers.
    bool m_failing = false;                     ///< Makes Alloc fail while set.
};

/** @brief Copy of the engine output of one frame together with its inputs. */
struct FrameView
{
    std::vector<ClusterInput> inputs;                ///< Inputs given to the update.
    std::vector<ClusterInfo> clusters;               ///< Reported clusters.
    std::vector<ClusterContribution> contributions;  ///< Reported contributions.
    bool overBudget = false;                         ///< Value of IsOverBudget.
    bool updated = false;                            ///< Return value of Update.
};

/** @brief Copies the current engine output into a frame view. */
void CaptureFrame(const ClusterEngine& in_engine, const std::vector<ClusterInput>& in_inputs, bool in_updated, FrameView& out_view);

/** @brief Returns the angle in degrees between two vectors, or zero if either is zero or not finite. */
double AngleBetween(const ClusterVec3& in_a, const ClusterVec3& in_b);

/** @brief Returns true if a position has only finite coordinates. */
bool IsFiniteVec(const ClusterVec3& in_v);

/** @brief Returns the length of a vector. */
double LengthOf(const ClusterVec3& in_v);

/** @brief Returns true if the engine treats an input as excluded under the given parameters. */
bool IsExcludedInput(const ClusterInput& in_input, const ClusterParams& in_params);

/** @brief Returns, per input, the index of its home cluster among the reported clusters, or -1. */
std::vector<int> FindHomeClusters(const FrameView& in_view);

/** @brief Summary of a series of timings in microseconds. */
struct TimingSummary
{
    double mean = 0.0; ///< Mean time.
    double p50 = 0.0;  ///< Median time.
    double p99 = 0.0;  ///< 99th percentile time.
    double max = 0.0;  ///< Largest time.
};

/** @brief Returns the mean, median, 99th percentile and largest of a series of timings, or zeros if it is empty. */
TimingSummary SummarizeTimings(const std::vector<double>& in_micros);

/** @brief Metrics of one frame of a scenario run. */
struct FrameMetrics
{
    int frame = 0;                  ///< Frame index.
    uint32_t inputs = 0;            ///< Number of inputs.
    uint32_t passthrough = 0;       ///< Number of passthrough inputs.
    uint32_t clusters = 0;          ///< Number of reported clusters.
    uint32_t outputs = 0;           ///< Clusters plus passthrough inputs.
    uint32_t moves = 0;             ///< Existing inputs that started fading into another cluster.
    uint32_t newClusters = 0;       ///< Clusters reported for the first time.
    double maxMemberAngle = 0.0;    ///< Largest angle between a member and its cluster in degrees.
    bool overBudget = false;        ///< Value of IsOverBudget.
    double updateMicroseconds = 0;  ///< Time spent in Update.
};

/** @brief Follows engine output over frames and computes per-frame metrics. */
class FrameTracker
{
public:
    /** @brief Computes the metrics of a frame and remembers the frame for the next one. */
    FrameMetrics Observe(int in_frame, const FrameView& in_view, double in_updateMicroseconds);

private:
    std::unordered_set<uint64_t> m_previousClusterable; ///< Keys of clusterable inputs in the previous frame.
};
