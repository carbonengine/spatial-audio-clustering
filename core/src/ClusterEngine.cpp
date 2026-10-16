/*
 * Copyright 2026 Fenris Creations
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
 */

#include "ClusterEngine.h"

#include <cfloat>
#include <cmath>
#include <cstring>

namespace
{
/** @brief Index value that marks a missing slot, node, cluster or partner. */
constexpr uint32_t kNone = 0xFFFFFFFFu;

/** @brief Largest number of inputs the engine accepts. */
constexpr uint32_t kMaxCapacity = 1u << 24;

/** @brief Smallest number of slots reserved when the engine grows. */
constexpr uint32_t kMinGrowth = 16;

/** @brief Number of clusters reserved for each slot. */
constexpr uint32_t kClustersPerSlot = 3;

/** @brief Alignment in bytes of each array in the memory block. */
constexpr size_t kBlockAlignment = 16;

/** @brief Distance from the listener below which an input has no direction. */
constexpr float kMinRange = 1e-4f;

/** @brief Weight added to the loudness so that silent inputs still count. */
constexpr float kLoudnessFloor = 1e-6f;

/** @brief Largest accepted input loudness. */
constexpr float kMaxLoudness = 1e9f;

/** @brief Largest accepted loudness smoothing coefficient. */
constexpr float kMaxSmoothing = 0.999999f;

/** @brief Ratio of a circle's circumference to its diameter. */
constexpr double kPi = 3.14159265358979323846;

/** @brief Number of degrees in one radian. */
constexpr double kDegreesPerRadian = 57.295779513082320876798;

/** @brief Number of radians in one degree. */
constexpr double kRadiansPerDegree = 0.017453292519943295769237;

/** @brief Share of the total weight below which a summed direction counts as cancelled out. */
constexpr double kCancelRatio = 1e-6;

/** @brief Distance larger than any distance between two clusters. */
constexpr double kAnyDistance = 1e30;

/** @brief Cosine below every real cosine, used when no angle limit applies. */
constexpr double kNoCosineLimit = -2.0;

/** @brief Largest distance weight the engine accepts, as a safety cap. */
constexpr double kDistanceWeightCap = 1e6;

/** @brief Largest leave ratio the engine accepts, as a safety cap. */
constexpr double kLeaveRatioCap = 1e6;

/** @brief Margin added to the direction Z band of the merge partner search. */
constexpr float kBandSlack = 1e-5f;

/** @brief Bit of a merge class that marks clusters of inputs with no direction. */
constexpr uint64_t kCentredClassBit = 1ull << 32;

/** @brief Returns the smaller of two values. */
template <typename T>
T MinOf(T in_a, T in_b)
{
    return in_b < in_a ? in_b : in_a;
}

/** @brief Returns the larger of two values. */
template <typename T>
T MaxOf(T in_a, T in_b)
{
    return in_a < in_b ? in_b : in_a;
}

/** @brief Returns the angle in degrees for a cosine, from 0 to 180. */
double AngleFromDot(double in_dot)
{
    if (in_dot >= 1.0)
    {
        return 0.0;
    }
    if (in_dot <= -1.0)
    {
        return 180.0;
    }
    return std::acos(in_dot) * kDegreesPerRadian;
}

/** @brief Returns the cosine of an angle in degrees, or kNoCosineLimit for angles above 180 degrees. */
double CosineLimit(double in_angle)
{
    if (in_angle > 180.0)
    {
        return kNoCosineLimit;
    }
    return std::cos(in_angle * kRadiansPerDegree);
}

/** @brief Returns the base 2 logarithm of a range, with the range raised to at least kMinRange. */
float LogRange(float in_range)
{
    return std::log2(MaxOf(in_range, kMinRange));
}

/** @brief Mixes the bits of an input key for the slot table. */
uint64_t MixKey(uint64_t in_key)
{
    uint64_t mixed = in_key + 0x9E3779B97F4A7C15ull;
    mixed = (mixed ^ (mixed >> 30)) * 0xBF58476D1CE4E5B9ull;
    mixed = (mixed ^ (mixed >> 27)) * 0x94D049BB133111EBull;
    return mixed ^ (mixed >> 31);
}

/** @brief Returns the index of the lowest set bit of a nonzero word. */
uint32_t LowestSetBit(uint64_t in_word)
{
    uint32_t bit = 0;
    if ((in_word & 0xFFFFFFFFull) == 0)
    {
        bit += 32;
        in_word >>= 32;
    }
    if ((in_word & 0xFFFFull) == 0)
    {
        bit += 16;
        in_word >>= 16;
    }
    if ((in_word & 0xFFull) == 0)
    {
        bit += 8;
        in_word >>= 8;
    }
    if ((in_word & 0xFull) == 0)
    {
        bit += 4;
        in_word >>= 4;
    }
    if ((in_word & 0x3ull) == 0)
    {
        bit += 2;
        in_word >>= 2;
    }
    if ((in_word & 0x1ull) == 0)
    {
        bit += 1;
    }
    return bit;
}

/** @brief Clears and returns the lowest set bit of a bit set, or kNone if no bit is set. */
uint32_t TakeLowestFree(uint64_t* io_pWords, uint32_t in_capacity)
{
    const uint32_t wordCount = (in_capacity + 63u) / 64u;
    for (uint32_t word = 0; word < wordCount; ++word)
    {
        if (io_pWords[word] != 0)
        {
            const uint32_t bit = LowestSetBit(io_pWords[word]);
            io_pWords[word] &= ~(1ull << bit);
            return word * 64u + bit;
        }
    }
    return kNone;
}

/** @brief Sets one bit of a bit set. */
void MarkFree(uint64_t* io_pWords, uint32_t in_index)
{
    io_pWords[in_index / 64u] |= 1ull << (in_index % 64u);
}

/** @brief Sorts items in place with a heap sort, using a function that returns true if the first item comes before the second. */
template <typename Item, typename IsBefore>
void HeapSort(Item* io_pItems, uint32_t in_count, IsBefore in_isBefore)
{
    auto siftDown = [io_pItems, &in_isBefore](uint32_t in_root, uint32_t in_end) {
        uint32_t root = in_root;
        for (;;)
        {
            const uint32_t left = root * 2u + 1u;
            if (left >= in_end)
            {
                return;
            }
            uint32_t largest = left;
            const uint32_t right = left + 1u;
            if (right < in_end && in_isBefore(io_pItems[left], io_pItems[right]))
            {
                largest = right;
            }
            if (!in_isBefore(io_pItems[root], io_pItems[largest]))
            {
                return;
            }
            const Item swap = io_pItems[root];
            io_pItems[root] = io_pItems[largest];
            io_pItems[largest] = swap;
            root = largest;
        }
    };
    if (in_count < 2)
    {
        return;
    }
    for (uint32_t start = in_count / 2u; start-- > 0;)
    {
        siftDown(start, in_count);
    }
    for (uint32_t end = in_count - 1u; end > 0; --end)
    {
        const Item swap = io_pItems[0];
        io_pItems[0] = io_pItems[end];
        io_pItems[end] = swap;
        siftDown(0, end);
    }
}

/** @brief Sorts indices by the values they refer to, with equal values in index order. */
void SortIndicesByValue(uint32_t* io_pItems, uint32_t in_count, const float* in_pValues)
{
    HeapSort(io_pItems, in_count, [in_pValues](uint32_t in_first, uint32_t in_second) {
        return in_pValues[in_first] < in_pValues[in_second] ||
               (in_pValues[in_first] == in_pValues[in_second] && in_first < in_second);
    });
}

/** @brief Returns the smallest power of two that is not less than a value. */
uint32_t NextPowerOfTwo(uint32_t in_value)
{
    uint32_t power = 1;
    while (power < in_value)
    {
        power <<= 1;
    }
    return power;
}

/** @brief Adds an aligned array of the given size to a block layout and returns its offset. */
size_t TakeBytes(size_t& io_size, size_t in_bytes)
{
    const size_t offset = io_size;
    io_size = (io_size + in_bytes + kBlockAlignment - 1) & ~(kBlockAlignment - 1);
    return offset;
}

/** @brief Returns the bit pattern of a float. */
uint32_t FloatBits(float in_value)
{
    uint32_t bits = 0;
    memcpy(&bits, &in_value, sizeof(bits));
    return bits;
}

/** @brief Returns true if a float is neither infinite nor NaN. */
bool IsFiniteFloat(float in_value)
{
    return (FloatBits(in_value) & 0x7F800000u) != 0x7F800000u;
}

/** @brief Returns true if a float is positive infinity. */
bool IsPositiveInfinity(float in_value)
{
    return FloatBits(in_value) == 0x7F800000u;
}

/** @brief Returns true if a position has only finite coordinates. */
bool IsFinitePosition(const ClusterVec3& in_position)
{
    return IsFiniteFloat(in_position.x) && IsFiniteFloat(in_position.y) && IsFiniteFloat(in_position.z);
}

/** @brief Returns a parameter limited to a range, or a fallback value if it is not finite. */
double ReadParam(float in_value, double in_fallback, double in_low, double in_high)
{
    if (!IsFiniteFloat(in_value))
    {
        return in_fallback;
    }
    return MinOf(MaxOf(static_cast<double>(in_value), in_low), in_high);
}

/** @brief Returns the merge class of a cluster, which combines its group and centred flag. */
uint64_t MergeClass(uint32_t in_group, bool in_centred)
{
    return static_cast<uint64_t>(in_group) | (in_centred ? kCentredClassBit : 0ull);
}
}

struct ClusterEngine::Settings
{
    double joinAngle;              ///< Join angle in degrees.
    double leaveAngle;             ///< Angle in degrees beyond which a home input starts to leave its cluster.
    double cosJoin;                ///< Cosine of the join angle.
    double cosLeave;               ///< Cosine of the leave angle.
    double distanceScale;          ///< Degrees added to the angle per doubling of distance.
    double nearFieldRadius;        ///< Inputs closer to the listener than this distance are excluded; 0 excludes none.
    float fadeStep;                ///< Gain change per frame of a crossfade.
    float loudnessSmoothing;       ///< Per-frame smoothing coefficient of the input loudness.
    uint32_t dwellFrames;          ///< Frames a change must last before a home input leaves or two clusters merge.
    uint32_t maxOutputs;           ///< Maximum number of outputs; 0 means no limit.
    uint32_t excludePriorityAbove; ///< Inputs with a higher priority are excluded.
    bool loudnessWeighting;        ///< Weights cluster positions by input loudness.
};

