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

#pragma once

#include <cstddef>
#include <cstdint>

/** @brief 3D vector used by the cluster engine. */
struct ClusterVec3
{
    float x; ///< X coordinate.
    float y; ///< Y coordinate.
    float z; ///< Z coordinate.
};

/** @brief Memory interface the cluster engine allocates through. */
class ClusterAllocator
{
public:
    /** @brief Destroys the allocator. */
    virtual ~ClusterAllocator() = default;

    /** @brief Allocates a block of memory, or returns nullptr. */
    virtual void* Alloc(size_t in_size) = 0;

    /** @brief Frees a block returned by Alloc. */
    virtual void Free(void* in_pBlock) = 0;
};

/** @brief Settings for one cluster engine update. */
struct ClusterParams
{
    float joinAngle = 15.f;              ///< Angle in degrees under which an input may join a cluster.
    float leaveRatio = 1.5f;             ///< Leave angle as a multiple of the join angle.
    uint32_t dwellFrames = 10;           ///< Frames a change must last before an input leaves its cluster or two clusters merge.
    uint32_t crossfadeFrames = 3;        ///< Frames over which a moving input fades between clusters.
    uint32_t maxOutputs = 0;             ///< Maximum number of outputs, passthrough inputs included; 0 means no limit.
    float nearFieldRadius = 0.f;         ///< Inputs closer to the listener than this distance are never merged.
    float distanceWeight = 0.f;          ///< At 1, a doubling of distance counts as much as the join angle.
    uint32_t excludePriorityAbove = 100; ///< Inputs with a higher priority are never merged.
    bool loudnessWeighting = true;       ///< Weights cluster positions by input loudness.
    float loudnessSmoothing = 0.f;       ///< Per-frame smoothing coefficient for input loudness, from 0 up to but not including 1.
};

/** @brief One input object for a cluster engine update. */
struct ClusterInput
{
    uint64_t key;         ///< Key that identifies the input across frames.
    ClusterVec3 position; ///< Position relative to the listener.
    float loudness;       ///< RMS level of the input in this frame.
    uint32_t priority;    ///< Playback priority from 0 to 100.
    uint32_t group;       ///< Only inputs with the same group share a cluster.
    bool clusterable;     ///< False for inputs that always pass through.
};

/** @brief One cluster reported by the cluster engine for the current frame. */
struct ClusterInfo
{
    uint32_t id;             ///< Cluster ID, reused only after the cluster is gone.
    bool isNew;              ///< True on the first frame the cluster is reported.
    ClusterVec3 position;    ///< Weighted position of the cluster relative to the listener.
    float angularRadius;     ///< Largest angle in degrees between a contributing input and the cluster direction.
    uint32_t priority;       ///< Highest priority among the contributing inputs.
    uint32_t memberCount;    ///< Number of inputs that contribute to the cluster in this frame.
    uint32_t representative; ///< Index in this frame's input array of the representative input: the loudest one when loudness weighting is on, otherwise the first input at full gain.
};

/** @brief Gain ramp with which one input is mixed into one cluster for the current frame. */
struct ClusterContribution
{
    uint32_t input;   ///< Index in this frame's input array.
    uint32_t cluster; ///< Index in this frame's reported clusters.
    float gainStart;  ///< Gain at the start of the frame.
    float gainEnd;    ///< Gain at the end of the frame.
};

/** @brief Groups input objects into persistent clusters by direction from the listener. */
class ClusterEngine
{
public:
    /** @brief Creates an engine with no memory. */
    ClusterEngine();

    /** @brief Frees all memory. */
    ~ClusterEngine();

    /** @brief Engines cannot be copied. */
    ClusterEngine(const ClusterEngine&) = delete;

    /** @brief Engines cannot be copied. */
    ClusterEngine& operator=(const ClusterEngine&) = delete;

    /** @brief Sets the allocator and reserves memory for the given number of inputs. */
    bool Init(ClusterAllocator* in_pAllocator, uint32_t in_reserveInputs);