struct ClusterEngine::Slot
{
    uint64_t key;         ///< Key of the input.
    float dirX;           ///< Direction X of the input from the listener.
    float dirY;           ///< Direction Y of the input from the listener.
    float dirZ;           ///< Direction Z of the input from the listener.
    float range;          ///< Distance of the input from the listener.
    float logRange;       ///< Base 2 logarithm of the range, or 0 when distance does not count.
    float loudness;       ///< Smoothed loudness of the input.
    float weight;         ///< Weight of the input in cluster positions.
    float fadeStart;      ///< Gain in the home cluster at the start of the frame.
    float fadeEnd;        ///< Gain in the home cluster at the end of the frame.
    uint32_t inputIndex;  ///< Index of the input in this frame's input array.
    uint32_t seenFrame;   ///< Last update in which the input was present.
    uint32_t placedFrame; ///< Last update in which the input was placed as a new input.
    uint32_t priority;    ///< Playback priority of the input.
    uint32_t group;       ///< Group of the input.
    uint32_t leaveFrames; ///< Consecutive updates in which the input was outside its home cluster.
    uint32_t primaryLink; ///< Which of the two nodes of the slot, 0 or 1, links the input to its home cluster.
    bool active;          ///< True while the slot holds an input.
    bool fresh;           ///< True until the loudness of the input has been read once.
    bool clusterable;     ///< False for passthrough inputs.
    bool excluded;        ///< True for inputs that keep a cluster of their own.
    bool centred;         ///< True for inputs too close to the listener to have a direction.
    bool fading;          ///< True while the input fades from its previous cluster into its home cluster.
    bool pending;         ///< True while the input waits to be placed in this update.
};

struct ClusterEngine::Node
{
    uint32_t cluster; ///< Cluster the node is linked into, or kNone.
    uint32_t prev;    ///< Previous node in the list of the cluster, or kNone.
    uint32_t next;    ///< Next node in the list of the cluster, or kNone.
};

struct ClusterEngine::Cluster
{
    float dirX;                     ///< Direction X of the cluster from the listener.
    float dirY;                     ///< Direction Y of the cluster from the listener.
    float dirZ;                     ///< Direction Z of the cluster from the listener.
    float logRange;                 ///< Base 2 logarithm of the range of the cluster.
    uint32_t group;                 ///< Group of the inputs the cluster takes.
    bool used;                      ///< True while the cluster ID is taken.
    bool dissolving;                ///< True once the cluster has no home inputs or merges into another cluster.
    bool excluded;                  ///< True if a home input is excluded, so that no other input joins and the cluster never merges.
    bool centred;                   ///< True if the cluster takes inputs with no direction.
    bool reported;                  ///< True once the cluster has been reported.
    bool busy;                      ///< True once the cluster took part in a merge in this update.
    bool changed;                   ///< True if the inputs of the cluster changed since it was last refreshed.
    float range;                    ///< Weighted average distance of the inputs from the listener.
    float radius;                   ///< Largest angle in degrees between a contributing input and the cluster direction.
    float homeRadius;               ///< Largest angle in degrees between a home input and the cluster direction.
    uint32_t head;                  ///< First node in the list of the cluster, or kNone.
    uint32_t tail;                  ///< Last node in the list of the cluster, or kNone.
    uint32_t nodeCount;             ///< Number of contributing inputs.
    uint32_t homeCount;             ///< Number of inputs whose home is this cluster.
    uint32_t priority;              ///< Highest priority among the contributing inputs.
    uint32_t generation;            ///< Counter that tells apart the clusters that used this ID.
    uint32_t mergeTarget;           ///< Cluster this cluster merges into, or kNone.
    uint32_t mergeTargetGeneration; ///< Generation of the merge target when the merge started.
    uint32_t candidate;             ///< Nearest mergeable cluster in recent updates, or kNone.
    uint32_t candidateGeneration;   ///< Generation of the candidate.
    uint32_t candidateFrames;       ///< Consecutive updates in which the candidate was the nearest mergeable cluster.
    uint32_t representative;        ///< Input index of the contributing input with the largest weighted gain.
    double sumX;                    ///< Sum of direction X of the inputs, each times its gain and weight.
    double sumY;                    ///< Sum of direction Y of the inputs, each times its gain and weight.
    double sumZ;                    ///< Sum of direction Z of the inputs, each times its gain and weight.
    double sumRange;                ///< Sum of the ranges of the inputs, each times its gain and weight.
    double sumWeight;               ///< Sum of the gains times the weights of the inputs.
};

struct ClusterEngine::HashEntry
{
    uint64_t key;  ///< Input key.
    uint32_t slot; ///< Slot that holds the input, or kNone for an empty entry.
};

struct ClusterEngine::PendingEntry
{
    uint64_t key;   ///< Input key.
    uint32_t index; ///< Input index before slots are assigned, slot index after.
};

struct ClusterEngine::BudgetEntry
{
    double cost;      ///< Budget cost of the merge, or a lower bound of it.
    uint32_t index;   ///< Listed cluster the entry belongs to.
    uint32_t version; ///< Version of the listed cluster when the entry was pushed.
};

ClusterEngine::ClusterEngine() = default;

ClusterEngine::~ClusterEngine()
{
    Term();
}

bool ClusterEngine::Init(ClusterAllocator* in_pAllocator, uint32_t in_reserveInputs)
{
    Term();
    if (in_pAllocator == nullptr || in_reserveInputs > kMaxCapacity)
    {
        return false;
    }
    m_pAllocator = in_pAllocator;
    if (in_reserveInputs == 0)
    {
        return true;
    }
    return Reserve(in_reserveInputs, in_reserveInputs * kClustersPerSlot);
}

void ClusterEngine::Term()
{
    if (m_pBlock != nullptr && m_pAllocator != nullptr)
    {
        m_pAllocator->Free(m_pBlock);
    }
    m_pAllocator = nullptr;
    m_pBlock = nullptr;
    m_pSlots = nullptr;
    m_pNodes = nullptr;
    m_pHash = nullptr;
    m_pFreeSlots = nullptr;
    m_pPending = nullptr;
    m_pContributions = nullptr;
    m_pClusters = nullptr;
    m_pFreeClusters = nullptr;
    m_pReleasedClusters = nullptr;
    m_pInfos = nullptr;
    m_pScratchIds = nullptr;
    m_pScratchX = nullptr;
    m_pScratchY = nullptr;
    m_pScratchZ = nullptr;
    m_pScratchLogRange = nullptr;
    m_pScratchClass = nullptr;
    m_pScratchPartner = nullptr;
    m_pScratchOrder = nullptr;
    m_pScratchCost = nullptr;
    m_pScratchAlive = nullptr;
    m_pScratchBound = nullptr;
    m_pScratchCosLimit = nullptr;
    m_pScratchVersion = nullptr;
    m_pBudgetHeap = nullptr;
    m_slotCapacity = 0;
    m_clusterCapacity = 0;
    m_hashMask = 0;
    ClearState();
}

void ClusterEngine::Reset()
{
    ClearState();
}

bool ClusterEngine::Update(const ClusterInput* in_pInputs, uint32_t in_numInputs, const ClusterParams& in_params)
{
    if (in_numInputs > 0 && in_pInputs == nullptr)
    {
        return false;
    }
    if (!EnsureCapacity(in_numInputs))
    {
        return false;
    }

    Settings settings;
    MakeSettings(in_params, settings);
    ++m_frame;
    m_overBudget = false;

    MapInputs(in_pInputs, in_numInputs);
    UpdateSlots(in_pInputs, settings);
    AdvanceFades(settings);
    RefreshClusters();
    ProcessLeaves(settings);
    PlacePendingInputs(settings);
    MergeNearbyClusters(settings);
    EnforceBudget(settings);
    BuildReport();
    FlushReleasedClusters();
    return true;
}

uint32_t ClusterEngine::GetClusterCount() const
{
    return m_infoCount;
}

const ClusterInfo& ClusterEngine::GetCluster(uint32_t in_index) const
{
    static const ClusterInfo s_empty = {};
    return in_index < m_infoCount ? m_pInfos[in_index] : s_empty;
}

uint32_t ClusterEngine::GetContributionCount() const
{
    return m_contributionCount;
}

const ClusterContribution& ClusterEngine::GetContribution(uint32_t in_index) const
{
    static const ClusterContribution s_empty = {};
    return in_index < m_contributionCount ? m_pContributions[in_index] : s_empty;
}

bool ClusterEngine::IsOverBudget() const
{
    return m_overBudget;
}

void ClusterEngine::MakeSettings(const ClusterParams& in_params, Settings& out_settings)
{
    const double joinAngle = ReadParam(in_params.joinAngle, 0.0, 0.0, 180.0);
    const double leaveRatio = ReadParam(in_params.leaveRatio, 1.0, 1.0, kLeaveRatioCap);
    const double distanceWeight = ReadParam(in_params.distanceWeight, 0.0, 0.0, kDistanceWeightCap);
    const double nearFieldRadius = IsPositiveInfinity(in_params.nearFieldRadius)
                                       ? DBL_MAX
                                       : ReadParam(in_params.nearFieldRadius, 0.0, 0.0, FLT_MAX);
    const float smoothing = static_cast<float>(ReadParam(in_params.loudnessSmoothing, 0.0, 0.0, kMaxSmoothing));

    out_settings.joinAngle = joinAngle;
    out_settings.leaveAngle = joinAngle * leaveRatio;
    out_settings.cosJoin = CosineLimit(joinAngle);
    out_settings.cosLeave = CosineLimit(out_settings.leaveAngle);
    out_settings.distanceScale = distanceWeight * joinAngle;
    out_settings.nearFieldRadius = nearFieldRadius;
    out_settings.fadeStep = 1.f / static_cast<float>(MaxOf(in_params.crossfadeFrames, 1u));
    out_settings.loudnessSmoothing = smoothing;
    out_settings.dwellFrames = in_params.dwellFrames;
    out_settings.maxOutputs = in_params.maxOutputs;
    out_settings.excludePriorityAbove = in_params.excludePriorityAbove;
    out_settings.loudnessWeighting = in_params.loudnessWeighting;
}

bool ClusterEngine::EnsureCapacity(uint32_t in_numInputs)
{
    if (in_numInputs > kMaxCapacity)
    {
        return false;
    }
    if (in_numInputs <= m_slotCapacity && m_liveClusters + in_numInputs <= m_clusterCapacity)
    {
        return true;
    }

    uint32_t slotCapacity = m_slotCapacity;
    if (in_numInputs > slotCapacity)
    {
        slotCapacity = MaxOf(slotCapacity * 2u, kMinGrowth);
        while (slotCapacity < in_numInputs)
        {
            slotCapacity *= 2u;
        }
    }
    uint32_t clusterCapacity = MaxOf(m_clusterCapacity, slotCapacity * kClustersPerSlot);
    if (m_liveClusters + in_numInputs > clusterCapacity)
    {
        clusterCapacity = MaxOf(clusterCapacity * 2u, m_liveClusters + in_numInputs);
    }
    return Reserve(slotCapacity, clusterCapacity);
}

bool ClusterEngine::Reserve(uint32_t in_slotCapacity, uint32_t in_clusterCapacity)
{
    if (m_pAllocator == nullptr)
    {
        return false;
    }

    const uint32_t hashCapacity = NextPowerOfTwo(MaxOf(in_slotCapacity * 2u, kMinGrowth));
    const uint32_t slotWords = (in_slotCapacity + 63u) / 64u;
    const uint32_t clusterWords = (in_clusterCapacity + 63u) / 64u;
    const size_t nodeCount = static_cast<size_t>(in_slotCapacity) * 2u;

    size_t size = 0;
    const size_t slotsAt = TakeBytes(size, sizeof(Slot) * in_slotCapacity);
    const size_t nodesAt = TakeBytes(size, sizeof(Node) * nodeCount);
    const size_t hashAt = TakeBytes(size, sizeof(HashEntry) * hashCapacity);
    const size_t freeSlotsAt = TakeBytes(size, sizeof(uint64_t) * slotWords);
    const size_t pendingAt = TakeBytes(size, sizeof(PendingEntry) * in_slotCapacity);
    const size_t contributionsAt = TakeBytes(size, sizeof(ClusterContribution) * nodeCount);
    const size_t clustersAt = TakeBytes(size, sizeof(Cluster) * in_clusterCapacity);
    const size_t freeClustersAt = TakeBytes(size, sizeof(uint64_t) * clusterWords);
    const size_t releasedAt = TakeBytes(size, sizeof(uint32_t) * in_clusterCapacity);
    const size_t infosAt = TakeBytes(size, sizeof(ClusterInfo) * in_clusterCapacity);
    const size_t scratchIdsAt = TakeBytes(size, sizeof(uint32_t) * in_clusterCapacity);
    const size_t scratchXAt = TakeBytes(size, sizeof(float) * in_clusterCapacity);
    const size_t scratchYAt = TakeBytes(size, sizeof(float) * in_clusterCapacity);
    const size_t scratchZAt = TakeBytes(size, sizeof(float) * in_clusterCapacity);
    const size_t scratchLogRangeAt = TakeBytes(size, sizeof(float) * in_clusterCapacity);
    const size_t scratchClassAt = TakeBytes(size, sizeof(uint64_t) * in_clusterCapacity);
    const size_t scratchPartnerAt = TakeBytes(size, sizeof(uint32_t) * in_clusterCapacity);
    const size_t scratchOrderAt = TakeBytes(size, sizeof(uint32_t) * in_clusterCapacity);
    const size_t scratchCostAt = TakeBytes(size, sizeof(double) * in_clusterCapacity);
    const size_t scratchAliveAt = TakeBytes(size, sizeof(uint8_t) * in_clusterCapacity);
    const size_t scratchBoundAt = TakeBytes(size, sizeof(uint8_t) * in_clusterCapacity);
    const size_t scratchCosLimitAt = TakeBytes(size, sizeof(double) * in_clusterCapacity);
    const size_t scratchVersionAt = TakeBytes(size, sizeof(uint32_t) * in_clusterCapacity);
    const size_t budgetHeapAt = TakeBytes(size, sizeof(BudgetEntry) * in_clusterCapacity);

    void* pBlock = m_pAllocator->Alloc(size);
    if (pBlock == nullptr)
    {
        return false;
    }
    memset(pBlock, 0, size);
    char* pBase = static_cast<char*>(pBlock);

    Slot* pSlots = reinterpret_cast<Slot*>(pBase + slotsAt);
    Node* pNodes = reinterpret_cast<Node*>(pBase + nodesAt);
    Cluster* pClusters = reinterpret_cast<Cluster*>(pBase + clustersAt);
    if (m_slotCapacity > 0)
    {
        memcpy(pSlots, m_pSlots, sizeof(Slot) * m_slotCapacity);
        memcpy(pNodes, m_pNodes, sizeof(Node) * m_slotCapacity * 2u);
    }
    for (size_t node = static_cast<size_t>(m_slotCapacity) * 2u; node < nodeCount; ++node)
    {
        pNodes[node].cluster = kNone;
        pNodes[node].prev = kNone;
        pNodes[node].next = kNone;
    }
    if (m_clusterCapacity > 0)
    {
        memcpy(pClusters, m_pClusters, sizeof(Cluster) * m_clusterCapacity);
    }

    void* pOldBlock = m_pBlock;
    m_pBlock = pBlock;
    m_pSlots = pSlots;
    m_pNodes = pNodes;
    m_pHash = reinterpret_cast<HashEntry*>(pBase + hashAt);
    m_pFreeSlots = reinterpret_cast<uint64_t*>(pBase + freeSlotsAt);
    m_pPending = reinterpret_cast<PendingEntry*>(pBase + pendingAt);
    m_pContributions = reinterpret_cast<ClusterContribution*>(pBase + contributionsAt);
    m_pClusters = pClusters;
    m_pFreeClusters = reinterpret_cast<uint64_t*>(pBase + freeClustersAt);
    m_pReleasedClusters = reinterpret_cast<uint32_t*>(pBase + releasedAt);
    m_pInfos = reinterpret_cast<ClusterInfo*>(pBase + infosAt);
    m_pScratchIds = reinterpret_cast<uint32_t*>(pBase + scratchIdsAt);
    m_pScratchX = reinterpret_cast<float*>(pBase + scratchXAt);
    m_pScratchY = reinterpret_cast<float*>(pBase + scratchYAt);
    m_pScratchZ = reinterpret_cast<float*>(pBase + scratchZAt);
    m_pScratchLogRange = reinterpret_cast<float*>(pBase + scratchLogRangeAt);
    m_pScratchClass = reinterpret_cast<uint64_t*>(pBase + scratchClassAt);
    m_pScratchPartner = reinterpret_cast<uint32_t*>(pBase + scratchPartnerAt);
    m_pScratchOrder = reinterpret_cast<uint32_t*>(pBase + scratchOrderAt);
    m_pScratchCost = reinterpret_cast<double*>(pBase + scratchCostAt);
    m_pScratchAlive = reinterpret_cast<uint8_t*>(pBase + scratchAliveAt);
    m_pScratchBound = reinterpret_cast<uint8_t*>(pBase + scratchBoundAt);
    m_pScratchCosLimit = reinterpret_cast<double*>(pBase + scratchCosLimitAt);
    m_pScratchVersion = reinterpret_cast<uint32_t*>(pBase + scratchVersionAt);
    m_pBudgetHeap = reinterpret_cast<BudgetEntry*>(pBase + budgetHeapAt);
    m_budgetHeapSize = 0;
    m_slotCapacity = in_slotCapacity;
    m_clusterCapacity = in_clusterCapacity;
    m_hashMask = hashCapacity - 1u;
    m_infoCount = 0;
    m_contributionCount = 0;

    for (uint32_t entry = 0; entry < hashCapacity; ++entry)
    {
        m_pHash[entry].slot = kNone;
    }
    for (uint32_t slot = 0; slot < m_slotEnd; ++slot)
    {
        if (m_pSlots[slot].active)
        {
            InsertSlotKey(m_pSlots[slot].key, slot);
        }
    }
    RebuildFreeSlots();
    RebuildFreeClusters();

    if (pOldBlock != nullptr)
    {
        m_pAllocator->Free(pOldBlock);
    }
    return true;
}

void ClusterEngine::ClearState()
{
    if (m_slotCapacity > 0)
    {
        memset(m_pSlots, 0, sizeof(Slot) * m_slotCapacity);
        for (uint32_t node = 0; node < m_slotCapacity * 2u; ++node)
        {
            m_pNodes[node].cluster = kNone;
            m_pNodes[node].prev = kNone;
            m_pNodes[node].next = kNone;
        }
        for (uint32_t entry = 0; entry <= m_hashMask; ++entry)
        {
            m_pHash[entry].slot = kNone;
        }
    }
    if (m_clusterCapacity > 0)
    {
        memset(m_pClusters, 0, sizeof(Cluster) * m_clusterCapacity);
    }
    m_slotEnd = 0;
    m_clusterEnd = 0;
    m_liveClusters = 0;
    m_activeClusters = 0;
    m_releasedCount = 0;
    m_pendingCount = 0;
    m_passthroughCount = 0;
    m_infoCount = 0;
    m_contributionCount = 0;
    m_frame = 0;
    m_overBudget = false;
    RebuildFreeSlots();
    RebuildFreeClusters();
}

void ClusterEngine::RebuildFreeSlots()
{
    const uint32_t wordCount = (m_slotCapacity + 63u) / 64u;
    for (uint32_t word = 0; word < wordCount; ++word)
    {
        m_pFreeSlots[word] = 0;
    }
    for (uint32_t slot = 0; slot < m_slotCapacity; ++slot)
    {
        if (!m_pSlots[slot].active)
        {
            MarkFree(m_pFreeSlots, slot);
        }
    }
}

void ClusterEngine::RebuildFreeClusters()
{
    const uint32_t wordCount = (m_clusterCapacity + 63u) / 64u;
    for (uint32_t word = 0; word < wordCount; ++word)
    {
        m_pFreeClusters[word] = 0;
    }
    for (uint32_t cluster = 0; cluster < m_clusterCapacity; ++cluster)
    {
        if (!m_pClusters[cluster].used)
        {
            MarkFree(m_pFreeClusters, cluster);
        }
    }
}