    /** @brief Frees all memory. */
    void Term();

    /** @brief Forgets all inputs and clusters. */
    void Reset();

    /** @brief Updates the clusters from this frame's inputs and returns false if memory ran out. */
    bool Update(const ClusterInput* in_pInputs, uint32_t in_numInputs, const ClusterParams& in_params);

    /** @brief Returns the number of clusters reported for this frame. */
    uint32_t GetClusterCount() const;

    /** @brief Returns a cluster reported for this frame. */
    const ClusterInfo& GetCluster(uint32_t in_index) const;

    /** @brief Returns the number of contributions for this frame. */
    uint32_t GetContributionCount() const;

    /** @brief Returns a contribution for this frame. */
    const ClusterContribution& GetContribution(uint32_t in_index) const;

    /** @brief Returns true if this frame needed more outputs than maxOutputs allows. */
    bool IsOverBudget() const;

private:
    /** @brief Parameters of one update, checked and converted for internal use. */
    struct Settings;

    /** @brief Persistent state of one input, found by key. */
    struct Slot;

    /** @brief Link of one input into one cluster. */
    struct Node;

    /** @brief Persistent state of one cluster. */
    struct Cluster;

    /** @brief Entry of the table that maps input keys to slots. */
    struct HashEntry;

    /** @brief Input that needs a cluster in this update, with its key for sorting. */
    struct PendingEntry;

    /** @brief Entry of the heap that orders budget merges by cost. */
    struct BudgetEntry;

    /** @brief Converts the public parameters into internal settings. */
    static void MakeSettings(const ClusterParams& in_params, Settings& out_settings);

    /** @brief Grows the memory so that the given number of inputs fits. */
    bool EnsureCapacity(uint32_t in_numInputs);

    /** @brief Moves all state into one new block with the given capacities. */
    bool Reserve(uint32_t in_slotCapacity, uint32_t in_clusterCapacity);

    /** @brief Forgets all inputs and clusters and keeps the memory. */
    void ClearState();

    /** @brief Marks every inactive slot as free. */
    void RebuildFreeSlots();

    /** @brief Marks every unused cluster as free. */
    void RebuildFreeClusters();

    /** @brief Returns the slot for a key, or an invalid index. */
    uint32_t FindSlot(uint64_t in_key) const;

    /** @brief Adds a key to the slot table. */
    void InsertSlotKey(uint64_t in_key, uint32_t in_slot);

    /** @brief Removes a key from the slot table. */
    void RemoveSlotKey(uint64_t in_key);

    /** @brief Takes the lowest free slot. */
    uint32_t AllocateSlot();

    /** @brief Returns a slot to the free set. */
    void FreeSlot(uint32_t in_slot);

    /** @brief Takes the lowest free cluster. */
    uint32_t AllocateCluster();

    /** @brief Frees a cluster so that its ID can be reused after this update. */
    void ReleaseCluster(uint32_t in_cluster);

    /** @brief Makes the clusters freed in this update available again. */
    void FlushReleasedClusters();

    /** @brief Matches inputs to slots, releases missing inputs and lists inputs that need a cluster. */
    void MapInputs(const ClusterInput* in_pInputs, uint32_t in_numInputs);

    /** @brief Sorts the pending list by input key. */
    void SortPendingByKey();

    /** @brief Creates slots for new pending inputs and turns the pending list into slot indices. */
    void AssignPendingSlots(const ClusterInput* in_pInputs);

    /** @brief Fills a new slot from an input. */
    void InitSlot(uint32_t in_slot, uint32_t in_inputIndex, const ClusterInput& in_input);

    /** @brief Removes an input from its clusters and frees its slot. */
    void ReleaseSlot(uint32_t in_slot);

    /** @brief Removes an input from its clusters without a fade. */
    void DetachSlot(uint32_t in_slot);

    /** @brief Reads position, loudness, weight and exclusion state of every input. */
    void UpdateSlots(const ClusterInput* in_pInputs, const Settings& in_settings);