uint32_t ClusterEngine::FindSlot(uint64_t in_key) const
{
    if (m_pHash == nullptr)
    {
        return kNone;
    }
    uint32_t entry = static_cast<uint32_t>(MixKey(in_key)) & m_hashMask;
    while (m_pHash[entry].slot != kNone)
    {
        if (m_pHash[entry].key == in_key)
        {
            return m_pHash[entry].slot;
        }
        entry = (entry + 1u) & m_hashMask;
    }
    return kNone;
}

void ClusterEngine::InsertSlotKey(uint64_t in_key, uint32_t in_slot)
{
    uint32_t entry = static_cast<uint32_t>(MixKey(in_key)) & m_hashMask;
    while (m_pHash[entry].slot != kNone)
    {
        entry = (entry + 1u) & m_hashMask;
    }
    m_pHash[entry].key = in_key;
    m_pHash[entry].slot = in_slot;
}

void ClusterEngine::RemoveSlotKey(uint64_t in_key)
{
    uint32_t hole = static_cast<uint32_t>(MixKey(in_key)) & m_hashMask;
    while (m_pHash[hole].slot != kNone && m_pHash[hole].key != in_key)
    {
        hole = (hole + 1u) & m_hashMask;
    }
    if (m_pHash[hole].slot == kNone)
    {
        return;
    }
    uint32_t entry = hole;
    for (;;)
    {
        entry = (entry + 1u) & m_hashMask;
        if (m_pHash[entry].slot == kNone)
        {
            break;
        }
        const uint32_t home = static_cast<uint32_t>(MixKey(m_pHash[entry].key)) & m_hashMask;
        const uint32_t distanceFromHome = (entry - home) & m_hashMask;
        const uint32_t distanceFromHole = (entry - hole) & m_hashMask;
        if (distanceFromHome >= distanceFromHole)
        {
            m_pHash[hole] = m_pHash[entry];
            hole = entry;
        }
    }
    m_pHash[hole].slot = kNone;
}

uint32_t ClusterEngine::AllocateSlot()
{
    const uint32_t slot = TakeLowestFree(m_pFreeSlots, m_slotCapacity);
    if (slot != kNone && slot >= m_slotEnd)
    {
        m_slotEnd = slot + 1u;
    }
    return slot;
}

void ClusterEngine::FreeSlot(uint32_t in_slot)
{
    m_pSlots[in_slot].active = false;
    MarkFree(m_pFreeSlots, in_slot);
    while (m_slotEnd > 0 && !m_pSlots[m_slotEnd - 1u].active)
    {
        --m_slotEnd;
    }
}

uint32_t ClusterEngine::AllocateCluster()
{
    const uint32_t cluster = TakeLowestFree(m_pFreeClusters, m_clusterCapacity);
    if (cluster != kNone && cluster >= m_clusterEnd)
    {
        m_clusterEnd = cluster + 1u;
    }
    return cluster;
}

void ClusterEngine::ReleaseCluster(uint32_t in_cluster)
{
    Cluster& cluster = m_pClusters[in_cluster];
    if (!cluster.dissolving)
    {
        --m_activeClusters;
    }
    cluster.used = false;
    cluster.dissolving = false;
    --m_liveClusters;
    m_pReleasedClusters[m_releasedCount++] = in_cluster;
}

void ClusterEngine::FlushReleasedClusters()
{
    for (uint32_t index = 0; index < m_releasedCount; ++index)
    {
        MarkFree(m_pFreeClusters, m_pReleasedClusters[index]);
    }
    m_releasedCount = 0;
    while (m_clusterEnd > 0 && !m_pClusters[m_clusterEnd - 1u].used)
    {
        --m_clusterEnd;
    }
}

void ClusterEngine::MapInputs(const ClusterInput* in_pInputs, uint32_t in_numInputs)
{
    m_pendingCount = 0;
    for (uint32_t inputIndex = 0; inputIndex < in_numInputs; ++inputIndex)
    {
        const uint64_t key = in_pInputs[inputIndex].key;
        const uint32_t slotIndex = FindSlot(key);
        if (slotIndex == kNone)
        {
            m_pPending[m_pendingCount].key = key;
            m_pPending[m_pendingCount].index = inputIndex;
            ++m_pendingCount;
            continue;
        }
        Slot& slot = m_pSlots[slotIndex];
        if (slot.seenFrame == m_frame)
        {
            continue;
        }
        slot.seenFrame = m_frame;
        slot.inputIndex = inputIndex;
    }

    for (uint32_t slotIndex = 0; slotIndex < m_slotEnd; ++slotIndex)
    {
        Slot& slot = m_pSlots[slotIndex];
        if (!slot.active)
        {
            continue;
        }
        if (slot.seenFrame != m_frame)
        {
            ReleaseSlot(slotIndex);
            continue;
        }
        const ClusterInput& input = in_pInputs[slot.inputIndex];
        slot.group = input.group;
        if (slot.clusterable != input.clusterable)
        {
            DetachSlot(slotIndex);
            slot.clusterable = input.clusterable;
            if (slot.clusterable)
            {
                m_pPending[m_pendingCount].key = slot.key;
                m_pPending[m_pendingCount].index = slot.inputIndex;
                ++m_pendingCount;
            }
        }
    }

    SortPendingByKey();
    AssignPendingSlots(in_pInputs);
}

void ClusterEngine::SortPendingByKey()
{
    HeapSort(m_pPending, m_pendingCount, [](const PendingEntry& in_first, const PendingEntry& in_second) {
        return in_first.key < in_second.key || (in_first.key == in_second.key && in_first.index < in_second.index);
    });
}

void ClusterEngine::AssignPendingSlots(const ClusterInput* in_pInputs)
{
    uint32_t written = 0;
    bool hasPrevious = false;
    uint64_t previousKey = 0;
    for (uint32_t read = 0; read < m_pendingCount; ++read)
    {
        const uint32_t inputIndex = m_pPending[read].index;
        const ClusterInput& input = in_pInputs[inputIndex];
        if (hasPrevious && input.key == previousKey)
        {
            continue;
        }
        hasPrevious = true;
        previousKey = input.key;

        uint32_t slotIndex = FindSlot(input.key);
        if (slotIndex == kNone)
        {
            slotIndex = AllocateSlot();
            InitSlot(slotIndex, inputIndex, input);
            InsertSlotKey(input.key, slotIndex);
        }
        Slot& slot = m_pSlots[slotIndex];
        if (!slot.clusterable)
        {
            continue;
        }
        slot.pending = true;
        m_pPending[written].key = slot.key;
        m_pPending[written].index = slotIndex;
        ++written;
    }
    m_pendingCount = written;
}

void ClusterEngine::InitSlot(uint32_t in_slot, uint32_t in_inputIndex, const ClusterInput& in_input)
{
    Slot& slot = m_pSlots[in_slot];
    memset(&slot, 0, sizeof(Slot));
    slot.key = in_input.key;
    slot.dirZ = 1.f;
    slot.fadeStart = 1.f;
    slot.fadeEnd = 1.f;
    slot.inputIndex = in_inputIndex;
    slot.seenFrame = m_frame;
    slot.priority = in_input.priority;
    slot.group = in_input.group;
    slot.active = true;
    slot.fresh = true;
    slot.clusterable = in_input.clusterable;
}

void ClusterEngine::ReleaseSlot(uint32_t in_slot)
{
    DetachSlot(in_slot);
    RemoveSlotKey(m_pSlots[in_slot].key);
    FreeSlot(in_slot);
}

void ClusterEngine::DetachSlot(uint32_t in_slot)
{
    Slot& slot = m_pSlots[in_slot];
    const uint32_t primaryNode = GetPrimaryNode(in_slot);
    if (m_pNodes[primaryNode].cluster != kNone)
    {
        DropHomeInput(m_pNodes[primaryNode].cluster);
        DetachNode(primaryNode);
    }
    if (slot.fading)
    {
        DetachNode(GetSourceNode(in_slot));
    }
    slot.fading = false;
    slot.pending = false;
    slot.fadeStart = 1.f;
    slot.fadeEnd = 1.f;
    slot.leaveFrames = 0;
}

void ClusterEngine::UpdateSlots(const ClusterInput* in_pInputs, const Settings& in_settings)
{
    m_passthroughCount = 0;
    for (uint32_t slotIndex = 0; slotIndex < m_slotEnd; ++slotIndex)
    {
        Slot& slot = m_pSlots[slotIndex];
        if (!slot.active)
        {
            continue;
        }
        const ClusterInput& input = in_pInputs[slot.inputIndex];
        slot.priority = input.priority;
        if (!slot.clusterable)
        {
            ++m_passthroughCount;
            continue;
        }

        const bool finite = IsFinitePosition(input.position);
        double range = 0.0;
        slot.centred = false;
        if (finite)
        {
            const double x = input.position.x;
            const double y = input.position.y;
            const double z = input.position.z;
            range = std::sqrt(x * x + y * y + z * z);
            if (range < kMinRange)
            {
                slot.centred = true;
                slot.dirX = 0.f;
                slot.dirY = 0.f;
                slot.dirZ = 1.f;
            }
            else
            {
                slot.dirX = static_cast<float>(x / range);
                slot.dirY = static_cast<float>(y / range);
                slot.dirZ = static_cast<float>(z / range);
            }
        }
        slot.range = static_cast<float>(MinOf(range, static_cast<double>(FLT_MAX)));
        slot.logRange = in_settings.distanceScale > 0.0 ? LogRange(slot.range) : 0.f;

        const float loudness = IsFiniteFloat(input.loudness) ? MinOf(MaxOf(input.loudness, 0.f), kMaxLoudness) : 0.f;
        if (slot.fresh)
        {
            slot.loudness = loudness;
            slot.fresh = false;
        }
        else
        {
            const float smoothing = in_settings.loudnessSmoothing;
            slot.loudness = smoothing * slot.loudness + (1.f - smoothing) * loudness;
        }
        slot.weight = in_settings.loudnessWeighting ? slot.loudness + kLoudnessFloor : 1.f;
        const bool nearField = in_settings.nearFieldRadius > 0.0 && range < in_settings.nearFieldRadius;
        slot.excluded = !finite || nearField || slot.priority > in_settings.excludePriorityAbove;
    }
}

void ClusterEngine::AdvanceFades(const Settings& in_settings)
{
    for (uint32_t slotIndex = 0; slotIndex < m_slotEnd; ++slotIndex)
    {
        Slot& slot = m_pSlots[slotIndex];
        if (!slot.active || !slot.fading)
        {
            continue;
        }
        if (slot.fadeEnd >= 1.f)
        {
            DetachNode(GetSourceNode(slotIndex));
            slot.fading = false;
            slot.fadeStart = 1.f;
            slot.fadeEnd = 1.f;
            continue;
        }
        slot.fadeStart = slot.fadeEnd;
        slot.fadeEnd = MinOf(1.f, slot.fadeEnd + in_settings.fadeStep);
    }
}

void ClusterEngine::RefreshClusters()
{
    for (uint32_t cluster = 0; cluster < m_clusterEnd; ++cluster)
    {
        if (m_pClusters[cluster].used)
        {
            RefreshCluster(cluster);
        }
    }
}

void ClusterEngine::RefreshCluster(uint32_t in_cluster)
{
    Cluster& cluster = m_pClusters[in_cluster];
    double sumX = 0.0;
    double sumY = 0.0;
    double sumZ = 0.0;
    double sumRange = 0.0;
    double sumWeight = 0.0;
    uint32_t priority = 0;
    bool excluded = false;
    uint32_t representative = kNone;
    double bestLoudnessEnd = -1.0;
    double bestLoudnessStart = -1.0;

    for (uint32_t node = cluster.head; node != kNone; node = m_pNodes[node].next)
    {
        const Slot& slot = m_pSlots[node >> 1u];
        float gainStart = 0.f;
        float gainEnd = 0.f;
        GetNodeGains(node, gainStart, gainEnd);
        const double share = static_cast<double>(gainEnd) * slot.weight;
        sumX += share * slot.dirX;
        sumY += share * slot.dirY;
        sumZ += share * slot.dirZ;
        sumRange += share * slot.range;
        sumWeight += share;
        priority = MaxOf(priority, slot.priority);
        excluded = excluded || (slot.excluded && IsPrimaryNode(node));

        const double shareStart = static_cast<double>(gainStart) * slot.weight;
        if (share > bestLoudnessEnd || (share == bestLoudnessEnd && shareStart > bestLoudnessStart))
        {
            bestLoudnessEnd = share;
            bestLoudnessStart = shareStart;
            representative = slot.inputIndex;
        }
    }

    cluster.sumX = sumX;
    cluster.sumY = sumY;
    cluster.sumZ = sumZ;
    cluster.sumRange = sumRange;
    cluster.sumWeight = sumWeight;
    ApplySums(cluster);

    float minDot = 1.f;
    float minHomeDot = 1.f;
    for (uint32_t node = cluster.head; node != kNone; node = m_pNodes[node].next)
    {
        const Slot& slot = m_pSlots[node >> 1u];
        const float dot = slot.dirX * cluster.dirX + slot.dirY * cluster.dirY + slot.dirZ * cluster.dirZ;
        minDot = MinOf(minDot, dot);
        if (IsPrimaryNode(node))
        {
            minHomeDot = MinOf(minHomeDot, dot);
        }
    }
    cluster.radius = static_cast<float>(AngleFromDot(minDot));
    cluster.homeRadius = minHomeDot == minDot ? cluster.radius : static_cast<float>(AngleFromDot(minHomeDot));
    cluster.priority = priority;
    cluster.excluded = excluded;
    cluster.representative = representative;
    cluster.changed = false;
}

void ClusterEngine::ApplySums(Cluster& io_cluster)
{
    if (!(io_cluster.sumWeight > 0.0))
    {
        return;
    }
    const double length = std::sqrt(io_cluster.sumX * io_cluster.sumX + io_cluster.sumY * io_cluster.sumY +
                                     io_cluster.sumZ * io_cluster.sumZ);
    if (length > kCancelRatio * io_cluster.sumWeight)
    {
        io_cluster.dirX = static_cast<float>(io_cluster.sumX / length);
        io_cluster.dirY = static_cast<float>(io_cluster.sumY / length);
        io_cluster.dirZ = static_cast<float>(io_cluster.sumZ / length);
    }
    const double range = MaxOf(io_cluster.sumRange / io_cluster.sumWeight, 0.0);
    io_cluster.range = static_cast<float>(MinOf(range, static_cast<double>(FLT_MAX)));
    io_cluster.logRange = LogRange(io_cluster.range);
}

void ClusterEngine::ProcessLeaves(const Settings& in_settings)
{
    for (uint32_t slotIndex = 0; slotIndex < m_slotEnd; ++slotIndex)
    {
        Slot& slot = m_pSlots[slotIndex];
        if (!slot.active || !slot.clusterable || slot.pending || slot.fading)
        {
            continue;
        }
        const uint32_t clusterIndex = m_pNodes[GetPrimaryNode(slotIndex)].cluster;
        if (clusterIndex == kNone)
        {
            continue;
        }
        Cluster& cluster = m_pClusters[clusterIndex];
        if (cluster.dissolving)
        {
            StartMove(slotIndex, FindFollowDestination(slotIndex, clusterIndex, in_settings), in_settings);
            continue;
        }
        if (slot.group != cluster.group)
        {
            if (cluster.nodeCount > 1)
            {
                StartMove(slotIndex, FindPlacement(slotIndex, clusterIndex, in_settings), in_settings);
                continue;
            }
            cluster.group = slot.group;
        }
        if (cluster.homeCount <= 1)
        {
            cluster.centred = slot.centred;
        }
        if (!IsOutsideCluster(slot, cluster, in_settings))
        {
            slot.leaveFrames = 0;
            continue;
        }
        if (slot.leaveFrames < kNone)
        {
            ++slot.leaveFrames;
        }
        if (slot.leaveFrames < in_settings.dwellFrames)
        {
            continue;
        }
        const uint32_t destination = FindLeaveDestination(slotIndex, clusterIndex, in_settings);
        if (destination == kNone)
        {
            slot.leaveFrames = 0;
            continue;
        }
        StartMove(slotIndex, destination, in_settings);
    }
}

void ClusterEngine::PlacePendingInputs(const Settings& in_settings)
{
    for (uint32_t index = 0; index < m_pendingCount; ++index)
    {
        const uint32_t slotIndex = m_pPending[index].index;
        m_pSlots[slotIndex].pending = false;
        m_pSlots[slotIndex].placedFrame = m_frame;
        JoinCluster(slotIndex, FindPlacement(slotIndex, kNone, in_settings));
    }
    m_pendingCount = 0;
}

void ClusterEngine::MergeNearbyClusters(const Settings& in_settings)
{
    const uint32_t count = GatherMergeableClusters();
    for (uint32_t index = 0; index < count; ++index)
    {
        m_pScratchPartner[index] = kNone;
        m_pScratchCost[index] = kAnyDistance;
        m_pScratchOrder[index] = index;
    }
    SortIndicesByValue(m_pScratchOrder, count, m_pScratchZ);

    auto keepCloser = [this](uint32_t in_index, uint32_t in_other, double in_distance) {
        if (in_distance < m_pScratchCost[in_index] ||
            (in_distance == m_pScratchCost[in_index] && in_other < m_pScratchPartner[in_index]))
        {
            m_pScratchCost[in_index] = in_distance;
            m_pScratchPartner[in_index] = in_other;
        }
    };
    const float band = static_cast<float>(2.0 * std::sin(0.5 * in_settings.joinAngle * kRadiansPerDegree)) + kBandSlack;
    for (uint32_t low = 0; low < count; ++low)
    {
        const uint32_t first = m_pScratchOrder[low];
        for (uint32_t high = low + 1u; high < count; ++high)
        {
            const uint32_t second = m_pScratchOrder[high];
            if (m_pScratchZ[second] - m_pScratchZ[first] > band)
            {
                break;
            }
            double distance = 0.0;
            if (!CanMergePair(first, second, in_settings, distance))
            {
                continue;
            }
            keepCloser(first, second, distance);
            keepCloser(second, first, distance);
        }
    }

    for (uint32_t index = 0; index < count; ++index)
    {
        Cluster& cluster = m_pClusters[m_pScratchIds[index]];
        if (cluster.busy)
        {
            continue;
        }
        uint32_t partner = m_pScratchPartner[index];
        if (partner != kNone && m_pClusters[m_pScratchIds[partner]].busy)
        {
            partner = FindMergePartner(index, count, in_settings);
        }
        if (partner == kNone)
        {
            cluster.candidate = kNone;
            cluster.candidateFrames = 0;
            continue;
        }
        const uint32_t partnerId = m_pScratchIds[partner];
        const uint32_t partnerGeneration = m_pClusters[partnerId].generation;
        if (cluster.candidate == partnerId && cluster.candidateGeneration == partnerGeneration)
        {
            if (cluster.candidateFrames < kNone)
            {
                ++cluster.candidateFrames;
            }
        }
        else
        {
            cluster.candidate = partnerId;
            cluster.candidateGeneration = partnerGeneration;
            cluster.candidateFrames = 1;
        }
        if (cluster.candidateFrames >= in_settings.dwellFrames)
        {
            MergePair(m_pScratchIds[index], partnerId, in_settings);
        }
    }
}

void ClusterEngine::EnforceBudget(const Settings& in_settings)
{
    if (in_settings.maxOutputs == 0 || GetOutputDemand() <= in_settings.maxOutputs)
    {
        return;
    }

    const uint32_t count = GatherMergeableClusters();
    m_budgetHeapSize = 0;
    for (uint32_t index = 0; index < count; ++index)
    {
        m_pScratchAlive[index] = 1;
        m_pScratchVersion[index] = 0;
        m_pScratchOrder[index] = index;
    }
    FindAllBudgetPartners(count, in_settings);
    for (uint32_t index = 0; index < count; ++index)
    {
        PushBudgetEntry(index, count);
    }

    uint32_t listCount = count;
    while (GetOutputDemand() > in_settings.maxOutputs)
    {
        const uint32_t pick = PopBudgetPick(listCount, in_settings);
        if (pick == kNone)
        {
            m_overBudget = true;
            return;
        }

        const uint32_t partner = m_pScratchPartner[pick];
        const Cluster& pickCluster = m_pClusters[m_pScratchIds[pick]];
        const Cluster& partnerCluster = m_pClusters[m_pScratchIds[partner]];
        const bool pickIsSource = pickCluster.sumWeight < partnerCluster.sumWeight ||
                                  (pickCluster.sumWeight == partnerCluster.sumWeight && m_pScratchIds[pick] > m_pScratchIds[partner]);
        const uint32_t sourceIndex = pickIsSource ? pick : partner;
        const uint32_t targetIndex = pickIsSource ? partner : pick;

        MergeInto(m_pScratchIds[sourceIndex], m_pScratchIds[targetIndex], in_settings);
        m_pScratchAlive[sourceIndex] = 0;
        listCount = RemoveFromBudgetList(sourceIndex, listCount);
        StoreScratchCluster(targetIndex, m_pScratchIds[targetIndex]);
        UpdateBudgetPartners(sourceIndex, targetIndex, listCount, in_settings);
    }
}