    /** @brief Advances every crossfade by one frame and drops finished sources. */
    void AdvanceFades(const Settings& in_settings);

    /** @brief Recomputes position, radius, priority and exclusion state of every cluster. */
    void RefreshClusters();

    /** @brief Recomputes one cluster from the gains of its contributions. */
    void RefreshCluster(uint32_t in_cluster);

    /** @brief Moves home inputs that changed group, stayed outside their cluster long enough, or belong to a dissolving cluster. */
    void ProcessLeaves(const Settings& in_settings);

    /** @brief Places every new input into a cluster. */
    void PlacePendingInputs(const Settings& in_settings);

    /** @brief Merges cluster pairs that stayed close long enough. */
    void MergeNearbyClusters(const Settings& in_settings);

    /** @brief Merges the cheapest cluster pairs until the output count fits maxOutputs. */
    void EnforceBudget(const Settings& in_settings);

    /** @brief Fills the reported clusters and contributions. */
    void BuildReport();

    /** @brief Returns true if a home input is far enough from its cluster, or no longer matches it, to start leaving. */
    bool IsOutsideCluster(const Slot& in_slot, const Cluster& in_cluster, const Settings& in_settings) const;

    /** @brief Returns the destination for a home input that leaves its cluster, or an invalid index. */
    uint32_t FindLeaveDestination(uint32_t in_slot, uint32_t in_from, const Settings& in_settings);

    /** @brief Returns the destination for a home input of a dissolving cluster. */
    uint32_t FindFollowDestination(uint32_t in_slot, uint32_t in_from, const Settings& in_settings);

    /** @brief Returns the cluster for an input that must be placed, creating one if no cluster is close enough. */
    uint32_t FindPlacement(uint32_t in_slot, uint32_t in_exclude, const Settings& in_settings);

    /** @brief Follows the merge targets of a dissolving cluster to a live cluster, or returns an invalid index. */
    uint32_t ResolveMergeTarget(uint32_t in_cluster) const;

    /** @brief Returns the nearest joinable cluster closer than a distance, or an invalid index. */
    uint32_t FindNearestCluster(uint32_t in_slot, uint32_t in_exclude, double in_maxDistance, const Settings& in_settings) const;

    /** @brief Returns true if an input of the given group and centred flag may join the cluster. */
    bool IsJoinable(const Cluster& in_cluster, uint32_t in_group, bool in_centred) const;

    /** @brief Returns true if one more cluster fits maxOutputs. */
    bool CanCreateCluster(const Settings& in_settings) const;

    /** @brief Returns the number of outputs the budget counts. */
    uint32_t GetOutputDemand() const;

    /** @brief Creates an empty cluster with the position, group and centred flag of an input. */
    uint32_t CreateCluster(uint32_t in_slot);

    /** @brief Adds a placed input to a cluster at full gain. */
    void JoinCluster(uint32_t in_slot, uint32_t in_cluster);

    /** @brief Starts a crossfade of a home input from its cluster into another cluster. */
    void StartMove(uint32_t in_slot, uint32_t in_cluster, const Settings& in_settings);

    /** @brief Moves an input placed in this update into another cluster at full gain, without a fade. */
    void RehomeNewInput(uint32_t in_slot, uint32_t in_cluster);

    /** @brief Dissolves a cluster and starts moving its settled home inputs into another cluster. */
    void MergeInto(uint32_t in_source, uint32_t in_target, const Settings& in_settings);

    /** @brief Merges the lighter of two clusters into the other. */
    void MergePair(uint32_t in_first, uint32_t in_second, const Settings& in_settings);

    /** @brief Adds or removes the full weight of an input to the position of a cluster within this update. */
    void AddDecisionWeight(Cluster& io_cluster, const Slot& in_slot, double in_sign);

    /** @brief Sets the largest angle between a home input of a cluster and its current direction. */
    void RefreshHomeRadius(Cluster& io_cluster) const;