uint32_t ClusterEngine::RemoveFromBudgetList(uint32_t in_index, uint32_t in_listCount)
{
    uint32_t low = 0;
    uint32_t high = in_listCount;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2u;
        if (m_pScratchOrder[middle] < in_index)
        {
            low = middle + 1u;
        }
        else
        {
            high = middle;
        }
    }
    if (low >= in_listCount || m_pScratchOrder[low] != in_index)
    {
        return in_listCount;
    }
    memmove(m_pScratchOrder + low, m_pScratchOrder + low + 1u, sizeof(uint32_t) * (in_listCount - low - 1u));
    return in_listCount - 1u;
}

void ClusterEngine::FindAllBudgetPartners(uint32_t in_count, const Settings& in_settings)
{
    for (uint32_t index = 0; index < in_count; ++index)
    {
        m_pScratchPartner[index] = kNone;
        m_pScratchCost[index] = kAnyDistance;
        m_pScratchCosLimit[index] = kNoCosineLimit;
        m_pScratchBound[index] = 0;
    }
    for (uint32_t first = 0; first < in_count; ++first)
    {
        const uint64_t firstClass = m_pScratchClass[first];
        const float firstX = m_pScratchX[first];
        const float firstY = m_pScratchY[first];
        const float firstZ = m_pScratchZ[first];
        for (uint32_t second = first + 1u; second < in_count; ++second)
        {
            if (m_pScratchClass[second] != firstClass)
            {
                continue;
            }
            const double dot = firstX * m_pScratchX[second] + firstY * m_pScratchY[second] + firstZ * m_pScratchZ[second];
            if (dot <= m_pScratchCosLimit[first] && dot <= m_pScratchCosLimit[second])
            {
                continue;
            }
            const double cost = GetBudgetCost(first, second, in_settings);
            if (dot > m_pScratchCosLimit[first] && cost < m_pScratchCost[first])
            {
                SetBudgetPartner(first, second, cost);
            }
            if (dot > m_pScratchCosLimit[second] && cost < m_pScratchCost[second])
            {
                SetBudgetPartner(second, first, cost);
            }
        }
    }
}

void ClusterEngine::SetBudgetPartner(uint32_t in_index, uint32_t in_partner, double in_cost)
{
    m_pScratchPartner[in_index] = in_partner;
    m_pScratchCost[in_index] = in_cost;
    m_pScratchBound[in_index] = 0;
    m_pScratchCosLimit[in_index] = CosineLimit(in_cost / (1.0 + m_pClusters[m_pScratchIds[in_index]].priority / 100.0));
}

void ClusterEngine::UpdateBudgetPartners(uint32_t in_source, uint32_t in_target, uint32_t in_listCount, const Settings& in_settings)
{
    const uint64_t targetClass = m_pScratchClass[in_target];
    const float targetX = m_pScratchX[in_target];
    const float targetY = m_pScratchY[in_target];
    const float targetZ = m_pScratchZ[in_target];
    const double targetFactor = 1.0 + m_pClusters[m_pScratchIds[in_target]].priority / 100.0;
    uint32_t targetPartner = kNone;
    double targetCost = kAnyDistance;
    double targetCosLimit = kNoCosineLimit;
    for (uint32_t position = 0; position < in_listCount; ++position)
    {
        const uint32_t index = m_pScratchOrder[position];
        if (index == in_target)
        {
            continue;
        }
        bool changed = false;
        const uint32_t best = m_pScratchPartner[index];
        if (m_pScratchBound[index] == 0 && (best == in_source || best == in_target))
        {
            m_pScratchBound[index] = 1;
            m_pScratchPartner[index] = kNone;
            changed = true;
        }
        if (m_pScratchClass[index] == targetClass)
        {
            const double dot = targetX * m_pScratchX[index] + targetY * m_pScratchY[index] + targetZ * m_pScratchZ[index];
            const bool checkTarget = dot > targetCosLimit;
            const bool checkIndex = dot > m_pScratchCosLimit[index];
            if (checkTarget || checkIndex)
            {
                const double cost = GetBudgetCost(in_target, index, in_settings);
                if (checkTarget && cost < targetCost)
                {
                    targetPartner = index;
                    targetCost = cost;
                    targetCosLimit = CosineLimit(cost / targetFactor);
                }
                const double current = m_pScratchCost[index];
                const bool cheaper = m_pScratchBound[index] != 0
                                         ? cost < current
                                         : cost < current || (cost == current && in_target < m_pScratchPartner[index]);
                if (checkIndex && cheaper)
                {
                    SetBudgetPartner(index, in_target, cost);
                    changed = true;
                }
            }
        }
        if (changed)
        {
            PushBudgetEntry(index, in_listCount);
        }
    }
    m_pScratchPartner[in_target] = targetPartner;
    m_pScratchCost[in_target] = targetCost;
    m_pScratchCosLimit[in_target] = targetCosLimit;
    m_pScratchBound[in_target] = 0;
    PushBudgetEntry(in_target, in_listCount);
}

uint32_t ClusterEngine::PopBudgetPick(uint32_t in_listCount, const Settings& in_settings)
{
    while (m_budgetHeapSize > 0)
    {
        const BudgetEntry top = m_pBudgetHeap[0];
        --m_budgetHeapSize;
        m_pBudgetHeap[0] = m_pBudgetHeap[m_budgetHeapSize];
        SiftBudgetDown(0);
        if (m_pScratchAlive[top.index] == 0 || m_pScratchVersion[top.index] != top.version)
        {
            continue;
        }
        if (m_pScratchBound[top.index] != 0)
        {
            FindBudgetPartner(top.index, in_listCount, in_settings);
            PushBudgetEntry(top.index, in_listCount);
            continue;
        }
        return top.index;
    }
    return kNone;
}

void ClusterEngine::PushBudgetEntry(uint32_t in_index, uint32_t in_listCount)
{
    ++m_pScratchVersion[in_index];
    if (m_pScratchBound[in_index] == 0 && m_pScratchPartner[in_index] == kNone)
    {
        return;
    }
    if (m_budgetHeapSize >= m_clusterCapacity)
    {
        RebuildBudgetHeap(in_listCount);
        return;
    }
    const uint32_t position = m_budgetHeapSize++;
    BudgetEntry& entry = m_pBudgetHeap[position];
    entry.cost = m_pScratchCost[in_index];
    entry.index = in_index;
    entry.version = m_pScratchVersion[in_index];
    SiftBudgetUp(position);
}

void ClusterEngine::RebuildBudgetHeap(uint32_t in_listCount)
{
    m_budgetHeapSize = 0;
    for (uint32_t position = 0; position < in_listCount; ++position)
    {
        const uint32_t index = m_pScratchOrder[position];
        if (m_pScratchBound[index] == 0 && m_pScratchPartner[index] == kNone)
        {
            continue;
        }
        BudgetEntry& entry = m_pBudgetHeap[m_budgetHeapSize++];
        entry.cost = m_pScratchCost[index];
        entry.index = index;
        entry.version = m_pScratchVersion[index];
    }
    for (uint32_t position = m_budgetHeapSize / 2u; position-- > 0;)
    {
        SiftBudgetDown(position);
    }
}

void ClusterEngine::SiftBudgetUp(uint32_t in_position)
{
    uint32_t position = in_position;
    const BudgetEntry entry = m_pBudgetHeap[position];
    while (position > 0)
    {
        const uint32_t parent = (position - 1u) / 2u;
        if (!IsBudgetEntryBefore(entry, m_pBudgetHeap[parent]))
        {
            break;
        }
        m_pBudgetHeap[position] = m_pBudgetHeap[parent];
        position = parent;
    }
    m_pBudgetHeap[position] = entry;
}

void ClusterEngine::SiftBudgetDown(uint32_t in_position)
{
    if (in_position >= m_budgetHeapSize)
    {
        return;
    }
    uint32_t position = in_position;
    const BudgetEntry entry = m_pBudgetHeap[position];
    for (;;)
    {
        const uint32_t left = position * 2u + 1u;
        if (left >= m_budgetHeapSize)
        {
            break;
        }
        uint32_t child = left;
        const uint32_t right = left + 1u;
        if (right < m_budgetHeapSize && IsBudgetEntryBefore(m_pBudgetHeap[right], m_pBudgetHeap[left]))
        {
            child = right;
        }
        if (!IsBudgetEntryBefore(m_pBudgetHeap[child], entry))
        {
            break;
        }
        m_pBudgetHeap[position] = m_pBudgetHeap[child];
        position = child;
    }
    m_pBudgetHeap[position] = entry;
}

bool ClusterEngine::IsBudgetEntryBefore(const BudgetEntry& in_first, const BudgetEntry& in_second)
{
    return in_first.cost < in_second.cost || (in_first.cost == in_second.cost && in_first.index < in_second.index);
}

void ClusterEngine::BuildReport()
{
    m_infoCount = 0;
    m_contributionCount = 0;
    for (uint32_t clusterIndex = 0; clusterIndex < m_clusterEnd; ++clusterIndex)
    {
        Cluster& cluster = m_pClusters[clusterIndex];
        if (!cluster.used)
        {
            continue;
        }
        if (cluster.changed)
        {
            RefreshCluster(clusterIndex);
        }

        ClusterInfo& info = m_pInfos[m_infoCount];
        info.id = clusterIndex;
        info.isNew = !cluster.reported;
        info.position.x = cluster.dirX * cluster.range;
        info.position.y = cluster.dirY * cluster.range;
        info.position.z = cluster.dirZ * cluster.range;
        info.angularRadius = cluster.radius;
        info.priority = cluster.priority;
        info.memberCount = cluster.nodeCount;
        info.representative = cluster.representative;
        cluster.reported = true;

        for (uint32_t node = cluster.head; node != kNone; node = m_pNodes[node].next)
        {
            ClusterContribution& contribution = m_pContributions[m_contributionCount++];
            contribution.input = m_pSlots[node >> 1u].inputIndex;
            contribution.cluster = m_infoCount;
            GetNodeGains(node, contribution.gainStart, contribution.gainEnd);
        }
        ++m_infoCount;
    }
}

bool ClusterEngine::IsOutsideCluster(const Slot& in_slot, const Cluster& in_cluster, const Settings& in_settings) const
{
    if (in_cluster.homeCount <= 1)
    {
        return false;
    }
    if (in_slot.excluded || in_slot.centred != in_cluster.centred)
    {
        return true;
    }
    const float dot = in_slot.dirX * in_cluster.dirX + in_slot.dirY * in_cluster.dirY + in_slot.dirZ * in_cluster.dirZ;
    if (in_settings.distanceScale <= 0.0)
    {
        return dot < in_settings.cosLeave;
    }
    const double rangeTerm =
        in_settings.distanceScale * std::fabs(static_cast<double>(in_slot.logRange) - in_cluster.logRange);
    const double halfChord = std::sqrt(MaxOf(0.0, 0.5 * (1.0 - dot)));
    if (rangeTerm + kPi * halfChord * kDegreesPerRadian <= in_settings.leaveAngle)
    {
        return false;
    }
    if (rangeTerm + 2.0 * halfChord * kDegreesPerRadian > in_settings.leaveAngle)
    {
        return true;
    }
    return AngleFromDot(dot) + rangeTerm > in_settings.leaveAngle;
}

uint32_t ClusterEngine::FindLeaveDestination(uint32_t in_slot, uint32_t in_from, const Settings& in_settings)
{
    if (m_pSlots[in_slot].excluded)
    {
        return CreateCluster(in_slot);
    }
    const uint32_t nearest = FindNearestCluster(in_slot, in_from, in_settings.joinAngle, in_settings);
    if (nearest != kNone)
    {
        return nearest;
    }
    if (CanCreateCluster(in_settings))
    {
        return CreateCluster(in_slot);
    }
    return kNone;
}

uint32_t ClusterEngine::FindFollowDestination(uint32_t in_slot, uint32_t in_from, const Settings& in_settings)
{
    const Slot& slot = m_pSlots[in_slot];
    if (!slot.excluded)
    {
        const uint32_t target = ResolveMergeTarget(in_from);
        if (target != kNone && IsJoinable(m_pClusters[target], slot.group, slot.centred))
        {
            return target;
        }
    }
    return FindPlacement(in_slot, in_from, in_settings);
}

uint32_t ClusterEngine::FindPlacement(uint32_t in_slot, uint32_t in_exclude, const Settings& in_settings)
{
    if (m_pSlots[in_slot].excluded)
    {
        return CreateCluster(in_slot);
    }
    uint32_t nearest = FindNearestCluster(in_slot, in_exclude, in_settings.joinAngle, in_settings);
    if (nearest != kNone)
    {
        return nearest;
    }
    if (!CanCreateCluster(in_settings))
    {
        nearest = FindNearestCluster(in_slot, in_exclude, in_settings.leaveAngle, in_settings);
        if (nearest != kNone)
        {
            return nearest;
        }
    }
    return CreateCluster(in_slot);
}

uint32_t ClusterEngine::ResolveMergeTarget(uint32_t in_cluster) const
{
    uint32_t target = m_pClusters[in_cluster].mergeTarget;
    uint32_t generation = m_pClusters[in_cluster].mergeTargetGeneration;
    for (uint32_t step = 0; step < m_clusterCapacity && target != kNone; ++step)
    {
        const Cluster& cluster = m_pClusters[target];
        if (!cluster.used || cluster.generation != generation)
        {
            return kNone;
        }
        if (!cluster.dissolving)
        {
            return target;
        }
        generation = cluster.mergeTargetGeneration;
        target = cluster.mergeTarget;
    }
    return kNone;
}

uint32_t ClusterEngine::FindNearestCluster(uint32_t in_slot, uint32_t in_exclude, double in_maxDistance,
                                           const Settings& in_settings) const
{
    const Slot& slot = m_pSlots[in_slot];
    const double cosLimit = CosineLimit(in_maxDistance);
    const bool useDistance = in_settings.distanceScale > 0.0;
    double bestScore = useDistance ? in_maxDistance : cosLimit;
    double cosBest = cosLimit;
    uint32_t best = kNone;
    for (uint32_t clusterIndex = 0; clusterIndex < m_clusterEnd; ++clusterIndex)
    {
        const Cluster& cluster = m_pClusters[clusterIndex];
        if (clusterIndex == in_exclude || !IsJoinable(cluster, slot.group, slot.centred))
        {
            continue;
        }
        const double dot = slot.dirX * cluster.dirX + slot.dirY * cluster.dirY + slot.dirZ * cluster.dirZ;
        if (dot <= cosBest)
        {
            continue;
        }
        if (useDistance)
        {
            const double rangeTerm = in_settings.distanceScale * std::fabs(static_cast<double>(slot.logRange) - cluster.logRange);
            if (rangeTerm >= bestScore)
            {
                continue;
            }
            const double distance = AngleFromDot(dot) + rangeTerm;
            if (distance < bestScore)
            {
                bestScore = distance;
                cosBest = CosineLimit(distance);
                best = clusterIndex;
            }
        }
        else if (dot > bestScore)
        {
            bestScore = dot;
            best = clusterIndex;
        }
    }
    return best;
}

bool ClusterEngine::IsJoinable(const Cluster& in_cluster, uint32_t in_group, bool in_centred) const
{
    return in_cluster.used && !in_cluster.dissolving && !in_cluster.excluded && in_cluster.group == in_group &&
           in_cluster.centred == in_centred;
}

bool ClusterEngine::CanCreateCluster(const Settings& in_settings) const
{
    return in_settings.maxOutputs == 0 || GetOutputDemand() < in_settings.maxOutputs;
}

uint32_t ClusterEngine::GetOutputDemand() const
{
    return m_passthroughCount + m_activeClusters;
}

uint32_t ClusterEngine::CreateCluster(uint32_t in_slot)
{
    const Slot& slot = m_pSlots[in_slot];
    const uint32_t clusterIndex = AllocateCluster();
    Cluster& cluster = m_pClusters[clusterIndex];
    const uint32_t generation = cluster.generation + 1u;
    memset(&cluster, 0, sizeof(Cluster));
    cluster.dirX = slot.dirX;
    cluster.dirY = slot.dirY;
    cluster.dirZ = slot.dirZ;
    cluster.range = slot.range;
    cluster.logRange = LogRange(slot.range);
    cluster.head = kNone;
    cluster.tail = kNone;
    cluster.group = slot.group;
    cluster.priority = slot.priority;
    cluster.generation = generation;
    cluster.mergeTarget = kNone;
    cluster.candidate = kNone;
    cluster.representative = slot.inputIndex;
    cluster.used = true;
    cluster.excluded = slot.excluded;
    cluster.centred = slot.centred;
    ++m_liveClusters;
    ++m_activeClusters;
    return clusterIndex;
}

void ClusterEngine::JoinCluster(uint32_t in_slot, uint32_t in_cluster)
{
    Slot& slot = m_pSlots[in_slot];
    AttachNode(GetPrimaryNode(in_slot), in_cluster);
    ++m_pClusters[in_cluster].homeCount;
    slot.fading = false;
    slot.fadeStart = 1.f;
    slot.fadeEnd = 1.f;
    slot.leaveFrames = 0;
    AddDecisionWeight(m_pClusters[in_cluster], slot, 1.0);
}

void ClusterEngine::StartMove(uint32_t in_slot, uint32_t in_cluster, const Settings& in_settings)
{
    Slot& slot = m_pSlots[in_slot];
    const uint32_t from = m_pNodes[GetPrimaryNode(in_slot)].cluster;
    AddDecisionWeight(m_pClusters[from], slot, -1.0);
    DropHomeInput(from);
    slot.primaryLink ^= 1u;
    AttachNode(GetPrimaryNode(in_slot), in_cluster);
    ++m_pClusters[in_cluster].homeCount;
    AddDecisionWeight(m_pClusters[in_cluster], slot, 1.0);
    slot.fading = true;
    slot.fadeStart = 0.f;
    slot.fadeEnd = MinOf(1.f, in_settings.fadeStep);
    slot.leaveFrames = 0;
}

void ClusterEngine::MergeInto(uint32_t in_source, uint32_t in_target, const Settings& in_settings)
{
    Cluster& source = m_pClusters[in_source];
    if (!source.dissolving)
    {
        source.dissolving = true;
        --m_activeClusters;
    }
    source.mergeTarget = in_target;
    source.mergeTargetGeneration = m_pClusters[in_target].generation;
    uint32_t node = source.head;
    while (node != kNone)
    {
        const uint32_t next = m_pNodes[node].next;
        const uint32_t slotIndex = node >> 1u;
        const Slot& slot = m_pSlots[slotIndex];
        if (IsPrimaryNode(node) && !slot.fading)
        {
            if (slot.placedFrame == m_frame)
            {
                RehomeNewInput(slotIndex, in_target);
            }
            else
            {
                StartMove(slotIndex, in_target, in_settings);
            }
        }
        node = next;
    }
}

void ClusterEngine::RehomeNewInput(uint32_t in_slot, uint32_t in_cluster)
{
    const uint32_t primaryNode = GetPrimaryNode(in_slot);
    const uint32_t from = m_pNodes[primaryNode].cluster;
    AddDecisionWeight(m_pClusters[from], m_pSlots[in_slot], -1.0);
    DropHomeInput(from);
    DetachNode(primaryNode);
    JoinCluster(in_slot, in_cluster);
}

void ClusterEngine::MergePair(uint32_t in_first, uint32_t in_second, const Settings& in_settings)
{
    Cluster& first = m_pClusters[in_first];
    Cluster& second = m_pClusters[in_second];
    const bool firstIsSource =
        first.sumWeight < second.sumWeight || (first.sumWeight == second.sumWeight && in_first > in_second);
    if (firstIsSource)
    {
        MergeInto(in_first, in_second, in_settings);
    }
    else
    {
        MergeInto(in_second, in_first, in_settings);
    }
    first.busy = true;
    second.busy = true;
    first.candidate = kNone;
    first.candidateFrames = 0;
    second.candidate = kNone;
    second.candidateFrames = 0;
}