    /** @brief Sets direction and range of a cluster from its sums. */
    static void ApplySums(Cluster& io_cluster);

    /** @brief Lists the mergeable clusters in the scratch arrays and returns their count. */
    uint32_t GatherMergeableClusters();

    /** @brief Copies the position of a listed cluster into the scratch arrays. */
    void StoreScratchCluster(uint32_t in_index, uint32_t in_cluster);

    /** @brief Returns true if two listed clusters may merge and gives their distance. */
    bool CanMergePair(uint32_t in_first, uint32_t in_second, const Settings& in_settings, double& out_distance) const;

    /** @brief Returns the nearest listed cluster that may merge with a listed cluster and is not busy. */
    uint32_t FindMergePartner(uint32_t in_index, uint32_t in_count, const Settings& in_settings) const;

    /** @brief Returns an upper bound of the angular radius of two clusters merged into one. */
    double PredictMergedRadius(const Cluster& in_first, const Cluster& in_second) const;

    /** @brief Returns the budget merge cost of two listed clusters. */
    double GetBudgetCost(uint32_t in_first, uint32_t in_second, const Settings& in_settings) const;

    /** @brief Finds the cheapest budget partner of a listed cluster among the clusters in the budget list. */
    void FindBudgetPartner(uint32_t in_index, uint32_t in_listCount, const Settings& in_settings);

    /** @brief Finds the cheapest budget partner of every listed cluster in one pass over the pairs. */
    void FindAllBudgetPartners(uint32_t in_count, const Settings& in_settings);

    /** @brief Removes a listed cluster from the budget list, which stays in index order, and returns the new length. */
    uint32_t RemoveFromBudgetList(uint32_t in_index, uint32_t in_listCount);

    /** @brief Sets the budget partner and cost of a listed cluster. */
    void SetBudgetPartner(uint32_t in_index, uint32_t in_partner, double in_cost);

    /** @brief Finds the new budget partner of a merge target and updates the budget partners of the other listed clusters. */
    void UpdateBudgetPartners(uint32_t in_source, uint32_t in_target, uint32_t in_listCount, const Settings& in_settings);

    /** @brief Pops budget heap entries until it finds the cluster of the cheapest merge, or returns an invalid index. */
    uint32_t PopBudgetPick(uint32_t in_listCount, const Settings& in_settings);

    /** @brief Pushes the current budget cost of a listed cluster and makes its older heap entries stale. */
    void PushBudgetEntry(uint32_t in_index, uint32_t in_listCount);

    /** @brief Refills the budget heap with one entry per listed cluster that can still merge. */
    void RebuildBudgetHeap(uint32_t in_listCount);

    /** @brief Moves a budget heap entry up until its parent comes before it. */
    void SiftBudgetUp(uint32_t in_position);

    /** @brief Moves a budget heap entry down until it comes before its children. */
    void SiftBudgetDown(uint32_t in_position);

    /** @brief Returns true if a budget heap entry must be popped before another. */
    static bool IsBudgetEntryBefore(const BudgetEntry& in_first, const BudgetEntry& in_second);

    /** @brief Returns the index of the node that links an input to its home cluster. */
    uint32_t GetPrimaryNode(uint32_t in_slot) const;

    /** @brief Returns the index of the node through which an input fades out of its previous cluster. */
    uint32_t GetSourceNode(uint32_t in_slot) const;

    /** @brief Returns true if the node links its input to its home cluster. */
    bool IsPrimaryNode(uint32_t in_node) const;

    /** @brief Returns the gain ramp of a node for this frame. */
    void GetNodeGains(uint32_t in_node, float& out_gainStart, float& out_gainEnd) const;

    /** @brief Appends a node to the node list of a cluster. */
    void AttachNode(uint32_t in_node, uint32_t in_cluster);

    /** @brief Removes a node from its cluster and frees the cluster once it is empty. */
    void DetachNode(uint32_t in_node);

    /** @brief Removes one home input from a cluster and dissolves the cluster when none is left. */
    void DropHomeInput(uint32_t in_cluster);

    ClusterAllocator* m_pAllocator = nullptr;          ///< Allocator for all engine memory.
    void* m_pBlock = nullptr;                          ///< Single memory block that holds all arrays.
    Slot* m_pSlots = nullptr;                          ///< Input slots.
    Node* m_pNodes = nullptr;                          ///< Two nodes per slot.
    HashEntry* m_pHash = nullptr;                      ///< Open addressing table from key to slot.
    uint64_t* m_pFreeSlots = nullptr;                  ///< Bit set of free slots.
    PendingEntry* m_pPending = nullptr;                ///< Inputs that need a cluster in this update.
    ClusterContribution* m_pContributions = nullptr;   ///< Contributions reported for this frame.
    Cluster* m_pClusters = nullptr;                    ///< Clusters indexed by ID.
    uint64_t* m_pFreeClusters = nullptr;               ///< Bit set of free cluster IDs.
    uint32_t* m_pReleasedClusters = nullptr;           ///< Cluster IDs freed in this update.
    ClusterInfo* m_pInfos = nullptr;                   ///< Clusters reported for this frame.
    uint32_t* m_pScratchIds = nullptr;                 ///< Cluster IDs listed for merging.
    float* m_pScratchX = nullptr;                      ///< Direction X of listed clusters.
    float* m_pScratchY = nullptr;                      ///< Direction Y of listed clusters.
    float* m_pScratchZ = nullptr;                      ///< Direction Z of listed clusters.
    float* m_pScratchLogRange = nullptr;               ///< Base 2 logarithm of the range of listed clusters.
    uint64_t* m_pScratchClass = nullptr;               ///< Merge class of listed clusters, which combines group and centred flag.
    uint32_t* m_pScratchPartner = nullptr;             ///< Best merge partner of listed clusters.
    uint32_t* m_pScratchOrder = nullptr;               ///< Listed clusters sorted by direction Z, or the budget list of clusters that can still merge.
    double* m_pScratchCost = nullptr;                  ///< Distance or cost to the best partner of listed clusters.
    uint8_t* m_pScratchAlive = nullptr;                ///< Nonzero for listed clusters that can still merge.
    uint8_t* m_pScratchBound = nullptr;                ///< Nonzero for listed clusters whose cost is only a lower bound of the cost to their best partner.
    double* m_pScratchCosLimit = nullptr;              ///< Cosine of the largest angle at which a listed cluster can still find a cheaper partner.
    uint32_t* m_pScratchVersion = nullptr;             ///< Version of the newest budget heap entry of listed clusters.
    BudgetEntry* m_pBudgetHeap = nullptr;              ///< Binary min-heap of budget merge costs.
    uint32_t m_slotCapacity = 0;                       ///< Number of slots.
    uint32_t m_clusterCapacity = 0;                    ///< Number of clusters.
    uint32_t m_hashMask = 0;                           ///< Size of the slot table minus one.
    uint32_t m_slotEnd = 0;                            ///< One past the highest active slot.
    uint32_t m_clusterEnd = 0;                         ///< One past the highest used cluster ID.
    uint32_t m_liveClusters = 0;                       ///< Number of used clusters.
    uint32_t m_activeClusters = 0;                     ///< Number of clusters that have home inputs and are not dissolving.
    uint32_t m_releasedCount = 0;                      ///< Number of cluster IDs freed in this update.
    uint32_t m_pendingCount = 0;                       ///< Number of inputs that need a cluster in this update.
    uint32_t m_passthroughCount = 0;                   ///< Number of passthrough inputs in this update.
    uint32_t m_infoCount = 0;                          ///< Number of clusters reported for this frame.
    uint32_t m_contributionCount = 0;                  ///< Number of contributions reported for this frame.
    uint32_t m_budgetHeapSize = 0;                     ///< Number of entries in the budget heap.
    uint32_t m_frame = 0;                              ///< Update counter.
    bool m_overBudget = false;                         ///< True if this frame needed more outputs than allowed.
};