void ClusterEngine::AddDecisionWeight(Cluster& io_cluster, const Slot& in_slot, double in_sign)
{
    const double share = in_sign * in_slot.weight;
    io_cluster.changed = true;
    io_cluster.sumX += share * in_slot.dirX;
    io_cluster.sumY += share * in_slot.dirY;
    io_cluster.sumZ += share * in_slot.dirZ;
    io_cluster.sumRange += share * in_slot.range;
    io_cluster.sumWeight += share;
    if (io_cluster.sumWeight <= kCancelRatio * std::fabs(share))
    {
        io_cluster.sumX = 0.0;
        io_cluster.sumY = 0.0;
        io_cluster.sumZ = 0.0;
        io_cluster.sumRange = 0.0;
        io_cluster.sumWeight = 0.0;
    }
    ApplySums(io_cluster);
    if (in_sign > 0.0)
    {
        io_cluster.priority = MaxOf(io_cluster.priority, in_slot.priority);
    }
}

void ClusterEngine::RefreshHomeRadius(Cluster& io_cluster) const
{
    float minDot = 1.f;
    for (uint32_t node = io_cluster.head; node != kNone; node = m_pNodes[node].next)
    {
        if (IsPrimaryNode(node))
        {
            const Slot& slot = m_pSlots[node >> 1u];
            minDot = MinOf(minDot, slot.dirX * io_cluster.dirX + slot.dirY * io_cluster.dirY + slot.dirZ * io_cluster.dirZ);
        }
    }
    io_cluster.homeRadius = static_cast<float>(AngleFromDot(minDot));
}

uint32_t ClusterEngine::GatherMergeableClusters()
{
    uint32_t count = 0;
    for (uint32_t clusterIndex = 0; clusterIndex < m_clusterEnd; ++clusterIndex)
    {
        Cluster& cluster = m_pClusters[clusterIndex];
        if (!cluster.used)
        {
            continue;
        }
        cluster.busy = false;
        if (cluster.dissolving || cluster.excluded)
        {
            cluster.candidate = kNone;
            cluster.candidateFrames = 0;
            continue;
        }
        if (cluster.changed)
        {
            RefreshHomeRadius(cluster);
        }
        m_pScratchIds[count] = clusterIndex;
        StoreScratchCluster(count, clusterIndex);
        ++count;
    }
    return count;
}

void ClusterEngine::StoreScratchCluster(uint32_t in_index, uint32_t in_cluster)
{
    const Cluster& cluster = m_pClusters[in_cluster];
    m_pScratchX[in_index] = cluster.dirX;
    m_pScratchY[in_index] = cluster.dirY;
    m_pScratchZ[in_index] = cluster.dirZ;
    m_pScratchLogRange[in_index] = cluster.logRange;
    m_pScratchClass[in_index] = MergeClass(cluster.group, cluster.centred);
}

bool ClusterEngine::CanMergePair(uint32_t in_first, uint32_t in_second, const Settings& in_settings, double& out_distance) const
{
    if (m_pScratchClass[in_first] != m_pScratchClass[in_second])
    {
        return false;
    }
    const double dot = m_pScratchX[in_first] * m_pScratchX[in_second] + m_pScratchY[in_first] * m_pScratchY[in_second] +
                       m_pScratchZ[in_first] * m_pScratchZ[in_second];
    if (dot <= in_settings.cosJoin)
    {
        return false;
    }
    const double distance =
        AngleFromDot(dot) +
        in_settings.distanceScale * std::fabs(static_cast<double>(m_pScratchLogRange[in_first]) - m_pScratchLogRange[in_second]);
    if (distance >= in_settings.joinAngle)
    {
        return false;
    }
    if (PredictMergedRadius(m_pClusters[m_pScratchIds[in_first]], m_pClusters[m_pScratchIds[in_second]]) > in_settings.joinAngle)
    {
        return false;
    }
    out_distance = distance;
    return true;
}

uint32_t ClusterEngine::FindMergePartner(uint32_t in_index, uint32_t in_count, const Settings& in_settings) const
{
    uint32_t best = kNone;
    double bestDistance = kAnyDistance;
    for (uint32_t other = 0; other < in_count; ++other)
    {
        if (other == in_index || m_pClusters[m_pScratchIds[other]].busy)
        {
            continue;
        }
        double distance = 0.0;
        if (CanMergePair(in_index, other, in_settings, distance) && distance < bestDistance)
        {
            bestDistance = distance;
            best = other;
        }
    }
    return best;
}

double ClusterEngine::PredictMergedRadius(const Cluster& in_first, const Cluster& in_second) const
{
    const double sumX = in_first.sumX + in_second.sumX;
    const double sumY = in_first.sumY + in_second.sumY;
    const double sumZ = in_first.sumZ + in_second.sumZ;
    const double length = std::sqrt(sumX * sumX + sumY * sumY + sumZ * sumZ);
    if (!(length > kCancelRatio * (in_first.sumWeight + in_second.sumWeight)))
    {
        return 180.0;
    }
    const double mergedX = sumX / length;
    const double mergedY = sumY / length;
    const double mergedZ = sumZ / length;
    const double firstShift = AngleFromDot(mergedX * in_first.dirX + mergedY * in_first.dirY + mergedZ * in_first.dirZ);
    const double secondShift = AngleFromDot(mergedX * in_second.dirX + mergedY * in_second.dirY + mergedZ * in_second.dirZ);
    return MaxOf(firstShift + in_first.homeRadius, secondShift + in_second.homeRadius);
}

double ClusterEngine::GetBudgetCost(uint32_t in_first, uint32_t in_second, const Settings& in_settings) const
{
    const double dot = m_pScratchX[in_first] * m_pScratchX[in_second] + m_pScratchY[in_first] * m_pScratchY[in_second] +
                       m_pScratchZ[in_first] * m_pScratchZ[in_second];
    const double distance =
        AngleFromDot(dot) +
        in_settings.distanceScale * std::fabs(static_cast<double>(m_pScratchLogRange[in_first]) - m_pScratchLogRange[in_second]);
    const uint32_t priority =
        MaxOf(m_pClusters[m_pScratchIds[in_first]].priority, m_pClusters[m_pScratchIds[in_second]].priority);
    return distance * (1.0 + priority / 100.0);
}

void ClusterEngine::FindBudgetPartner(uint32_t in_index, uint32_t in_listCount, const Settings& in_settings)
{
    const double ownFactor = 1.0 + m_pClusters[m_pScratchIds[in_index]].priority / 100.0;
    const uint64_t ownClass = m_pScratchClass[in_index];
    const float ownX = m_pScratchX[in_index];
    const float ownY = m_pScratchY[in_index];
    const float ownZ = m_pScratchZ[in_index];
    uint32_t best = kNone;
    double bestCost = kAnyDistance;
    double cosBound = kNoCosineLimit;
    for (uint32_t position = 0; position < in_listCount; ++position)
    {
        const uint32_t other = m_pScratchOrder[position];
        if (other == in_index || m_pScratchClass[other] != ownClass)
        {
            continue;
        }
        const double dot = ownX * m_pScratchX[other] + ownY * m_pScratchY[other] + ownZ * m_pScratchZ[other];
        if (dot <= cosBound)
        {
            continue;
        }
        const double cost = GetBudgetCost(in_index, other, in_settings);
        if (cost < bestCost)
        {
            bestCost = cost;
            best = other;
            cosBound = CosineLimit(bestCost / ownFactor);
        }
    }
    m_pScratchPartner[in_index] = best;
    m_pScratchCost[in_index] = bestCost;
    m_pScratchCosLimit[in_index] = cosBound;
    m_pScratchBound[in_index] = 0;
}

uint32_t ClusterEngine::GetPrimaryNode(uint32_t in_slot) const
{
    return in_slot * 2u + m_pSlots[in_slot].primaryLink;
}

uint32_t ClusterEngine::GetSourceNode(uint32_t in_slot) const
{
    return in_slot * 2u + (m_pSlots[in_slot].primaryLink ^ 1u);
}

bool ClusterEngine::IsPrimaryNode(uint32_t in_node) const
{
    return (in_node & 1u) == m_pSlots[in_node >> 1u].primaryLink;
}

void ClusterEngine::GetNodeGains(uint32_t in_node, float& out_gainStart, float& out_gainEnd) const
{
    const Slot& slot = m_pSlots[in_node >> 1u];
    if ((in_node & 1u) == slot.primaryLink)
    {
        out_gainStart = slot.fadeStart;
        out_gainEnd = slot.fadeEnd;
    }
    else
    {
        out_gainStart = 1.f - slot.fadeStart;
        out_gainEnd = 1.f - slot.fadeEnd;
    }
}

void ClusterEngine::AttachNode(uint32_t in_node, uint32_t in_cluster)
{
    Cluster& cluster = m_pClusters[in_cluster];
    Node& node = m_pNodes[in_node];
    node.cluster = in_cluster;
    node.prev = cluster.tail;
    node.next = kNone;
    if (cluster.tail != kNone)
    {
        m_pNodes[cluster.tail].next = in_node;
    }
    else
    {
        cluster.head = in_node;
    }
    cluster.tail = in_node;
    ++cluster.nodeCount;
}

void ClusterEngine::DetachNode(uint32_t in_node)
{
    Node& node = m_pNodes[in_node];
    const uint32_t clusterIndex = node.cluster;
    Cluster& cluster = m_pClusters[clusterIndex];
    if (node.prev != kNone)
    {
        m_pNodes[node.prev].next = node.next;
    }
    else
    {
        cluster.head = node.next;
    }
    if (node.next != kNone)
    {
        m_pNodes[node.next].prev = node.prev;
    }
    else
    {
        cluster.tail = node.prev;
    }
    node.cluster = kNone;
    node.prev = kNone;
    node.next = kNone;
    --cluster.nodeCount;
    if (cluster.nodeCount == 0)
    {
        ReleaseCluster(clusterIndex);
    }
}

void ClusterEngine::DropHomeInput(uint32_t in_cluster)
{
    Cluster& cluster = m_pClusters[in_cluster];
    --cluster.homeCount;
    if (cluster.homeCount == 0 && !cluster.dissolving)
    {
        cluster.dissolving = true;
        --m_activeClusters;
    }
}
